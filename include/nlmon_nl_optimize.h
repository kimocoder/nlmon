/* nlmon_nl_optimize.h - Netlink performance optimizations
 *
 * Optimized functions for hot paths in netlink message processing.
 */

#ifndef NLMON_NL_OPTIMIZE_H
#define NLMON_NL_OPTIMIZE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/netlink.h>
#include <netlink/attr.h>

#include "nlmon_nl_route.h"

/**
 * nlmon_parse_link_msg_fast() - Fast-path link message parser
 * @nlh: Netlink message header
 * @link_info: Output structure for link information
 *
 * Optimized version that avoids allocations and uses inline parsing.
 *
 * Returns: 0 on success, negative error code on failure
 */
int nlmon_parse_link_msg_fast(struct nlmsghdr *nlh, struct nlmon_link_info *link_info);

/**
 * nlmon_parse_addr_msg_fast() - Fast-path address message parser
 * @nlh: Netlink message header
 * @addr_info: Output structure for address information
 *
 * Optimized version with inline attribute parsing.
 *
 * Returns: 0 on success, negative error code on failure
 */
int nlmon_parse_addr_msg_fast(struct nlmsghdr *nlh, struct nlmon_addr_info *addr_info);

/**
 * nlmon_find_attr_fast() - Fast attribute lookup
 * @nlh: Netlink message header
 * @hdrlen: Length of protocol-specific header
 * @attrtype: Attribute type to find
 *
 * Fast path for finding a single attribute without full parsing.
 *
 * Returns: Attribute pointer or NULL if not found
 */
struct nlattr *nlmon_find_attr_fast(struct nlmsghdr *nlh, int hdrlen, int attrtype);

/**
 * nlmon_extract_link_attrs_batch() - Batch attribute extraction
 * @nlh: Netlink message header
 * @ifname: Output buffer for interface name (can be NULL)
 * @ifname_len: Size of ifname buffer
 * @mtu: Output for MTU value (can be NULL)
 * @flags: Output for interface flags (can be NULL)
 *
 * Extract multiple common attributes in one pass.
 *
 * Returns: 0 on success, negative error code on failure
 */
int nlmon_extract_link_attrs_batch(struct nlmsghdr *nlh,
                                   char *ifname, size_t ifname_len,
                                   uint32_t *mtu,
                                   uint32_t *flags);

/**
 * Message classification
 */
enum nlmon_msg_class {
	MSG_CLASS_LINK,
	MSG_CLASS_ADDR,
	MSG_CLASS_ROUTE,
	MSG_CLASS_NEIGH,
	MSG_CLASS_OTHER
};

/**
 * nlmon_classify_route_msg() - Fast message type classification
 * @msg_type: Netlink message type
 *
 * Returns: Message class
 */
enum nlmon_msg_class nlmon_classify_route_msg(uint16_t msg_type);

/**
 * nlmon_validate_msg_fast() - Quick message validation
 * @nlh: Netlink message header
 * @buf_len: Buffer length
 *
 * Quick validation checks before full parsing.
 *
 * Returns: true if message is valid
 */
bool nlmon_validate_msg_fast(struct nlmsghdr *nlh, size_t buf_len);

/**
 * nlmon_estimate_attr_count() - Estimate attribute count
 * @nlh: Netlink message header
 * @hdrlen: Length of protocol-specific header
 *
 * Quick estimate of attribute count for pre-allocation.
 *
 * Returns: Estimated number of attributes
 */
size_t nlmon_estimate_attr_count(struct nlmsghdr *nlh, int hdrlen);

/**
 * nlmon_get_attr_data_zerocopy() - Zero-copy attribute access
 * @attr: Attribute pointer
 * @len: Output for data length
 *
 * Get attribute data pointer without copying.
 *
 * Returns: Pointer to attribute data or NULL
 */
const void *nlmon_get_attr_data_zerocopy(struct nlattr *attr, size_t *len);

/**
 * nlmon_get_string_attr_fast() - Fast string attribute extraction
 * @attr: Attribute pointer
 *
 * Extract string with minimal overhead.
 *
 * Returns: Pointer to string or NULL
 */
const char *nlmon_get_string_attr_fast(struct nlattr *attr);

/**
 * nlmon_process_messages_batch() - Batch message processing
 * @messages: Array of message pointers
 * @count: Number of messages
 * @handler: Handler function for each message
 * @user_data: User data passed to handler
 *
 * Process multiple messages in one call for better cache locality.
 *
 * Returns: Number of messages processed, or negative error code
 */
int nlmon_process_messages_batch(struct nlmsghdr **messages,
                                 size_t count,
                                 int (*handler)(struct nlmsghdr *, void *),
                                 void *user_data);

/**
 * nlmon_iterate_messages_optimized() - Optimized multi-message iteration
 * @buf: Buffer containing messages
 * @len: Buffer length
 * @handler: Handler function for each message
 * @user_data: User data passed to handler
 *
 * Iterate through multiple messages with prefetching for better performance.
 *
 * Returns: Number of messages processed, or negative error code
 */
int nlmon_iterate_messages_optimized(void *buf, size_t len,
                                    int (*handler)(struct nlmsghdr *, void *),
                                    void *user_data);

#endif /* NLMON_NL_OPTIMIZE_H */

