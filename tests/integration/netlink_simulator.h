/* netlink_simulator.h - Simulate netlink events for testing */

#ifndef NETLINK_SIMULATOR_H
#define NETLINK_SIMULATOR_H

#include <stdint.h>
#include <linux/rtnetlink.h>

/* Simulated netlink event */
struct sim_netlink_event {
	uint16_t type;           /* RTM_NEWLINK, RTM_DELLINK, etc. */
	uint32_t seq;            /* Sequence number */
	char interface[16];      /* Interface name */
	uint32_t ifindex;        /* Interface index */
	uint32_t flags;          /* Interface flags */
	uint8_t family;          /* Address family */
	void *data;              /* Additional data */
	size_t data_len;         /* Data length */
};

/* Create a simulated RTM_NEWLINK event */
struct sim_netlink_event *sim_create_newlink(const char *ifname, uint32_t ifindex,
                                             uint32_t flags);

/* Create a simulated RTM_DELLINK event */
struct sim_netlink_event *sim_create_dellink(const char *ifname, uint32_t ifindex);

/* Create a simulated RTM_NEWADDR event */
struct sim_netlink_event *sim_create_newaddr(const char *ifname, uint32_t ifindex,
                                             const char *addr, uint8_t prefix_len);

/* Create a simulated RTM_NEWROUTE event */
struct sim_netlink_event *sim_create_newroute(const char *dst, uint8_t prefix_len,
                                              const char *gateway, const char *ifname);

/* Create a simulated RTM_NEWNEIGH event */
struct sim_netlink_event *sim_create_newneigh(const char *ifname, const char *ip,
                                              const uint8_t *mac);

/* Free simulated event */
void sim_free_event(struct sim_netlink_event *event);

/* Convert simulated event to nlmon event structure */
int sim_to_nlmon_event(struct sim_netlink_event *sim_event,
                       struct nlmon_event *nlmon_event);

#endif /* NETLINK_SIMULATOR_H */
