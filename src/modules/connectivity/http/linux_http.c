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
#include <curl/curl.h>
#include <openssl/ssl.h>
#include <openssl/engine.h>
#include <artik_log.h>
#include <pthread.h>

#include <artik_module.h>
#include <artik_security.h>
#include <artik_http.h>
#include <artik_loop.h>
#include <artik_ssl.h>
#include "os_http.h"
#include "common_http.h"

#define WAIT_CONNECT_POLLING_MS  500
#define FLAG_EVENT               (0x1 << 0)
#define MAX(a, b)                ((a > b) ? a : b)
#define NUM_FDS                  2
#define FD_CLOSE                 0
#define FD_CONNECT               1
#define MAX_QUEUE_NAME           1024
#define MAX_QUEUE_SIZE           128
#define MAX_MESSAGE_SIZE         2048
#define PEM_END_CERTIFICATE_UNIX "-----END CERTIFICATE-----\n"
#define PEM_END_CERTIFICATE_WIN  "-----END CERTIFICATE-----\r\n"

typedef struct {
	artik_http_stream_callback callback;
	void *user_data;
} stream_callback_params;

typedef struct {
	artik_http_response_callback callback;
	void *user_data;
} response_callback_params;

typedef struct {
	int loop_process_id;
	char *url;
	artik_http_headers *headers;
	char *body;
	char *response;
	int status;
	artik_ssl_config *ssl;
	stream_callback_params stream_cb_params;
	response_callback_params response_cb_params;
} os_http_interface;

static pthread_mutex_t lock;
static bool lock_initialized = false;

static void mutex_lock(void)
{
	if (!lock_initialized) {
		pthread_mutex_init(&lock, NULL);
		lock_initialized = true;
	}
	pthread_mutex_lock(&lock);
}

static void mutex_unlock(void)
{
	pthread_mutex_unlock(&lock);
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

static CURLcode ssl_ctx_callback(CURL *curl, void *sslctx, void *parm)
{
	CURLcode ret = CURLE_OK;
	artik_ssl_config *ssl_config = (artik_ssl_config *)parm;
	artik_security_module *security = NULL;
	SSL_CTX *ctx = (SSL_CTX *)sslctx;
	BIO *b64 = NULL;
	X509 *x509_cert = NULL;
	EVP_PKEY *pk = NULL;
	X509_STORE *keystore = NULL;
	char *start = NULL, *end = NULL;
	int remain = 0;
	char *uri = NULL;

	log_dbg("");

	if (ssl_config->se_config) {
		security = (artik_security_module *)
			artik_request_api_module("security");
		if (security->load_openssl_engine() != S_OK) {
			log_err("Failed to load openssl engine");
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}
	}

	if (ssl_config->ca_cert.data && ssl_config->ca_cert.len &&
		ssl_config->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		/* Create a new keystore */
		keystore = X509_STORE_new();
		if (!keystore) {
			log_err("Failed to create keystore");
			ret = CURLE_SSL_CERTPROBLEM;
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
				if (!end)
					break;
				end += strlen(PEM_END_CERTIFICATE_WIN);
			}

			/* Convert CA certificate string into a BIO */
			b64 = BIO_new(BIO_s_mem());
			BIO_write(b64, start, end - start);

			/* Extract X509 cert from the BIO */
			x509_cert = PEM_read_bio_X509(b64, NULL, NULL, NULL);
			if (!x509_cert) {
				log_err("Failed to extract cert from the bio");
				BIO_free(b64);
				ret = CURLE_SSL_CERTPROBLEM;
				goto exit;
			}

			/* Add certificate to keystore */
			if (!X509_STORE_add_cert(keystore, x509_cert)) {
				log_err("Failed add certificate to the keystore");
				BIO_free(b64);
				X509_free(x509_cert);
				x509_cert = NULL;
				ret = CURLE_SSL_CERTPROBLEM;
				goto exit;
			}

			BIO_free(b64);
			X509_free(x509_cert);
			x509_cert = NULL;

			remain -= end - start;
			start = end;
		} while (remain);

		SSL_CTX_set_cert_store(ctx, keystore);
	}

	log_dbg("");

	if (ssl_config->client_cert.data && ssl_config->client_cert.len) {
		/* Convert certificate string into a BIO */
		b64 = BIO_new(BIO_s_mem());
		BIO_write(b64, ssl_config->client_cert.data,
			ssl_config->client_cert.len);

		/* Extract X509 cert from the BIO */
		x509_cert = PEM_read_bio_X509(b64, NULL, NULL, NULL);
		if (!x509_cert) {
			BIO_free(b64);
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}

		log_dbg("");

		BIO_free(b64);

		/* Set certificate to context */
		if (!SSL_CTX_use_certificate(ctx, x509_cert)) {
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}
	}

	log_dbg("");

	if (ssl_config->se_config && ssl_config->se_config->key_id) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
		ENGINE *engine = ENGINE_get_default_ECDSA();
#else
		ENGINE *engine = ENGINE_get_default_EC();
#endif
		if (!engine) {
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}

		uri = create_key_uri(ssl_config->se_config);
		if (!uri) {
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}

		pk = ENGINE_load_private_key(engine,
									 uri, NULL, NULL);
		free(uri);

		if (!pk) {
			log_dbg("");
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
		if (ssl_config->se_config)
			SSL_CTX_set1_curves_list(ctx, "brainpoolP256r1:prime256v1");
#endif
		SSL_CTX_set1_sigalgs_list(ctx, "ECDSA+SHA256");
	} else if (ssl_config->client_key.data && ssl_config->client_key.len) {
		log_dbg("");

		/* Convert key string into a BIO */
		b64 = BIO_new(BIO_s_mem());
		if (!BIO_write(b64, ssl_config->client_key.data,
				ssl_config->client_key.len)) {
			BIO_free(b64);
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}

		/* Extract EVP key from the BIO */
		pk = PEM_read_bio_PrivateKey(b64, NULL, 0, NULL);
		BIO_free(b64);
		if (!pk) {
			log_dbg("");
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}
	}

	/* Set private key to context */
	if (pk) {
		if (!SSL_CTX_use_PrivateKey(ctx, pk)) {
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}

		log_dbg("");

	/* Check certificate/key pair validity */
		if (!SSL_CTX_check_private_key(ctx)) {
			ret = CURLE_SSL_CERTPROBLEM;
			goto exit;
		}
	}

exit:
	if (security)
		artik_release_api_module(security);


	if (pk)
		EVP_PKEY_free(pk);

	if (x509_cert)
		X509_free(x509_cert);

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

static size_t response_callback(void *ptr, size_t size, size_t nmemb,
	void *userp)
{
	char **response = (char **)userp;
	int len = size * nmemb;
	const char *rx_data = (const char *)ptr;

	log_dbg("");

	if (!*response) {
		/* first call, allocate the response string */
		*response = strndup(rx_data, len);
		if (!*response)
			return 0;
	} else {
		/* not first call, need to realloc memory
		 * before concatenating
		 */
		*response = realloc(*response, strlen(*response) + len + 1);
		if (!*response)
			return 0;

		strncat(*response, rx_data, len);
	}

	return len;
}

static size_t stream_callback(void *ptr, size_t size, size_t nmemb,
	void *userp)
{
	stream_callback_params *cb_params = (stream_callback_params *)userp;
	int len = size * nmemb;
	char *data = (char *)ptr;

	log_dbg("");

	return (size_t)(cb_params->callback)(data, len, cb_params->user_data);
}

static int os_http_process_get_stream(void *user_data)
{
	os_http_interface *interface = (os_http_interface *)user_data;
	artik_error ret;

	ret = os_http_get_stream(interface->url, interface->headers,
		&interface->status, interface->stream_cb_params.callback,
		interface->stream_cb_params.user_data, interface->ssl);

	log_dbg("");

	if (ret != S_OK)
		log_err("os_http_process_get_stream");

	if (interface->response_cb_params.callback)
		interface->response_cb_params.callback(ret,
			interface->status,
			NULL,
			interface->response_cb_params.user_data);


	if (interface->url)
		free((void *)interface->url);

	if (interface->headers)
		free(interface->headers);

	if (interface->ssl)
		free_ssl_config(interface->ssl);

	free(interface);

	return 0;
}

static int os_http_process_get(void *user_data)
{
	os_http_interface *interface = (os_http_interface *)user_data;
	artik_error ret;

	log_dbg("");

	ret = os_http_get(interface->url, interface->headers,
		&interface->response, &interface->status, interface->ssl);

	if (ret != S_OK)
		log_err("os_http_process_get");

	if (interface->response_cb_params.callback)
		interface->response_cb_params.callback(ret,
			interface->status,
			interface->response,
			interface->response_cb_params.user_data);

	if (interface->url)
		free((void *)interface->url);

	if (interface->headers)
		free(interface->headers);

	if (interface->ssl)
		free_ssl_config(interface->ssl);

	free(interface);

	return 0;
}

static int os_http_process_post(void *user_data)
{
	os_http_interface *interface = (os_http_interface *)user_data;
	artik_error ret;

	log_dbg("");

	ret = os_http_post(interface->url, interface->headers, interface->body,
		&interface->response, &interface->status, interface->ssl);

	if (ret != S_OK)
		log_err("os_http_process_post");

	if (interface->response_cb_params.callback)
		interface->response_cb_params.callback(ret,
			interface->status,
			interface->response,
			interface->response_cb_params.user_data);

	if (interface->url)
		free((void *)interface->url);

	if (interface->headers)
		free(interface->headers);

	if (interface->body)
		free(interface->body);

	if (interface->ssl)
		free_ssl_config(interface->ssl);

	free(interface);

	return 0;
}

static int os_http_process_put(void *user_data)
{
	os_http_interface *interface = (os_http_interface *)user_data;
	artik_error ret;

	log_dbg("");

	ret = os_http_put(interface->url, interface->headers, interface->body,
		&interface->response, &interface->status, interface->ssl);

	if (ret != S_OK)
		log_err("os_http_process_put");

	if (interface->response_cb_params.callback)
		interface->response_cb_params.callback(ret,
			interface->status,
			interface->response,
			interface->response_cb_params.user_data);

	if (interface->url)
		free((void *)interface->url);

	if (interface->headers)
		free(interface->headers);

	if (interface->body)
		free(interface->body);

	if (interface->ssl)
		free_ssl_config(interface->ssl);

	free(interface);

	return 0;
}

static int os_http_process_delete(void *user_data)
{
	os_http_interface *interface = (os_http_interface *)user_data;
	artik_error ret;

	log_dbg("");

	ret = os_http_delete(interface->url, interface->headers,
		&interface->response, &interface->status, interface->ssl);

	if (ret != S_OK)
		log_err("os_http_process_delete");

	if (interface->response_cb_params.callback)
		interface->response_cb_params.callback(ret,
			interface->status,
			interface->response,
			interface->response_cb_params.user_data);

	if (interface->url)
		free((void *)interface->url);

	if (interface->headers)
		free(interface->headers);

	if (interface->ssl)
		free_ssl_config(interface->ssl);

	free(interface);

	return 0;
}

artik_error os_http_get_stream(const char *url, artik_http_headers *headers,
		int *status, artik_http_stream_callback callback,
		void *user_data, artik_ssl_config *ssl)
{
	CURL *curl;
	CURLcode res;
	struct curl_slist *h_list = NULL;
	artik_error ret = S_OK;
	stream_callback_params cb_params = { 0 };
	int i;
	long lstatus;

	log_dbg("");

	if (!url || !callback)
		return E_BAD_ARGS;

	mutex_lock();

	/* Initialize curl */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if (!curl) {
		log_err("Failed to initialize curl");
		curl_global_cleanup();
		mutex_unlock();
		return E_NOT_SUPPORTED;
	}

	/* Build request headers if any */
	if (headers && headers->num_fields) {
		for (i = 0; i < headers->num_fields; i++) {
			int hdrlen = strlen(headers->fields[i].name) + 2 +
					strlen(headers->fields[i].data) + 1;
			char *h = malloc(hdrlen);

			snprintf(h, hdrlen, "%s: %s", headers->fields[i].name,
						headers->fields[i].data);
			h_list = curl_slist_append(h_list, h);
			free(h);
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h_list);
	}

	cb_params.callback = callback;
	cb_params.user_data = user_data;

	/* Prepare curl parameters */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&cb_params);
#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	if (ssl && ssl->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
		curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
		curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	if (ssl) {
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION,
							ssl_ctx_callback);
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, ssl);
	}

#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	/* Perform request */
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ret = E_HTTP_ERROR;
		goto exit;
	}

exit:
	if (status) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lstatus);
		*status = (int)lstatus;
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	if (ssl && ssl->se_config)
		release_openssl_engine();

	mutex_unlock();

	if (h_list)
		curl_slist_free_all(h_list);

	return ret;
}

artik_error os_http_get_stream_async(const char *url,
	artik_http_headers *headers,
	artik_http_stream_callback stream_callback,
	artik_http_response_callback response_callback,
	void *user_data,
	artik_ssl_config *ssl)
{
	os_http_interface *interface;
	artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

	log_dbg("");

	if (!url || !stream_callback || !response_callback) {
		log_err("Bad arguments");
		return E_BAD_ARGS;
	}

	interface = malloc(sizeof(os_http_interface));

	if (interface == NULL) {
		log_err("Failed to allocate memory");
		return E_NO_MEM;
	}

	memset(interface, 0, sizeof(os_http_interface));

	interface->url = strdup(url);
	if (headers) {
		interface->headers = malloc(sizeof(artik_http_headers));
		memcpy(interface->headers, headers, sizeof(artik_http_headers));
	}
	interface->stream_cb_params.callback = stream_callback;
	interface->stream_cb_params.user_data = user_data;
	interface->response_cb_params.callback = response_callback;
	interface->response_cb_params.user_data = user_data;

	if (ssl) {
		interface->ssl = copy_ssl_config(ssl);
		if (!interface->ssl) {
			if (interface->headers)
				free(interface->headers);
			free(interface->url);
			free(interface);
			return E_NO_MEM;
		}
	}

	if (loop->add_idle_callback(&interface->loop_process_id,
		os_http_process_get_stream, (void *)interface) != S_OK)
		return E_HTTP_ERROR;

	return S_OK;
}

artik_error os_http_get(const char *url, artik_http_headers *headers,
	char **response, int *status, artik_ssl_config *ssl)
{
	CURL *curl;
	CURLcode res;
	struct curl_slist *h_list = NULL;
	artik_error ret = S_OK;
	int i;
	long lstatus;

	log_dbg("");

	if (!url || !response)
		return E_BAD_ARGS;

	mutex_lock();

	/* Initialize curl */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if (!curl) {
		log_err("Failed to initialize curl");
		curl_global_cleanup();
		mutex_unlock();
		return E_NOT_SUPPORTED;
	}

	/* Build request headers if any */
	if (headers && headers->num_fields) {
		for (i = 0; i < headers->num_fields; i++) {
			int hdrlen = strlen(headers->fields[i].name) + 2 +
					strlen(headers->fields[i].data) + 1;
			char *h = malloc(hdrlen);

			snprintf(h, hdrlen, "%s: %s", headers->fields[i].name,
						headers->fields[i].data);
			h_list = curl_slist_append(h_list, h);
			free(h);
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h_list);
	}

	/* Initialize response */
	*response = NULL;

	/* Prepare curl parameters */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);
#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	if (ssl && ssl->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
		curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
		curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	if (ssl) {
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION,
							ssl_ctx_callback);
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, ssl);
	}

#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	/* Perform request */
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ret = E_HTTP_ERROR;
		goto exit;
	}

exit:
	if (status) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lstatus);
		*status = (int)lstatus;
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	mutex_unlock();

	if (ssl && ssl->se_config)
		release_openssl_engine();

	if (h_list)
		curl_slist_free_all(h_list);

	return ret;
}

artik_error os_http_get_async(const char *url, artik_http_headers *headers,
	artik_http_response_callback callback, void *user_data,
	artik_ssl_config *ssl)
{
	os_http_interface *interface;
	artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

	log_dbg("");

	if (!url || !callback) {
		log_err("Bad arguments");
		return E_BAD_ARGS;
	}

	interface = malloc(sizeof(os_http_interface));

	if (interface == NULL) {
		log_err("Failed to allocate memory");
		return E_NO_MEM;
	}

	memset(interface, 0, sizeof(os_http_interface));

	interface->url = strdup(url);
	if (headers) {
		interface->headers = copy_http_headers(headers);
		if (!interface->headers) {
			log_err("Failed to allocate memory");
			return E_NO_MEM;
		}
	}

	interface->response_cb_params.callback = callback;
	interface->response_cb_params.user_data = user_data;
	interface->ssl = copy_ssl_config(ssl);

	if (loop->add_idle_callback(&interface->loop_process_id,
		os_http_process_get, (void *)interface) != S_OK)
		return E_HTTP_ERROR;

	return S_OK;
}

artik_error os_http_post(const char *url, artik_http_headers *headers,
	const char *body, char **response, int *status, artik_ssl_config *ssl)
{
	CURL *curl;
	CURLcode res;
	struct curl_slist *h_list = NULL;
	artik_error ret = S_OK;
	int i;
	long lstatus;

	log_dbg("");

	if (!url || !response) {
		log_err("Bad arguments");
		return E_BAD_ARGS;
	}

	mutex_lock();

	/* Initialize curl */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if (!curl) {
		log_err("Failed to initialize curl");
		curl_global_cleanup();
		mutex_unlock();
		return E_NOT_SUPPORTED;
	}

	/* Build request headers if any */
	if (headers && headers->num_fields) {
		for (i = 0; i < headers->num_fields; i++) {
			int hdrlen = strlen(headers->fields[i].name) + 2 +
					strlen(headers->fields[i].data) + 1;
			char *h = malloc(hdrlen);

			snprintf(h, hdrlen, "%s: %s", headers->fields[i].name,
						headers->fields[i].data);
			h_list = curl_slist_append(h_list, h);
			free(h);
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h_list);
	}

	/* Initialize response */
	*response = NULL;

	/* Prepare curl parameters */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);
#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	if (ssl && ssl->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
		curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
		curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	if (ssl) {
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION,
							ssl_ctx_callback);
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, ssl);
	}

	if (body)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (void *)body);
	else
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	/* Perform request */
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_err("curl request failed (curl err=%d)", res);
		ret = E_HTTP_ERROR;
		goto exit;
	}

exit:
	if (status) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lstatus);
		*status = (int)lstatus;
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	if (ssl && ssl->se_config)
		release_openssl_engine();

	mutex_unlock();

	if (h_list)
		curl_slist_free_all(h_list);

	return ret;
}

artik_error os_http_post_async(const char *url, artik_http_headers *headers,
	const char *body, artik_http_response_callback callback,
	void *user_data, artik_ssl_config *ssl)
{
	os_http_interface *interface;
	artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

	log_dbg("");

	if (!url || !callback) {
		log_err("Bad arguments");
		return E_BAD_ARGS;
	}

	interface = malloc(sizeof(os_http_interface));

	if (interface == NULL) {
		log_err("Failed to allocate memory");
		return E_NO_MEM;
	}

	memset(interface, 0, sizeof(os_http_interface));

	interface->url = strdup(url);
	if (headers) {
		interface->headers = copy_http_headers(headers);
		if (!interface->headers) {
			log_err("Failed to allocate memory");
			return E_NO_MEM;
		}
	}

	interface->body = body ? strdup(body) : NULL;
	interface->response_cb_params.callback = callback;
	interface->response_cb_params.user_data = user_data;
	interface->ssl = copy_ssl_config(ssl);

	if (loop->add_idle_callback(&interface->loop_process_id,
		os_http_process_post, (void *)interface) != S_OK)
		return E_HTTP_ERROR;

	return S_OK;
}

artik_error os_http_put(const char *url, artik_http_headers *headers,
	const char *body, char **response, int *status, artik_ssl_config *ssl)
{
	CURL *curl;
	CURLcode res;
	struct curl_slist *h_list = NULL;
	artik_error ret = S_OK;
	int i;
	long lstatus;

	log_dbg("");

	if (!url || !response)
		return E_BAD_ARGS;

	mutex_lock();

	/* Initialize curl */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if (!curl) {
		log_err("Failed to initialize curl");
		curl_global_cleanup();
		mutex_unlock();
		return E_NOT_SUPPORTED;
	}

	/* Build request headers if any */
	if (headers && headers->num_fields) {
		for (i = 0; i < headers->num_fields; i++) {
			int hdrlen = strlen(headers->fields[i].name) + 2 +
					strlen(headers->fields[i].data) + 1;
			char *h = malloc(hdrlen);

			snprintf(h, hdrlen, "%s: %s", headers->fields[i].name,
						headers->fields[i].data);
			h_list = curl_slist_append(h_list, h);
			free(h);
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h_list);
	}

	/* Initialize response */
	*response = NULL;

	/* Prepare curl parameters */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);
#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	if (ssl && ssl->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
		curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
		curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	if (ssl) {
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION,
							ssl_ctx_callback);
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, ssl);
	}

	if (body)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (void *)body);

#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	/* Perform request */
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ret = E_HTTP_ERROR;
		goto exit;
	}

exit:
	if (status) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lstatus);
		*status = (int)lstatus;
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	mutex_unlock();

	if (ssl && ssl->se_config)
		release_openssl_engine();

	if (h_list)
		curl_slist_free_all(h_list);

	return ret;
}

artik_error os_http_put_async(const char *url, artik_http_headers *headers,
	const char *body, artik_http_response_callback callback,
	void *user_data, artik_ssl_config *ssl)
{
	os_http_interface *interface;
	artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

	log_dbg("");

	if (!url || !callback) {
		log_err("Bad arguments");
		return E_BAD_ARGS;
	}

	interface = malloc(sizeof(os_http_interface));

	if (interface == NULL) {
		log_err("Failed to allocate memory");
		return E_NO_MEM;
	}

	memset(interface, 0, sizeof(os_http_interface));

	interface->url = strdup(url);
	if (headers) {
		interface->headers = copy_http_headers(headers);
		if (!interface->headers) {
			log_err("Failed to allocate memory");
			return E_NO_MEM;
		}
	}

	interface->body = body ? strdup(body) : NULL;
	interface->response_cb_params.callback = callback;
	interface->response_cb_params.user_data = user_data;
	interface->ssl = copy_ssl_config(ssl);

	if (loop->add_idle_callback(&interface->loop_process_id,
		os_http_process_put, (void *)interface) != S_OK)
		return E_HTTP_ERROR;

	return S_OK;
}

artik_error os_http_delete(const char *url, artik_http_headers *headers,
	char **response, int *status, artik_ssl_config *ssl)
{
	CURL *curl;
	CURLcode res;
	struct curl_slist *h_list = NULL;
	artik_error ret = S_OK;
	int i;
	long lstatus;

	log_dbg("");

	if (!url || !response)
		return E_BAD_ARGS;

	mutex_lock();

	/* Initialize curl */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if (!curl) {
		log_err("Failed to initialize curl");
		curl_global_cleanup();
		mutex_unlock();
		return E_NOT_SUPPORTED;
	}

	/* Build request headers if any */
	if (headers && headers->num_fields) {
		for (i = 0; i < headers->num_fields; i++) {
			int hdrlen = strlen(headers->fields[i].name) + 2 +
					strlen(headers->fields[i].data) + 1;
			char *h = malloc(hdrlen);

			snprintf(h, hdrlen, "%s: %s", headers->fields[i].name,
						headers->fields[i].data);
			h_list = curl_slist_append(h_list, h);
			free(h);
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h_list);
	}

	/* Initialize response */
	*response = NULL;

	/* Prepare curl parameters */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);
#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	if (ssl && ssl->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
		curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
		curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	if (ssl) {
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION,
							ssl_ctx_callback);
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, ssl);
	}

#ifndef NDEBUG
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	/* Perform request */
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ret = E_HTTP_ERROR;
		goto exit;
	}

exit:
	if (status) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lstatus);
		*status = (int)lstatus;
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	mutex_unlock();

	if (ssl && ssl->se_config)
		release_openssl_engine();

	if (h_list)
		curl_slist_free_all(h_list);

	return ret;
}

artik_error os_http_delete_async(const char *url, artik_http_headers *headers,
	artik_http_response_callback callback, void *user_data,
	artik_ssl_config *ssl)
{
	os_http_interface *interface;
	artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

	log_dbg("");

	if (!url || !callback) {
		log_err("Bad arguments");
		return E_BAD_ARGS;
	}

	interface = malloc(sizeof(os_http_interface));

	if (interface == NULL) {
		log_err("Failed to allocate memory");
		return E_NO_MEM;
	}

	memset(interface, 0, sizeof(os_http_interface));

	interface->url = strdup(url);
	if (headers) {
		interface->headers = copy_http_headers(headers);
		if (!interface->headers) {
			log_err("Failed to allocate memory");
			return E_NO_MEM;
		}
	}
	interface->response_cb_params.callback = callback;
	interface->response_cb_params.user_data = user_data;
	interface->ssl = copy_ssl_config(ssl);

	if (loop->add_idle_callback(&interface->loop_process_id,
		os_http_process_delete, (void *)interface) != S_OK)
		return E_HTTP_ERROR;

	return S_OK;
}
