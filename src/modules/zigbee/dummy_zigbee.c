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

#include "os_zigbee.h"
#include "artik_log.h"


artik_error os_set_local_endpoint(artik_zigbee_local_endpoint_info
								*endpoint_info)
{
	return E_NOT_SUPPORTED;
}

artik_error os_get_local_endpoint(artik_zigbee_local_endpoint_info
								*endpoint_info)
{
	return E_NOT_SUPPORTED;
}

void os_deinitialize(void)
{
}

artik_error os_zigbee_initialize(artik_zigbee_client_callback callback,
							 void *user_data)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_start(artik_zigbee_network_state *state)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_form_manually(const artik_zigbee_network_info
								*network_info)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_form(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_permitjoin(int duration_sec)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_leave(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_join(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_stop_scan(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_join_manually(const artik_zigbee_network_info
								*network_info)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_find(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_network_request_my_network_status(
					artik_zigbee_network_state *state)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_device_discover(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_set_discover_cycle_time(unsigned int cycle_duration)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_get_discovered_device_list(artik_zigbee_device_info
								*device_info)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_device_request_my_node_type(artik_zigbee_node_type *type)
{
	return E_NOT_SUPPORTED;
}

void os_zigbee_device_find_by_cluster(artik_zigbee_endpoint_list *endpoints,
			int cluster_id, int is_server)
{
}

void os_zigbee_raw_request(const char *command)
{
}

artik_error os_identify_request(artik_zigbee_endpoint_handle handle,
					const artik_zigbee_endpoint *endpoint,
					int duration)
{
	return E_NOT_SUPPORTED;
}

artik_error os_identify_get_remaining_time(artik_zigbee_endpoint_handle handle,
					const artik_zigbee_endpoint *endpoint,
					int *time)
{
	return E_NOT_SUPPORTED;
}

artik_error os_onoff_command(artik_zigbee_endpoint_handle handle,
				const artik_zigbee_endpoint *endpoint,
				artik_zigbee_onoff_status target_status)
{
	return E_NOT_SUPPORTED;
}

artik_error os_groups_get_local_name_support(
				artik_zigbee_endpoint_handle handle,
				int endpoint_id)
{
	return E_NOT_SUPPORTED;
}

artik_error os_groups_set_local_name_support(
				artik_zigbee_endpoint_handle handle,
				bool support)
{
	return E_NOT_SUPPORTED;
}

artik_error os_onoff_get_value(artik_zigbee_endpoint_handle handle,
				artik_zigbee_onoff_status *status)
{
	return E_NOT_SUPPORTED;
}

artik_error os_level_control_request(artik_zigbee_endpoint_handle handle,
			const artik_zigbee_endpoint *endpoint,
			const artik_zigbee_level_control_command *command)
{
	return E_NOT_SUPPORTED;
}

artik_error os_level_control_get_value(artik_zigbee_endpoint_handle handle,
			int *value)
{
	return E_NOT_SUPPORTED;
}

artik_error os_color_control_request(artik_zigbee_endpoint_handle handle,
			const artik_zigbee_endpoint *endpoint,
			const artik_zigbee_color_control_command *command)
{
	return E_NOT_SUPPORTED;
}

artik_error os_color_control_get_value(artik_zigbee_endpoint_handle handle,
			artik_zigbee_color_control_value *value)
{
	return E_NOT_SUPPORTED;
}

artik_error os_illum_set_measured_value_range(
			artik_zigbee_endpoint_handle handle,
			int min, int max)
{
	return E_NOT_SUPPORTED;
}

artik_error os_illum_set_measured_value(artik_zigbee_endpoint_handle handle,
			int value)
{
	return E_NOT_SUPPORTED;
}

artik_error os_illum_get_measured_value(artik_zigbee_endpoint_handle handle,
			int *value)
{
	return E_NOT_SUPPORTED;
}

artik_error os_occupancy_set_type(artik_zigbee_endpoint_handle handle,
			artik_zigbee_occupancy_type type)
{
	return E_NOT_SUPPORTED;
}

artik_error os_occupancy_set_value(artik_zigbee_endpoint_handle handle,
			artik_zigbee_occupancy_status status)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_get_occupied_cooling_setpoint(
			artik_zigbee_endpoint_handle handle,
			int *setpoint)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_get_occupied_heating_setpoint(
			artik_zigbee_endpoint_handle handle,
			int *setpoint)
{
	return E_NOT_SUPPORTED;
}

artik_error os_fan_get_mode(artik_zigbee_endpoint_handle handle,
			artik_zigbee_fan_mode *mode)
{
	return E_NOT_SUPPORTED;
}

artik_error os_fan_get_mode_sequence(artik_zigbee_endpoint_handle handle,
			artik_zigbee_fan_mode_sequence *seq)
{
	return E_NOT_SUPPORTED;
}

/*ezmode commissioning*/
artik_error os_ezmode_commissioning(artik_zigbee_endpoint_handle handle,
				bool initiator, bool start)
{
	return E_NOT_SUPPORTED;
}

artik_error os_factory_reset(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_reset_local(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_network_steering(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_find_bind(artik_zigbee_endpoint_handle handle, bool initiator,
						 bool start)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_broadcast_permitjoin(int duration)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_broadcast_identify_query(
					artik_zigbee_endpoint_handle handle)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_ieee_addr_request(int node_id)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_simple_descriptor_request(int node_id,
					int target_endpoint)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_broadcast_match_descriptor_request(
				ARTIK_ZIGBEE_PROFILE profile_id,
				int cluster_id,
				bool server_cluster)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_get_binding(int index,
				artik_zigbee_binding_table_entry *value)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_set_binding(int index,
				artik_zigbee_binding_table_entry *value)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_delete_binding(int index)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_clear_binding(void)
{
	return E_NOT_SUPPORTED;
}

artik_error os_zigbee_end_device_bind_request(
					artik_zigbee_endpoint_handle handle)
{
	return E_NOT_SUPPORTED;
}

int os_fan_write_mode(artik_zigbee_endpoint_handle handle,
					  const artik_zigbee_endpoint *endpoint,
					  artik_zigbee_fan_mode mode)
{
	return E_NOT_SUPPORTED;
}

int os_fan_write_mode_sequence(artik_zigbee_endpoint_handle handle,
					const artik_zigbee_endpoint *endpoint,
					artik_zigbee_fan_mode_sequence seq)
{
	return E_NOT_SUPPORTED;
}

int os_fan_read_mode(artik_zigbee_endpoint_handle handle,
					 const artik_zigbee_endpoint *endpoint,
					 artik_zigbee_fan_mode *fanmode)
{
	return E_NOT_SUPPORTED;
}

int os_fan_read_mode_sequence(artik_zigbee_endpoint_handle handle,
			const artik_zigbee_endpoint *endpoint,
			artik_zigbee_fan_mode_sequence *fanmode_sequence)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_set_local_temperature(
				artik_zigbee_endpoint_handle handle,
				int temperature)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_get_local_temperature(
				artik_zigbee_endpoint_handle handle,
				int *temperature)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_set_occupancy_status(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_occupancy_status status)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_get_occupancy_status(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_occupancy_status *status)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_set_occupied_cooling_setpoint(
				artik_zigbee_endpoint_handle handle,
				int temperature)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_set_occupied_heating_setpoint(
				artik_zigbee_endpoint_handle handle,
				int temperature)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_set_system_mode(artik_zigbee_endpoint_handle handle,
				artik_zigbee_thermostat_system_mode mode)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_get_system_mode(artik_zigbee_endpoint_handle handle,
				artik_zigbee_thermostat_system_mode *mode)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_set_control_sequence(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_thermostat_control_sequence seq)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_get_control_sequence(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_thermostat_control_sequence *seq)
{
	return E_NOT_SUPPORTED;
}

int os_temperature_set_measured_value(artik_zigbee_endpoint_handle handle,
				int value)
{
	return E_NOT_SUPPORTED;
}

int os_temperature_set_measured_value_range(artik_zigbee_endpoint_handle handle,
				int min,
				int max)
{
	return E_NOT_SUPPORTED;
}

artik_error os_reset_to_factory_default(artik_zigbee_endpoint_handle handle,
				const artik_zigbee_endpoint *endpoint)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_request_setpoint_raise_lower(
				artik_zigbee_endpoint_handle handle,
				const artik_zigbee_endpoint *endpoint,
				artik_zigbee_thermostat_setpoint_mode mode,
				int temperature)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_read_occupied_cooling_setpoint(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_endpoint *endpoint,
				int *temperature)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_read_occupied_heating_setpoint(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_endpoint *endpoint,
				int *temperature)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_read_system_mode(artik_zigbee_endpoint_handle handle,
				artik_zigbee_endpoint *endpoint,
				artik_zigbee_thermostat_system_mode *mode)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_write_system_mode(artik_zigbee_endpoint_handle handle,
				artik_zigbee_endpoint *endpoint,
				artik_zigbee_thermostat_system_mode mode)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_read_control_sequence(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_endpoint *endpoint,
				artik_zigbee_thermostat_control_sequence *seq)
{
	return E_NOT_SUPPORTED;
}

artik_error os_thermostat_write_control_sequence(
				artik_zigbee_endpoint_handle handle,
				artik_zigbee_endpoint *endpoint,
				artik_zigbee_thermostat_control_sequence seq)
{
	return E_NOT_SUPPORTED;
}

artik_error os_request_reporting(artik_zigbee_endpoint_handle handle,
				const artik_zigbee_endpoint *endpoint,
				artik_zigbee_reporting_type report_type,
				int min_interval, int max_interval,
				int change_threshold)
{
	return E_NOT_SUPPORTED;
}

artik_error os_stop_reporting(artik_zigbee_endpoint_handle handle,
				const artik_zigbee_endpoint *endpoint,
				artik_zigbee_reporting_type report_type)
{
	return E_NOT_SUPPORTED;
}
