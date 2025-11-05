#ifndef NLMON_NL_EVENT_H
#define NLMON_NL_EVENT_H

#include <stdint.h>
#include <linux/netlink.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nl_msg;
struct nlmon_event;
struct nlmsghdr;

/**
 * Convert netlink message to nlmon event
 * 
 * Extracts common netlink header information and calls protocol-specific
 * parsers to populate the event structure. This is the main entry point
 * for translating libnl messages to nlmon events.
 * 
 * @param msg Netlink message from libnl
 * @param evt Event structure to populate
 * @param protocol Netlink protocol (NETLINK_ROUTE, NETLINK_GENERIC, etc.)
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_msg_to_event(struct nl_msg *msg, struct nlmon_event *evt, int protocol);

/**
 * Extract common netlink header information
 * 
 * Populates the event structure with common netlink header fields
 * (message type, flags, sequence number, port ID).
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 */
void nlmon_nl_extract_header(struct nlmsghdr *nlh, struct nlmon_event *evt);

/**
 * Parse attributes into event structure
 * 
 * Generic attribute parsing function that can be used by protocol-specific
 * parsers to extract and validate netlink attributes.
 * 
 * @param nlh Netlink message header
 * @param hdrlen Header length (protocol-specific header size)
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_parse_attributes(struct nlmsghdr *nlh, int hdrlen,
                               struct nlmon_event *evt);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NL_EVENT_H */
