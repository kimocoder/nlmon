#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>

#include "nlmon_nl_event.h"
#include "event_processor.h"
#include "nlmon_nl_route.h"
#include "nlmon_nl_genl.h"
#include "nlmon_nl_diag.h"
#include "nlmon_nl_netfilter.h"

/**
 * Get current timestamp in microseconds
 */
static uint64_t get_timestamp_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/**
 * Extract common netlink header information
 */
void nlmon_nl_extract_header(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	if (!nlh || !evt)
		return;
	
	/* Extract netlink header fields */
	evt->netlink.msg_type = nlh->nlmsg_type;
	evt->netlink.msg_flags = nlh->nlmsg_flags;
	evt->netlink.seq = nlh->nlmsg_seq;
	evt->netlink.pid = nlh->nlmsg_pid;
	
	/* Set timestamp */
	evt->timestamp = get_timestamp_us();
}

/**
 * Parse attributes into event structure
 * 
 * This is a generic helper that can be extended for protocol-specific
 * attribute parsing. Currently it serves as a placeholder for future
 * generic attribute handling.
 */
int nlmon_nl_parse_attributes(struct nlmsghdr *nlh, int hdrlen,
                               struct nlmon_event *evt)
{
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Protocol-specific parsers handle attribute parsing */
	/* This function can be extended for common attribute handling */
	
	return 0;
}

/**
 * Convert netlink message to nlmon event
 */
int nlmon_nl_msg_to_event(struct nl_msg *msg, struct nlmon_event *evt, int protocol)
{
	struct nlmsghdr *nlh;
	int ret = 0;
	
	if (!msg || !evt)
		return -EINVAL;
	
	/* Get netlink message header */
	nlh = nlmsg_hdr(msg);
	if (!nlh)
		return -EINVAL;
	
	/* Initialize event structure */
	memset(evt, 0, sizeof(*evt));
	
	/* Set protocol */
	evt->netlink.protocol = protocol;
	
	/* Extract common header information */
	nlmon_nl_extract_header(nlh, evt);
	
	/* Call protocol-specific parser based on protocol */
	switch (protocol) {
	case NETLINK_ROUTE:
		/* Route protocol messages */
		switch (nlh->nlmsg_type) {
		case RTM_NEWLINK:
		case RTM_DELLINK:
		case RTM_GETLINK:
			ret = nlmon_parse_link_msg(nlh, evt);
			evt->event_type = nlh->nlmsg_type;
			break;
			
		case RTM_NEWADDR:
		case RTM_DELADDR:
		case RTM_GETADDR:
			ret = nlmon_parse_addr_msg(nlh, evt);
			evt->event_type = nlh->nlmsg_type;
			break;
			
		case RTM_NEWROUTE:
		case RTM_DELROUTE:
		case RTM_GETROUTE:
			ret = nlmon_parse_route_msg(nlh, evt);
			evt->event_type = nlh->nlmsg_type;
			break;
			
		case RTM_NEWNEIGH:
		case RTM_DELNEIGH:
		case RTM_GETNEIGH:
			ret = nlmon_parse_neigh_msg(nlh, evt);
			evt->event_type = nlh->nlmsg_type;
			break;
			
		default:
			/* Unknown route message type */
			ret = -ENOTSUP;
			break;
		}
		break;
		
	case NETLINK_GENERIC:
		/* Generic netlink messages */
		ret = nlmon_parse_nl80211_msg(nlh, evt);
		if (ret == 0) {
			evt->event_type = nlh->nlmsg_type;
		} else {
			/* Try QCA vendor parser */
			ret = nlmon_parse_qca_vendor_msg(nlh, evt);
			if (ret == 0)
				evt->event_type = nlh->nlmsg_type;
		}
		break;
		
	case NETLINK_SOCK_DIAG:
		/* Socket diagnostics messages */
		ret = nlmon_parse_inet_diag_msg(nlh, evt);
		if (ret == 0)
			evt->event_type = nlh->nlmsg_type;
		break;
		
	case NETLINK_NETFILTER:
		/* Netfilter messages */
		ret = nlmon_parse_conntrack_msg(nlh, evt);
		if (ret == 0)
			evt->event_type = nlh->nlmsg_type;
		break;
		
	default:
		/* Unknown protocol */
		ret = -EPROTONOSUPPORT;
		break;
	}
	
	/* Store raw message for debugging if parsing succeeded */
	if (ret == 0) {
		evt->raw_msg = nlh;
		evt->raw_msg_len = nlh->nlmsg_len;
	}
	
	return ret;
}
