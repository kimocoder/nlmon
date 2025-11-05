#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <linux/neighbour.h>
#include <net/if.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/handlers.h>

#include "nlmon_netlink.h"
#include "nlmon_nl_route.h"
#include "event_processor.h"

/**
 * Route message callback handler
 * 
 * This is the main callback for NETLINK_ROUTE messages. It extracts the
 * message type and routes to the appropriate parser function.
 */
int nlmon_route_msg_handler(struct nl_msg *msg, void *arg)
{
	struct nlmon_nl_manager *mgr = (struct nlmon_nl_manager *)arg;
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlmon_event evt;
	int ret = 0;
	
	if (!mgr || !nlh)
		return NL_SKIP;
	
	/* Initialize event structure */
	memset(&evt, 0, sizeof(evt));
	evt.timestamp = time(NULL) * 1000000000ULL; /* Convert to nanoseconds */
	evt.netlink.protocol = NETLINK_ROUTE;
	evt.netlink.msg_type = nlh->nlmsg_type;
	evt.netlink.msg_flags = nlh->nlmsg_flags;
	evt.netlink.seq = nlh->nlmsg_seq;
	evt.netlink.pid = nlh->nlmsg_pid;
	
	/* Route to appropriate parser based on message type */
	switch (nlh->nlmsg_type) {
	case RTM_NEWLINK:
	case RTM_DELLINK:
		/* Update link cache if enabled */
		nlmon_nl_cache_update_link(mgr, msg);
		
		ret = nlmon_parse_link_msg(nlh, &evt);
		evt.event_type = nlh->nlmsg_type;
		break;
		
	case RTM_NEWADDR:
	case RTM_DELADDR:
		/* Update address cache if enabled */
		nlmon_nl_cache_update_addr(mgr, msg);
		
		ret = nlmon_parse_addr_msg(nlh, &evt);
		evt.event_type = nlh->nlmsg_type;
		break;
		
	case RTM_NEWROUTE:
	case RTM_DELROUTE:
		/* Update route cache if enabled */
		nlmon_nl_cache_update_route(mgr, msg);
		
		ret = nlmon_parse_route_msg(nlh, &evt);
		evt.event_type = nlh->nlmsg_type;
		break;
		
	case RTM_NEWNEIGH:
	case RTM_DELNEIGH:
		ret = nlmon_parse_neigh_msg(nlh, &evt);
		evt.event_type = nlh->nlmsg_type;
		break;
		
	case NLMSG_DONE:
		/* End of multipart message */
		return NL_STOP;
		
	case NLMSG_ERROR:
		/* Error message */
		fprintf(stderr, "Received NLMSG_ERROR in route handler\n");
		return NL_STOP;
		
	default:
		/* Unknown or unhandled message type */
		return NL_SKIP;
	}
	
	/* If parsing failed, skip this message */
	if (ret < 0) {
		fprintf(stderr, "Failed to parse route message type %d: %d\n",
		        nlh->nlmsg_type, ret);
		return NL_SKIP;
	}
	
	/* Forward event to nlmon event processor if callback is set */
	if (mgr->event_callback) {
		mgr->event_callback(&evt, mgr->user_data);
	}
	
	/* Free any allocated data in the event */
	if (evt.netlink.data.generic) {
		free(evt.netlink.data.generic);
		evt.netlink.data.generic = NULL;
	}
	
	return NL_OK;
}

/**
 * Parse link (interface) message
 * 
 * Extracts interface information from RTM_NEWLINK/RTM_DELLINK messages.
 */
int nlmon_parse_link_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct ifinfomsg *ifi;
	struct nlattr *tb[IFLA_MAX + 1];
	struct nlmon_link_info *link_info;
	int ret;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Get interface info header */
	ifi = (struct ifinfomsg *)nlmsg_data(nlh);
	
	/* Parse attributes */
	ret = nlmsg_parse(nlh, sizeof(*ifi), tb, IFLA_MAX, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse link message attributes: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	/* Allocate link info structure */
	link_info = calloc(1, sizeof(*link_info));
	if (!link_info)
		return -ENOMEM;
	
	/* Extract interface index */
	link_info->ifindex = ifi->ifi_index;
	
	/* Extract interface flags */
	link_info->flags = ifi->ifi_flags;
	
	/* Extract interface name */
	if (tb[IFLA_IFNAME]) {
		nla_strlcpy(link_info->ifname, tb[IFLA_IFNAME], IFNAMSIZ);
	} else {
		/* Fallback to index-based name */
		if_indextoname(ifi->ifi_index, link_info->ifname);
	}
	
	/* Extract MTU */
	if (tb[IFLA_MTU]) {
		link_info->mtu = nla_get_u32(tb[IFLA_MTU]);
	}
	
	/* Extract MAC address */
	if (tb[IFLA_ADDRESS]) {
		int addr_len = nla_len(tb[IFLA_ADDRESS]);
		if (addr_len <= ETH_ALEN) {
			memcpy(link_info->addr, nla_data(tb[IFLA_ADDRESS]), addr_len);
		}
	}
	
	/* Extract qdisc */
	if (tb[IFLA_QDISC]) {
		nla_strlcpy(link_info->qdisc, tb[IFLA_QDISC], sizeof(link_info->qdisc));
	}
	
	/* Extract operational state */
	if (tb[IFLA_OPERSTATE]) {
		link_info->operstate = nla_get_u8(tb[IFLA_OPERSTATE]);
	}
	
	/* Store in event */
	evt->netlink.data.link = link_info;
	snprintf(evt->interface, sizeof(evt->interface), "%s", link_info->ifname);
	
	return 0;
}

/**
 * Parse address message
 * 
 * Extracts address information from RTM_NEWADDR/RTM_DELADDR messages.
 */
int nlmon_parse_addr_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct ifaddrmsg *ifa;
	struct nlattr *tb[IFA_MAX + 1];
	struct nlmon_addr_info *addr_info;
	int ret;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Get address info header */
	ifa = (struct ifaddrmsg *)nlmsg_data(nlh);
	
	/* Parse attributes */
	ret = nlmsg_parse(nlh, sizeof(*ifa), tb, IFA_MAX, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse address message attributes: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	/* Allocate address info structure */
	addr_info = calloc(1, sizeof(*addr_info));
	if (!addr_info)
		return -ENOMEM;
	
	/* Extract address family */
	addr_info->family = ifa->ifa_family;
	
	/* Extract interface index */
	addr_info->ifindex = ifa->ifa_index;
	
	/* Extract prefix length */
	addr_info->prefixlen = ifa->ifa_prefixlen;
	
	/* Extract scope */
	addr_info->scope = ifa->ifa_scope;
	
	/* Extract IP address */
	if (tb[IFA_ADDRESS]) {
		void *addr_data = nla_data(tb[IFA_ADDRESS]);
		
		if (ifa->ifa_family == AF_INET) {
			inet_ntop(AF_INET, addr_data, addr_info->addr, sizeof(addr_info->addr));
		} else if (ifa->ifa_family == AF_INET6) {
			inet_ntop(AF_INET6, addr_data, addr_info->addr, sizeof(addr_info->addr));
		}
	}
	
	/* Extract label */
	if (tb[IFA_LABEL]) {
		nla_strlcpy(addr_info->label, tb[IFA_LABEL], IFNAMSIZ);
	} else {
		/* Use interface name as label */
		if_indextoname(ifa->ifa_index, addr_info->label);
	}
	
	/* Store in event */
	evt->netlink.data.addr = addr_info;
	if_indextoname(addr_info->ifindex, evt->interface);
	
	return 0;
}

/**
 * Parse route message
 * 
 * Extracts routing information from RTM_NEWROUTE/RTM_DELROUTE messages.
 */
int nlmon_parse_route_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct rtmsg *rtm;
	struct nlattr *tb[RTA_MAX + 1];
	struct nlmon_route_info *route_info;
	int ret;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Get route message header */
	rtm = (struct rtmsg *)nlmsg_data(nlh);
	
	/* Parse attributes */
	ret = nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse route message attributes: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	/* Allocate route info structure */
	route_info = calloc(1, sizeof(*route_info));
	if (!route_info)
		return -ENOMEM;
	
	/* Extract route family */
	route_info->family = rtm->rtm_family;
	
	/* Extract prefix lengths */
	route_info->dst_len = rtm->rtm_dst_len;
	route_info->src_len = rtm->rtm_src_len;
	
	/* Extract TOS */
	route_info->tos = rtm->rtm_tos;
	
	/* Extract protocol */
	route_info->protocol = rtm->rtm_protocol;
	
	/* Extract scope */
	route_info->scope = rtm->rtm_scope;
	
	/* Extract type */
	route_info->type = rtm->rtm_type;
	
	/* Extract destination address */
	if (tb[RTA_DST]) {
		void *addr_data = nla_data(tb[RTA_DST]);
		
		if (rtm->rtm_family == AF_INET) {
			inet_ntop(AF_INET, addr_data, route_info->dst, sizeof(route_info->dst));
		} else if (rtm->rtm_family == AF_INET6) {
			inet_ntop(AF_INET6, addr_data, route_info->dst, sizeof(route_info->dst));
		}
	}
	
	/* Extract source address */
	if (tb[RTA_SRC]) {
		void *addr_data = nla_data(tb[RTA_SRC]);
		
		if (rtm->rtm_family == AF_INET) {
			inet_ntop(AF_INET, addr_data, route_info->src, sizeof(route_info->src));
		} else if (rtm->rtm_family == AF_INET6) {
			inet_ntop(AF_INET6, addr_data, route_info->src, sizeof(route_info->src));
		}
	}
	
	/* Extract gateway address */
	if (tb[RTA_GATEWAY]) {
		void *addr_data = nla_data(tb[RTA_GATEWAY]);
		
		if (rtm->rtm_family == AF_INET) {
			inet_ntop(AF_INET, addr_data, route_info->gateway, sizeof(route_info->gateway));
		} else if (rtm->rtm_family == AF_INET6) {
			inet_ntop(AF_INET6, addr_data, route_info->gateway, sizeof(route_info->gateway));
		}
	}
	
	/* Extract output interface */
	if (tb[RTA_OIF]) {
		route_info->oif = nla_get_u32(tb[RTA_OIF]);
		if_indextoname(route_info->oif, evt->interface);
	}
	
	/* Extract priority */
	if (tb[RTA_PRIORITY]) {
		route_info->priority = nla_get_u32(tb[RTA_PRIORITY]);
	}
	
	/* Store in event */
	evt->netlink.data.route = route_info;
	
	return 0;
}

/**
 * Parse neighbor message
 * 
 * Extracts neighbor (ARP/NDP) information from RTM_NEWNEIGH/RTM_DELNEIGH messages.
 */
int nlmon_parse_neigh_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct ndmsg *ndm;
	struct nlattr *tb[NDA_MAX + 1];
	struct nlmon_neigh_info *neigh_info;
	int ret;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Get neighbor message header */
	ndm = (struct ndmsg *)nlmsg_data(nlh);
	
	/* Parse attributes */
	ret = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse neighbor message attributes: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	/* Allocate neighbor info structure */
	neigh_info = calloc(1, sizeof(*neigh_info));
	if (!neigh_info)
		return -ENOMEM;
	
	/* Extract address family */
	neigh_info->family = ndm->ndm_family;
	
	/* Extract interface index */
	neigh_info->ifindex = ndm->ndm_ifindex;
	
	/* Extract state */
	neigh_info->state = ndm->ndm_state;
	
	/* Extract flags */
	neigh_info->flags = ndm->ndm_flags;
	
	/* Extract destination (neighbor) address */
	if (tb[NDA_DST]) {
		void *addr_data = nla_data(tb[NDA_DST]);
		
		if (ndm->ndm_family == AF_INET) {
			inet_ntop(AF_INET, addr_data, neigh_info->dst, sizeof(neigh_info->dst));
		} else if (ndm->ndm_family == AF_INET6) {
			inet_ntop(AF_INET6, addr_data, neigh_info->dst, sizeof(neigh_info->dst));
		}
	}
	
	/* Extract link-layer address */
	if (tb[NDA_LLADDR]) {
		int addr_len = nla_len(tb[NDA_LLADDR]);
		if (addr_len <= ETH_ALEN) {
			memcpy(neigh_info->lladdr, nla_data(tb[NDA_LLADDR]), addr_len);
		}
	}
	
	/* Store in event */
	evt->netlink.data.neigh = neigh_info;
	if_indextoname(neigh_info->ifindex, evt->interface);
	
	return 0;
}
