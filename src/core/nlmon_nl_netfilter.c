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
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <netinet/in.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/handlers.h>

#include "nlmon_netlink.h"
#include "nlmon_nl_netfilter.h"
#include "event_processor.h"

/**
 * Netfilter message callback handler
 * 
 * This is the main callback for NETLINK_NETFILTER messages. It extracts the
 * message type and routes to the appropriate parser function.
 */
int nlmon_nf_msg_handler(struct nl_msg *msg, void *arg)
{
	struct nlmon_nl_manager *mgr = (struct nlmon_nl_manager *)arg;
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nfgenmsg *nfmsg;
	struct nlmon_event evt;
	int ret = 0;
	
	if (!mgr || !nlh)
		return NL_SKIP;
	
	/* Initialize event structure */
	memset(&evt, 0, sizeof(evt));
	evt.timestamp = time(NULL) * 1000000000ULL; /* Convert to nanoseconds */
	evt.netlink.protocol = NETLINK_NETFILTER;
	evt.netlink.msg_type = nlh->nlmsg_type;
	evt.netlink.msg_flags = nlh->nlmsg_flags;
	evt.netlink.seq = nlh->nlmsg_seq;
	evt.netlink.pid = nlh->nlmsg_pid;
	
	/* Get netfilter generic message header */
	nfmsg = (struct nfgenmsg *)nlmsg_data(nlh);
	if (!nfmsg) {
		fprintf(stderr, "Failed to get nfgenmsg header\n");
		return NL_SKIP;
	}
	
	/* Determine subsystem from message type */
	uint8_t subsys = NFNL_SUBSYS_ID(nlh->nlmsg_type);
	uint8_t msg_type = NFNL_MSG_TYPE(nlh->nlmsg_type);
	
	/* Handle special netlink message types first */
	if (nlh->nlmsg_type == NLMSG_DONE) {
		/* End of multipart message */
		return NL_STOP;
	}
	
	if (nlh->nlmsg_type == NLMSG_ERROR) {
		/* Error message */
		fprintf(stderr, "Received NLMSG_ERROR in netfilter handler\n");
		return NL_STOP;
	}
	
	/* Route to appropriate parser based on subsystem */
	switch (subsys) {
	case NFNL_SUBSYS_CTNETLINK:
		/* Connection tracking subsystem */
		ret = nlmon_parse_conntrack_msg(nlh, &evt);
		evt.event_type = nlh->nlmsg_type;
		break;
		
	case NFNL_SUBSYS_CTNETLINK_EXP:
		/* Connection tracking expect subsystem */
		/* Not implemented yet */
		return NL_SKIP;
		
	case NFNL_SUBSYS_QUEUE:
		/* Netfilter queue subsystem */
		/* Not implemented yet */
		return NL_SKIP;
		
	case NFNL_SUBSYS_ULOG:
		/* Netfilter userspace logging */
		/* Not implemented yet */
		return NL_SKIP;
		
	default:
		/* Unknown or unhandled subsystem */
		return NL_SKIP;
	}
	
	/* If parsing failed, skip this message */
	if (ret < 0) {
		fprintf(stderr, "Failed to parse netfilter message subsys %d type %d: %d\n",
		        subsys, msg_type, ret);
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
 * Parse connection tracking message
 * 
 * Extracts connection tracking information from conntrack messages.
 * Supports new, update, and destroy events.
 */
int nlmon_parse_conntrack_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct nfgenmsg *nfmsg;
	struct nlattr *tb[CTA_MAX + 1];
	struct nlmon_ct_info *ct_info;
	int ret;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Verify message has enough data for nfgenmsg */
	if ((size_t)nlmsg_len(nlh) < sizeof(*nfmsg)) {
		fprintf(stderr, "conntrack message too short\n");
		return -EINVAL;
	}
	
	/* Get netfilter generic message header */
	nfmsg = (struct nfgenmsg *)nlmsg_data(nlh);
	
	/* Parse attributes */
	ret = nlmsg_parse(nlh, sizeof(*nfmsg), tb, CTA_MAX, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse conntrack message attributes: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	/* Allocate connection tracking info structure */
	ct_info = calloc(1, sizeof(*ct_info));
	if (!ct_info)
		return -ENOMEM;
	
	/* Parse tuple original direction */
	if (tb[CTA_TUPLE_ORIG]) {
		struct nlattr *tb_orig[CTA_TUPLE_MAX + 1];
		
		ret = nla_parse_nested(tb_orig, CTA_TUPLE_MAX, tb[CTA_TUPLE_ORIG], NULL);
		if (ret >= 0) {
			/* Parse IP addresses */
			if (tb_orig[CTA_TUPLE_IP]) {
				struct nlattr *tb_ip[CTA_IP_MAX + 1];
				
				ret = nla_parse_nested(tb_ip, CTA_IP_MAX, tb_orig[CTA_TUPLE_IP], NULL);
				if (ret >= 0) {
					/* Check for IPv4 */
					if (tb_ip[CTA_IP_V4_SRC]) {
						struct in_addr addr;
						addr.s_addr = nla_get_u32(tb_ip[CTA_IP_V4_SRC]);
						inet_ntop(AF_INET, &addr, ct_info->src_addr, 
						          sizeof(ct_info->src_addr));
					}
					if (tb_ip[CTA_IP_V4_DST]) {
						struct in_addr addr;
						addr.s_addr = nla_get_u32(tb_ip[CTA_IP_V4_DST]);
						inet_ntop(AF_INET, &addr, ct_info->dst_addr, 
						          sizeof(ct_info->dst_addr));
					}
					/* Check for IPv6 */
					if (tb_ip[CTA_IP_V6_SRC]) {
						struct in6_addr addr;
						memcpy(&addr, nla_data(tb_ip[CTA_IP_V6_SRC]), sizeof(addr));
						inet_ntop(AF_INET6, &addr, ct_info->src_addr, 
						          sizeof(ct_info->src_addr));
					}
					if (tb_ip[CTA_IP_V6_DST]) {
						struct in6_addr addr;
						memcpy(&addr, nla_data(tb_ip[CTA_IP_V6_DST]), sizeof(addr));
						inet_ntop(AF_INET6, &addr, ct_info->dst_addr, 
						          sizeof(ct_info->dst_addr));
					}
				}
			}
			
			/* Parse protocol and ports */
			if (tb_orig[CTA_TUPLE_PROTO]) {
				struct nlattr *tb_proto[CTA_PROTO_MAX + 1];
				
				ret = nla_parse_nested(tb_proto, CTA_PROTO_MAX, 
				                      tb_orig[CTA_TUPLE_PROTO], NULL);
				if (ret >= 0) {
					/* Extract protocol number */
					if (tb_proto[CTA_PROTO_NUM]) {
						ct_info->protocol = nla_get_u8(tb_proto[CTA_PROTO_NUM]);
					}
					
					/* Extract ports for TCP/UDP */
					if (tb_proto[CTA_PROTO_SRC_PORT]) {
						ct_info->src_port = ntohs(nla_get_u16(tb_proto[CTA_PROTO_SRC_PORT]));
					}
					if (tb_proto[CTA_PROTO_DST_PORT]) {
						ct_info->dst_port = ntohs(nla_get_u16(tb_proto[CTA_PROTO_DST_PORT]));
					}
				}
			}
		}
	}
	
	/* Parse protocol-specific information */
	if (tb[CTA_PROTOINFO]) {
		struct nlattr *tb_protoinfo[CTA_PROTOINFO_MAX + 1];
		
		ret = nla_parse_nested(tb_protoinfo, CTA_PROTOINFO_MAX, 
		                      tb[CTA_PROTOINFO], NULL);
		if (ret >= 0 && tb_protoinfo[CTA_PROTOINFO_TCP]) {
			struct nlattr *tb_tcp[CTA_PROTOINFO_TCP_MAX + 1];
			
			ret = nla_parse_nested(tb_tcp, CTA_PROTOINFO_TCP_MAX,
			                      tb_protoinfo[CTA_PROTOINFO_TCP], NULL);
			if (ret >= 0 && tb_tcp[CTA_PROTOINFO_TCP_STATE]) {
				ct_info->tcp_state = nla_get_u8(tb_tcp[CTA_PROTOINFO_TCP_STATE]);
			}
		}
	}
	
	/* Parse counters */
	if (tb[CTA_COUNTERS_ORIG]) {
		struct nlattr *tb_counters[CTA_COUNTERS_MAX + 1];
		
		ret = nla_parse_nested(tb_counters, CTA_COUNTERS_MAX,
		                      tb[CTA_COUNTERS_ORIG], NULL);
		if (ret >= 0) {
			if (tb_counters[CTA_COUNTERS_PACKETS]) {
				ct_info->packets_orig = nla_get_u64(tb_counters[CTA_COUNTERS_PACKETS]);
			}
			if (tb_counters[CTA_COUNTERS_BYTES]) {
				ct_info->bytes_orig = nla_get_u64(tb_counters[CTA_COUNTERS_BYTES]);
			}
		}
	}
	
	if (tb[CTA_COUNTERS_REPLY]) {
		struct nlattr *tb_counters[CTA_COUNTERS_MAX + 1];
		
		ret = nla_parse_nested(tb_counters, CTA_COUNTERS_MAX,
		                      tb[CTA_COUNTERS_REPLY], NULL);
		if (ret >= 0) {
			if (tb_counters[CTA_COUNTERS_PACKETS]) {
				ct_info->packets_reply = nla_get_u64(tb_counters[CTA_COUNTERS_PACKETS]);
			}
			if (tb_counters[CTA_COUNTERS_BYTES]) {
				ct_info->bytes_reply = nla_get_u64(tb_counters[CTA_COUNTERS_BYTES]);
			}
		}
	}
	
	/* Parse connection mark */
	if (tb[CTA_MARK]) {
		ct_info->mark = nla_get_u32(tb[CTA_MARK]);
	}
	
	/* Store in event */
	evt->netlink.data.generic = ct_info;
	
	return 0;
}
