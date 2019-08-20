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

#ifndef INCLUDE_ARTIK_A05X_PLATFORM_H_
#define INCLUDE_ARTIK_A05X_PLATFORM_H_

/*! \file artik_a05x_platform.h
 *
 *  \brief Hardware specific definitions for the ARTIK05x platform
 *
 */

#ifdef __TIZENRT__
#include <artik_adc.h>
#include <artik_gpio.h>
#include <artik_i2c.h>
#include <artik_serial.h>
#include <artik_pwm.h>
#include <artik_spi.h>
#include <artik_network.h>
#include <artik_http.h>
#include <artik_websocket.h>
#include <artik_cloud.h>
#include <artik_security.h>
#include <artik_wifi.h>
#include <artik_lwm2m.h>
#include <artik_mqtt.h>
#include <artik_time.h>
#include <artik_coap.h>
#include <artik_utils.h>

/* List of modules available for the platform */
static const artik_api_module artik_api_a05x_modules[] = {
	{ARTIK_MODULE_ADC,    (char *)"adc",    (char *)&adc_module},
	{ARTIK_MODULE_GPIO,   (char *)"gpio",   (char *)&gpio_module},
	{ARTIK_MODULE_I2C,    (char *)"i2c",    (char *)&i2c_module},
	{ARTIK_MODULE_SERIAL, (char *)"serial",	(char *)&serial_module},
	{ARTIK_MODULE_PWM,    (char *)"pwm",    (char *)&pwm_module},
	{ARTIK_MODULE_SPI,    (char *)"spi",    (char *)&spi_module},
	{ARTIK_MODULE_NETWORK,
		(char *)"network", (char *)&network_module},
	{ARTIK_MODULE_WEBSOCKET,
		(char *)"websocket", (char *)&websocket_module},
	{ARTIK_MODULE_HTTP,
		(char *)"http",      (char *)&http_module},
	{ARTIK_MODULE_CLOUD,
		(char *)"cloud",     (char *)&cloud_module},
	{ARTIK_MODULE_SECURITY,
		(char *)"security",  (char *)&security_module},
	{ARTIK_MODULE_WIFI, (char *)"wifi", (char *)&wifi_module},
	{ARTIK_MODULE_LWM2M, (char *)"lwm2m", (char *)&lwm2m_module},
	{ARTIK_MODULE_MQTT, (char *)"mqtt", (char *)&mqtt_module},
	{ARTIK_MODULE_TIME, (char *)"time", (char *)&time_module},
	{ARTIK_MODULE_COAP, (char *)"coap", (char *)&coap_module},
	{ARTIK_MODULE_UTILS, (char *)"utils", (char *)&utils_module},
	{(artik_module_id_t)-1, NULL, (char *)NULL},
};

/* List of available GPIO IDs */
#define ARTIK_A05x_XGPIO0	29	// GPG0[0]
#define ARTIK_A05x_XGPIO1	30	// GPG0[1]
#define ARTIK_A05x_XGPIO2	31	// GPG0[2]
#define ARTIK_A05x_XGPIO3	32	// GPG0[3]
#define ARTIK_A05x_XGPIO4	33	// GPG0[4]
#define ARTIK_A05x_XGPIO5	34	// GPG0[5]
#define ARTIK_A05x_XGPIO6	35	// GPG0[6]
#define ARTIK_A05x_XGPIO7	36	// GPG0[7]
#define ARTIK_A05x_XGPIO8	37	// GPG1[0]
#define ARTIK_A05x_XGPIO9	38	// GPG1[1]
#define ARTIK_A05x_XGPIO10	39	// GPG1[2]
#define ARTIK_A05x_XGPIO11	40	// GPG1[3]
#define ARTIK_A05x_XGPIO12	41	// GPG1[4]
#define ARTIK_A05x_XGPIO13	42	// GPG1[5]
#define ARTIK_A05x_XGPIO14	43	// GPG1[6]
#define ARTIK_A05x_XGPIO15	44	// GPG1[7]
#define ARTIK_A05x_XGPIO16	45	// GPG2[0]
#define ARTIK_A05x_XGPIO17	46	// GPG2[1]
#define ARTIK_A05x_XGPIO18	47	// GPG2[2]
#define ARTIK_A05x_XGPIO19	48	// GPG2[3]
#define ARTIK_A05x_XGPIO20	49	// GPG2[4]
#define ARTIK_A05x_XGPIO21	50	// GPG2[5]
#define ARTIK_A05x_XGPIO22	51	// GPG2[6]
#define ARTIK_A05x_XGPIO23	52	// GPG2[7]
#define ARTIK_A05x_XGPIO24	53	// GPG3[0]
#define ARTIK_A05x_XGPIO25	54	// GPG3[1]
#define ARTIK_A05x_XGPIO26	55	// GPG3[2]
#define ARTIK_A05x_XGPIO27	56	// GPG3[3]
#define ARTIK_A05x_XGPIO28	20	// GPG2[4]
#define ARTIK_A05x_XEINT0	57	// GPA0[0]
#define ARTIK_A05x_XEINT1	58	// GPA0[1]
#define ARTIK_A05x_XEINT2	59	// GPA0[2]

/* List of available PWM pin IDs */
#define ARTIK_A05x_XPWMOUT0	0
#define ARTIK_A05x_XPWMOUT1	1
#define ARTIK_A05x_XPWMOUT2	2
#define ARTIK_A05x_XPWMOUT3	3
#define ARTIK_A05x_XPWMOUT4	4
#define ARTIK_A05x_XPWMOUT5	5

/* List of available Analog pin IDs */
#define ARTIK_A05x_XADC0AIN0	0
#define ARTIK_A05x_XADC0AIN1	1
#define ARTIK_A05x_XADC0AIN2	2
#define ARTIK_A05x_XADC0AIN3	3

/* List of available UART IDs */
#define ARTIK_A05x_XUART0	1
#define ARTIK_A05x_XUART1	2
#define ARTIK_A05x_XUART2	3
#define ARTIK_A05x_XUART3	4

/* List of available I2C IDs */
#define ARTIK_A05x_XI2C0	0
#define ARTIK_A05x_XI2C1	1

/* List of available SPI IDs */
#define ARTIK_A05x_XSPI0	0
#define ARTIK_A05x_XSPI1	1
#define ARTIK_A05x_XSPI2	2
#define ARTIK_A05x_XSPI3	3

#endif
#endif /* INCLUDE_ARTIK_A05x_PLATFORM_H_ */
