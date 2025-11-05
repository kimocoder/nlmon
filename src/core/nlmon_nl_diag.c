#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>
#include <netinet/in.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/handlers.h>

#include "nlmon_netlink.h"
#include "nlmon_nl_diag.h"
#include "event_processor.h"

/**
 * Socket diagnostics message callback handler
 * 
 * This is the main callback for NETLINK_SOCK_DIAG messages. It extracts the
 * message type and routes to the appropriate parser function.
 */
int nlmon_diag_msg_handler(struct nl_msg *msg, void *arg)
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
	evt.netlink.protocol = NETLINK_SOCK_DIAG;
	evt.netlink.msg_type = nlh->nlmsg_type;
	evt.netlink.msg_flags = nlh->nlmsg_flags;
	evt.netlink.seq = nlh->nlmsg_seq;
	evt.netlink.pid = nlh->nlmsg_pid;
	
	/* Route to appropriate parser based on message type */
	switch (nlh->nlmsg_type) {
	case SOCK_DIAG_BY_FAMILY:
		/* This is a response to a diagnostic request */
		ret = nlmon_parse_inet_diag_msg(nlh, &evt);
		evt.event_type = nlh->nlmsg_type;
		break;
		
	case NLMSG_DONE:
		/* End of multipart message */
		return NL_STOP;
		
	case NLMSG_ERROR:
		/* Error message */
		fprintf(stderr, "Received NLMSG_ERROR in diag handler\n");
		return NL_STOP;
		
	default:
		/* Unknown or unhandled message type */
		return NL_SKIP;
	}
	
	/* If parsing failed, skip this message */
	if (ret < 0) {
		fprintf(stderr, "Failed to parse diag message type %d: %d\n",
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
 * Parse inet diagnostics message
 * 
 * Extracts socket diagnostic information from INET_DIAG messages.
 * Supports TCP, UDP, and other INET protocols.
 */
int nlmon_parse_inet_diag_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct inet_diag_msg *diag_msg;
	struct nlmon_diag_info *diag_info;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Verify message has enough data for inet_diag_msg */
	if ((size_t)nlmsg_len(nlh) < sizeof(*diag_msg)) {
		fprintf(stderr, "inet_diag message too short\n");
		return -EINVAL;
	}
	
	/* Get diagnostic message */
	diag_msg = (struct inet_diag_msg *)nlmsg_data(nlh);
	
	/* Allocate diagnostic info structure */
	diag_info = calloc(1, sizeof(*diag_info));
	if (!diag_info)
		return -ENOMEM;
	
	/* Extract address family */
	diag_info->family = diag_msg->idiag_family;
	
	/* Extract socket state */
	diag_info->state = diag_msg->idiag_state;
	
	/* Extract timer and retransmission info */
	diag_info->timer = diag_msg->idiag_timer;
	diag_info->retrans = diag_msg->idiag_retrans;
	
	/* Extract queue sizes */
	diag_info->rqueue = diag_msg->idiag_rqueue;
	diag_info->wqueue = diag_msg->idiag_wqueue;
	
	/* Extract UID and inode */
	diag_info->uid = diag_msg->idiag_uid;
	diag_info->inode = diag_msg->idiag_inode;
	
	/* Extract ports (convert from network byte order) */
	diag_info->src_port = ntohs(diag_msg->id.idiag_sport);
	diag_info->dst_port = ntohs(diag_msg->id.idiag_dport);
	
	/* Extract source and destination addresses based on family */
	if (diag_msg->idiag_family == AF_INET) {
		/* IPv4 addresses - only use first element of array */
		struct in_addr src_addr, dst_addr;
		
		src_addr.s_addr = diag_msg->id.idiag_src[0];
		dst_addr.s_addr = diag_msg->id.idiag_dst[0];
		
		inet_ntop(AF_INET, &src_addr, diag_info->src_addr, 
		          sizeof(diag_info->src_addr));
		inet_ntop(AF_INET, &dst_addr, diag_info->dst_addr, 
		          sizeof(diag_info->dst_addr));
		
		/* Determine protocol based on port usage */
		if (diag_info->src_port != 0 || diag_info->dst_port != 0) {
			/* Assume TCP if state is set, otherwise UDP */
			diag_info->protocol = (diag_msg->idiag_state != 0) ? 
			                      IPPROTO_TCP : IPPROTO_UDP;
		}
	} else if (diag_msg->idiag_family == AF_INET6) {
		/* IPv6 addresses - use full array */
		struct in6_addr src_addr, dst_addr;
		
		memcpy(&src_addr, diag_msg->id.idiag_src, sizeof(src_addr));
		memcpy(&dst_addr, diag_msg->id.idiag_dst, sizeof(dst_addr));
		
		inet_ntop(AF_INET6, &src_addr, diag_info->src_addr, 
		          sizeof(diag_info->src_addr));
		inet_ntop(AF_INET6, &dst_addr, diag_info->dst_addr, 
		          sizeof(diag_info->dst_addr));
		
		/* Determine protocol based on port usage */
		if (diag_info->src_port != 0 || diag_info->dst_port != 0) {
			/* Assume TCP if state is set, otherwise UDP */
			diag_info->protocol = (diag_msg->idiag_state != 0) ? 
			                      IPPROTO_TCP : IPPROTO_UDP;
		}
	} else {
		/* Unsupported address family */
		fprintf(stderr, "Unsupported address family in inet_diag: %d\n",
		        diag_msg->idiag_family);
		free(diag_info);
		return -EAFNOSUPPORT;
	}
	
	/* Store in event */
	evt->netlink.data.generic = diag_info;
	
	/* Set interface name if available (from idiag_if) */
	if (diag_msg->id.idiag_if != 0) {
		snprintf(evt->interface, sizeof(evt->interface), 
		         "if%u", diag_msg->id.idiag_if);
	}
	
	return 0;
}
