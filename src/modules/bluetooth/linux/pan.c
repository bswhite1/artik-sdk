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
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <gio/gio.h>
#pragma GCC diagnostic pop
#include <glib.h>
#include <ctype.h>

#include "core.h"
#include "pan.h"

#define MAC_ADDR_LEN		17
#define MAC_PREFIX			"/dev_"
#define NETWORK_PATH_LEN	64

#define NAP					"nap"
#define PANU				"panu"
#define GN					"gn"

static char _network_path[NETWORK_PATH_LEN];

static int _is_network_path_valid(void)
{
	/* return value: 1 is valid, 0 invalid */
	if (strncmp(DBUS_BLUEZ_OBJECT_PATH_HCI0, _network_path,
			strlen(DBUS_BLUEZ_OBJECT_PATH_HCI0)) == 0)
		return 1;
	log_err("Network path invalid: [%s]", _network_path);
	return 0;
}

static artik_error _get_pan_property(char *_property, GVariant **variant)
{
	GVariant *result = NULL;
	GError *error = NULL;

	if (_is_network_path_valid() == 0)
		return E_NOT_INITIALIZED;

	result = g_dbus_connection_call_sync(
			hci.conn,
			DBUS_BLUEZ_BUS,
			_network_path,
			DBUS_IF_PROPERTIES,
			"Get",
			g_variant_new("(ss)", DBUS_IF_NETWORK1, _property),
			G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE,
			G_MAXINT, NULL, &error);

	if (error) {
		log_err("Get property failed: %s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	g_variant_get(result, "(v)", variant);
	g_variant_unref(result);
	return S_OK;
}

artik_error _pan_parameter_check(const char *addr,
	const char *uuid)
{
	if (addr) {
		if (strlen(addr) != MAC_ADDR_LEN)
			return E_BT_ERROR;
	}
	if (uuid) {
		if ((strncmp(uuid, NAP, strlen(NAP)))
			&& (strncmp(uuid, PANU, strlen(PANU)))
			&& (strncmp(uuid, GN, strlen(GN))))
			return E_BT_ERROR;
	}
	return S_OK;
}

artik_error bt_pan_register(const char *uuid, const char *bridge)
{
	GVariant *result;
	GError *error = NULL;

	if (!bridge)
		return E_INVALID_VALUE;

	if (_pan_parameter_check(NULL, uuid) != S_OK)
		return E_INVALID_VALUE;

	result = g_dbus_connection_call_sync(hci.conn,
		DBUS_BLUEZ_BUS,
		DBUS_BLUEZ_OBJECT_PATH_HCI0,
		DBUS_IF_NETWORK_SERVER1,
		"Register",
		g_variant_new("(ss)", uuid, bridge),
		NULL, G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);

	if (error) {
		log_dbg("Register network sevice failed :%s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	g_variant_unref(result);

	return S_OK;
}

artik_error bt_pan_unregister(const char *uuid)
{
	GVariant *result;
	GError *error = NULL;

	if (_pan_parameter_check(NULL, uuid) != S_OK)
		return E_INVALID_VALUE;

	result = g_dbus_connection_call_sync(hci.conn,
		DBUS_BLUEZ_BUS,
		DBUS_BLUEZ_OBJECT_PATH_HCI0,
		DBUS_IF_NETWORK_SERVER1,
		"Unregister",
		g_variant_new("(s)", uuid),
		NULL, G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);

	if (error) {
		log_dbg("Unregister network sevice failed :%s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	g_variant_unref(result);

	return S_OK;
}

static artik_error _pan_connect(char *_path, const char *_role,
	char **_interface)
{
	GError *error = NULL;
	GVariant *v;

	v = g_dbus_connection_call_sync(
			hci.conn,
			DBUS_BLUEZ_BUS,
			_path,
			DBUS_IF_NETWORK1,
			"Connect",
			g_variant_new("(s)", _role),
			G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE,
			G_MAXINT, NULL, &error);

	if (error) {
		log_err("Connect failed: %s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	g_variant_get(v, "(s)", _interface);
	g_variant_unref(v);

	return S_OK;
}

static artik_error _generate_device_path(char *_path,
	const char *_mac_addr)
{
	char *token = NULL;
	int input_mac_len = strlen(_mac_addr);
	int cat_count = 0;

	if (input_mac_len != MAC_ADDR_LEN) {
		log_err("MAC length incorrect(%d), must be %d!",
				input_mac_len, MAC_ADDR_LEN);
		return E_BT_ERROR;
	}

	memset(_path, 0, NETWORK_PATH_LEN);
	strncpy(_path, DBUS_BLUEZ_OBJECT_PATH_HCI0,
		strlen(DBUS_BLUEZ_OBJECT_PATH_HCI0));
	strncat(_path, MAC_PREFIX, NETWORK_PATH_LEN - strlen(_path) - 1);

	token = strtok((char *)_mac_addr, ":");
	while (token) {
		++cat_count;
		if (strlen(token) != 2) {
			log_err("MAC format error! Must be as XX:XX:XX:XX:XX:XX");
			return E_BT_ERROR;
		}
		strncat(_path, token, NETWORK_PATH_LEN - strlen(_path) - 1);
		/*last mac addr didn't appen '_'*/
		if (cat_count != 6)
			strncat(_path, "_", NETWORK_PATH_LEN - strlen(_path) - 1);
		token = strtok(NULL, ":");
	}

	log_dbg("get network path:%s", _path);
	return S_OK;
}

artik_error bt_pan_connect(const char *mac_addr,
	const char *uuid, char **network_interface)
{
	if (_is_network_path_valid() == 1)
		return E_BT_ERROR;
	if (!mac_addr)
		return E_INVALID_VALUE;
	if (_pan_parameter_check(mac_addr, uuid) != S_OK)
		return E_INVALID_VALUE;
	if (!network_interface)
		return E_INVALID_VALUE;

	if (_generate_device_path(_network_path, mac_addr) != S_OK)
		return E_BAD_ARGS;

	if (_pan_connect(_network_path, uuid, network_interface) == S_OK)
		return S_OK;
	else
		return E_BT_ERROR;
}

artik_error bt_pan_disconnect(void)
{
	GVariant *result;
	GError *error = NULL;

	if (_is_network_path_valid() == 0)
		return E_NOT_INITIALIZED;

	result = g_dbus_connection_call_sync(
			hci.conn,
			DBUS_BLUEZ_BUS,
			_network_path,
			DBUS_IF_NETWORK1,
			"Disconnect",
			NULL,
			NULL, G_DBUS_CALL_FLAGS_NONE,
			G_MAXINT, NULL, &error);

	memset(_network_path, 0, NETWORK_PATH_LEN);

	if (error) {
		log_err("Connect failed: %s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	g_variant_unref(result);

	return S_OK;
}

bool bt_pan_is_connected(void)
{
	artik_error e = S_OK;
	GVariant *v = NULL;
	bool connected = false;

	e = _get_pan_property("Connected", &v);
	if (e == S_OK && v) {
		connected = g_variant_get_boolean(v);
		g_variant_unref(v);
	}
	return connected;
}

artik_error bt_pan_get_interface(char **_interface)
{
	artik_error e = S_OK;
	GVariant *v = NULL;

	e = _get_pan_property("Interface", &v);
	if (e == S_OK && v) {
		g_variant_get(v, "s", _interface);
		g_variant_unref(v);
	}
	return e;
}

artik_error bt_pan_get_UUID(char **uuid)
{
	artik_error e = S_OK;
	GVariant *v = NULL;

	e = _get_pan_property("UUID", &v);
	if (e == S_OK && v) {
		g_variant_get(v, "s", uuid);
		g_variant_unref(v);
	}
	return e;
}
