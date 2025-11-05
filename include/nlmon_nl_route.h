#ifndef NLMON_NL_ROUTE_H
#define NLMON_NL_ROUTE_H

#include <stdint.h>
#include <net/if.h>
#include <linux/if_ether.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nlmsghdr;
struct nlmon_event;
struct nlmon_nl_manager;
struct nl_msg;

/**
 * Link (interface) information structure
 */
struct nlmon_link_info {
	char ifname[IFNAMSIZ];           /* Interface name */
	int ifindex;                     /* Interface index */
	unsigned int flags;              /* IFF_UP, IFF_RUNNING, etc. */
	unsigned int mtu;                /* MTU */
	unsigned char addr[ETH_ALEN];    /* MAC address */
	char qdisc[32];                  /* Queueing discipline */
	int operstate;                   /* Operational state */
};

/**
 * Address information structure
 */
struct nlmon_addr_info {
	int family;                      /* AF_INET, AF_INET6 */
	int ifindex;                     /* Interface index */
	unsigned char prefixlen;         /* Prefix length */
	unsigned char scope;             /* Address scope */
	char addr[64];                   /* IP address string */
	char label[IFNAMSIZ];            /* Address label */
};

/**
 * Route information structure
 */
struct nlmon_route_info {
	int family;                      /* Address family */
	unsigned char dst_len;           /* Destination prefix length */
	unsigned char src_len;           /* Source prefix length */
	unsigned char tos;               /* Type of service */
	unsigned char protocol;          /* Routing protocol */
	unsigned char scope;             /* Route scope */
	unsigned char type;              /* Route type */
	char dst[64];                    /* Destination address */
	char src[64];                    /* Source address */
	char gateway[64];                /* Gateway address */
	int oif;                         /* Output interface */
	unsigned int priority;           /* Route priority */
};

/**
 * Neighbor (ARP/NDP) information structure
 */
struct nlmon_neigh_info {
	int family;                      /* Address family */
	int ifindex;                     /* Interface index */
	uint16_t state;                  /* Neighbor state */
	uint8_t flags;                   /* Neighbor flags */
	char dst[64];                    /* Neighbor address */
	unsigned char lladdr[ETH_ALEN];  /* Link-layer address */
};

/**
 * Route message callback handler
 * 
 * Processes NETLINK_ROUTE messages and routes them to appropriate parsers.
 * 
 * @param msg Netlink message
 * @param arg User data (nlmon_nl_manager)
 * @return NL_OK to continue, NL_STOP to stop, or negative error code
 */
int nlmon_route_msg_handler(struct nl_msg *msg, void *arg);

/**
 * Parse link (interface) message
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_link_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

/**
 * Parse address message
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_addr_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

/**
 * Parse route message
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_route_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

/**
 * Parse neighbor message
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_neigh_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NL_ROUTE_H */
