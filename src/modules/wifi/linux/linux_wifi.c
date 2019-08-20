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
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "artik_log.h"
#include "artik_types.h"
#include "artik_wifi.h"
#include "../os_wifi.h"
#include "wpas/wpa_ctrl.h"
#include "wifi.h"

static bool wifi_initialized = false;
static bool wifi_connected = false;
static artik_wifi_mode_t wifi_mode = ARTIK_WIFI_MODE_NONE;

artik_error os_wifi_disconnect(void)
{
	int ret;

	if (!wifi_connected)
		return E_NOT_INITIALIZED;

	ret = wifi_disconnect();
	if (ret != WIFI_SUCCESS)
		return E_WIFI_ERROR;

	wifi_connected = false;

	wifi_mode = ARTIK_WIFI_MODE_NONE;

	return S_OK;
}

artik_error os_wifi_scan_request(void)
{
	int ret;

	if (wifi_mode != ARTIK_WIFI_MODE_STATION)
		return E_NOT_INITIALIZED;

	ret = wifi_scan_request();
	if (ret != WIFI_SUCCESS)
		return (ret == WIFI_ERROR_SCAN_FAIL_BUSY ?
				E_WIFI_ERROR_SCAN_BUSY : E_WIFI_ERROR);

	return S_OK;
}

artik_error os_wifi_init(artik_wifi_mode_t mode)
{
	int ret;

	if (wifi_initialized)
		return E_BUSY;

	if (mode != ARTIK_WIFI_MODE_STATION &&
			mode != ARTIK_WIFI_MODE_AP)
		return E_BAD_ARGS;

	if (mode != ARTIK_WIFI_MODE_AP) {
		ret = wifi_initialize();
		if (ret != WIFI_SUCCESS)
			return E_WIFI_ERROR;
	}

	wifi_initialized = true;

	wifi_mode = mode;

	return S_OK;
}

artik_error os_wifi_deinit(void)
{
	if (!wifi_initialized)
		return E_NOT_INITIALIZED;

	if (wifi_mode != ARTIK_WIFI_MODE_AP)
		wifi_deinitialize();

	wifi_initialized = false;
	wifi_mode = ARTIK_WIFI_MODE_NONE;

	return S_OK;
}

artik_error os_wifi_start_ap(const char *ssid, const char *password,
		unsigned int channel, unsigned int encryption_flags)
{
	struct wpa_ctrl *ctrl = NULL;
	unsigned int len = 128;
	char cmd[128] = "";
	char reply[128] = "";
	artik_error ret = S_OK;

	if (wifi_mode != ARTIK_WIFI_MODE_AP)
		return E_NOT_INITIALIZED;

	if (!ssid ||
	    (strlen(ssid) < 1) ||
		(strlen(ssid) > MAX_AP_NAME_LEN) ||
	    (channel < 1) ||
		(channel > MAX_AP_CHANNEL))
		return E_BAD_ARGS;

	/* Only WPA2 and open modes supported for now */
	if (encryption_flags && !(encryption_flags & WIFI_ENCRYPTION_WPA2))
		return E_NOT_SUPPORTED;

	/* Check if password is provided and valid for relevant modes */
	if (encryption_flags & WIFI_ENCRYPTION_WPA2) {
		if (!password ||
			(strlen(password) < MIN_AP_WPA2PASS_LEN) ||
			(strlen(password) > MAX_AP_WPA2PASS_LEN))
			return E_BAD_ARGS;
	}

	ctrl = wpa_ctrl_open("/var/run/hostapd/wlan0");
	if (!ctrl)
		return E_ACCESS_DENIED;

	/* Disable in case it was already started */
	strncpy(cmd, "DISABLE", 128);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
		ret = E_ACCESS_DENIED;
		goto exit;
	}
	log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

	snprintf(cmd, 128, "SET ssid %s", ssid);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
		ret = E_ACCESS_DENIED;
		goto exit;
	}
	log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

	snprintf(cmd, 128, "SET channel %d", channel);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
		ret = E_ACCESS_DENIED;
		goto exit;
	}
	log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

	if (!encryption_flags) {
		strncpy(cmd, "SET wpa 0", 128);
		if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);
	} else if (encryption_flags & WIFI_ENCRYPTION_WPA2) {
		strncpy(cmd, "SET wpa 2", 128);
		if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

		strncpy(cmd, "SET wpa_key_mgmt WPA-PSK", 128);
		if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

		strncpy(cmd, "SET wpa_pairwise TKIP", 128);
		if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

		strncpy(cmd, "SET rsn_pairwise CCMP", 128);
		if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

		snprintf(cmd, 128, "SET wpa_passphrase %s", password);
		if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
			ret = E_ACCESS_DENIED;
			goto exit;
		}
		log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);
	}

	strncpy(cmd, "ENABLE", 128);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, &len, NULL) < 0) {
		ret = E_ACCESS_DENIED;
		goto exit;
	}
	log_dbg("wpa_ctrl_request: %s => %s", cmd, reply);

exit:
	wpa_ctrl_close(ctrl);

	return ret;
}

artik_error os_wifi_get_scan_result(artik_wifi_ap **aps, int *num_aps)
{
	int i;
	int ret;
	wifi_scan_bssinfo *bssinfo = NULL;
	artik_wifi_ap *result = NULL;

	if (wifi_mode != ARTIK_WIFI_MODE_STATION)
		return E_NOT_INITIALIZED;

	if (!aps || !num_aps)
		return E_BAD_ARGS;

	ret = wifi_get_scan_result(&bssinfo);

	if (ret != WIFI_SUCCESS)
		return E_WIFI_ERROR;

	if (!bssinfo || !bssinfo->bss_count)
		return E_WIFI_ERROR;

	*num_aps = bssinfo->bss_count;
	result = malloc(bssinfo->bss_count * sizeof(artik_wifi_ap));

	if (!result)
		return E_NO_MEM;

	for (i = 0; i < bssinfo->bss_count; i++) {
		snprintf(result[i].name, MAX_AP_NAME_LEN, "%s",
			bssinfo->bss_list[i].ssid);
		snprintf(result[i].bssid, MAX_AP_BSSID_LEN, "%x:%x:%x:%x:%x:%x",
				bssinfo->bss_list[i].bssid[0],
				bssinfo->bss_list[i].bssid[1],
				bssinfo->bss_list[i].bssid[2],
				bssinfo->bss_list[i].bssid[3],
				bssinfo->bss_list[i].bssid[4],
				bssinfo->bss_list[i].bssid[5]);
		result[i].frequency = bssinfo->bss_list[i].freq;
		result[i].signal_level = bssinfo->bss_list[i].rssi;

		switch (bssinfo->bss_list[i].encrypt) {
		case WIFI_SECURITY_MODE_AUTH_OPEN:
			result[i].encryption_flags = WIFI_ENCRYPTION_OPEN;
			break;
		case WIFI_SECURITY_MODE_AUTH_WPA_PSK:
			result[i].encryption_flags = WIFI_ENCRYPTION_WPA;
			break;
		case WIFI_SECURITY_MODE_AUTH_WPA_EAP:
			result[i].encryption_flags = WIFI_ENCRYPTION_WPA;
			break;
		case WIFI_SECURITY_MODE_AUTH_WPA2_PSK:
			result[i].encryption_flags = WIFI_ENCRYPTION_WPA2;
			break;
		case WIFI_SECURITY_MODE_AUTH_WPA2_EAP:
			result[i].encryption_flags = WIFI_ENCRYPTION_WPA2;
			break;
		case WIFI_SECURITY_MODE_ENCRYPT_WEP:
			result[i].encryption_flags = WIFI_ENCRYPTION_WEP;
			break;
		case WIFI_SECURITY_MODE_ENCRYPT_CCMP:
			result[i].encryption_flags = WIFI_ENCRYPTION_WPA2;
			break;
		case WIFI_SECURITY_MODE_ENCRYPT_TKIP:
			result[i].encryption_flags = WIFI_ENCRYPTION_WPA;
			break;
		default:
			result[i].encryption_flags = 0;
			break;
		}
	}

	if (bssinfo) {
		free(bssinfo);
		bssinfo = NULL;
	}

	*aps = result;

	return S_OK;
}

artik_error os_wifi_get_info(artik_wifi_connection_info *connection_info, artik_wifi_ap *ap)
{
	wifi_info info;
	int ret;

	if (wifi_mode != ARTIK_WIFI_MODE_STATION)
		return E_NOT_INITIALIZED;

	if (!ap || !connection_info)
		return E_BAD_ARGS;

	memset(&info, 0, sizeof(wifi_info));
	ret = wifi_get_info(&info);

	if (ret != WIFI_SUCCESS)
		return E_WIFI_ERROR;

	connection_info->connected = (strcmp(info.wpa_state, "COMPLETED")) ? false : true;
	connection_info->error = S_OK;

	snprintf(ap->name, MAX_AP_NAME_LEN, "%s", info.bss.ssid);
	snprintf(ap->bssid, MAX_AP_BSSID_LEN, "%x:%x:%x:%x:%x:%x",
		info.bss.bssid[0],
		info.bss.bssid[1],
		info.bss.bssid[2],
		info.bss.bssid[3],
		info.bss.bssid[4],
		info.bss.bssid[5]);
	ap->frequency = info.bss.freq;
	ap->signal_level = info.bss.rssi;

	switch (info.bss.encrypt) {
	case WIFI_SECURITY_MODE_AUTH_OPEN:
		ap->encryption_flags = WIFI_ENCRYPTION_OPEN;
		break;
	case WIFI_SECURITY_MODE_AUTH_WPA_PSK:
		ap->encryption_flags = WIFI_ENCRYPTION_WPA;
		break;
	case WIFI_SECURITY_MODE_AUTH_WPA_EAP:
		ap->encryption_flags = WIFI_ENCRYPTION_WPA;
		break;
	case WIFI_SECURITY_MODE_AUTH_WPA2_PSK:
		ap->encryption_flags = WIFI_ENCRYPTION_WPA2;
		break;
	case WIFI_SECURITY_MODE_AUTH_WPA2_EAP:
		ap->encryption_flags = WIFI_ENCRYPTION_WPA2;
		break;
	case WIFI_SECURITY_MODE_ENCRYPT_WEP:
		ap->encryption_flags = WIFI_ENCRYPTION_WEP;
		break;
	case WIFI_SECURITY_MODE_ENCRYPT_CCMP:
		ap->encryption_flags = WIFI_ENCRYPTION_WPA2;
		break;
	case WIFI_SECURITY_MODE_ENCRYPT_TKIP:
		ap->encryption_flags = WIFI_ENCRYPTION_WPA;
		break;
	default:
		ap->encryption_flags = 0;
		break;
	}

	return S_OK;
}

artik_error os_wifi_connect(const char *ssid, const char *password,
				bool persistent)
{
	int ret;
	artik_wifi_connection_info info;
	artik_wifi_ap ap;

	if (wifi_mode != ARTIK_WIFI_MODE_STATION)
		return E_NOT_INITIALIZED;


	memset(&info, 0, sizeof(artik_wifi_connection_info));
	memset(&ap, 0, sizeof(artik_wifi_ap));

	ret = os_wifi_get_info(&info, &ap);

	if (ret != WIFI_SUCCESS)
		return E_WIFI_ERROR;

	if (info.connected && !strncmp(ap.name, ssid, MAX_AP_NAME_LEN-1)) {
		wifi_connected = true;
		wifi_force_connect_callback();
		return S_OK;
	}

	ret = wifi_connect(ssid, password, persistent);
	switch (ret) {
	case WIFI_SUCCESS:
		break;
	case WIFI_ERROR_CONNECT_INVALID_SSID:
	case WIFI_ERROR_CONNECT_INVALID_PSK:
		return E_BAD_ARGS;
	default:
		return E_WIFI_ERROR;
	}

	wifi_connected = true;

	return S_OK;
}

artik_error os_wifi_set_connect_callback(artik_wifi_callback user_callback,
					void *user_data)
{
	wifi_set_connect_callback(user_callback, user_data);

	return S_OK;
}

artik_error os_wifi_set_scan_result_callback(artik_wifi_callback user_callback,
						void *user_data)
{
	wifi_set_scan_result_callback(user_callback, user_data);

	return S_OK;
}

artik_error os_wifi_unset_connect_callback(void)
{
	wifi_unset_connect_callback();

	return S_OK;
}

artik_error os_wifi_unset_scan_result_callback(void)
{
	wifi_unset_scan_result_callback();

	return S_OK;
}
