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

#ifndef INCLUDE_ARTIK_ERROR_H_
#define INCLUDE_ARTIK_ERROR_H_

/*! \file artik_error.h
 *
 *  \brief Error codes
 *
 *  Error codes returned by most of the functions
 *  of the API
 */

#define MAX_ERRR_MSG_LEN	64
#define INVALID_MODULE (void *)(-1)
/*!
 *  \brief Error type
 */
typedef int artik_error;

/*!
 *  \brief No error occurred while processing the function
 */
#define S_OK				 (0)

/*!
 *  \brief Wrong arguments passed to the function
 */
#define E_BAD_ARGS			(-1)

/*!
 *  \brief The device is currently in use by another caller
 */
#define E_BUSY				(-2)

/*!
 *  \brief The called module has not been initialized yet
 */
#define E_NOT_INITIALIZED	(-3)

/*!
 *  \brief A memory allocation could not be performed
 */
#define E_NO_MEM			(-4)

/*!
 *  \brief The called function is not supported by the device
 */
#define E_NOT_SUPPORTED		(-5)

/*!
 *  \brief An overflow occurred while receiving data
 */
#define E_OVERFLOW			(-6)

/*!
 *  \brief The function could not access one resource due to
 *  missing permissions
 */
#define E_ACCESS_DENIED		(-7)

/*!
 *  \brief The blocking function was interrupted by an error or
 *  a manual operation
 */
#define E_INTERRUPTED		(-8)

/*!
 *  \brief Requested data is not yet available, try again
 */
#define E_TRY_AGAIN			(-9)

/*!
 *  \brief The function exited because it reached its timeout period
 */
#define E_TIMEOUT			(-10)

/*!
 *  \brief An error occurred with the CoAP
 */
#define E_COAP_ERROR		(-15)

/*!
 *  \brief An invalid value was returned
 */
#define E_INVALID_VALUE		(-11)

/*!
 *  \brief The network connection is invalid
 */
#define E_NOT_CONNECTED     (-13)

/*!
 *  \brief An error happened on a HTTP request
 */
#define E_HTTP_ERROR		(-1000)

/*!
 *  \brief An error occurred with the Bluetooth adapter
 */
#define E_BT_ERROR			(-2000)

/*!
 *  \brief An error occurred with the ZigBee service
 */
#define E_ZIGBEE_ERROR		(-3000)
/*!
 *  \brief The version of Zigbee daemon is wrong
 */
#define E_ZIGBEE_INVALID_DAEMON	(-3001)
/*!
 *  \brief Zigbee daemon wasn't started
 */
#define E_ZIGBEE_NO_DAEMON	(-3002)
/*!
 *  \brief Fail to read message from Zigbee socket
 */
#define E_ZIGBEE_NO_MESSAGE	(-3003)
/*!
 *  \brief No Zigbee device has been discovered
 */
#define E_ZIGBEE_NO_DEVICE	(-3004)
/*!
 *  \brief Zigbee Socket error
 */
#define E_ZIGBEE_ERR_SOCK	(-3005)
/*!
 *  \brief Zigbee Message send error
 */
#define E_ZIGBEE_MSG_SEND_ERROR	(-3006)
/*!
 *  \brief Current network already exists.
 *  It is used for network_join_manually and network_form_manually.
 */
#define E_ZIGBEE_NETWORK_EXIST	(-3007)

/*!
 *  \brief An error occurred in a websocket process
 */
#define E_WEBSOCKET_ERROR	(-4000)

/*!
 *  \brief An error occurred with the Wi-Fi service
 */
#define E_WIFI_ERROR			    (-5000)
#define E_WIFI_ERROR_AUTHENTICATION	(-5001)
#define E_WIFI_ERROR_ASSOCIATION	(-5002)
#define E_WIFI_ERROR_BAD_PARAMS     (-5003)
#define E_WIFI_ERROR_SCAN_BUSY     (-5004)

/*!
 *  \brief An error occurred with the LWM2M service
 */
#define E_LWM2M_ERROR		(-6000)
#define E_LWM2M_DISCONNECTION_ERROR	(-6001)
/*!
 *  \brief An error occurred with the MQTT
 */
#define E_MQTT_ERROR		(-18)

/*!
 *  \brief An error happened on Network service
 */
#define E_NETWORK_ERROR		(-7000)

/*!
 *  \brief Errors specific to the security module
 */
#define E_SECURITY_ERROR                  (-7000)
#define E_SECURITY_INVALID_X509           (-7001)
#define E_SECURITY_INVALID_PKCS7          (-7002)
#define E_SECURITY_CA_VERIF_FAILED        (-7003)
#define E_SECURITY_DIGEST_MISMATCH        (-7004)
#define E_SECURITY_SIGNATURE_MISMATCH     (-7005)
#define E_SECURITY_SIGNING_TIME_ROLLBACK  (-7006)

/*!
 *  \brief The API'action is in progress
 */
#define E_IN_PROGRESS			(-16)

/*!
 *  \brief Error message definition
 *
 *  Structure defining an error and
 *  its user-friendly message
 */
typedef struct {
	int err;
	char msg[MAX_ERRR_MSG_LEN];
} artik_error_msg_string;

static const artik_error_msg_string error_msg_strings[] = {
	{S_OK, "OK"},
	{E_BAD_ARGS, "Bad arguments"},
	{E_BUSY, "Busy"},
	{E_NOT_INITIALIZED, "Not initialized"},
	{E_NO_MEM, "No memory"},
	{E_NOT_SUPPORTED, "Not supported"},
	{E_OVERFLOW, "Overflow"},
	{E_ACCESS_DENIED, "Access denied"},
	{E_INTERRUPTED, "Interrupted"},
	{E_HTTP_ERROR, "HTTP error"},
	{E_TRY_AGAIN, "Try again"},
	{E_TIMEOUT, "Timeout"},
	{E_WEBSOCKET_ERROR, "Websocket error"},
	{E_BT_ERROR, "Bluetooth adapter error"},
	{E_WIFI_ERROR, "Wi-Fi error"},
	{E_COAP_ERROR, "CoAP error"},
	{E_WIFI_ERROR_AUTHENTICATION, "Wi-Fi Authentication failed"},
	{E_WIFI_ERROR_ASSOCIATION, "Wi-Fi Association failed"},
	{E_WIFI_ERROR_BAD_PARAMS, "Wrong Wi-Fi parameters"},
	{E_ZIGBEE_ERROR, "ZigBee error"},
	{E_ZIGBEE_INVALID_DAEMON, "Invalid zigbee daemon"},
	{E_ZIGBEE_NO_DAEMON, "No zigbee daemon"},
	{E_ZIGBEE_NO_MESSAGE, "No zigbee message"},
	{E_ZIGBEE_NO_DEVICE, "No zigbee device"},
	{E_ZIGBEE_ERR_SOCK, "Socket error"},
	{E_ZIGBEE_MSG_SEND_ERROR, "Send message error"},
	{E_ZIGBEE_NETWORK_EXIST, "Network has existed"},
	{E_INVALID_VALUE, "Invalid value"},
	{E_LWM2M_ERROR, "LWM2M error"},
	{E_LWM2M_DISCONNECTION_ERROR, "LWM2M disconnection"},
	{E_MQTT_ERROR, "MQTT error"},
	{E_NETWORK_ERROR, "Network error"},
	{E_SECURITY_ERROR, "Security error"},
	{E_SECURITY_INVALID_X509, "X509 data is not valid"},
	{E_SECURITY_INVALID_PKCS7, "PKCS7 data is not valid"},
	{E_SECURITY_CA_VERIF_FAILED, "Certificate verification against CA failed"},
	{E_SECURITY_DIGEST_MISMATCH, "Computed digest mismatch"},
	{E_SECURITY_SIGNATURE_MISMATCH, "Signature mismatch"},
	{E_SECURITY_SIGNING_TIME_ROLLBACK, "Signing time rollback error"},
	{E_IN_PROGRESS, "Operation in progress"}
};

static const char *error_msg_null = "null";

static inline const char *error_msg(artik_error err)
{
	const artik_error_msg_string *msgs;
	int i, size;

	msgs = error_msg_strings;
	size = sizeof(error_msg_strings) / sizeof(artik_error_msg_string);
	for (i = 0; i < size; i++) {
		if (msgs[i].err == err)
			return (char *)msgs[i].msg;
	}

	return error_msg_null;
}

#endif /* INCLUDE_ARTIK_ERROR_H_ */
