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

#include <artik_network.h>
#include <artik_loop.h>
#include <artik_module.h>
#include <artik_log.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <gio/gio.h>
#pragma GCC diagnostic pop

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "linux/netutils/dhcpc.h"
#include "linux/netutils/dhcpd.h"
#include "linux/netutils/netlib.h"

#include "os_network.h"
#include "common_network.h"

#define ROUTE_EXISTS 17

typedef struct {
	artik_list *root;
	int netlink_sock;
	int icmp_sock;
	int watch_netlink_id;
	int watch_icmp_id;
	GResolver *resolver;
	artik_loop_module *loop;
} watch_online_status_t;

typedef struct {
	artik_watch_online_status_callback callback;
	char *addr;
	int interval;
	int timeout;
	void *user_data;
} watch_online_config;

typedef struct {
	artik_list node;
	watch_online_config config;
	artik_loop_module *loop;
	GResolver *resolver;
	int sock;
	bool online_status;
	int timeout_echo_id;
	bool update_online_status;
	struct sockaddr_storage to;
	bool resolved;
	bool force;
	uint16_t seqno;
} watch_online_node_t;

typedef struct {
	artik_list node;
	int sockfd;
	int watch_id;
	const char *interface;
	artik_network_dhcp_server_config config;
	void *dhcpd_handle;
} dhcp_handle_server;

typedef struct {
	artik_list node;
	int renew_cbk_id;
	artik_loop_module *loop_module;
	const char *interface;
	void *dhcpc_handle;
} dhcp_handle_client;

#define ICMP_HDR_SIZE (sizeof(struct iphdr) + 8)
#define SOCK_ADDR_IN_ADDR(sa) (((struct sockaddr_in *)(sa))->sin_addr)

static int dhcp_client_renew(artik_network_dhcp_client_handle *handle,
		const char *interface);
static void update_online_status(watch_online_node_t *node);

static watch_online_status_t *watch_online_status = NULL;

static artik_list *requested_node = NULL;

static int check_dhcp_server_config(artik_network_dhcp_server_config *config)
{
	const char *str_regex = "^(([0-9]|[1-9][0-9]"\
	"|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]"\
	"|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$";
	regex_t preg;
	int match;
	size_t nmatch = 3;
	regmatch_t pmatch[nmatch];
	int ret = 0;

	if (regcomp(&preg, str_regex, REG_EXTENDED))
		return -1;

	if (preg.re_nsub != 3) {
		ret = -1;
		goto exit;
	}

	if (strcmp(config->ip_addr.address, "")) {
		match = regexec(&preg,
			config->ip_addr.address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong ip_addr");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("ip_addr not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->netmask.address, "")) {
		match = regexec(&preg,
			config->netmask.address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong netmask");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("netmask not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->gw_addr.address, "")) {
		match = regexec(&preg,
			config->gw_addr.address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong gw_addr");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("gw_addr not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->dns_addr[0].address, "")) {
		match = regexec(&preg,
			config->dns_addr[0].address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong dns_addr[0]");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("dns_addr[0] not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->dns_addr[1].address, "")) {
		match = regexec(&preg,
			config->dns_addr[1].address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong dns_addr[1]");
			ret = -1;
			goto exit;
		}
	}

	if (strcmp(config->start_addr.address, "")) {
		match = regexec(&preg,
			config->start_addr.address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong start_addr");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("start_addr not defined");
		ret = -1;
		goto exit;
	}

exit:
	regfree(&preg);
	return ret;
}

static int check_network_config(artik_network_config *config)
{
	const char *str_regex = "^(([0-9]|[1-9][0-9]"\
	"|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]"\
	"|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$";
	regex_t preg;
	int match;
	size_t nmatch = 3;
	regmatch_t pmatch[nmatch];
	int ret = 0;

	if (regcomp(&preg, str_regex, REG_EXTENDED))
		return -1;

	if (preg.re_nsub != 3) {
		ret = -1;
		goto exit;
	}

	if (strcmp(config->ip_addr.address, "")) {
		match = regexec(&preg,
			config->ip_addr.address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong ip_addr");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("ip_addr not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->netmask.address, "")) {
		match = regexec(&preg,
			config->netmask.address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong netmask");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("netmask not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->gw_addr.address, "")) {
		match = regexec(&preg,
			config->gw_addr.address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong gw_addr");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("gw_addr not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->dns_addr[0].address, "")) {
		match = regexec(&preg,
			config->dns_addr[0].address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong dns_addr[0]");
			ret = -1;
			goto exit;
		}
	} else {
		log_err("dns_addr[0] not defined");
		ret = -1;
		goto exit;
	}

	if (strcmp(config->dns_addr[1].address, "")) {
		match = regexec(&preg,
			config->dns_addr[1].address,
			nmatch, pmatch, 0);

		if (match != 0) {
			log_err("Wrong dns_addr[1]");
			ret = -1;
			goto exit;
		}
	}

exit:
	regfree(&preg);
	return ret;
}

bool os_check_echo_response(char *buf, ssize_t len, uint16_t seqno)
{
	struct iphdr *ip = NULL;
	struct icmphdr *icp = NULL;
	pid_t id = getpid();

	if (len >= ICMP_HDR_SIZE) {
		ip = (struct iphdr *)buf;
		icp = (struct icmphdr *)(buf + (ip->ihl*4));

		if (icp->type == ICMP_ECHOREPLY
			&& icp->un.echo.sequence == htons(seqno)
			&& icp->un.echo.id == id) {
			return true;
		}
	}

	log_dbg("Bad echo response");
	return false;
}

bool os_send_echo(int sock, const struct sockaddr *to, uint16_t seqno)
{
	int ret;
	struct icmphdr icp;
	pid_t id = getpid();

	icp.type = ICMP_ECHO;
	icp.code = 0;
	icp.checksum = 0;
	icp.un.echo.sequence = htons(seqno);
	icp.un.echo.id = id;
	icp.checksum = ~chksum(&icp, 8);

	ret = sendto(sock, &icp, 8, 0, to, sizeof(struct sockaddr_in));
	if (ret <= 0) {
		log_dbg("sendto: unable to send ICMP request: %s", strerror(errno));
		return false;
	}

	return true;
}

static void timeout_send_echo_callback(void *user_data)
{
	update_online_status((watch_online_node_t *)user_data);
}

static void notify_online_status_change(watch_online_node_t *node, bool status)
{
	artik_loop_module *loop = node->loop;

	node->resolved = status;
	node->update_online_status = false;

	loop->remove_timeout_callback(node->timeout_echo_id);
	loop->add_timeout_callback(&node->timeout_echo_id, node->config.interval,
							   timeout_send_echo_callback, node);

	if (status == node->online_status && !node->force)
		return;

	node->force = false;
	node->online_status = status;
	node->resolved = status;
	node->config.callback(status, node->config.addr, node->config.user_data);
}

static void timeout_receive_er_callback(void *user_data)
{
	watch_online_node_t *node = user_data;

	notify_online_status_change(node, false);
}

static void send_echo_request(watch_online_node_t *node)
{
	artik_loop_module *loop = node->loop;

	loop->add_timeout_callback(&node->timeout_echo_id, node->config.timeout,
							   timeout_receive_er_callback, node);
	log_dbg("Send echo request - timeoutid %d", node->timeout_echo_id);
	os_send_echo(node->sock, (struct sockaddr *)&node->to, node->seqno);
	node->seqno++;
}

static void get_addresses(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GResolver *resolver = G_RESOLVER(source);
	GList *addresses, *a;
	GSocketAddress *sockaddr = NULL;
	watch_online_node_t *node = (watch_online_node_t *)user_data;

	addresses = g_resolver_lookup_by_name_finish(resolver, res, NULL);

	if (!addresses) {
		notify_online_status_change(node, false);
		return;
	}

	for (a = addresses; a; a = a->next) {
		if (g_inet_address_get_family(a->data) == G_SOCKET_FAMILY_IPV4) {
			sockaddr = g_inet_socket_address_new(a->data, 0);
			break;
		}
	}

	if (!sockaddr) {
		notify_online_status_change((watch_online_node_t *)user_data, false);
		return;
	}

	if (!g_socket_address_to_native(sockaddr, &node->to, sizeof(struct sockaddr_storage), NULL)) {
		notify_online_status_change((watch_online_node_t *)user_data, false);
		return;
	}

	node->resolved = true;
	send_echo_request(node);
}

static void update_online_status(watch_online_node_t *node)
{
	node->update_online_status = true;
	if (!node->resolved) {
		g_resolver_lookup_by_name_async(node->resolver, node->config.addr,
										NULL, get_addresses, node);
		return;
	}

	send_echo_request(node);
}

static int search_node_with_sockaddr(watch_online_node_t *node, struct sockaddr *sock_addr)
{
	if (node->to.ss_family != sock_addr->sa_family)
		return 0;

	if (node->to.ss_family != AF_INET)
		return 0;

	if (SOCK_ADDR_IN_ADDR(&node->to).s_addr != SOCK_ADDR_IN_ADDR(sock_addr).s_addr)
		return 0;

	return 1;
}

static int echo_response_watch(int fd, enum watch_io io, void *user_data)
{
	ssize_t len;
	char buf[64];
	socklen_t fromlen = sizeof(struct sockaddr_storage);
	struct sockaddr_storage from;
	watch_online_node_t *node;

	if (io & (WATCH_IO_NVAL | WATCH_IO_HUP | WATCH_IO_ERR)) {
		log_dbg("IO error");
		return 1;
	}

	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);

	node =
		(watch_online_node_t *)artik_list_get_by_check(
			watch_online_status->root,
			(ARTIK_LIST_FUNCB)&search_node_with_sockaddr,
			&from);

	if (!node) {
#ifndef CONFIG_RELEASE
		char host[INET6_ADDRSTRLEN];

		getnameinfo((struct sockaddr *)&from, fromlen,
					host, INET6_ADDRSTRLEN, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV);
		log_dbg("Node %s not found", host);
#endif
		return 1;
	}

	if (!os_check_echo_response(buf, len, node->seqno - 1))
		return 1;

	notify_online_status_change(node, true);
	return 1;
}

static int network_connection(int fd, enum watch_io io, void *user_data)
{
	int len = 0;
	unsigned char buf[4096];
	struct iovec iov = { buf, sizeof(buf) };
	struct sockaddr_nl addr;
	struct nlmsghdr *hdr = NULL;
	struct ifinfomsg *infomsg = NULL;
	struct msghdr msg = { &addr, sizeof(addr), &iov, 1, NULL, 0, 0 };
	bool online_status = false;

	if (io & (WATCH_IO_NVAL | WATCH_IO_HUP | WATCH_IO_ERR)) {
		log_dbg("%s netlink error", __func__);
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	len = recvmsg(fd, &msg, 0);

	for (hdr = (struct nlmsghdr *)buf; len > 0 && NLMSG_OK(hdr, len); hdr =
							NLMSG_NEXT(hdr, len)) {
		if (hdr->nlmsg_type == NLMSG_DONE ||
						hdr->nlmsg_type == NLMSG_ERROR)
			return 1;

		if (hdr->nlmsg_type ==  RTM_NEWLINK ||
					hdr->nlmsg_type == RTM_DELLINK) {
			infomsg = (struct ifinfomsg *)NLMSG_DATA(hdr);
			online_status = infomsg->ifi_flags & IFF_UP;
		} else if (hdr->nlmsg_type == RTM_NEWADDR ||
						hdr->nlmsg_type == RTM_NEWROUTE)
			online_status = true;

		else if (hdr->nlmsg_type == RTM_DELADDR ||
						hdr->nlmsg_type == RTM_DELROUTE)
			online_status = false;
	}


	watch_online_node_t *node = NULL;

	for (node = (watch_online_node_t *)watch_online_status->root;
		node;
		node = (watch_online_node_t *)node->node.next) {
		artik_loop_module *loop = node->loop;

		if (node->update_online_status)
			continue;

		if (node->online_status == online_status)
			continue;

		log_dbg("remove timeoutid %d", node->timeout_echo_id);
		loop->remove_timeout_callback(node->timeout_echo_id);
		update_online_status(node);
	}

	return 1;
}

static void clean_watch_online_status(void)
{
	watch_online_status->loop->remove_fd_watch(watch_online_status->watch_netlink_id);
	watch_online_status->loop->remove_fd_watch(watch_online_status->watch_icmp_id);
	if (watch_online_status->netlink_sock > 0)
		close(watch_online_status->netlink_sock);
	if (watch_online_status->icmp_sock > 0)
		close(watch_online_status->icmp_sock);
	artik_release_api_module(watch_online_status->loop);
	free(watch_online_status);
	watch_online_status = NULL;
}

static artik_error initialize_watch_online_status(void)
{
	artik_loop_module *loop = artik_request_api_module("loop");
	artik_error ret = S_OK;
	struct sockaddr_nl addr;

	if (!loop)
		return E_NO_MEM;

	watch_online_status = (watch_online_status_t *)
					malloc(sizeof(watch_online_status_t));
	if (!watch_online_status) {
		artik_release_api_module(loop);
		return E_NO_MEM;
	}

	watch_online_status->loop = loop;
	watch_online_status->netlink_sock = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC,
								NETLINK_ROUTE);
	if (watch_online_status->netlink_sock == -1) {
		log_err("couldn't open NETLINK_ROUTE socket");
		free(watch_online_status);
		watch_online_status = NULL;
		artik_release_api_module(loop);
		return E_ACCESS_DENIED;
	}

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE |
					(1<<(RTNLGRP_ND_USEROPT-1));

	if (bind(watch_online_status->netlink_sock, (struct sockaddr *)&addr,
							sizeof(addr)) != 0) {
		log_err("couldn't bind NETLINK_ROUTE socket");
		close(watch_online_status->netlink_sock);
		free(watch_online_status);
		watch_online_status = NULL;
		artik_release_api_module(loop);
		return E_ACCESS_DENIED;
	}

	watch_online_status->icmp_sock = create_icmp_socket(0);
	if (watch_online_status->icmp_sock == -1) {
		log_err("cloudn't open ICMP socket");
		close(watch_online_status->netlink_sock);
		free(watch_online_status);
		watch_online_status = NULL;
		artik_release_api_module(loop);
		return E_ACCESS_DENIED;
	}

	ret = loop->add_fd_watch(watch_online_status->netlink_sock,
		(WATCH_IO_IN | WATCH_IO_ERR | WATCH_IO_HUP |
		WATCH_IO_NVAL),
		network_connection,
		NULL,
		&(watch_online_status->watch_netlink_id));

	if (ret != S_OK) {
		close(watch_online_status->netlink_sock);
		close(watch_online_status->icmp_sock);
		free(watch_online_status);
		watch_online_status = NULL;
		artik_release_api_module(loop);
		log_err("cloudn't watch netlink socket.");
		return E_ACCESS_DENIED;
	}

	ret = loop->add_fd_watch(watch_online_status->icmp_sock,
		(WATCH_IO_IN | WATCH_IO_ERR | WATCH_IO_HUP | WATCH_IO_NVAL),
		echo_response_watch,
		NULL,
		&(watch_online_status->watch_icmp_id));
	if (ret != S_OK) {
		loop->remove_fd_watch(watch_online_status->watch_netlink_id);
		close(watch_online_status->netlink_sock);
		close(watch_online_status->icmp_sock);
		free(watch_online_status);
		watch_online_status = NULL;
		artik_release_api_module(loop);
		log_err("cloudn't watch icmp socket.");
		return E_ACCESS_DENIED;
	}

	watch_online_status->resolver = g_resolver_get_default();
	watch_online_status->root = NULL;

	return S_OK;
}

artik_error os_network_add_watch_online_status(
				artik_watch_online_status_handle * handle,
				const char *addr,
				int interval,
				int timeout,
				artik_watch_online_status_callback app_callback,
				void *user_data)
{
	artik_error ret = S_OK;

	if (!watch_online_status) {
		ret = initialize_watch_online_status();

		if (!watch_online_status || (ret != S_OK))
			return ret;
	}

	watch_online_node_t *node = (watch_online_node_t *)
		artik_list_add(&(watch_online_status->root), 0,
						sizeof(watch_online_node_t));

	if (!node) {
		if (artik_list_size(watch_online_status->root) == 0)
			clean_watch_online_status();

		return E_NO_MEM;
	}

	node->config.addr = strdup(addr);
	node->config.interval = interval;
	node->config.timeout = timeout;
	node->config.callback = app_callback;
	node->config.user_data = user_data;
	node->loop = watch_online_status->loop;
	node->resolver = watch_online_status->resolver;
	node->sock = watch_online_status->icmp_sock;
	node->resolved  = false;
	node->online_status = false;
	node->timeout_echo_id = -1;
	node->update_online_status = false;
	node->resolved = false;
	node->force = true;
	node->seqno = 0;

	*handle = (artik_watch_online_status_handle)node->node.handle;
	update_online_status(node);

	return ret;
}

artik_error os_network_remove_watch_online_status(
					artik_watch_online_status_handle handle)
{
	watch_online_node_t *node;

	if (!watch_online_status)
		return E_NOT_INITIALIZED;

	node =
		(watch_online_node_t *)artik_list_get_by_handle(
			watch_online_status->root,
			(ARTIK_LIST_HANDLE)handle);
	if (!node) {
		log_dbg("node not found");
		return E_NOT_INITIALIZED;
	}

	free(node->config.addr);
	node->loop->remove_timeout_callback(node->timeout_echo_id);

	artik_list_delete_handle(&(watch_online_status->root),
						(ARTIK_LIST_HANDLE)handle);
	if (artik_list_size(watch_online_status->root) == 0)
		clean_watch_online_status();

	return S_OK;
}

static void on_dhcp_client_renew_callback(void *user_data)
{
	artik_network_dhcp_client_handle *handle =
				(artik_network_dhcp_client_handle *)user_data;

	dhcp_handle_client *dhcp_client = (dhcp_handle_client *)
		artik_list_get_by_handle(requested_node,
						(ARTIK_LIST_HANDLE) * handle);

	struct in_addr addr;

	if (!dhcp_client) {
		log_err("No dhcp_client");
		return;
	}

	if (dhcp_client_renew(handle, dhcp_client->interface) != OK) {
		log_err("Failed to renew IP address in callback");

		/* Set IP address to 0.0.0.0 */
		addr.s_addr = INADDR_ANY;
		if (set_ipv4addr(dhcp_client->interface, &addr) == ERROR) {
			log_err("Set IPv4 address failed: %s", strerror(errno));
			return;
		}

		artik_loop_module *loop = (artik_loop_module *)
					artik_request_api_module("loop");

		loop->quit();
	}
}

static int dhcp_client_renew(artik_network_dhcp_client_handle *handle,
		const char *interface)
{
	int ret = OK;
	struct in_addr addr;

	if (!*handle) {
		log_err("DHCP Client open failed");
		ret = ERROR;
		goto exit;
	} else {
		struct dhcpc_state ds;
		dhcp_handle_client *dhcp_client = (dhcp_handle_client *)
			artik_list_get_by_handle(requested_node,
						(ARTIK_LIST_HANDLE) * handle);

		log_dbg("Renewing IP address");

		if (!dhcp_client) {
			log_err("Could not find DHCP client instance");
			ret = ERROR;
			goto exit;
		}

		if (get_ipv4addr(interface, &addr) < 0) {
			log_err("Failed to get IP address");
			ret = ERROR;
			goto exit;
		}

		if (dhcpc_request(*handle, &ds, interface, &addr, true)
								== ERROR) {
			log_err("DHCP Client request failed");
			ret = ERROR;
			goto exit;
		}

		/* Set IP address */
		if (set_ipv4addr(interface, &ds.ipaddr) == ERROR) {
			log_err("Set IPv4 address failed: %s", strerror(errno));
			ret = ERROR;
			goto exit;
		}

		/* Set net mask */
		if (ds.netmask.s_addr != 0) {
			if (set_ipv4netmask(interface, &ds.netmask) == ERROR) {
				log_err("Set IPv4 network mask failed: %s",
							strerror(errno));
				ret = ERROR;
				goto exit;
			}
		}

		/* Set default router */
		if (ds.default_router.s_addr != 0) {
			if (set_dripv4addr(interface, &ds.default_router)
								== ERROR) {
				log_err("Set default router address failed: %s",
							strerror(errno));
				ret = ERROR;
				goto exit;
			}
		}

		/* Set DNS address */
		if (ds.dnsaddr.s_addr != 0) {
			if (set_ipv4dnsaddr(&ds.dnsaddr, false) == ERROR) {
				log_err("Set DNS adress failed: %s",
							strerror(errno));
				ret = ERROR;
				goto exit;
			}
		}

		/* Set route with gateway */
		if (set_defaultroute(interface, &ds.default_router, true)
					== ERROR && errno != ROUTE_EXISTS) {
			log_err("Set default route with GW failed: %s",
							strerror(errno));
			ret = ERROR;
			goto exit;
		}

		/*
		 * Add timeout callback for renewing IP address before the lease
		 * expires
		 */
		ret = dhcp_client->loop_module->remove_timeout_callback(
						dhcp_client->renew_cbk_id);

		if (ret != S_OK) {
			log_err("Failed to remove callback for renewing IP addr"
									);
			return ret;
		}

		ret = dhcp_client->loop_module->add_timeout_callback(
						&dhcp_client->renew_cbk_id,
						(ds.lease_time-(30))*1000,
						on_dhcp_client_renew_callback,
						handle);

		if (ret != S_OK) {
			log_err("Failed to start callback for renewing IP addr"
									);
			return ret;
		}

		log_dbg("IP: %s", inet_ntoa(ds.ipaddr));
	}


exit:
	return ret;
}


artik_error os_dhcp_client_start(artik_network_dhcp_client_handle *handle,
		artik_network_interface_t interface)
{
	artik_error ret = S_OK;
	uint8_t mac[IFHWADDRLEN];
	struct in_addr addr;
	struct dhcpc_state ds;
	dhcp_handle_client *dhcp_client = NULL;

	dhcp_client = (dhcp_handle_client *)artik_list_add(&requested_node, 0,
			sizeof(dhcp_handle_client));

	if (!dhcp_client) {
		ret = E_NO_MEM;
		goto exit;
	}

	dhcp_client->node.handle = (ARTIK_LIST_HANDLE)dhcp_client;
	dhcp_client->loop_module = (artik_loop_module *)
				artik_request_api_module("loop");
	dhcp_client->interface = (interface == ARTIK_WIFI) ? "wlan0" : "eth0";

	log_dbg("Getting IP address");

	/* Delete all routes from interface if they exist */
	if (del_allroutes_interface(dhcp_client->interface) == ERROR) {
		log_err("Delete all routes from interface %s failed: %s",
				dhcp_client->interface, strerror(errno));
		return E_NETWORK_ERROR;
	}

	/* Set the default route to 0.0.0.0 */
	addr.s_addr = INADDR_ANY;
	if (set_defaultroute(dhcp_client->interface, &addr, false) == ERROR) {
		log_err("Set default route failed: %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	/* Get the MAC address */
	if (getmacaddr(dhcp_client->interface, mac) == ERROR) {
		log_err("Get MAC address failed : %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	/* Set up the DHCPC modules */
	dhcp_client->dhcpc_handle = dhcpc_open(&mac, IFHWADDRLEN);
	if (!dhcp_client->dhcpc_handle) {
		log_err("DHCP Client open failed");
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Start DHCP request */
	if (dhcpc_request(dhcp_client->dhcpc_handle, &ds, dhcp_client->interface,
			NULL, false) == ERROR) {
		log_err("DHCP Client request failed");
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set IP address */
	if (set_ipv4addr(dhcp_client->interface, &ds.ipaddr) == ERROR) {
		log_err("Set IPv4 address failed: %s", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set net mask */
	if (ds.netmask.s_addr != 0) {
		if (set_ipv4netmask(dhcp_client->interface, &ds.netmask) == ERROR) {
			log_err("Set IPv4 network mask failed: %s",	strerror(errno));
			ret = E_NETWORK_ERROR;
			goto exit;
		}
	}

	/* Set default router */
	if (ds.default_router.s_addr != 0) {
		if (set_dripv4addr(dhcp_client->interface, &ds.default_router)
				== ERROR) {
			log_err("Set default router address failed: %s",
					strerror(errno));
			ret = E_NETWORK_ERROR;
			goto exit;
		}
	}

	/* Set DNS address */
	if (ds.dnsaddr.s_addr != 0) {
		if (set_ipv4dnsaddr(&ds.dnsaddr, false) == ERROR) {
			log_err("Set DNS adress failed: %s", strerror(errno));
			ret = E_NETWORK_ERROR;
			goto exit;
		}
	}

	/* Set route with gateway */
	if (set_defaultroute(dhcp_client->interface, &ds.default_router, true)
			== ERROR) {
		log_err("Set default route with GW failed: %s",	strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/*
	 * Add timeout callback for renewing IP address before the lease
	 * expires
	 */
	ret = dhcp_client->loop_module->add_timeout_callback(
					&dhcp_client->renew_cbk_id,
					(ds.lease_time-(30))*1000,
					on_dhcp_client_renew_callback,
					(void *)handle);

	if (ret != S_OK) {
		log_err("Failed to start callback for renewing IP addr");
		goto exit;
	}

	log_dbg("IP: %s", inet_ntoa(ds.ipaddr));

	*handle = (artik_network_dhcp_client_handle *)dhcp_client;

exit:
	if (ret != S_OK) {
		if (dhcp_client) {
			if (dhcp_client->dhcpc_handle)
				dhcpc_close(dhcp_client->dhcpc_handle);
			if (dhcp_client->loop_module)
				artik_release_api_module(dhcp_client->loop_module);
			artik_list_delete_node(&requested_node, (artik_list *)dhcp_client);
		}
	}

	return ret;
}

artik_error os_dhcp_client_stop(artik_network_dhcp_client_handle handle)
{
	dhcp_handle_client *dhcp_client = (dhcp_handle_client *)
		artik_list_get_by_handle(requested_node,
		(ARTIK_LIST_HANDLE) handle);

	if (!dhcp_client)
		return E_BAD_ARGS;

	dhcpc_close(dhcp_client->dhcpc_handle);
	artik_release_api_module(dhcp_client->loop_module);
	artik_list_delete_node(&requested_node, (artik_list *)dhcp_client);

	return S_OK;
}

artik_error os_dhcp_server_start(artik_network_dhcp_server_handle *handle,
		artik_network_dhcp_server_config *config)
{
	artik_error ret = S_OK;
	struct in_addr addr;
	dhcp_handle_server *dhcp_server = NULL;

	if (!handle || !config)
		return E_BAD_ARGS;

	if (check_dhcp_server_config(config) < 0) {
		log_err("Wrong server config");
		return E_NETWORK_ERROR;
	}

	dhcp_server = (dhcp_handle_server *)artik_list_add(&requested_node, 0,
						sizeof(dhcp_handle_server));

	if (!dhcp_server)
		return E_NO_MEM;

	dhcp_server->node.handle = (ARTIK_LIST_HANDLE)dhcp_server;
	dhcp_server->interface = config->interface == ARTIK_WIFI ? "wlan0" : "eth0";
	memcpy(&dhcp_server->config, config, sizeof(dhcp_server->config));

	/* Delete all routes from interface if they exist */
	if (del_allroutes_interface(dhcp_server->interface) == ERROR) {
		log_err("Delete all routes from interface %s failed: %s",
			dhcp_server->interface,
			strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set IP address */
	addr.s_addr = inet_addr(dhcp_server->config.ip_addr.address);
	if (set_ipv4addr(dhcp_server->interface, &addr) == ERROR) {
		log_err("Set IPv4 address failed: %s", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set net mask */
	addr.s_addr = inet_addr(dhcp_server->config.netmask.address);
	if (set_ipv4netmask(dhcp_server->interface, &addr) == ERROR) {
		log_err("Set IPv4 network mask failed: %s", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set default router */
	addr.s_addr = inet_addr(dhcp_server->config.gw_addr.address);
	if (set_dripv4addr(dhcp_server->interface, &addr) == ERROR) {
		log_err("Set default router address failed: %s", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set the default route to 0.0.0.0 */
	addr.s_addr = INADDR_ANY;
	if (set_defaultroute(dhcp_server->interface, &addr, false) == ERROR) {
		log_err("Set default route failed: %s", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Start the DHCP server */
	dhcp_server->dhcpd_handle = dhcpd_start(&dhcp_server->config);
	if (!dhcp_server->dhcpd_handle) {
		log_err("Failed to start DHCP Server (err=%s)", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	*handle = (artik_network_dhcp_server_handle)dhcp_server;

exit:
	if (ret != S_OK)
		artik_list_delete_node(&requested_node, (artik_list *)dhcp_server);

	return ret;
}

artik_error os_dhcp_server_stop(artik_network_dhcp_server_handle handle)
{
	artik_error ret = S_OK;
	struct in_addr addr;
	dhcp_handle_server *dhcp_server = (dhcp_handle_server *)
		artik_list_get_by_handle(requested_node,
		(ARTIK_LIST_HANDLE) handle);

	if (!dhcp_server)
		return E_BAD_ARGS;

	/* Stop the DHCP server */
	dhcpd_stop(dhcp_server->dhcpd_handle);

	/* Delete all routes from interface if they exist */
	if (del_allroutes_interface(dhcp_server->interface) == ERROR) {
		log_err("Delete all routes from interface %s failed: %s",
			dhcp_server->interface,
			strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set the default route to 0.0.0.0 */
	addr.s_addr = INADDR_ANY;
	if (set_defaultroute(dhcp_server->interface, &addr, false) == ERROR) {
		log_err("Set default route failed: %s", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

	/* Set IP address to 0.0.0.0 */
	if (set_ipv4addr(dhcp_server->interface, &addr) == ERROR) {
		log_err("Set IPv4 address failed: %s", strerror(errno));
		ret = E_NETWORK_ERROR;
		goto exit;
	}

exit:
	artik_list_delete_node(&requested_node, (artik_list *)dhcp_server);

	return ret;
}

artik_error os_set_network_config(artik_network_config *config,
		artik_network_interface_t interface)
{
	artik_error ret = S_OK;
	char *_interface;
	struct in_addr addr;

	if (!config)
		return E_BAD_ARGS;

	if (((int)interface < 0) || ((int)interface > ARTIK_ETHERNET))
		return E_BAD_ARGS;

	_interface = (interface == ARTIK_WIFI) ? "wlan0" : "eth0";

	if (check_network_config(config) < 0) {
		log_err("Wrong network config");
		return E_BAD_ARGS;
	}

	/* Set IP address */
	if (inet_aton(config->ip_addr.address, &addr) == 0) {
		log_err("Error inet_aton ip_addr");
		return E_NETWORK_ERROR;
	}

	if (set_ipv4addr(_interface, &addr) == ERROR) {
		log_err("Set IPv4 address failed: %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	/* Set net mask */
	if (inet_aton(config->netmask.address, &addr) == 0) {
		log_err("Error inet_aton netmask");
		return E_NETWORK_ERROR;
	}

	if (set_ipv4netmask(_interface, &addr) == ERROR) {
		log_err("Set IPv4 network mask failed: %s",
			strerror(errno));
		return E_NETWORK_ERROR;
	}

	/* Set default router */
	if (inet_aton(config->gw_addr.address, &addr) == 0) {
		log_err("Error inet_aton gw_addr");
		return E_NETWORK_ERROR;
	}

	if (set_dripv4addr(_interface, &addr) == ERROR) {
		log_err("Set default router address failed: %s",
			strerror(errno));
		return E_NETWORK_ERROR;
	}

	/* Set route with gateway */
	if (set_defaultroute(_interface, &addr, true) == ERROR) {
		log_err("Set default route with GW failed: %s",
			strerror(errno));
		return E_NETWORK_ERROR;
	}

	/* Set DNS address */
	for (int i = 0; i < MAX_DNS_ADDRESSES; i++) {

		bool append = i == 0 ? false : true;

		if (strcmp(config->dns_addr[i].address, "")) {
			if (inet_aton(config->dns_addr[i].address,
							&addr) == 0) {
				log_err("Error inet_aton dns_addr");
				return E_NETWORK_ERROR;
			}


			if (set_ipv4dnsaddr(&addr, append) == ERROR) {
				log_err("Set DNS adress failed: %s",
					strerror(errno));
				return E_NETWORK_ERROR;
			}
		}
	}

	return ret;
}

artik_error os_get_network_config(artik_network_config *config,
		artik_network_interface_t interface)
{
	artik_error ret = S_OK;
	uint8_t mac[IFHWADDRLEN];
	char *_interface;
	struct in_addr addr;
	struct in_addr dnsAddr[MAX_DNS_ADDRESSES] = { 0 };

	if (!config)
		return E_BAD_ARGS;

	if (((int)interface < 0) || ((int)interface > ARTIK_ETHERNET))
		return E_BAD_ARGS;

	_interface = (interface == ARTIK_WIFI) ? "wlan0" : "eth0";

	/* Get IP address */
	if (get_ipv4addr(_interface, &addr) == ERROR) {
		log_err("Get IPv4 address failed: %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	snprintf(config->ip_addr.address, MAX_IP_ADDRESS_LEN,
			"%d.%d.%d.%d",
			(addr.s_addr) & 0xff,
			(addr.s_addr >> 8) & 0xff,
			(addr.s_addr >> 16) & 0xff,
			(addr.s_addr >> 24) & 0xff);

	/* Get MAC address */
	if (getmacaddr(_interface, mac) == ERROR) {
		log_err("Get MAC address failed : %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	snprintf((char *)config->mac_addr, MAX_MAC_ADDRESS_LEN,
		"%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2],
		mac[3], mac[4], mac[5]);

	/* Get Mask */
	if (get_ipv4netmask(_interface, &addr) == ERROR) {
		log_err("Get mask failed: %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	snprintf(config->netmask.address, MAX_IP_ADDRESS_LEN,
			"%d.%d.%d.%d",
			(addr.s_addr) & 0xff,
			(addr.s_addr >> 8) & 0xff,
			(addr.s_addr >> 16) & 0xff,
			(addr.s_addr >> 24) & 0xff);

	/* Get the gateway address */
	if (get_dripv4addr(_interface, &addr) == ERROR) {
		log_err("Get gateway address failed: %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	snprintf(config->gw_addr.address, MAX_IP_ADDRESS_LEN,
			"%d.%d.%d.%d",
			(addr.s_addr) & 0xff,
			(addr.s_addr >> 8) & 0xff,
			(addr.s_addr >> 16) & 0xff,
			(addr.s_addr >> 24) & 0xff);

	/* Get DNS servers */
	if (get_ipv4dnsaddr(dnsAddr, MAX_DNS_ADDRESSES) == ERROR) {
		log_err("Get DNS servers failed: %s", strerror(errno));
		return E_NETWORK_ERROR;
	}

	for (int i = 0; i < MAX_DNS_ADDRESSES; i++) {
		snprintf(config->dns_addr[i].address, MAX_IP_ADDRESS_LEN,
			"%d.%d.%d.%d",
			(dnsAddr[i].s_addr) & 0xff,
			(dnsAddr[i].s_addr >> 8) & 0xff,
			(dnsAddr[i].s_addr >> 16) & 0xff,
			(dnsAddr[i].s_addr >> 24) & 0xff);
	}

	return ret;
}
