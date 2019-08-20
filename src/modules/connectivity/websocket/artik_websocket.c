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

#include <artik_log.h>
#include <artik_list.h>
#include <artik_utils.h>
#include <artik_module.h>
#include <artik_websocket.h>
#include "os_websocket.h"

static artik_error artik_websocket_request(artik_websocket_handle * handle,
					artik_websocket_config * config);
static artik_error artik_websocket_open_stream(artik_websocket_handle handle);
static artik_error artik_websocket_write_stream(artik_websocket_handle handle,
						char *message);
static artik_error artik_websocket_set_connection_callback(
					artik_websocket_handle
					handle,
					artik_websocket_callback callback,
					void *user_data);
static artik_error artik_websocket_set_receive_callback(artik_websocket_handle
					handle,
					artik_websocket_callback callback,
					void *user_data);
static artik_error artik_websocket_close_stream(artik_websocket_handle handle);
static artik_error artik_websocket_release(artik_websocket_handle handle);

const artik_websocket_module websocket_module = {
	artik_websocket_request,
	artik_websocket_open_stream,
	artik_websocket_write_stream,
	artik_websocket_set_connection_callback,
	artik_websocket_set_receive_callback,
	artik_websocket_close_stream,
	artik_websocket_release
};

typedef struct {
	artik_list node;
	artik_websocket_config config;
	char *host;
	char *path;
	int port;
	bool use_tls;
} websocket_node;

static artik_list *requested_node = NULL;

static int websocket_parse_uri(const char *uri, char **host, char **path,
								int *port, bool *use_tls)
{
	artik_utils_module *utils = artik_request_api_module("utils");
	artik_uri_info uri_info;
	int ret = -1;
	int default_port;
	char *_host = NULL;
	char *_path = NULL;

	if (utils->get_uri_info(&uri_info, uri) != S_OK) {
		artik_release_api_module(utils);
		return -1;
	}

	if (strcmp(uri_info.scheme, "wss") == 0) {
		default_port = 443;
		*use_tls = true;
	} else if (strcmp(uri_info.scheme, "ws") == 0) {
		default_port = 80;
		*use_tls = false;
	} else {
		goto error;
	}

	if (uri_info.port != -1)
		*port = uri_info.port;
	else
		*port = default_port;

	_host = strdup(uri_info.hostname);
	if (!_host)
		goto error;

	_path = strdup(uri_info.path);
	if (!_path) {
		free(_host);
		goto error;
	}

	*host = _host;
	*path = _path;

	ret = 1;

error:
	utils->free_uri_info(&uri_info);
	artik_release_api_module(utils);
	return ret;
}

artik_error artik_websocket_request(artik_websocket_handle *handle,
					artik_websocket_config *config)
{
	websocket_node *node;

	if (!handle || !config || !config->uri)
		return E_BAD_ARGS;

	node = (websocket_node *) artik_list_add(
				&requested_node, 0, sizeof(websocket_node));
	if (!node)
		return E_NO_MEM;

	if (websocket_parse_uri(config->uri, &node->host, &node->path, &node->port,
			&node->use_tls) < 0) {
		log_err("Failed to parse uri");
		artik_list_delete_node(&requested_node, (artik_list *)node);
		return E_BAD_ARGS;
	}

	node->node.handle = (ARTIK_LIST_HANDLE)node;
	if (config != NULL)
		memcpy(&node->config, config, sizeof(node->config));
	*handle = (artik_websocket_handle)node;
	return S_OK;
}

artik_error artik_websocket_open_stream(artik_websocket_handle handle)
{
	artik_error ret = S_OK;
	websocket_node *node = (websocket_node *)artik_list_get_by_handle(
				requested_node, (ARTIK_LIST_HANDLE) handle);

	log_dbg("");

	if (!node)
		return E_BAD_ARGS;

	ret = os_websocket_open_stream(&node->config, node->host, node->path,
			node->port, node->use_tls);
	if (ret != S_OK)
		ret = E_WEBSOCKET_ERROR;

	return ret;
}

artik_error artik_websocket_write_stream(artik_websocket_handle handle,
							char *message)
{
	artik_error ret = S_OK;
	websocket_node *node = (websocket_node *)artik_list_get_by_handle(
				requested_node, (ARTIK_LIST_HANDLE) handle);
	int message_len = 0;

	log_dbg("");

	if (!node || !message)
		return E_BAD_ARGS;

	message_len = strlen(message);
	ret = os_websocket_write_stream(&node->config, message, message_len);
	if (ret != S_OK)
		ret = E_WEBSOCKET_ERROR;

	return ret;
}

artik_error artik_websocket_set_connection_callback(
			artik_websocket_handle handle,
			artik_websocket_callback callback, void *user_data)
{
	artik_error ret = S_OK;
	websocket_node *node = (websocket_node *)artik_list_get_by_handle(
				requested_node, (ARTIK_LIST_HANDLE) handle);

	log_dbg("");

	if (!node)
		return E_BAD_ARGS;

	ret = os_websocket_set_connection_callback(&node->config, callback,
								user_data);
	if (ret != S_OK)
		log_err("set connection callback failed: %d\n", ret);

	return ret;
}

artik_error artik_websocket_set_receive_callback(artik_websocket_handle handle,
			  artik_websocket_callback callback, void *user_data)
{
	artik_error ret = S_OK;
	websocket_node *node = (websocket_node *)artik_list_get_by_handle(
				requested_node, (ARTIK_LIST_HANDLE) handle);

	log_dbg("");

	if (!node)
		return E_BAD_ARGS;

	ret = os_websocket_set_receive_callback(&node->config, callback,
								user_data);
	if (ret != S_OK)
		log_err("set receive callback failed: %d\n", ret);

	return ret;
}

artik_error artik_websocket_close_stream(artik_websocket_handle handle)
{
	artik_error ret = S_OK;
	websocket_node *node = (websocket_node *)artik_list_get_by_handle(
				requested_node, (ARTIK_LIST_HANDLE) handle);

	log_dbg("");

	if (!node)
		return E_BAD_ARGS;

	ret = os_websocket_close_stream(&node->config);
	if (ret != S_OK)
		log_err("close stream failed: %d\n", ret);

	return ret;
}

artik_error artik_websocket_release(artik_websocket_handle handle)
{
	websocket_node *node = (websocket_node *)artik_list_get_by_handle(
				requested_node, (ARTIK_LIST_HANDLE) handle);

	log_dbg("");

	if (!node)
		return E_BAD_ARGS;

	if (node->host)
		free(node->host);
	if (node->path)
		free(node->path);

	artik_list_delete_node(&requested_node, (artik_list *)node);

	return S_OK;
}
