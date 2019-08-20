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
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>

#include <artik_module.h>
#include <artik_loop.h>
#include <artik_cloud.h>
#include <artik_security.h>

#define TEST_TIMEOUT_MS	(10*1000)
#define TEST_WRITE_LIMIT (5)
#define TEST_WRITE_PERIODIC_MS (1*1000)
#define MAX_PARAM_LEN (128)

static char access_token[MAX_PARAM_LEN];
static char device_id[MAX_PARAM_LEN];
static char *test_message = NULL;

static void websocket_connection_callback(void *user_data, void *result)
{

	intptr_t connected = (intptr_t)result;

	if (connected == ARTIK_WEBSOCKET_CONNECTED)
		fprintf(stdout, "Websocket connected\n");
	else if (connected == ARTIK_WEBSOCKET_CLOSED) {
		fprintf(stdout, "Websocket closed\n");

		artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");
		loop->quit();
		artik_release_api_module(loop);
	} else if (connected == ARTIK_WEBSOCKET_CONNECTION_ERROR) {
		fprintf(stdout, "Websocket connection error\n");

		artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");
		loop->quit();
		artik_release_api_module(loop);
	} else {
		fprintf(stderr, "TEST failed, handshake error\n");

		artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");
		loop->quit();
		artik_release_api_module(loop);
	}
}

static void on_timeout_callback(void *user_data)
{
	artik_loop_module *loop = (artik_loop_module *) user_data;

	fprintf(stdout, "exiting loop\n");

	loop->quit();
}

static int count = 0;

static int on_write_periodic_callback(void *user_data)
{
	artik_cloud_module *cloud = (artik_cloud_module *)
					artik_request_api_module("cloud");

	fprintf(stdout, "writing: %s\n", test_message);
	cloud->websocket_send_message(*(artik_websocket_handle *)user_data,
					test_message);

	artik_release_api_module(cloud);

	count++;

	if (count > TEST_WRITE_LIMIT)
		return 0;

	return 1;
}

static void websocket_receive_callback(void *user_data, void *result)
{
	char *buffer = (char *)result;

	if (buffer == NULL) {
		fprintf(stdout, "receive failed\n");
		return;
	}
	fprintf(stdout, "received: %s\n", buffer);
	free(result);
}

static artik_error test_websocket_read(int timeout_ms,
				artik_ssl_config ssl_config)
{
	artik_error ret = S_OK;
	artik_cloud_module *cloud = (artik_cloud_module *)
					artik_request_api_module("cloud");
	artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

	artik_websocket_handle handle;
	int timeout_id = 0;

	fprintf(stdout, "TEST: %s starting\n", __func__);

	/* Open websocket to ARTIK Cloud and register device to
	 * receive messages from cloud
	 */
	ret = cloud->websocket_open_stream(&handle, access_token, device_id,
					    10000, 5000, &ssl_config);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not open Websocket (%d)\n",
			ret);
		goto exit;
	}

	ret = cloud->websocket_set_connection_callback(handle,
		websocket_connection_callback, &handle);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not open Websocket (%d)\n",
			ret);
		goto exit;
	}

	ret = cloud->websocket_set_receive_callback(handle,
		websocket_receive_callback, &handle);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not open Websocket (%d)\n",
			ret);
		goto exit;
	}

	ret = loop->add_timeout_callback(&timeout_id, timeout_ms,
					on_timeout_callback,
					(void *)loop);

	loop->run();

	cloud->websocket_close_stream(handle);

	fprintf(stdout, "TEST: %s finished\n", __func__);

exit:
	artik_release_api_module(cloud);
	artik_release_api_module(loop);

	return ret;
}

static artik_error test_websocket_write(int timeout_ms,
					artik_ssl_config ssl_config)
{
	artik_error ret = S_OK;
	artik_cloud_module *cloud = (artik_cloud_module *)
					artik_request_api_module("cloud");
	artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

	artik_websocket_handle handle;
	int timeout_id = 0;
	int write_periodic_id = 0;

	fprintf(stdout, "TEST: %s starting\n", __func__);

	/* Open websocket to ARTIK Cloud and register device to receive message
	 * from cloud
	 */
	ret = cloud->websocket_open_stream(&handle, access_token, device_id,
					    10000, 5000, &ssl_config);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not open Websocket (%d)\n",
			ret);
		goto exit;
	}

	ret = cloud->websocket_set_connection_callback(handle,
		websocket_connection_callback, &handle);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not open Websocket (%d)\n",
			ret);
		goto exit;
	}

	ret = cloud->websocket_set_receive_callback(handle,
		websocket_receive_callback, &handle);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not open Websocket (%d)\n",
			ret);
		goto exit;
	}

	ret = loop->add_timeout_callback(&timeout_id, timeout_ms,
		on_timeout_callback, (void *)loop);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not add timeout\n"
			"callback (%d)\n", ret);
		goto exit;
	}

	ret = loop->add_periodic_callback(&write_periodic_id,
		TEST_WRITE_PERIODIC_MS, on_write_periodic_callback, &handle);
	if (ret != S_OK) {
		fprintf(stderr, "TEST failed, could not add periodic\n"
			"callback (%d)\n", ret);
		goto exit;
	}

	loop->run();

	cloud->websocket_close_stream(handle);

	fprintf(stdout, "TEST: %s finished\n", __func__);

exit:
	artik_release_api_module(cloud);
	artik_release_api_module(loop);

	return ret;
}

static artik_error fill_ssl_config(artik_ssl_config *ssl, const char *cert_name)
{
	artik_secure_element_config *se_config = NULL;
	artik_security_module *security = NULL;
	artik_security_handle sec_handle = NULL;

	se_config = malloc(sizeof(artik_secure_element_config));
	if (!se_config) {
		fprintf(stderr, "Failed to allocate memory\n");
		return E_SECURITY_ERROR;
	}

	se_config->key_id = cert_name;
	se_config->key_algo = ECC_SEC_P256R1;
	ssl->se_config = se_config;

	security = (artik_security_module *)artik_request_api_module("security");
	if (security->request(&sec_handle) != S_OK) {
		fprintf(stderr, "Failed to request security module");
		free(se_config);
		artik_release_api_module(security);
		return E_SECURITY_ERROR;
	}

	if (security->get_certificate(sec_handle, cert_name,
			ARTIK_SECURITY_CERT_TYPE_PEM,
			(unsigned char **)&ssl->client_cert.data, &ssl->client_cert.len) != S_OK) {
		fprintf(stderr, "Failed to get certificate from the"\
		   " security module");
		goto error;
	}

	if (security->get_publickey(sec_handle, ECC_SEC_P256R1, cert_name,
			(unsigned char **)&ssl->client_key.data, &ssl->client_key.len) != S_OK) {
		fprintf(stderr, "Failed to get private key form the"\
		   " security module");
		goto error;
	}

	security->release(&sec_handle);
	artik_release_api_module(security);
	return S_OK;

error:
	if (ssl->client_cert.data)
		free(ssl->client_cert.data);

	if (ssl->client_key.data)
		free(ssl->client_key.data);

	if (ssl->se_config)
		free(ssl->se_config);

	security->release(&sec_handle);
	artik_release_api_module(security);
	return E_SECURITY_ERROR;
}

int main(int argc, char *argv[])
{
	int opt;
	artik_error ret = S_OK;
	artik_ssl_config ssl_config;
	struct stat st;
	FILE *f;
	char *root_ca = NULL;
	char *cert_name = NULL;

	memset(&ssl_config, 0, sizeof(artik_ssl_config));

	while ((opt = getopt(argc, argv, "t:d:m:s:vr:")) != -1) {
		switch (opt) {
		case 't':
			strncpy(access_token, optarg, MAX_PARAM_LEN);
			break;
		case 'd':
			strncpy(device_id, optarg, MAX_PARAM_LEN);
			break;
		case 'm':
			test_message = strndup(optarg, strlen(optarg));
			break;
		case 's':
			cert_name = optarg;
			break;
		case 'v':
			ssl_config.verify_cert = ARTIK_SSL_VERIFY_REQUIRED;
			break;
		case 'r':
			f = fopen(optarg, "rb");
			if (!f) {
				fprintf(stderr, "File not found for parameter -r\n");
				return -1;
			}
			if (fstat(fileno(f), &st) < 0) {
				fprintf(stderr, "Failed to get file size\n");
				fclose(f);
				return -1;
			}

			if (root_ca)
				free(root_ca);

			root_ca = malloc(st.st_size + 1);
			if (!root_ca) {
				fprintf(stderr, "Failed to allocate memory for the root CA\n");
				fclose(f);
				return -1;
			}

			if (!fread(root_ca, st.st_size, 1, f)) {
				fprintf(stderr, "Failed to read root CA file\n");
				free(root_ca);
				fclose(f);
				return -1;
			}

			fclose(f);
			break;
		default:
			fprintf(stdout, "Usage: websocket-cloud-test\n"
				"[-t <access token>] [-d <device id>]\n"
				"[-m <JSON type test message>]\n"
				"[-s <certificate name> for enabling SDR (Secure Device\n"
				"Registered) test] \r\n");
			fprintf(stdout, "\t[-v for verifying root certificate]\n"
				"[-r <CA root file>]\r\n");
			return 0;
		}
	}

	if (cert_name) {
		if (fill_ssl_config(&ssl_config, cert_name) != S_OK) {
			if (test_message != NULL)
				free(test_message);
			return -1;
		}
	}

	if (root_ca) {
		ssl_config.ca_cert.data = strdup(root_ca);
		ssl_config.ca_cert.len = strlen(root_ca);
		free(root_ca);
	}

	ret = test_websocket_write(TEST_TIMEOUT_MS, ssl_config);
	if (ret != S_OK)
		goto exit;

	ret = test_websocket_read(TEST_TIMEOUT_MS, ssl_config);

exit:
	if (ssl_config.se_config)
		free(ssl_config.se_config);
	if (test_message != NULL)
		free(test_message);
	if (ssl_config.ca_cert.data != NULL)
		free(ssl_config.ca_cert.data);

	return (ret == S_OK) ? 0 : -1;
}
