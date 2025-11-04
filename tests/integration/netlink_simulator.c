/* netlink_simulator.c - Simulate netlink events for testing */

#include "netlink_simulator.h"
#include "event_processor.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

struct sim_netlink_event *sim_create_newlink(const char *ifname, uint32_t ifindex,
                                             uint32_t flags)
{
	struct sim_netlink_event *event = calloc(1, sizeof(*event));
	if (!event)
		return NULL;
	
	event->type = RTM_NEWLINK;
	event->seq = ifindex;
	event->ifindex = ifindex;
	event->flags = flags;
	strncpy(event->interface, ifname, sizeof(event->interface) - 1);
	
	return event;
}

struct sim_netlink_event *sim_create_dellink(const char *ifname, uint32_t ifindex)
{
	struct sim_netlink_event *event = calloc(1, sizeof(*event));
	if (!event)
		return NULL;
	
	event->type = RTM_DELLINK;
	event->seq = ifindex;
	event->ifindex = ifindex;
	strncpy(event->interface, ifname, sizeof(event->interface) - 1);
	
	return event;
}

struct sim_netlink_event *sim_create_newaddr(const char *ifname, uint32_t ifindex,
                                             const char *addr, uint8_t prefix_len)
{
	struct sim_netlink_event *event = calloc(1, sizeof(*event));
	if (!event)
		return NULL;
	
	event->type = RTM_NEWADDR;
	event->seq = ifindex;
	event->ifindex = ifindex;
	event->family = AF_INET;
	strncpy(event->interface, ifname, sizeof(event->interface) - 1);
	
	/* Store address info in data */
	struct {
		char addr[64];
		uint8_t prefix_len;
	} *addr_data = malloc(sizeof(*addr_data));
	
	if (addr_data) {
		strncpy(addr_data->addr, addr, sizeof(addr_data->addr) - 1);
		addr_data->prefix_len = prefix_len;
		event->data = addr_data;
		event->data_len = sizeof(*addr_data);
	}
	
	return event;
}

struct sim_netlink_event *sim_create_newroute(const char *dst, uint8_t prefix_len,
                                              const char *gateway, const char *ifname)
{
	struct sim_netlink_event *event = calloc(1, sizeof(*event));
	if (!event)
		return NULL;
	
	event->type = RTM_NEWROUTE;
	event->family = AF_INET;
	if (ifname)
		strncpy(event->interface, ifname, sizeof(event->interface) - 1);
	
	/* Store route info in data */
	struct {
		char dst[64];
		char gateway[64];
		uint8_t prefix_len;
	} *route_data = malloc(sizeof(*route_data));
	
	if (route_data) {
		strncpy(route_data->dst, dst, sizeof(route_data->dst) - 1);
		if (gateway)
			strncpy(route_data->gateway, gateway, sizeof(route_data->gateway) - 1);
		route_data->prefix_len = prefix_len;
		event->data = route_data;
		event->data_len = sizeof(*route_data);
	}
	
	return event;
}

struct sim_netlink_event *sim_create_newneigh(const char *ifname, const char *ip,
                                              const uint8_t *mac)
{
	struct sim_netlink_event *event = calloc(1, sizeof(*event));
	if (!event)
		return NULL;
	
	event->type = RTM_NEWNEIGH;
	event->family = AF_INET;
	strncpy(event->interface, ifname, sizeof(event->interface) - 1);
	
	/* Store neighbor info in data */
	struct {
		char ip[64];
		uint8_t mac[6];
	} *neigh_data = malloc(sizeof(*neigh_data));
	
	if (neigh_data) {
		strncpy(neigh_data->ip, ip, sizeof(neigh_data->ip) - 1);
		if (mac)
			memcpy(neigh_data->mac, mac, 6);
		event->data = neigh_data;
		event->data_len = sizeof(*neigh_data);
	}
	
	return event;
}

void sim_free_event(struct sim_netlink_event *event)
{
	if (!event)
		return;
	
	free(event->data);
	free(event);
}

int sim_to_nlmon_event(struct sim_netlink_event *sim_event,
                       struct nlmon_event *nlmon_event)
{
	if (!sim_event || !nlmon_event)
		return -1;
	
	memset(nlmon_event, 0, sizeof(*nlmon_event));
	
	nlmon_event->message_type = sim_event->type;
	nlmon_event->sequence = sim_event->seq;
	strncpy(nlmon_event->interface, sim_event->interface,
	        sizeof(nlmon_event->interface) - 1);
	
	/* Set event type based on message type */
	switch (sim_event->type) {
	case RTM_NEWLINK:
	case RTM_DELLINK:
		nlmon_event->event_type = 1; /* Link event */
		break;
	case RTM_NEWADDR:
	case RTM_DELADDR:
		nlmon_event->event_type = 2; /* Address event */
		break;
	case RTM_NEWROUTE:
	case RTM_DELROUTE:
		nlmon_event->event_type = 3; /* Route event */
		break;
	case RTM_NEWNEIGH:
	case RTM_DELNEIGH:
		nlmon_event->event_type = 4; /* Neighbor event */
		break;
	default:
		nlmon_event->event_type = 0;
	}
	
	nlmon_event->data = sim_event->data;
	nlmon_event->data_size = sim_event->data_len;
	
	return 0;
}
