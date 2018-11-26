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

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <mosquitto.h>
#include <artik_log.h>
#include <artik_loop.h>
#include <artik_module.h>
#include "../mqtt_client.h"

#define TLS_CA_FILENAME     "/tmp/mqtt-ca.cert"
#define TLS_CERT_FILENAME   "/tmp/mqtt-client.cert"
#define TLS_KEY_FILENAME    "/tmp/mqtt-client.key"

static const char *libname = "libmosquitto";

typedef struct {
	artik_list node;
	artik_mqtt_config *config;
	artik_loop_module *loop;
	const char *libname;
	int version;
	void *mosq;
	int watch_id;
	int periodic_id;

	void *data_cb_connect;
	void *data_cb_disconnect;
	void *data_cb_subscribe;
	void *data_cb_unsubscribe;
	void *data_cb_publish;
	void *data_cb_message;

	connect_callback on_connect;
	disconnect_callback on_disconnect;
	subscribe_callback on_subscribe;
	unsubscribe_callback on_unsubscribe;
	publish_callback on_publish;
	message_callback on_message;
} mqtt_handle_client;

static artik_list *requested_node = NULL;

static void on_connect_callback(struct mosquitto *client, void *handle_client,
				int result)
{
	mqtt_handle_client *client_data = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
		(ARTIK_LIST_HANDLE)handle_client);

	log_dbg("");

	if (client_data && client_data->on_connect)
		client_data->on_connect(client_data->config,
			client_data->data_cb_connect,
			result ? E_MQTT_ERROR : S_OK);
}

static void on_disconnect_callback(struct mosquitto *client,
					void *handle_client, int result)
{
	mqtt_handle_client *client_data = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	log_dbg("");

	if (client_data && client_data->on_disconnect)
		client_data->on_disconnect(client_data->config,
				client_data->data_cb_disconnect,
				result ? E_MQTT_ERROR : S_OK);
}

static void on_subscribe_callback(struct mosquitto *client, void *handle_client,
		int mid, int qos_count, const int *granted_qos)
{
	mqtt_handle_client *client_data = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	log_dbg("");

	if (client_data && client_data->on_subscribe)
		client_data->on_subscribe(client_data->config,
			client_data->data_cb_subscribe, mid, qos_count,
			granted_qos);
}

static void on_unsubscribe_callback(struct mosquitto *client,
					void *handle_client, int mid)
{
	mqtt_handle_client *client_data = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	log_dbg("");

	if (client_data && client_data->on_unsubscribe)
		client_data->on_unsubscribe(client_data->config,
			client_data->data_cb_unsubscribe, mid);
}

static void on_publish_callback(struct mosquitto *client, void *handle_client,
				int mid)
{
	mqtt_handle_client *client_data = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	log_dbg("");

	if (client_data && client_data->on_publish)
		client_data->on_publish(client_data->config,
			client_data->data_cb_publish, mid);
}

static void on_message_callback(struct mosquitto *client, void *handle_client,
				const struct mosquitto_message *msg)
{
	mqtt_handle_client *client_data = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);
	artik_mqtt_msg *received_msg;

	log_dbg("");

	received_msg = (artik_mqtt_msg *) malloc(sizeof(artik_mqtt_msg));

	if (!received_msg) {
		log_err("received_msg is null!");
		return;
	}

	received_msg->msg_id = msg->mid;
	received_msg->topic = msg->topic;
	received_msg->payload = msg->payload;
	received_msg->payload_len = msg->payloadlen;
	received_msg->qos = msg->qos;
	received_msg->retain = msg->retain;

	if (client_data && client_data->on_message)
		client_data->on_message(client_data->config,
			client_data->data_cb_message, received_msg);

	free(received_msg);
}

static void my_log_callback(struct mosquitto *mosq, void *obj, int level,
				const char *str)
{
	log_dbg("%s\n", str);
}

static void tls_cleanup_temp_cert_files(void)
{
	if (access(TLS_CA_FILENAME, F_OK) != -1)
		if (remove(TLS_CA_FILENAME) < 0)
			log_dbg("Failed to delete %s (err=%d)",
					TLS_CA_FILENAME, errno);

	if (access(TLS_CERT_FILENAME, F_OK) != -1)
		if (remove(TLS_CERT_FILENAME) < 0)
			log_dbg("Failed to delete %s (err=%d)",
					TLS_CERT_FILENAME, errno);

	if (access(TLS_KEY_FILENAME, F_OK) != -1)
		if (remove(TLS_KEY_FILENAME) < 0)
			log_dbg("Failed to delete %s (err=%d)",
					TLS_KEY_FILENAME, errno);
}

static artik_error tls_write_temp_cert_files(struct mosquitto *mosq,
		const artik_ssl_config *config)
{
	artik_error ret = S_OK;
	char *ca_cert = NULL;
	char *dev_cert = NULL;
	char *dev_key = NULL;
	int fd = 0;

	if (config->ca_cert.data && config->ca_cert.len) {
		ca_cert = TLS_CA_FILENAME;
		fd = open(ca_cert, O_CREAT | O_WRONLY, 0600);
		if (fd < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}

		if (write(fd, config->ca_cert.data, config->ca_cert.len) < 0) {
			close(fd);
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		close(fd);
	} else if (config->verify_cert == ARTIK_SSL_VERIFY_REQUIRED) {
		/* CA cert is mandatory when requesting verification */
		ret = E_BAD_ARGS;
		goto exit;
	}

	if (config->client_cert.data && config->client_cert.len) {
		dev_cert = TLS_CERT_FILENAME;
		fd = open(dev_cert, O_CREAT | O_WRONLY, 0600);
		if (fd < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}

		if (write(fd, config->client_cert.data, config->client_cert.len) < 0) {
			close(fd);
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		close(fd);
	}

	if (config->client_key.data && config->client_key.len) {
		dev_key = TLS_KEY_FILENAME;
		fd = open(dev_key, O_CREAT | O_WRONLY, 0600);
		if (fd < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}

		if (write(fd, config->client_key.data, config->client_key.len) < 0) {
			close(fd);
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		close(fd);
	}

	if (mosquitto_tls_set(mosq, ca_cert, NULL, dev_cert, dev_key, NULL) !=
			MOSQ_ERR_SUCCESS) {
		ret = E_MQTT_ERROR;
		goto exit;
	}

exit:
	if (ret != S_OK)
		tls_cleanup_temp_cert_files();

	return ret;
}

artik_mqtt_handle mqtt_create_client(artik_mqtt_config *config)
{
	mqtt_handle_client *mqtt_client = NULL;
	int rc;
	int major, minor, revision;

	log_dbg("");

	mqtt_client = (mqtt_handle_client *)artik_list_add(&requested_node, 0,
			sizeof(mqtt_handle_client));
	if (!mqtt_client) {
		log_err("mqtt_client is null.");
		return NULL;
	}
	mqtt_client->node.handle = (ARTIK_LIST_HANDLE)mqtt_client;
	mqtt_client->libname = libname;
	mosquitto_lib_version(&major, &minor, &revision);
	mqtt_client->version = major * 1000000 + minor * 1000 + revision;
	mqtt_client->config = config;

	mosquitto_lib_init();

	mqtt_client->mosq = mosquitto_new(config->client_id,
			config->clean_session, NULL);

	if (!mqtt_client->mosq) {
		free(mqtt_client);
		return NULL;
	}

	if (config->user_name != NULL && config->pwd != NULL) {
		rc = mosquitto_username_pw_set(
				(struct mosquitto *) mqtt_client->mosq,
				config->user_name,
				config->pwd);
		if (rc != MOSQ_ERR_SUCCESS) {
			mqtt_client_destroy_client(mqtt_client);
			return NULL;
		}
	}

	if (config->will_msg) {
		const char *topic;
		int payload_len;
		const void *payload;
		int qos;
		bool retain;

		topic = config->will_msg->topic;
		payload_len = config->will_msg->payload_len;
		payload = config->will_msg->payload;
		qos = config->will_msg->qos;
		retain = config->will_msg->retain;

		rc = mosquitto_will_set((struct mosquitto *) mqtt_client->mosq,
				topic, payload_len, payload, qos, retain);
		if (rc != MOSQ_ERR_SUCCESS) {
			log_err("Invalid parameters for will (err=%d)", rc);
			mqtt_client_destroy_client(mqtt_client);
			return NULL;
		}
	}

	/* set callback */
	mosquitto_user_data_set((struct mosquitto *)mqtt_client->mosq,
			mqtt_client);
	mosquitto_log_callback_set((struct mosquitto *)mqtt_client->mosq,
			my_log_callback);
	mosquitto_connect_callback_set((struct mosquitto *)mqtt_client->mosq,
			on_connect_callback);
	mosquitto_disconnect_callback_set(
			(struct mosquitto *)mqtt_client->mosq,
			on_disconnect_callback);
	mosquitto_publish_callback_set((struct mosquitto *)mqtt_client->mosq,
			on_publish_callback);
	mosquitto_message_callback_set((struct mosquitto *)mqtt_client->mosq,
			on_message_callback);
	mosquitto_subscribe_callback_set((struct mosquitto *)mqtt_client->mosq,
			on_subscribe_callback);
	mosquitto_unsubscribe_callback_set(
			(struct mosquitto *) mqtt_client->mosq,
			on_unsubscribe_callback);

	/* set security parameters */
	if (config->tls) {
		artik_error ret = S_OK;

		mosquitto_tls_opts_set((struct mosquitto *)mqtt_client->mosq,
				(config->tls->verify_cert ==
					ARTIK_SSL_VERIFY_REQUIRED) ? 1 : 0,
				"tlsv1.2", NULL);
		ret = tls_write_temp_cert_files((struct mosquitto *)mqtt_client->mosq,
				config->tls);
		if (ret != S_OK) {
			log_err("Failed to process TLS configuration (err=%d)", ret);
			mqtt_client_destroy_client(mqtt_client);
			return NULL;
		}
	} else if (config->psk && config->psk->psk && config->psk->identity) {
		mosquitto_tls_opts_set((struct mosquitto *)mqtt_client->mosq, 0,
				"tlsv1.2", NULL);
		rc = mosquitto_tls_psk_set((struct mosquitto *)
				mqtt_client->mosq,
				config->psk->psk, config->psk->identity,
				config->psk->ciphers);
		if (rc != MOSQ_ERR_SUCCESS) {
			log_dbg("Failed to set PSK");
			mqtt_client_destroy_client(mqtt_client);
			return NULL;
		}
	}

	mqtt_client->loop = (artik_loop_module *)
					artik_request_api_module("loop");

	return (artik_mqtt_handle)mqtt_client;
}

void mqtt_client_destroy_client(artik_mqtt_handle handle_client)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	log_dbg("");

	if (client) {
		if (client->watch_id > 0)
			client->loop->remove_fd_watch(client->watch_id);
		if (client->periodic_id > 0)
			client->loop->remove_periodic_callback(client->periodic_id);

		mosquitto_destroy((struct mosquitto *) client->mosq);
		mosquitto_lib_cleanup();
		client->mosq = NULL;

		if (client->config->tls)
			tls_cleanup_temp_cert_files();

		if (client->loop)
			artik_release_api_module(client->loop);

		artik_list_delete_node(&requested_node, (artik_list *)client);
	}
}

int mqtt_client_set_willmsg(artik_mqtt_config *config, const char *willtopic,
		const char *willmsg, int qos, bool retain)
{
	artik_mqtt_msg *will_msg;

	log_dbg("");

	if (config && willtopic && willmsg && qos <= 2 && qos >= 0) {
		if (config->will_msg != NULL)
			mqtt_client_free_willmsg(config);
		config->will_msg = (artik_mqtt_msg *)
						malloc(sizeof(artik_mqtt_msg));
		if (!config->will_msg)
			return -MQTT_ERROR_NOMEM;

		will_msg = config->will_msg;
		will_msg->topic = (char *) malloc(strlen(willtopic) + 1);
		if (!will_msg->topic)
			return -MQTT_ERROR_NOMEM;
		strncpy(will_msg->topic, willtopic, strlen(willtopic));
		will_msg->payload = (char *) malloc(strlen(willmsg) + 1);
		if (!will_msg->payload)
			return -MQTT_ERROR_NOMEM;
		strncpy(will_msg->payload, willmsg, strlen(willmsg));
		will_msg->payload_len = strlen(willmsg);
		will_msg->qos = qos;
		will_msg->retain = retain;
	} else
		return -MQTT_ERROR_PARAM;

	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_free_willmsg(artik_mqtt_config *config)
{
	log_dbg("");

	if (config && config->will_msg) {
		if (config->will_msg->topic) {
			free(config->will_msg->topic);
			config->will_msg->topic = NULL;
		}
		if (config->will_msg->payload) {
			free(config->will_msg->payload);
			config->will_msg->payload = NULL;
		}
		free(config->will_msg);
		config->will_msg = NULL;
	}

	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_clear_willmsg(artik_mqtt_handle handle_client)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	int rc = -1;

	log_dbg("");

	if (client && client->mosq) {
		mqtt_client_free_willmsg(client->config);
		rc = mosquitto_will_clear(client->mosq);
		if (rc != MOSQ_ERR_SUCCESS)
			return -MQTT_ERROR_LIB;
		else
			return MQTT_ERROR_SUCCESS;
	} else {
		log_dbg("Clear willmsg error\n");
		return -MQTT_ERROR_PARAM;
	}
}

int mqtt_client_set_connect(artik_mqtt_handle handle_client,
		connect_callback cb, void *user_connect_data)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	if (!client)
		return -MQTT_ERROR_PARAM;
	client->on_connect = cb;
	client->data_cb_connect = user_connect_data;
	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_set_disconnect(artik_mqtt_handle handle_client,
		disconnect_callback cb,	void *user_disconnect_data)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	if (!client)
		return -MQTT_ERROR_PARAM;
	client->on_disconnect = cb;
	client->data_cb_disconnect = user_disconnect_data;
	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_set_subscribe(artik_mqtt_handle handle_client,
		subscribe_callback cb, void *user_subscribe_data)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	if (!client)
		return -MQTT_ERROR_PARAM;
	client->on_subscribe = cb;
	client->data_cb_subscribe = user_subscribe_data;
	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_set_unsubscribe(artik_mqtt_handle handle_client,
		unsubscribe_callback cb, void *user_unsubscribe_data)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	if (!client)
		return -MQTT_ERROR_PARAM;
	client->on_unsubscribe = cb;
	client->data_cb_unsubscribe = user_unsubscribe_data;
	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_set_publish(artik_mqtt_handle handle_client,
		publish_callback cb, void *user_publish_data)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	if (!client)
		return -MQTT_ERROR_PARAM;
	client->on_publish = cb;
	client->data_cb_publish = user_publish_data;
	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_set_message(artik_mqtt_handle handle_client,
		message_callback cb, void *user_message_data)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	if (!client)
		return -MQTT_ERROR_PARAM;
	client->on_message = cb;
	client->data_cb_message = user_message_data;
	return MQTT_ERROR_SUCCESS;
}

static void loop_handle_mosquitto_error(mqtt_handle_client *client, int err)
{
	switch (err) {
	case MOSQ_ERR_NO_CONN:
	case MOSQ_ERR_CONN_LOST:
		if (client && client->on_connect)
			client->on_connect(client->config, client->data_cb_connect,
					E_MQTT_ERROR);
		break;
	default:
		break;
	}
}

static int loop_handler(int fd, enum watch_io io, void *handle_client)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);
	int rc = 0;

	log_dbg("");

	if (!client || !client->mosq)
		return 0;

#if LIBMOSQUITTO_VERSION_NUMBER >= 1004015
	rc = mosquitto_loop_want_connect(client->mosq);
	if (rc != MOSQ_ERR_SUCCESS) {
		log_dbg("mosquitto_loop_want_connect returned %d", rc);
		loop_handle_mosquitto_error(client, rc);
		client->watch_id = 0;
		return 0;
	}
#endif

	rc = mosquitto_loop_read(client->mosq, 1);
	if (rc != MOSQ_ERR_SUCCESS) {
		log_dbg("mosquitto_loop_read returned %d", rc);
		loop_handle_mosquitto_error(client, rc);
		client->watch_id = 0;
		return 0;
	}

	rc = mosquitto_loop_write(client->mosq, 1);
	if (rc != MOSQ_ERR_SUCCESS) {
		log_dbg("mosquitto_loop_write returned %d", rc);
		loop_handle_mosquitto_error(client, rc);
		client->watch_id = 0;
		return 0;
	}

	rc = mosquitto_loop_misc(client->mosq);
	if (rc != MOSQ_ERR_SUCCESS) {
		log_dbg("mosquitto_loop_misc returned %d", rc);
		loop_handle_mosquitto_error(client, rc);
		client->watch_id = 0;
		return 0;
	}

	return 1;
}

static int misc_handler(void *handle_client)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);
	int rc = 0;

	log_dbg("");

	if (!client || !client->mosq)
		return 0;

	rc = mosquitto_loop_misc(client->mosq);
	if (rc != MOSQ_ERR_SUCCESS) {
		log_dbg("mosquitto_loop_misc returned %d", rc);
		loop_handle_mosquitto_error(client, rc);
		client->periodic_id = 0;
		return 0;
	}

	return 1;
}

int mqtt_client_connect(artik_mqtt_handle handle_client, const char *host,
		int port)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);
	int rc;
	int socket_fd;

	log_dbg("");

	if (!client)
		return -MQTT_ERROR_PARAM;

	if (client->config->block)
		rc = mosquitto_connect((struct mosquitto *) client->mosq, host,
				port,
				client->config->keep_alive_time / 1000);
	else
		rc = mosquitto_connect_async((struct mosquitto *) client->mosq,
				host,
				port, client->config->keep_alive_time / 1000);

	if (rc != MOSQ_ERR_SUCCESS)
		return -MQTT_ERROR_LIB;

	socket_fd = mosquitto_socket((struct mosquitto *) client->mosq);

	if (socket_fd == -1) {
		mqtt_client_destroy_client(client);
		return -MQTT_ERROR_LIB;
	}

	/* Add callback for handling socket events */
	client->loop->add_fd_watch(socket_fd,
			WATCH_IO_IN | WATCH_IO_ERR | WATCH_IO_HUP |
			WATCH_IO_NVAL,
			loop_handler, client, &client->watch_id);

	/* Add periodic callback for handling pings */
	client->loop->add_periodic_callback(&client->periodic_id,
			client->config->keep_alive_time / 2, misc_handler, client);

	return MQTT_ERROR_SUCCESS;
}

int mqtt_client_disconnect(artik_mqtt_handle handle_client)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);

	log_dbg("");

	if (!client)
		return -MQTT_ERROR_PARAM;

	return mosquitto_disconnect((struct mosquitto *) client->mosq);
}

int mqtt_client_subscribe(artik_mqtt_handle handle_client, int qos,
		const char *msgtopic)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);
	int rc = MQTT_ERROR_SUCCESS;
	int err = MOSQ_ERR_SUCCESS;

	log_dbg("");

	if (qos < 0 || qos > 2)
		return -MQTT_ERROR_PARAM;

	if (!msgtopic || !client)
		return -MQTT_ERROR_PARAM;

	err = mosquitto_subscribe((struct mosquitto *) client->mosq, NULL,
			msgtopic, qos);

	log_dbg("mosquitto_subscribe rc %d\n", rc);

	if (err != MOSQ_ERR_SUCCESS)
		rc = -MQTT_ERROR_LIB;

	return rc;
}

int mqtt_client_unsubscribe(artik_mqtt_handle handle_client,
		const char *msg_topic)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);
	int rc = MQTT_ERROR_SUCCESS;
	int err = MOSQ_ERR_SUCCESS;

	log_dbg("");

	if (!client || !msg_topic)
		return -MQTT_ERROR_PARAM;

	err = mosquitto_unsubscribe((struct mosquitto *) client->mosq, NULL,
			msg_topic);

	if (err != MOSQ_ERR_SUCCESS)
		rc = -MQTT_ERROR_LIB;

	return rc;
}

int mqtt_client_publish(artik_mqtt_handle handle_client, int qos, bool retain,
		const char *msg_topic, int payload_len, const char *msg_content)
{
	mqtt_handle_client *client = (mqtt_handle_client *)
		artik_list_get_by_handle(requested_node,
			(ARTIK_LIST_HANDLE)handle_client);
	int rc = MQTT_ERROR_SUCCESS;
	int err = MOSQ_ERR_SUCCESS;

	log_dbg("");

	if (qos < 0 || qos > 2)
		return -MQTT_ERROR_PARAM;

	if (!client || !msg_topic || payload_len == 0 || !msg_content)
		return -MQTT_ERROR_PARAM;

	err = mosquitto_publish((struct mosquitto *) client->mosq, NULL,
			msg_topic,
			payload_len, msg_content, qos, retain);

	if (err != MOSQ_ERR_SUCCESS)
		rc = -MQTT_ERROR_LIB;

	return rc;
}
