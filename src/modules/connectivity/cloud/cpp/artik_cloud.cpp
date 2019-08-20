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

#include "artik_cloud.hh"

artik::Cloud::Cloud(const char* token) {
  m_module = reinterpret_cast<artik_cloud_module*>(
      artik_request_api_module("cloud"));
  if (token)
    m_token = strndup(token, MAX_TOKEN_LEN);
  else
    m_token = NULL;

  m_ws_handle = NULL;
}

artik::Cloud::~Cloud() {
  if (m_token)
    free(m_token);

  artik_release_api_module(reinterpret_cast<void*>(this->m_module));
}

artik_error artik::Cloud::send_message(const char* device_id,
    const char* message, char **response, artik_ssl_config *ssl) {
  return m_module->send_message(m_token, device_id, message, response, ssl);
}

artik_error artik::Cloud::send_message_async(const char* device_id,
    const char* message, artik_cloud_callback callback, void *user_data,
    artik_ssl_config *ssl) {
  return m_module->send_message_async(m_token, device_id, message,
    callback, user_data, ssl);
}

artik_error artik::Cloud::send_action(const char* device_id, const char* action,
    char **response, artik_ssl_config *ssl) {
  return m_module->send_action(m_token, device_id, action, response, ssl);
}

artik_error artik::Cloud::send_action_async(const char* device_id,
    const char* action, artik_cloud_callback callback, void *user_data,
    artik_ssl_config *ssl) {
  return m_module->send_action_async(m_token, device_id, action,
    callback, user_data, ssl);
}

artik_error artik::Cloud::get_current_user_profile(char **response,
    artik_ssl_config *ssl) {
  return m_module->get_current_user_profile(m_token, response, ssl);
}

artik_error artik::Cloud::get_current_user_profile_async(
    artik_cloud_callback callback, void *user_data,
    artik_ssl_config *ssl) {
  return m_module->get_current_user_profile_async(m_token,
    callback, user_data, ssl);
}

artik_error artik::Cloud::get_user_devices(int count, bool properties,
    int offset, const char *user_id, char **response, artik_ssl_config *ssl) {
  return m_module->get_user_devices(m_token, count, properties, offset, user_id,
      response, ssl);
}

artik_error artik::Cloud::get_user_devices_async(int count, bool properties,
    int offset, const char *user_id, artik_cloud_callback callback,
    void *user_data, artik_ssl_config *ssl) {
  return m_module->get_user_devices_async(m_token, count, properties, offset,
      user_id, callback, user_data, ssl);
}

artik_error artik::Cloud::get_user_device_types(int count, bool shared,
    int offset, const char *user_id, char **response, artik_ssl_config *ssl) {
  return m_module->get_user_device_types(m_token, count, shared, offset,
      user_id, response, ssl);
}

artik_error artik::Cloud::get_user_device_types_async(int count, bool shared,
    int offset, const char *user_id,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->get_user_device_types_async(m_token, count, shared, offset,
      user_id, callback, user_data, ssl);
}

artik_error artik::Cloud::get_user_application_properties(const char *user_id,
    const char *app_id, char **response, artik_ssl_config *ssl) {
  return m_module->get_user_application_properties(m_token, user_id, app_id,
      response, ssl);
}

artik_error artik::Cloud::get_user_application_properties_async(
    const char *user_id, const char *app_id,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->get_user_application_properties_async(m_token, user_id,
      app_id, callback, user_data, ssl);
}

artik_error artik::Cloud::get_device(const char *device_id, bool properties,
    char **response, artik_ssl_config *ssl) {
  return m_module->get_device(m_token, device_id, properties, response, ssl);
}

artik_error artik::Cloud::get_device_async(
    const char *device_id, bool properties,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->get_device_async(m_token, device_id, properties,
    callback, user_data, ssl);
}

artik_error artik::Cloud::get_device_token(const char *device_id,
    char **response, artik_ssl_config *ssl) {
  return m_module->get_device_token(m_token, device_id, response, ssl);
}

artik_error artik::Cloud::get_device_token_async(const char *device_id,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->get_device_token_async(m_token, device_id,
    callback, user_data, ssl);
}

artik_error artik::Cloud::add_device(const char *user_id, const char *dt_id,
    const char *name, char **response, artik_ssl_config *ssl) {
  return m_module->add_device(m_token, user_id, dt_id, name, response, ssl);
}

artik_error artik::Cloud::add_device_async(
    const char *user_id, const char *dt_id, const char *name,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->add_device_async(m_token, user_id, dt_id, name,
    callback, user_data, ssl);
}

artik_error artik::Cloud::update_device_token(const char *device_id,
    char **response, artik_ssl_config *ssl) {
  return m_module->update_device_token(m_token, device_id, response, ssl);
}

artik_error artik::Cloud::update_device_token_async(const char *device_id,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->update_device_token_async(m_token, device_id,
    callback, user_data, ssl);
}

artik_error artik::Cloud::delete_device_token(const char *device_id,
    char **response, artik_ssl_config *ssl) {
  return m_module->delete_device_token(m_token, device_id, response, ssl);
}

artik_error artik::Cloud::delete_device_token_async(const char *device_id,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->delete_device_token_async(m_token, device_id,
    callback, user_data, ssl);
}

artik_error artik::Cloud::delete_device(const char *device_id, char **response,
    artik_ssl_config *ssl) {
  return m_module->delete_device(m_token, device_id, response, ssl);
}

artik_error artik::Cloud::delete_device_async(const char *device_id,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->delete_device_async(m_token, device_id,
    callback, user_data, ssl);
}

artik_error artik::Cloud::get_device_properties(const char *device_id,
    bool timestamp, char **response, artik_ssl_config *ssl) {
  return m_module->get_device_properties(m_token, device_id, timestamp,
      response, ssl);
}

artik_error artik::Cloud::get_device_properties_async(
    const char *device_id, bool timestamp,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->get_device_properties_async(m_token, device_id, timestamp,
      callback, user_data, ssl);
}

artik_error artik::Cloud::set_device_server_properties(const char *device_id,
    const char *data, char **response, artik_ssl_config *ssl) {
  return m_module->set_device_server_properties(m_token, device_id, data,
      response, ssl);
}

artik_error artik::Cloud::set_device_server_properties_async(
    const char *device_id, const char *data,
    artik_cloud_callback callback, void *user_data, artik_ssl_config *ssl) {
  return m_module->set_device_server_properties_async(m_token, device_id, data,
      callback, user_data, ssl);
}

artik_error artik::Cloud::sdr_start_registration(
    artik_secure_element_config *se_config,
    const char* device_type_id, const char* vendor_id, char **response) {
  return m_module->sdr_start_registration(se_config, device_type_id, vendor_id,
                                          response);
}

artik_error artik::Cloud::sdr_start_registration_async(
    artik_secure_element_config *se_config,
    const char* device_type_id, const char* vendor_id,
    artik_cloud_callback callback, void *user_data) {
  return m_module->sdr_start_registration_async(se_config, device_type_id,
    vendor_id, callback, user_data);
}

artik_error artik::Cloud::sdr_registration_status(
    artik_secure_element_config *se_config,
    const char* reg_id,
    char **response) {
  return m_module->sdr_registration_status(se_config, reg_id, response);
}

artik_error artik::Cloud::sdr_registration_status_async(
    artik_secure_element_config *se_config,
    const char* reg_id,
    artik_cloud_callback callback, void *user_data) {
  return m_module->sdr_registration_status_async(se_config, reg_id,
    callback, user_data);
}

artik_error artik::Cloud::sdr_complete_registration(
    artik_secure_element_config *se_config,
    const char* reg_id, const char* reg_nonce, char **response) {
  return m_module->sdr_complete_registration(se_config, reg_id, reg_nonce,
                                             response);
}

artik_error artik::Cloud::sdr_complete_registration_async(
    artik_secure_element_config *se_config,
    const char* reg_id, const char* reg_nonce,
    artik_cloud_callback callback, void *user_data) {
  return m_module->sdr_complete_registration_async(se_config, reg_id, reg_nonce,
                                             callback, user_data);
}

artik_error artik::Cloud::websocket_open_stream(const char *access_token,
    const char *device_id, unsigned int ping_period, unsigned int pong_timeout,
    artik_ssl_config *ssl) {
  if (m_ws_handle)
    return E_BUSY;

  return m_module->websocket_open_stream(&m_ws_handle, access_token, device_id,
      ping_period, pong_timeout, ssl);
}

artik_error artik::Cloud::websocket_send_message(char *message) {
  return m_module->websocket_send_message(m_ws_handle, message);
}

artik_error artik::Cloud::websocket_set_connection_callback(
    artik_websocket_callback callback, void *user_data) {
  return m_module->websocket_set_connection_callback(m_ws_handle, callback,
      user_data);
}

artik_error artik::Cloud::websocket_set_receive_callback(
    artik_websocket_callback callback, void *user_data) {
  return m_module->websocket_set_receive_callback(m_ws_handle, callback,
      user_data);
}

artik_error artik::Cloud::websocket_close_stream() {
  artik_error ret = S_OK;

  ret = m_module->websocket_close_stream(m_ws_handle);
  if (ret == S_OK)
    m_ws_handle = NULL;

  return ret;
}
