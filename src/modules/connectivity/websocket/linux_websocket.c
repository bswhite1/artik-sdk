/*
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <openssl/ssl.h>
#include <openssl/engine.h>
#include <libwebsockets.h>
#include <errno.h>
#include <regex.h>

#include <artik_log.h>
#include <artik_module.h>
#include <artik_loop.h>
#include <artik_websocket.h>
#include <artik_security.h>
#include <artik_utils.h>
#include "os_websocket.h"

#define WAIT_CONNECT_POLLING_MS		500
#define FLAG_EVENT			(0x1 << 0)
#define MAX(a, b)			((a > b) ? a : b)
#define CB_CONTAINER			((os_websocket_container *)\
					lws_get_protocol(wsi)->user)
#define CB_FDS				(((os_websocket_fds *)\
					CB_CONTAINER->fds)->fdset)

#define MAX_QUEUE_NAME			1024
#define MAX_QUEUE_SIZE			128
#define MAX_MESSAGE_SIZE		2048
#define PROCESS_TIMEOUT_MS		10

#define ARTIK_WEBSOCKET_INTERFACE	((os_websocket_interface *)\
					config->private_data)
#define ARTIK_WEBSOCKET_PROTOCOL_NAME	"artik-websocket"
#define SSL_ALERT_FATAL			!strcmp(SSL_alert_type_string_long\
					(ret), "fatal")
#define UNKNOWN_CA			!strcmp(SSL_alert_desc_string_long\
					(ret), "unknown CA")
#define BAD_CERTIFICATE			!strcmp(SSL_alert_desc_string_long\
					(ret), "bad certificate")
#define HANDSHAKE_FAILURE		!strcmp(SSL_alert_desc_string_long\
					(ret), "handshake failure")
#define PEM_END_CERTIFICATE_UNIX "-----END CERTIFICATE-----\n"
#define PEM_END_CERTIFICATE_WIN  "-----END CERTIFICATE-----\r\n"

enum fd_event {
	FD_CLOSE,
	FD_CONNECT,
	FD_RECEIVE,
	FD_ERROR,
	FD_CONNECTION_ERROR,
	NUM_FDS
};

typedef struct {
	int fdset[NUM_FDS];
} os_websocket_fds;

typedef struct {
	char *send_message;
	int send_message_len;
	char *receive_message;
	os_websocket_fds *fds;
	int timeout_id;
	int periodic_id;
	unsigned int ping_period;
	unsigned int pong_timeout;
} os_websocket_container;

typedef struct {
	int watch_id;
	enum fd_event fd;
	artik_websocket_callback callback;
	void *user_data;
	artik_loop_module *loop;
} os_websocket_data;

typedef struct {
	struct lws_context *context;
	struct lws *wsi;
	struct lws_protocols *protocols;
	SSL_CTX *ssl_ctx;
	int loop_process_id;
	os_websocket_container container;
	os_websocket_data data[NUM_FDS];
	bool error_connect;
} os_websocket_interface;

typedef struct {
	artik_list node;
	os_websocket_interface interface;
} websocket_node;

static artik_list *requested_node = NULL;

static const struct lws_extension exts[] = {
	{
		"permessage-deflate",
		lws_extension_callback_pm_deflate,
		"permessage-deflate; client_max_window_bits"
	},
	{
		"deflate-frame",
		lws_extension_callback_pm_deflate,
		"deflate_frame"
	},
	{ NULL, NULL, NULL /* terminator */ }
};

static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
						void *user, void *in, size_t len);

static void ssl_ctx_info_callback(const SSL *ssl, int where, int ret);

static int ping_periodic_callback(void *user_data);
static void pong_timeout_callback(void *user_data);

void lws_cleanup(artik_websocket_config *config)
{
	void *protocol = (void *)lws_get_protocol(
					ARTIK_WEBSOCKET_INTERFACE->wsi);

	log_dbg("");

	artik_loop_module *loop = (artik_loop_module *)
		artik_request_api_module("loop");

	loop->remove_fd_watch(
	  ARTIK_WEBSOCKET_INTERFACE->data[FD_CLOSE].watch_id);
	loop->remove_fd_watch(
	  ARTIK_WEBSOCKET_INTERFACE->data[FD_CONNECT].watch_id);
	loop->remove_fd_watch(
	  ARTIK_WEBSOCKET_INTERFACE->data[FD_RECEIVE].watch_id);
	loop->remove_fd_watch(
	  ARTIK_WEBSOCKET_INTERFACE->data[FD_ERROR].watch_id);
	loop->remove_fd_watch(
	  ARTIK_WEBSOCKET_INTERFACE->data[FD_CONNECTION_ERROR].watch_id);
	loop->remove_idle_callback(ARTIK_WEBSOCKET_INTERFACE->loop_process_id);

	if ((ARTIK_WEBSOCKET_INTERFACE->container.pong_timeout) &&
		(ARTIK_WEBSOCKET_INTERFACE->container.timeout_id != -1))
		loop->remove_timeout_callback(
			ARTIK_WEBSOCKET_INTERFACE->container.timeout_id);

	if ((ARTIK_WEBSOCKET_INTERFACE->container.ping_period) &&
		(ARTIK_WEBSOCKET_INTERFACE->container.periodic_id != -1))
		loop->remove_periodic_callback(
			ARTIK_WEBSOCKET_INTERFACE->container.periodic_id);

	artik_release_api_module(loop);

	/* Destroy context in libwebsockets API */
	lws_context_destroy(ARTIK_WEBSOCKET_INTERFACE->context);

	/* Free variables in ARTIK API */
	close(ARTIK_WEBSOCKET_INTERFACE->container.fds->fdset[FD_CLOSE]);
	close(ARTIK_WEBSOCKET_INTERFACE->container.fds->fdset[FD_CONNECT]);
	close(ARTIK_WEBSOCKET_INTERFACE->container.fds->fdset[FD_RECEIVE]);
	close(ARTIK_WEBSOCKET_INTERFACE->container.fds->fdset[FD_ERROR]);
	close(ARTIK_WEBSOCKET_INTERFACE->container.fds->fdset[FD_CONNECTION_ERROR]);
	free(ARTIK_WEBSOCKET_INTERFACE->container.fds);

	free(protocol);

	/* Free OpenSSL context */
	SSL_CTX_free(ARTIK_WEBSOCKET_INTERFACE->ssl_ctx);

	/* Finalize freeing process */
	free(ARTIK_WEBSOCKET_INTERFACE->protocols);
	free(config->private_data);
	config->private_data = NULL;
}

int lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
				void *user, void *in, size_t len)
{
	uint64_t event_setter = FLAG_EVENT;
	char *received = NULL;
	artik_error ret = S_OK;
	artik_loop_module *loop = (artik_loop_module *)
		artik_request_api_module("loop");

	switch (reason) {

	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		log_dbg("LWS_CALLBACK_CLIENT_ESTABLISHED");
		if (write(CB_FDS[FD_CONNECT], &event_setter, sizeof(event_setter)) < 0)
			log_err("Failed to set connect event");

		if (CB_CONTAINER->ping_period) {
			ret = loop->add_periodic_callback(&CB_CONTAINER->periodic_id,
				CB_CONTAINER->ping_period, ping_periodic_callback, (void *) wsi);

			if (ret != S_OK) {
				log_err("Failed to set ping periodic callback");
				artik_release_api_module(loop);
				return -1;
			}
		}

		break;

	case LWS_CALLBACK_CLIENT_WRITEABLE:
		log_dbg("LWS_CALLBACK_CLIENT_WRITEABLE");
		if (CB_CONTAINER->send_message) {
			lws_write(wsi, (unsigned char *) CB_CONTAINER->send_message,
				CB_CONTAINER->send_message_len, LWS_WRITE_TEXT);
			free(CB_CONTAINER->send_message -
				LWS_SEND_BUFFER_PRE_PADDING);
			CB_CONTAINER->send_message = NULL;
		}
		log_dbg("");
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE:
		received = strndup((char *)in, strlen((char *)in) + 1);
		if (!received) {
			log_err("Failed to allocate memory");
			break;
		}
		CB_CONTAINER->receive_message = received;

		if (write(CB_FDS[FD_RECEIVE], &event_setter, sizeof(event_setter)) < 0)
			log_err("Failed to set connect event");
		break;

	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		log_dbg("LWS_CALLBACK_CLIENT_CONNECTION_ERROR");
		if (write(CB_FDS[FD_CONNECTION_ERROR], &event_setter,
			sizeof(event_setter)) < 0)
			log_err("Failed to connection error event");
		break;

	case LWS_CALLBACK_CLOSED:
		log_dbg("LWS_CALLBACK_CLOSED");
		if (write(CB_FDS[FD_CLOSE], &event_setter, sizeof(event_setter)) < 0)
			log_err("Failed to set close event");
		break;

	case LWS_CALLBACK_WSI_CREATE:
		log_dbg("LWS_CALLBACK_WSI_CREATE");
		break;

	case LWS_CALLBACK_WSI_DESTROY:
		log_dbg("LWS_CALLBACK_WSI_DESTROY");
		websocket_node *node = (websocket_node *)artik_list_get_by_handle(
			requested_node, (ARTIK_LIST_HANDLE)wsi);

		if (!node) {
			log_err("Failed to find websocket instance");
			artik_release_api_module(loop);
			return -1;
		}

		node->interface.error_connect = true;
		if (write(CB_FDS[FD_CLOSE], &event_setter,
			sizeof(event_setter)) < 0)
			log_err("Failed to set close event");
		break;

	case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED:
		log_err("LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED: %s",
			(const char *)in);
		break;

	case LWS_CALLBACK_LOCK_POLL:
		log_dbg("LWS_CALLBACK_LOCK_POLL");
		break;

	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
		log_dbg("LWS_CALLBACK_CHANGE_MODE_POLL_FD");
		break;

	case LWS_CALLBACK_ADD_POLL_FD:
		log_dbg("LWS_CALLBACK_ADD_POLL_FD");
		break;

	case LWS_CALLBACK_UNLOCK_POLL:
		log_dbg("LWS_CALLBACK_UNLOCK_POLL");
		break;

	case LWS_CALLBACK_DEL_POLL_FD:
		log_dbg("LWS_CALLBACK_DEL_POLL_FD");
		break;

	case LWS_CALLBACK_PROTOCOL_INIT:
		log_dbg("LWS_CALLBACK_PROTOCOL_INIT");
		break;

	case LWS_CALLBACK_PROTOCOL_DESTROY:
		log_dbg("LWS_CALLBACK_PROTOCOL_DESTROY");
		break;

	case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
		log_dbg("LWS_CALLBACK_WS_PEER_INITIATED_CLOSE");
		break;

	case LWS_CALLBACK_GET_THREAD_ID:
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
		log_dbg("LWS_CALLBACK_CLIENT_RECEIVE_PONG");
		if ((CB_CONTAINER->pong_timeout) && (CB_CONTAINER->timeout_id != -1)) {
			loop->remove_timeout_callback(CB_CONTAINER->timeout_id);
			CB_CONTAINER->timeout_id = -1;
		}

		break;

	case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
		log_dbg("LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH");
		break;

	case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
		log_dbg("LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER");
		break;

	case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS:
		log_dbg("LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS");
		break;
	case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
		log_dbg("LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS");
		break;

	default:
		log_dbg("reason = %d", reason);
		break;
	}

	artik_release_api_module(loop);

	return 0;
}

static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
	return 1;
}

static int verify_cert_cb(X509_STORE_CTX *ctx, void *arg)
{
	return 1;
}

void ssl_ctx_info_callback(const SSL *ssl, int where, int ret)
{
	const char *str;
	int w;
	uint64_t event_setter = FLAG_EVENT;
	SSL_CTX *ssl_ctx = SSL_get_SSL_CTX(ssl);
	struct lws *wsi = (struct lws *)SSL_CTX_get_ex_data(ssl_ctx, 0);

	w = where & ~SSL_ST_MASK;

	if (w & SSL_ST_CONNECT)
		str = "SSL_connect";
	else if (w & SSL_ST_ACCEPT)
		str = "SSL_accept";
	else
		str = "undefined";

	if (where & SSL_CB_ALERT) {
		str = (where & SSL_CB_READ) ? "read" : "write";
		log_dbg("SSL Alert %s:%s:%s", str, SSL_alert_type_string_long(ret),
			SSL_alert_desc_string_long(ret));

		if (SSL_ALERT_FATAL && (UNKNOWN_CA || BAD_CERTIFICATE ||
			HANDSHAKE_FAILURE)) {
			if (write(CB_FDS[FD_ERROR], &event_setter,
				sizeof(event_setter)) < 0)
				log_err("Failed to set close event : %d",
					errno);
		}
	} else if (where & SSL_CB_EXIT) {
		if (ret == 0)
			log_err("%s:failed in %s", str, SSL_state_string_long(ssl));
	} else if (where & SSL_CB_HANDSHAKE_DONE)
		log_dbg("%s", SSL_state_string_long(ssl));
}


static char *create_key_uri(artik_secure_element_config *se_config)
{
	const char *prefix;
	char *engine_key_uri;

	switch (se_config->key_algo) {
	case RSA_1024:
		prefix = "rsa1024://";
		break;
	case RSA_2048:
		prefix = "rsa2048://";
		break;
	case ECC_BRAINPOOL_P256R1:
		prefix = "bp256://";
		break;
	case ECC_SEC_P256R1:
		prefix = "ec256://";
		break;
	case ECC_SEC_P384R1:
		prefix = "ec384://";
		break;
	case ECC_SEC_P521R1:
		prefix = "ec521://";
		break;
	default:
		log_dbg("algo %d not supported", se_config->key_algo);
		return NULL;
	}

	engine_key_uri = malloc(strlen(prefix) + strlen(se_config->key_id) + 1);
	if (!engine_key_uri)
		return NULL;

	strcpy(engine_key_uri, prefix);
	strcat(engine_key_uri, se_config->key_id);

	return engine_key_uri;
}

artik_error setup_ssl_ctx(SSL_CTX **pctx, artik_ssl_config *ssl_config,
		char *host)
{
	artik_error ret = S_OK;
	SSL_CTX *ssl_ctx = NULL;

	const SSL_METHOD *method;
	artik_security_module *security = NULL;
	BIO *bio = NULL;
	X509 *x509_cert = NULL;
	EVP_PKEY *pk = NULL;
	X509_VERIFY_PARAM *param = NULL;
	X509_STORE *keystore = NULL;
	char *start = NULL, *end = NULL;
	int remain = 0;
	char *uri = NULL;

	log_dbg("");

	/* Initialize OpenSSL library */
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();

	method = (SSL_METHOD *)SSLv23_client_method();
	if (method == NULL) {
		log_err("problem creating ssl method\n");
		return E_NO_MEM;
	}

	/* Create an SSL Context */
	ssl_ctx = SSL_CTX_new(method);
	if (ssl_ctx == NULL) {
		log_err("problem creating ssl context\n");
		ret = E_NO_MEM;
		goto exit;
	}

	if (ssl_config->se_config) {
		security = (artik_security_module *)
			artik_request_api_module("security");
		if (security->load_openssl_engine() != S_OK) {
			log_err("Failed to load openssl engine");
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}
	}

	/* Set options for TLS */
	SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_COMPRESSION);
	SSL_CTX_set_options(ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
	SSL_CTX_set_info_callback(ssl_ctx, ssl_ctx_info_callback);

	if (ssl_config->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		param = SSL_CTX_get0_param(ssl_ctx);
		X509_VERIFY_PARAM_set1_host(param, host, 0);
		SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
	} else {
		SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, verify_callback);
		SSL_CTX_set_default_verify_paths(ssl_ctx);
		SSL_CTX_load_verify_locations(ssl_ctx, NULL, "/etc/ssl/certs/");
		SSL_CTX_set_cert_verify_callback(ssl_ctx, verify_cert_cb, NULL);
	}

	if ((!ssl_config->ca_cert.data || !ssl_config->ca_cert.len)
		&& ssl_config->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		log_err("No root CA set");
		ret = E_BAD_ARGS;
		goto exit;
	}

	if (ssl_config->ca_cert.data && ssl_config->ca_cert.len &&
			ssl_config->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		/* Create a new keystore */
		keystore = X509_STORE_new();
		if (!keystore) {
			log_err("Failed to create keystore");
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}

		/* CA certs may come as a bundle, parse them all */
		start = ssl_config->ca_cert.data;
		end = start;
		remain = ssl_config->ca_cert.len;

		do {
			/* Look for UNIX style ending first */
			end = strstr(start, PEM_END_CERTIFICATE_UNIX);
			if (end) {
				end += strlen(PEM_END_CERTIFICATE_UNIX);
			} else {
				/* If not found, check for Windows stye ending */
				end = strstr(start, PEM_END_CERTIFICATE_WIN);
				if (!end) {
					log_dbg("Do not find PEM_END");
					ret = E_BAD_ARGS;
					goto exit;
				}
				end += strlen(PEM_END_CERTIFICATE_WIN);
			}

			/* Convert CA certificate string into a BIO */
			bio = BIO_new(BIO_s_mem());
			BIO_write(bio, start, end - start);

			/* Extract X509 cert from the BIO */
			x509_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
			if (!x509_cert) {
				log_err("Failed to extract cert from the bio");
				BIO_free(bio);
				ret = E_BAD_ARGS;
				goto exit;
			}

			/* Add certificate to keystore */
			if (!X509_STORE_add_cert(keystore, x509_cert)) {
				log_err("Failed add certificate to the keystore");
				BIO_free(bio);
				X509_free(x509_cert);
				x509_cert = NULL;
				ret = E_WEBSOCKET_ERROR;
				goto exit;
			}

			BIO_free(bio);
			X509_free(x509_cert);
			x509_cert = NULL;

			remain -= end - start;
			start = end;
		} while (remain > 0);

		SSL_CTX_set_cert_store(ssl_ctx, keystore);
	}

	log_dbg("");

	if (ssl_config->client_cert.data &&
					ssl_config->client_cert.len) {
		/* Convert certificate string into a BIO */
		bio = BIO_new(BIO_s_mem());
		BIO_write(bio, ssl_config->client_cert.data,
				ssl_config->client_cert.len);

		/* Extract X509 cert from the BIO */
		x509_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
		if (!x509_cert) {
			BIO_free(bio);
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}

		log_dbg("");

		BIO_free(bio);

		/* Set certificate to context */
		if (!SSL_CTX_use_certificate(ssl_ctx, x509_cert)) {
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}
	}

	log_dbg("");

	if (ssl_config->client_key.data && ssl_config->client_key.len) {
		/* Convert key string into a BIO */
		bio = BIO_new(BIO_s_mem());
		if (!BIO_write(bio, ssl_config->client_key.data,
				ssl_config->client_key.len)) {
			BIO_free(bio);
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}

		log_dbg("");
		if (ssl_config->se_config) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
			ENGINE *engine = ENGINE_get_default_ECDSA();
#else
			ENGINE *engine = ENGINE_get_default_EC();
#endif
			if (!engine) {
				ret = E_WEBSOCKET_ERROR;
				goto exit;
			}

			uri = create_key_uri(ssl_config->se_config);
			if (!uri) {
				ret = E_WEBSOCKET_ERROR;
				goto exit;
			}

			pk = ENGINE_load_private_key(engine,
					uri, NULL, NULL);
			free(uri);
		} else {
			/* Extract EVP key from the BIO */
			pk = PEM_read_bio_PrivateKey(bio, NULL, 0, NULL);
		}

		if (!pk) {
			BIO_free(bio);
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}

		BIO_free(bio);

		/* Set private key to context */
		if (!SSL_CTX_use_PrivateKey(ssl_ctx, pk)) {
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}

		log_dbg("");

		/* Check certificate/key pair validity */
		if (!SSL_CTX_check_private_key(ssl_ctx)) {
			ret = E_WEBSOCKET_ERROR;
			goto exit;
		}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
		if (ssl_config->se_config)
			SSL_CTX_set1_curves_list(ssl_ctx, "brainpoolP256r1:prime256v1");
#endif
		SSL_CTX_set1_sigalgs_list(ssl_ctx, "ECDSA+SHA256");
	}

	*pctx = ssl_ctx;

exit:
	/* Clean up allocated memories and handle */
	if (security)
		artik_release_api_module(security);
	if (pk)
		EVP_PKEY_free(pk);
	if (x509_cert)
		X509_free(x509_cert);

	if (ret != S_OK && ssl_ctx != NULL)
		SSL_CTX_free(ssl_ctx);

	return ret;
}

static void release_openssl_engine(void)
{
	artik_security_module *security = (artik_security_module *)
		artik_request_api_module("security");

	if (!security) {
		log_err("Failed to request security module");
		return;
	}

	if (security->unload_openssl_engine() != S_OK)
		log_err("Failed to unload openssl engine");


	artik_release_api_module(security);
}


static int os_websocket_process_stream(void *user_data)
{
	os_websocket_interface *interface = (os_websocket_interface *)user_data;
	int n;

	n = lws_service(interface->context, PROCESS_TIMEOUT_MS);
	if (n < 0) {
		log_err("os_websocket_process_stream");
		return 0;
	}

	return 1;
}

#ifndef LIBWEBSOCKETS_VHOST_API
static artik_error set_proxy(struct lws_context *context, artik_uri_info *uri_proxy)
{
	artik_error ret = S_OK;
	char *lws_proxy = NULL;
	size_t len;
	int lws_ret;

	/* The max length for lws_proxy is strlen(uri_proxy.hostname) + 6
	 * 6 = 1 + 5
	 *    1 for '\0'
	 *    5 for the max size of the port (the max value for a port is 65 535)
	 */
	len = strlen(uri_proxy->hostname) + 6;
	lws_proxy = malloc(sizeof(char)*len);
	if (!lws_proxy) {
		ret = E_NO_MEM;
		goto exit;
	}

	snprintf(lws_proxy, len, "%s:%d", uri_proxy->hostname, uri_proxy->port);
	lws_ret = lws_set_proxy(context, lws_proxy);
	if (lws_ret != 0) {
		ret = E_WEBSOCKET_ERROR;
		goto exit;
	}

exit:
	if (lws_proxy)
		free(lws_proxy);

	return ret;
}
#endif

artik_error os_websocket_open_stream(artik_websocket_config *config, char *host,
		char *path, int port, bool use_tls)
{
	artik_error ret = S_OK;
	os_websocket_fds *fds = NULL;
	os_websocket_interface *interface = NULL;
	struct lws_context *context = NULL;
	struct lws_context_creation_info info;
	struct lws *wsi = NULL;
	websocket_node *node = NULL;
	struct lws_client_connect_info conn_info;
	artik_loop_module *loop = (artik_loop_module *)
		artik_request_api_module("loop");
	artik_utils_module *utils = artik_request_api_module("utils");

	if (config->ping_period < config->pong_timeout) {
		log_err("The pong_timeout value must be significantly smaller "
			"than ping_period.");
		ret = E_BAD_ARGS;
		goto exit;
	}

	log_dbg("");

	interface = malloc(sizeof(os_websocket_interface));
	if (!interface) {
		log_err("Failed to allocate memory");
		ret = E_NO_MEM;
		goto exit;
	}

	interface->protocols = malloc(2 * sizeof(struct lws_protocols));
	if (!interface->protocols) {
		log_err("Failed to allocate memory");
		ret = E_NO_MEM;
		goto exit;
	}

	memset(interface->protocols, 0, 2 * sizeof(struct lws_protocols));
	interface->protocols[0].name = ARTIK_WEBSOCKET_PROTOCOL_NAME;
	interface->protocols[0].callback = lws_callback;
	interface->protocols[0].per_session_data_size = 0;
	interface->protocols[0].rx_buffer_size = 4096;
	interface->protocols[0].id = (uintptr_t)config;
	interface->protocols[0].user = (void *)&interface->container;

	memset(&info, 0, sizeof(struct lws_context_creation_info));
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.iface = NULL;
	info.protocols = interface->protocols;
	info.gid = -1;
	info.uid = -1;
#ifdef LIBWEBSOCKETS_VHOST_API
	info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
#endif

	ret = setup_ssl_ctx(&info.provided_client_ssl_ctx, &config->ssl_config,
			host);
	if (ret != S_OK)
		goto exit;

	lws_set_log_level(0, NULL);

	/* Check if there is an enabled proxy */
	char *http_proxy = getenv("http_proxy");
	char *https_proxy = getenv("https_proxy");
	char *uri_proxy = NULL;
	artik_uri_info lws_proxy;

	if (http_proxy || https_proxy) {
		int default_port;

		if (use_tls && https_proxy) {
			uri_proxy = https_proxy;
			default_port = 443;
		} else if (!use_tls && http_proxy) {
			uri_proxy = http_proxy;
			default_port = 80;
		}

		if (uri_proxy) {
			if (utils->get_uri_info(&lws_proxy, uri_proxy) != S_OK) {
				log_err("Wrong websocket proxy (%s)",  uri_proxy);
				ret = E_WEBSOCKET_ERROR;
				goto exit;
			}

			if (lws_proxy.port == -1)
				lws_proxy.port = default_port;

#ifdef LIBWEBSOCKETS_VHOST_API
			info.http_proxy_address = strdup(lws_proxy.hostname);
			info.http_proxy_port = lws_proxy.port;
#endif
		}
	}

	context = lws_create_context(&info);
	if (context == NULL) {
		log_err("Creating libwebsocket context failed");
		ret = E_WEBSOCKET_ERROR;
		goto exit;
	}

#ifndef LIBWEBSOCKETS_VHOST_API
	if (uri_proxy)
		set_proxy(context, &lws_proxy);
#endif
	if (uri_proxy)
		utils->free_uri_info(&lws_proxy);

	memset(&conn_info, 0, sizeof(conn_info));
	conn_info.context = context;
	conn_info.address = host ? host : "";
	conn_info.port = port;
	conn_info.path = path ? path : "";
	conn_info.host = host ? host : "";
	conn_info.origin = host ? host : "";
	conn_info.protocol = ARTIK_WEBSOCKET_PROTOCOL_NAME;
	conn_info.ietf_version_or_minus_one = -1;
	conn_info.client_exts = exts;

	if (use_tls) {
		switch (config->ssl_config.verify_cert) {
		case ARTIK_SSL_VERIFY_NONE:
		case ARTIK_SSL_VERIFY_OPTIONAL:
			conn_info.ssl_connection = 2;
			break;
		default:
			conn_info.ssl_connection = 1;
			break;
		}
	} else {
		conn_info.ssl_connection = 0;
	}

	fds = malloc(sizeof(*fds));
	if (fds == NULL) {
		log_err("Failed to allocate memory");
		ret = E_NO_MEM;
		goto exit;
	}
	memset(fds, 0, sizeof(*fds));

	fds->fdset[FD_CLOSE] = eventfd(0, 0);
	fds->fdset[FD_CONNECT] = eventfd(0, 0);
	fds->fdset[FD_RECEIVE] = eventfd(0, 0);
	fds->fdset[FD_ERROR] = eventfd(0, 0);
	fds->fdset[FD_CONNECTION_ERROR] = eventfd(0, 0);

	memset(interface, 0, sizeof(*interface));
	interface->context = (void *)context;
	interface->container.fds = (void *)fds;
	interface->ssl_ctx = info.provided_client_ssl_ctx;
	interface->error_connect = false;
	interface->container.periodic_id = -1;
	interface->container.timeout_id = -1;
	interface->container.ping_period = config->ping_period;
	interface->container.pong_timeout = config->pong_timeout;

	loop->add_idle_callback(&interface->loop_process_id,
		os_websocket_process_stream, (void *)interface);

	config->private_data = (void *)interface;

	wsi = lws_client_connect_via_info(&conn_info);
	if (wsi == NULL) {
		log_err("Connecting websocket failed");
		ret = E_WEBSOCKET_ERROR;
		goto exit;
	}

	interface->wsi = (void *)wsi;
	SSL_CTX_set_ex_data(interface->ssl_ctx, 0, (void *)wsi);

	node = (websocket_node *)artik_list_add(&requested_node,
			(ARTIK_LIST_HANDLE)wsi, sizeof(websocket_node));
	if (!node)
		return E_NO_MEM;

	memcpy(&node->interface, interface, sizeof(node->interface));

exit:
	if (ret != S_OK) {
		if (interface) {
			if (interface->protocols)
				free(interface->protocols);
			free(interface);
			config->private_data = NULL;
		}
	}

	artik_release_api_module(utils);

	return ret;
}

artik_error os_websocket_write_stream(artik_websocket_config *config,
	char *message, int len)
{
	artik_error ret = S_OK;
	char *websocket_buf = NULL;

	log_dbg("");

	websocket_node *node = (websocket_node *)artik_list_get_by_handle(
		requested_node, (ARTIK_LIST_HANDLE)
		ARTIK_WEBSOCKET_INTERFACE->wsi);

	if (!node) {
		log_err("Could not find websocket instance");
		ret = E_WEBSOCKET_ERROR;
		goto exit;
	}

	if (node->interface.error_connect) {
		log_err("Impossible to write, no connection");
		ret = E_WEBSOCKET_ERROR;
		goto exit;
	}

	websocket_buf = malloc(LWS_SEND_BUFFER_PRE_PADDING + len +
		LWS_SEND_BUFFER_POST_PADDING);
	if (websocket_buf == NULL) {
		log_err("Failed to allocate memory");
		ret = E_NO_MEM;
		goto exit;
	}

	memcpy(websocket_buf + LWS_SEND_BUFFER_PRE_PADDING, message, len);
	ARTIK_WEBSOCKET_INTERFACE->container.send_message = websocket_buf +
		LWS_SEND_BUFFER_PRE_PADDING;
	ARTIK_WEBSOCKET_INTERFACE->container.send_message_len = len;
	lws_callback_on_writable(ARTIK_WEBSOCKET_INTERFACE->wsi);

exit:
	return ret;
}

int os_websocket_close_callback(int fd, enum watch_io io, void *user_data)
{
	uint64_t n = 0;
	os_websocket_data *data = (os_websocket_data *)user_data;

	log_dbg("");

	if (read(fd, &n, sizeof(uint64_t)) < 0) {
		log_err("close callback error");
		return 0;
	}

	data[FD_CLOSE].callback(data->user_data, (void *)
		ARTIK_WEBSOCKET_CLOSED);

	return 1;
}

int os_websocket_connection_callback(int fd, enum watch_io io, void *user_data)
{
	uint64_t n = 0;
	os_websocket_data *data = (os_websocket_data *)user_data;

	log_dbg("");

	if (read(fd, &n, sizeof(uint64_t)) < 0) {
		log_err("connection callback error");
		return 0;
	}

	data[FD_CONNECT].callback(data->user_data, (void *)
		ARTIK_WEBSOCKET_CONNECTED);

	return 1;
}

int os_websocket_error_callback(int fd, enum watch_io io, void *user_data)
{
	uint64_t n = 0;
	os_websocket_data *data = (os_websocket_data *)user_data;

	log_dbg("");

	if (read(fd, &n, sizeof(uint64_t)) < 0) {
		log_err("error callback error");
		return 0;
	}

	data[FD_ERROR].callback(data->user_data, (void *)
		ARTIK_WEBSOCKET_HANDSHAKE_ERROR);

	return 1;
}

int os_websocket_connection_error_callback(int fd, enum watch_io io,
	void *user_data)
{
	uint64_t n = 0;
	os_websocket_data *data = (os_websocket_data *)user_data;

	log_dbg("");

	if (read(fd, &n, sizeof(uint64_t)) < 0) {
		log_err("connection error callback error");
		return 0;
	}

	data[FD_CONNECTION_ERROR].callback(data->user_data, (void *)
		ARTIK_WEBSOCKET_CONNECTION_ERROR);

	return 1;
}

artik_error os_websocket_set_connection_callback(artik_websocket_config *config,
	artik_websocket_callback callback, void *user_data)
{
	artik_error ret = S_OK;
	os_websocket_fds *fds = ARTIK_WEBSOCKET_INTERFACE->container.fds;
	artik_loop_module *loop = (artik_loop_module *)
								artik_request_api_module("loop");
	os_websocket_data *data = ((os_websocket_interface *)
								config->private_data)->data;

	log_dbg("");

	if (data[FD_CLOSE].callback)
		loop->remove_fd_watch(data[FD_CLOSE].watch_id);

	if (data[FD_CONNECT].callback)
		loop->remove_fd_watch(data[FD_CONNECT].watch_id);

	if (data[FD_ERROR].callback)
		loop->remove_fd_watch(data[FD_ERROR].watch_id);

	data[FD_CLOSE].callback = callback;
	data[FD_CLOSE].user_data = user_data;
	data[FD_CONNECT].callback = callback;
	data[FD_CONNECT].user_data = user_data;
	data[FD_ERROR].callback = callback;
	data[FD_ERROR].user_data = user_data;
	data[FD_CONNECTION_ERROR].callback = callback;
	data[FD_CONNECTION_ERROR].user_data = user_data;

	if (!callback)
		return S_OK;

	ret = loop->add_fd_watch(fds->fdset[FD_CLOSE], WATCH_IO_IN,
		os_websocket_close_callback, (void *)data,
		&data[FD_CLOSE].watch_id);

	if (ret != S_OK) {
		log_err("Failed to set fd watch close callback");
		goto exit;
	}

	ret = loop->add_fd_watch(fds->fdset[FD_CONNECT], WATCH_IO_IN,
		os_websocket_connection_callback, (void *)data,
		&data[FD_CONNECT].watch_id);

	if (ret != S_OK) {
		log_err("Failed to set fd watch connection callback");
		goto exit;
	}

	ret = loop->add_fd_watch(fds->fdset[FD_ERROR], WATCH_IO_IN,
		os_websocket_error_callback, (void *)data,
		&data[FD_ERROR].watch_id);

	if (ret != S_OK) {
		log_err("Failed to set fd watch error callback");
		goto exit;
	}

	ret = loop->add_fd_watch(fds->fdset[FD_CONNECTION_ERROR], WATCH_IO_IN,
		os_websocket_connection_error_callback, (void *)data,
		&data[FD_CONNECTION_ERROR].watch_id);

	if (ret != S_OK) {
		log_err("Failed to set fd watch connnection error callback");
		goto exit;
	}

exit:
	artik_release_api_module(loop);
	return ret;
}

static int ping_periodic_callback(void *user_data)
{
	artik_error ret = S_OK;
	static unsigned char pingbuf[] = {
		0x81, 0x85, 0x37, 0xFA,
		0x21, 0x3d, 0x7F, 0x9F,
		0x4D, 0x51, 0x58
	};
	static unsigned int size = sizeof(pingbuf);
	artik_loop_module *loop = (artik_loop_module *)
		artik_request_api_module("loop");
	struct lws *wsi = (struct lws *)user_data;
	unsigned char *buf = (unsigned char *)malloc(LWS_PRE + size);

	if (!buf) {
		log_err("Failed to allocate buffer");
		return 0;
	}

	memset(buf, 0, LWS_PRE + size);
	memcpy(&buf[LWS_PRE], pingbuf, sizeof(pingbuf));

	log_dbg("");

	lws_write(wsi, &buf[LWS_PRE], size, LWS_WRITE_PING);
	free(buf);

	ret = loop->add_timeout_callback(&CB_CONTAINER->timeout_id,
		CB_CONTAINER->pong_timeout, pong_timeout_callback, (void *) wsi);

	if (ret != S_OK) {
		log_err("Failed to add on_timeout_callback error");
		return 0;
	}

	artik_release_api_module(loop);
	return 1;
}

static void pong_timeout_callback(void *user_data)
{
	uint64_t event_setter = FLAG_EVENT;

	struct lws *wsi = (struct lws *)user_data;

	log_err("Failed to ping websocket server %s error", __func__);

	if (write(CB_FDS[FD_CONNECTION_ERROR], &event_setter,
		sizeof(event_setter)) < 0)
		log_err("Failed write connection error event");

	CB_CONTAINER->timeout_id = -1;
}

int os_websocket_receive_callback(int fd, enum watch_io io, void *user_data)
{
	uint64_t n = 0;
	artik_websocket_config *config = (artik_websocket_config *)user_data;
	os_websocket_data *data = ((os_websocket_interface *)
		config->private_data)->data;

	log_dbg("");

	if (read(fd, &n, sizeof(uint64_t)) < 0) {
		log_err("receive callback error");
		return 0;
	}

	if (ARTIK_WEBSOCKET_INTERFACE->container.receive_message == NULL) {
		log_err("Websocket: receive message failed");
		return 0;
	}

	data[FD_RECEIVE].callback(data[FD_RECEIVE].user_data, (void *)
		ARTIK_WEBSOCKET_INTERFACE->container.receive_message);

	return 1;
}

artik_error os_websocket_set_receive_callback(artik_websocket_config *config,
	artik_websocket_callback callback, void *user_data)
{
	artik_error ret = S_OK;
	os_websocket_fds *fds = ARTIK_WEBSOCKET_INTERFACE->container.fds;
	os_websocket_data *data = ((os_websocket_interface *)
		config->private_data)->data;
	artik_loop_module *loop = (artik_loop_module *)
		artik_request_api_module("loop");

	log_dbg("");

	if (data[FD_RECEIVE].callback)
		loop->remove_fd_watch(data[FD_RECEIVE].watch_id);

	data[FD_RECEIVE].callback = callback;
	data[FD_RECEIVE].user_data = user_data;

	if (!callback)
		return S_OK;

	ret = loop->add_fd_watch(fds->fdset[FD_RECEIVE], WATCH_IO_IN,
		os_websocket_receive_callback, (void *)config,
		&data[FD_RECEIVE].watch_id);
	if (ret != S_OK) {
		log_err("Failed to set fd watch close callback");
		goto exit;
	}

exit:
	artik_release_api_module(loop);
	return ret;
}

artik_error os_websocket_close_stream(artik_websocket_config *config)
{
	artik_error ret = S_OK;

	log_dbg("");
	if (config->private_data == NULL)
		return E_NOT_CONNECTED;

	if (config->ssl_config.se_config)
		release_openssl_engine();
	lws_cleanup(config);
	return ret;
}
