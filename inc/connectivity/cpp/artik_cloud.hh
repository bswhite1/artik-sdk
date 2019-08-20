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

#ifndef CONNECTIVITY_CPP_ARTIK_CLOUD_HH_
#define CONNECTIVITY_CPP_ARTIK_CLOUD_HH_

#include <artik_module.h>
#include <artik_cloud.h>

/*! \file artik_cloud.hh
 *
 *  \brief C++ Wrapper to the Cloud module
 *
 *  This is a class encapsulation of the C
 *  Cloud module API \ref artik_cloud.h
 */

namespace artik {
/*!
 *  \brief Cloud module C++ Class
 */
class Cloud {
 private:
  artik_cloud_module *m_module;
  char *m_token;
  artik_websocket_handle m_ws_handle;

 public:
  explicit Cloud(const char* token);
  ~Cloud();

  artik_error send_message(const char* device_id, const char* message,
      char **response, artik_ssl_config *ssl);
  artik_error send_message_async(const char* device_id, const char* message,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
  artik_error send_action(const char* device_id, const char* action,
      char **response, artik_ssl_config *ssl);
  artik_error send_action_async(const char* device_id, const char* action,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
  artik_error get_current_user_profile(char **response, artik_ssl_config *ssl);
  artik_error get_current_user_profile_async(
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
  artik_error get_user_devices(int count, bool properties, int offset,
      const char *user_id, char **response, artik_ssl_config *ssl);
  artik_error get_user_devices_async(
      int count, bool properties, int offset, const char *user_id,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
    artik_error get_user_device_types(int count, bool shared, int offset,
      const char *user_id, char **response, artik_ssl_config *ssl);
    artik_error get_user_device_types_async(
      int count, bool shared, int offset, const char *user_id,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
    artik_error get_user_application_properties(const char *user_id,
      const char *app_id, char **response, artik_ssl_config *ssl);
    artik_error get_user_application_properties_async(
     const char *user_id, const char *app_id,
     artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
    artik_error get_device(const char *device_id, bool properties,
      char **response, artik_ssl_config *ssl);
    artik_error get_device_async(const char *device_id, bool properties,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
    artik_error get_device_token(const char *device_id, char **response,
      artik_ssl_config *ssl);
    artik_error get_device_token_async(const char *device_id,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
    artik_error add_device(const char *user_id, const char *dt_id,
      const char *name, char **response, artik_ssl_config *ssl);
    artik_error add_device_async(
      const char *user_id, const char *dt_id, const char *name,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
    artik_error update_device_token(const char *device_id, char **response,
      artik_ssl_config *ssl);
    artik_error update_device_token_async(const char *device_id,
      artik_cloud_callback callback, void *user_data,
      artik_ssl_config *ssl);
    artik_error delete_device_token(const char *device_id, char **response,
      artik_ssl_config *ssl);
    artik_error delete_device_token_async(const char *device_id,
      artik_cloud_callback callback, void *user_data,
      artik_ssl_config *ssl);
    artik_error delete_device(const char *device_id, char **response,
      artik_ssl_config *ssl);
    artik_error delete_device_async(const char *device_id,
      artik_cloud_callback callback, void *user_data,
      artik_ssl_config *ssl);
    artik_error get_device_properties(const char *device_id,
      bool timestamp, char **response, artik_ssl_config *ssl);
    artik_error get_device_properties_async(
      const char *device_id, bool timestamp,
      artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl);
    artik_error set_device_server_properties(const char *device_id,
      const char *data, char **response, artik_ssl_config *ssl_config);
    artik_error set_device_server_properties_async(
      const char *device_id, const char *data,
      artik_cloud_callback callback, void *user_data,
      artik_ssl_config *ssl_config);
    artik_error sdr_start_registration(
      artik_secure_element_config *se_config, const char* device_type_id,
      const char* vendor_id, char **response);
    artik_error sdr_start_registration_async(
      artik_secure_element_config *se_config, const char* device_type_id,
      const char* vendor_id, artik_cloud_callback callback, void *user_data);
    artik_error sdr_registration_status(
      artik_secure_element_config *se_config, const char* reg_id,
      char **response);
    artik_error sdr_registration_status_async(
      artik_secure_element_config *se_config, const char* reg_id,
      artik_cloud_callback callback, void *user_data);
    artik_error sdr_complete_registration(
      artik_secure_element_config *se_config, const char* reg_id,
      const char* reg_nonce, char **response);
    artik_error sdr_complete_registration_async(
      artik_secure_element_config *se_config, const char* reg_id,
      const char* reg_nonce, artik_cloud_callback callback, void *user_data);
  artik_error websocket_open_stream(const char *access_token,
      const char *device_id, unsigned int ping_period,
      unsigned int pong_timeout, artik_ssl_config *ssl);
  artik_error websocket_send_message(char *message);
  artik_error websocket_set_connection_callback(
      artik_websocket_callback callback, void *user_data);
  artik_error websocket_set_receive_callback(artik_websocket_callback callback,
      void *user_data);
  artik_error websocket_close_stream();
};

}  // namespace artik

#endif  // CONNECTIVITY_CPP_ARTIK_CLOUD_HH_
