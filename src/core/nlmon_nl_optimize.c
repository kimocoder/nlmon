/* nlmon_nl_optimize.c - Netlink performance optimizations
 *
 * This module implements optimizations for hot paths in netlink message processing:
 * - Inline attribute parsing for common cases
 * - Reduced memory allocations in critical paths
 * - Fast-path processing for frequent message types
 * - Optimized attribute iteration
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <net/if.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

#include "nlmon_nl_optimize.h"
#include "nlmon_nl_route.h"

/**
 * Fast-path link message parser
 * 
 * Optimized version that avoids allocations for common cases
 * and uses inline attribute parsing.
 */
int nlmon_parse_link_msg_fast(struct nlmsghdr *nlh, struct nlmon_link_info *link_info)
{
	struct ifinfomsg *ifi;
	struct nlattr *attr;
	int remaining;
	
	if (!nlh || !link_info)
		return -EINVAL;
	
	/* Get interface info header */
	ifi = (struct ifinfomsg *)nlmsg_data(nlh);
	
	/* Initialize output structure */
	memset(link_info, 0, sizeof(*link_info));
	
	/* Extract basic fields from header */
	link_info->ifindex = ifi->ifi_index;
	link_info->flags = ifi->ifi_flags;
	
	/* Fast inline attribute iteration */
	nlmsg_for_each_attr(attr, nlh, sizeof(*ifi), remaining) {
		switch (nla_type(attr)) {
		case IFLA_IFNAME:
			/* Inline string copy - avoid nla_strlcpy overhead */
			{
				const char *name = nla_data(attr);
				size_t len = nla_len(attr);
				if (len > 0 && len < IFNAMSIZ) {
					memcpy(link_info->ifname, name, len);
					link_info->ifname[len] = '\0';
				}
			}
			break;
			
		case IFLA_MTU:
			if (nla_len(attr) == sizeof(uint32_t))
				link_info->mtu = *(uint32_t *)nla_data(attr);
			break;
			
		case IFLA_ADDRESS:
			{
				size_t addr_len = nla_len(attr);
				if (addr_len <= ETH_ALEN)
					memcpy(link_info->addr, nla_data(attr), addr_len);
			}
			break;
			
		case IFLA_QDISC:
			{
				const char *qdisc = nla_data(attr);
				size_t len = nla_len(attr);
				if (len > 0 && len < sizeof(link_info->qdisc)) {
					memcpy(link_info->qdisc, qdisc, len);
					link_info->qdisc[len] = '\0';
				}
			}
			break;
			
		case IFLA_OPERSTATE:
			if (nla_len(attr) == sizeof(uint8_t))
				link_info->operstate = *(uint8_t *)nla_data(attr);
			break;
		}
	}
	
	/* Fallback to if_indextoname if name not in attributes */
	if (link_info->ifname[0] == '\0') {
		if_indextoname(ifi->ifi_index, link_info->ifname);
	}
	
	return 0;
}

/**
 * Fast-path address message parser
 * 
 * Optimized version with inline attribute parsing.
 */
int nlmon_parse_addr_msg_fast(struct nlmsghdr *nlh, struct nlmon_addr_info *addr_info)
{
	struct ifaddrmsg *ifa;
	struct nlattr *attr;
	int remaining;
	
	if (!nlh || !addr_info)
		return -EINVAL;
	
	/* Get address info header */
	ifa = (struct ifaddrmsg *)nlmsg_data(nlh);
	
	/* Initialize output structure */
	memset(addr_info, 0, sizeof(*addr_info));
	
	/* Extract basic fields from header */
	addr_info->family = ifa->ifa_family;
	addr_info->ifindex = ifa->ifa_index;
	addr_info->prefixlen = ifa->ifa_prefixlen;
	addr_info->scope = ifa->ifa_scope;
	
	/* Fast inline attribute iteration */
	nlmsg_for_each_attr(attr, nlh, sizeof(*ifa), remaining) {
		switch (nla_type(attr)) {
		case IFA_ADDRESS:
			{
				void *addr_data = nla_data(attr);
				
				if (ifa->ifa_family == AF_INET) {
					inet_ntop(AF_INET, addr_data, addr_info->addr, sizeof(addr_info->addr));
				} else if (ifa->ifa_family == AF_INET6) {
					inet_ntop(AF_INET6, addr_data, addr_info->addr, sizeof(addr_info->addr));
				}
			}
			break;
			
		case IFA_LABEL:
			{
				const char *label = nla_data(attr);
				size_t len = nla_len(attr);
				if (len > 0 && len < IFNAMSIZ) {
					memcpy(addr_info->label, label, len);
					addr_info->label[len] = '\0';
				}
			}
			break;
		}
	}
	
	/* Fallback to if_indextoname if label not in attributes */
	if (addr_info->label[0] == '\0') {
		if_indextoname(ifa->ifa_index, addr_info->label);
	}
	
	return 0;
}

/**
 * Optimized attribute lookup
 * 
 * Fast path for finding a single attribute without full parsing.
 */
struct nlattr *nlmon_find_attr_fast(struct nlmsghdr *nlh, int hdrlen, int attrtype)
{
	struct nlattr *attr;
	int remaining;
	
	if (!nlh)
		return NULL;
	
	nlmsg_for_each_attr(attr, nlh, hdrlen, remaining) {
		if (nla_type(attr) == attrtype)
			return attr;
	}
	
	return NULL;
}

/**
 * Batch attribute extraction
 * 
 * Extract multiple common attributes in one pass.
 */
int nlmon_extract_link_attrs_batch(struct nlmsghdr *nlh,
                                   char *ifname, size_t ifname_len,
                                   uint32_t *mtu,
                                   uint32_t *flags)
{
	struct ifinfomsg *ifi;
	struct nlattr *attr;
	int remaining;
	bool found_name = false;
	bool found_mtu = false;
	
	if (!nlh)
		return -EINVAL;
	
	ifi = (struct ifinfomsg *)nlmsg_data(nlh);
	
	/* Extract flags from header */
	if (flags)
		*flags = ifi->ifi_flags;
	
	/* Fast attribute scan */
	nlmsg_for_each_attr(attr, nlh, sizeof(*ifi), remaining) {
		switch (nla_type(attr)) {
		case IFLA_IFNAME:
			if (ifname && ifname_len > 0) {
				const char *name = nla_data(attr);
				size_t len = nla_len(attr);
				if (len > 0 && len < ifname_len) {
					memcpy(ifname, name, len);
					ifname[len] = '\0';
					found_name = true;
				}
			}
			break;
			
		case IFLA_MTU:
			if (mtu && nla_len(attr) == sizeof(uint32_t)) {
				*mtu = *(uint32_t *)nla_data(attr);
				found_mtu = true;
			}
			break;
		}
		
		/* Early exit if we found everything */
		if (found_name && found_mtu)
			break;
	}
	
	return 0;
}

/**
 * Inline message type check
 * 
 * Fast check for common message types without function call overhead.
 */
static inline bool is_link_message(uint16_t msg_type)
{
	return msg_type == RTM_NEWLINK || msg_type == RTM_DELLINK || msg_type == RTM_GETLINK;
}

static inline bool is_addr_message(uint16_t msg_type)
{
	return msg_type == RTM_NEWADDR || msg_type == RTM_DELADDR || msg_type == RTM_GETADDR;
}

static inline bool is_route_message(uint16_t msg_type)
{
	return msg_type == RTM_NEWROUTE || msg_type == RTM_DELROUTE || msg_type == RTM_GETROUTE;
}

/**
 * Fast message type classification
 */
enum nlmon_msg_class {
	MSG_CLASS_LINK,
	MSG_CLASS_ADDR,
	MSG_CLASS_ROUTE,
	MSG_CLASS_NEIGH,
	MSG_CLASS_OTHER
};

enum nlmon_msg_class nlmon_classify_route_msg(uint16_t msg_type)
{
	if (is_link_message(msg_type))
		return MSG_CLASS_LINK;
	if (is_addr_message(msg_type))
		return MSG_CLASS_ADDR;
	if (is_route_message(msg_type))
		return MSG_CLASS_ROUTE;
	if (msg_type == RTM_NEWNEIGH || msg_type == RTM_DELNEIGH || msg_type == RTM_GETNEIGH)
		return MSG_CLASS_NEIGH;
	
	return MSG_CLASS_OTHER;
}

/**
 * Optimized message validation
 * 
 * Quick validation checks before full parsing.
 */
bool nlmon_validate_msg_fast(struct nlmsghdr *nlh, size_t buf_len)
{
	/* Check minimum size */
	if (buf_len < sizeof(struct nlmsghdr))
		return false;
	
	/* Check message length */
	if (nlh->nlmsg_len < sizeof(struct nlmsghdr))
		return false;
	
	if (nlh->nlmsg_len > buf_len)
		return false;
	
	/* Check alignment */
	if (nlh->nlmsg_len & (NLMSG_ALIGNTO - 1))
		return false;
	
	return true;
}

/**
 * Attribute count estimation
 * 
 * Quick estimate of attribute count for pre-allocation.
 */
size_t nlmon_estimate_attr_count(struct nlmsghdr *nlh, int hdrlen)
{
	size_t count = 0;
	struct nlattr *attr;
	int remaining;
	
	if (!nlh)
		return 0;
	
	nlmsg_for_each_attr(attr, nlh, hdrlen, remaining) {
		count++;
		/* Cap at reasonable limit */
		if (count >= 64)
			break;
	}
	
	return count;
}

/**
 * Zero-copy attribute access
 * 
 * Get attribute data pointer without copying.
 */
const void *nlmon_get_attr_data_zerocopy(struct nlattr *attr, size_t *len)
{
	if (!attr || !len)
		return NULL;
	
	*len = nla_len(attr);
	return nla_data(attr);
}

/**
 * Optimized string attribute extraction
 * 
 * Extract string with minimal overhead.
 */
const char *nlmon_get_string_attr_fast(struct nlattr *attr)
{
	if (!attr)
		return NULL;
	
	/* Verify it's a valid string (null-terminated) */
	const char *str = nla_data(attr);
	size_t len = nla_len(attr);
	
	if (len > 0 && str[len - 1] == '\0')
		return str;
	
	return NULL;
}

/**
 * Batch message processing
 * 
 * Process multiple messages in one call for better cache locality.
 */
int nlmon_process_messages_batch(struct nlmsghdr **messages,
                                 size_t count,
                                 int (*handler)(struct nlmsghdr *, void *),
                                 void *user_data)
{
	size_t i;
	int ret;
	int processed = 0;
	
	if (!messages || !handler || count == 0)
		return -EINVAL;
	
	for (i = 0; i < count; i++) {
		if (!messages[i])
			continue;
		
		ret = handler(messages[i], user_data);
		if (ret == 0)
			processed++;
	}
	
	return processed;
}

/**
 * Prefetch next message
 * 
 * Hint to CPU to prefetch next message for better cache performance.
 */
static inline void prefetch_message(struct nlmsghdr *nlh)
{
	if (nlh) {
		__builtin_prefetch(nlh, 0, 3);
	}
}

/**
 * Optimized multi-message iteration
 * 
 * Iterate through multiple messages with prefetching.
 */
int nlmon_iterate_messages_optimized(void *buf, size_t len,
                                    int (*handler)(struct nlmsghdr *, void *),
                                    void *user_data)
{
	struct nlmsghdr *nlh;
	struct nlmsghdr *next_nlh;
	int processed = 0;
	
	if (!buf || !handler || len == 0)
		return -EINVAL;
	
	for (nlh = (struct nlmsghdr *)buf;
	     NLMSG_OK(nlh, len);
	     nlh = NLMSG_NEXT(nlh, len)) {
		
		/* Prefetch next message */
		next_nlh = NLMSG_NEXT(nlh, len);
		if (NLMSG_OK(next_nlh, len)) {
			prefetch_message(next_nlh);
		}
		
		/* Process current message */
		if (handler(nlh, user_data) == 0)
			processed++;
	}
	
	return processed;
}

