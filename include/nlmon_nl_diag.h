#ifndef NLMON_NL_DIAG_H
#define NLMON_NL_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nlmsghdr;
struct nlmon_event;
struct nlmon_nl_manager;

/**
 * Socket diagnostic information structure
 */
struct nlmon_diag_info {
	uint8_t family;                  /* Address family (AF_INET, AF_INET6) */
	uint8_t state;                   /* Socket state */
	uint8_t protocol;                /* Protocol (IPPROTO_TCP, IPPROTO_UDP, etc.) */
	uint16_t src_port;               /* Source port */
	uint16_t dst_port;               /* Destination port */
	char src_addr[64];               /* Source address string */
	char dst_addr[64];               /* Destination address string */
	uint32_t inode;                  /* Socket inode */
	uint32_t uid;                    /* User ID */
	uint32_t rqueue;                 /* Receive queue size */
	uint32_t wqueue;                 /* Write queue size */
	uint8_t timer;                   /* Timer state */
	uint8_t retrans;                 /* Retransmission count */
};

/**
 * Socket diagnostics message callback handler
 * 
 * Processes NETLINK_SOCK_DIAG messages and routes them to appropriate parsers.
 * 
 * @param msg Netlink message
 * @param arg User data (nlmon_nl_manager)
 * @return NL_OK to continue, NL_STOP to stop, or negative error code
 */
int nlmon_diag_msg_handler(struct nl_msg *msg, void *arg);

/**
 * Parse inet diagnostics message
 * 
 * Parses INET socket diagnostic messages (TCP, UDP, etc.)
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_inet_diag_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NL_DIAG_H */
