#ifndef NLMON_NL_NETFILTER_H
#define NLMON_NL_NETFILTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nlmsghdr;
struct nlmon_event;
struct nlmon_nl_manager;

/**
 * Connection tracking information structure
 */
struct nlmon_ct_info {
	uint8_t protocol;                /* Protocol (IPPROTO_TCP, IPPROTO_UDP, etc.) */
	uint8_t tcp_state;               /* TCP state (for TCP connections) */
	char src_addr[64];               /* Source address string */
	char dst_addr[64];               /* Destination address string */
	uint16_t src_port;               /* Source port */
	uint16_t dst_port;               /* Destination port */
	uint32_t mark;                   /* Connection mark */
	uint64_t packets_orig;           /* Packets in original direction */
	uint64_t packets_reply;          /* Packets in reply direction */
	uint64_t bytes_orig;             /* Bytes in original direction */
	uint64_t bytes_reply;            /* Bytes in reply direction */
};

/**
 * Netfilter message callback handler
 * 
 * Processes NETLINK_NETFILTER messages and routes them to appropriate parsers.
 * 
 * @param msg Netlink message
 * @param arg User data (nlmon_nl_manager)
 * @return NL_OK to continue, NL_STOP to stop, or negative error code
 */
int nlmon_nf_msg_handler(struct nl_msg *msg, void *arg);

/**
 * Parse connection tracking message
 * 
 * Parses conntrack messages (new, update, destroy)
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_conntrack_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NL_NETFILTER_H */
