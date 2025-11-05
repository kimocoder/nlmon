#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>
#include <net/if.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/handlers.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "nlmon_netlink.h"
#include "nlmon_nl_genl.h"
#include "event_processor.h"
#include "qca_vendor.h"

/**
 * Generic netlink message callback handler
 * 
 * This is the main callback for NETLINK_GENERIC messages. It extracts the
 * generic netlink header, determines the family ID and command, and routes
 * to the appropriate parser function.
 */
int nlmon_genl_msg_handler(struct nl_msg *msg, void *arg)
{
	struct nlmon_nl_manager *mgr = (struct nlmon_nl_manager *)arg;
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct genlmsghdr *gnlh;
	struct nlmon_event evt;
	int ret = 0;
	
	if (!mgr || !nlh)
		return NL_SKIP;
	
	/* Get generic netlink header */
	gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
	if (!gnlh)
		return NL_SKIP;
	
	/* Initialize event structure */
	memset(&evt, 0, sizeof(evt));
	evt.timestamp = time(NULL) * 1000000000ULL; /* Convert to nanoseconds */
	evt.netlink.protocol = NETLINK_GENERIC;
	evt.netlink.msg_type = nlh->nlmsg_type;
	evt.netlink.msg_flags = nlh->nlmsg_flags;
	evt.netlink.seq = nlh->nlmsg_seq;
	evt.netlink.pid = nlh->nlmsg_pid;
	
	/* Extract generic netlink specific fields */
	evt.netlink.genl_cmd = gnlh->cmd;
	evt.netlink.genl_version = gnlh->version;
	evt.netlink.genl_family_id = nlh->nlmsg_type;
	
	/* Determine family and route to appropriate parser */
	if (mgr->nl80211_id > 0 && nlh->nlmsg_type == (uint16_t)mgr->nl80211_id) {
		/* nl80211 message */
		strncpy(evt.netlink.genl_family_name, "nl80211", 
		        sizeof(evt.netlink.genl_family_name) - 1);
		ret = nlmon_parse_nl80211_msg(nlh, &evt);
		evt.event_type = nlh->nlmsg_type;
	} else {
		/* Unknown family - skip for now */
		return NL_SKIP;
	}
	
	/* If parsing failed, skip this message */
	if (ret < 0) {
		fprintf(stderr, "Failed to parse generic netlink message family %d cmd %d: %d\n",
		        nlh->nlmsg_type, gnlh->cmd, ret);
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
 * Parse nl80211 message
 * 
 * Extracts nl80211 command and attributes from nl80211 messages.
 */
int nlmon_parse_nl80211_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct genlmsghdr *gnlh;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct nlmon_nl80211_info *nl80211_info;
	int ret;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Get generic netlink header */
	gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
	if (!gnlh)
		return -EINVAL;
	
	/* Parse nl80211 attributes */
	ret = genlmsg_parse(nlh, 0, tb, NL80211_ATTR_MAX, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse nl80211 message attributes: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	/* Allocate nl80211 info structure */
	nl80211_info = calloc(1, sizeof(*nl80211_info));
	if (!nl80211_info)
		return -ENOMEM;
	
	/* Extract nl80211 command */
	nl80211_info->cmd = gnlh->cmd;
	
	/* Extract wiphy index */
	if (tb[NL80211_ATTR_WIPHY]) {
		nl80211_info->wiphy = nla_get_u32(tb[NL80211_ATTR_WIPHY]);
	} else {
		nl80211_info->wiphy = -1;
	}
	
	/* Extract interface index */
	if (tb[NL80211_ATTR_IFINDEX]) {
		nl80211_info->ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
		/* Get interface name from index */
		if_indextoname(nl80211_info->ifindex, nl80211_info->ifname);
		/* Also set in event structure */
		strncpy(evt->interface, nl80211_info->ifname, sizeof(evt->interface) - 1);
	} else {
		nl80211_info->ifindex = -1;
	}
	
	/* Extract interface type */
	if (tb[NL80211_ATTR_IFTYPE]) {
		nl80211_info->iftype = nla_get_u32(tb[NL80211_ATTR_IFTYPE]);
	}
	
	/* Extract MAC address */
	if (tb[NL80211_ATTR_MAC]) {
		int addr_len = nla_len(tb[NL80211_ATTR_MAC]);
		if (addr_len <= ETH_ALEN) {
			memcpy(nl80211_info->mac, nla_data(tb[NL80211_ATTR_MAC]), addr_len);
		}
	}
	
	/* Extract frequency */
	if (tb[NL80211_ATTR_WIPHY_FREQ]) {
		nl80211_info->freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
	}
	
	/* Extract channel type */
	if (tb[NL80211_ATTR_WIPHY_CHANNEL_TYPE]) {
		nl80211_info->channel_type = nla_get_u32(tb[NL80211_ATTR_WIPHY_CHANNEL_TYPE]);
	}
	
	/* Check if this is a vendor command */
	if (gnlh->cmd == NL80211_CMD_VENDOR && tb[NL80211_ATTR_VENDOR_ID]) {
		uint32_t vendor_id = nla_get_u32(tb[NL80211_ATTR_VENDOR_ID]);
		
		/* If it's a QCA vendor command, parse it separately */
		if (vendor_id == OUI_QCA) {
			/* Free nl80211_info as we'll use QCA vendor info instead */
			free(nl80211_info);
			return nlmon_parse_qca_vendor_msg(nlh, evt);
		}
	}
	
	/* Store in event */
	evt->netlink.data.generic = nl80211_info;
	
	return 0;
}

/**
 * Parse QCA vendor command message
 * 
 * Extracts QCA vendor ID, subcommand, and nested vendor data attributes.
 */
int nlmon_parse_qca_vendor_msg(struct nlmsghdr *nlh, struct nlmon_event *evt)
{
	struct genlmsghdr *gnlh;
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct nlmon_qca_vendor_info *qca_info;
	uint32_t vendor_id;
	uint32_t subcmd;
	int ret;
	
	if (!nlh || !evt)
		return -EINVAL;
	
	/* Get generic netlink header */
	gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
	if (!gnlh)
		return -EINVAL;
	
	/* Parse nl80211 attributes */
	ret = genlmsg_parse(nlh, 0, tb, NL80211_ATTR_MAX, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse QCA vendor message attributes: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	/* Verify this is a vendor command */
	if (gnlh->cmd != NL80211_CMD_VENDOR) {
		fprintf(stderr, "Not a vendor command (cmd=%d)\n", gnlh->cmd);
		return -EINVAL;
	}
	
	/* Extract vendor ID */
	if (!tb[NL80211_ATTR_VENDOR_ID]) {
		fprintf(stderr, "Missing vendor ID attribute\n");
		return -EINVAL;
	}
	vendor_id = nla_get_u32(tb[NL80211_ATTR_VENDOR_ID]);
	
	/* Verify it's a QCA vendor command */
	if (vendor_id != OUI_QCA) {
		fprintf(stderr, "Not a QCA vendor command (vendor_id=0x%06x)\n", vendor_id);
		return -EINVAL;
	}
	
	/* Extract vendor subcommand */
	if (!tb[NL80211_ATTR_VENDOR_SUBCMD]) {
		fprintf(stderr, "Missing vendor subcommand attribute\n");
		return -EINVAL;
	}
	subcmd = nla_get_u32(tb[NL80211_ATTR_VENDOR_SUBCMD]);
	
	/* Allocate QCA vendor info structure */
	qca_info = calloc(1, sizeof(*qca_info));
	if (!qca_info)
		return -ENOMEM;
	
	/* Store vendor ID and subcommand */
	qca_info->vendor_id = vendor_id;
	qca_info->subcmd = subcmd;
	
	/* Get subcommand name from existing qca_vendor.c function */
	const char *subcmd_name = qca_vendor_subcmd_to_string(subcmd);
	if (subcmd_name) {
		strncpy(qca_info->subcmd_name, subcmd_name, sizeof(qca_info->subcmd_name) - 1);
	} else {
		snprintf(qca_info->subcmd_name, sizeof(qca_info->subcmd_name), 
		         "UNKNOWN_0x%x", subcmd);
	}
	
	/* Parse nested vendor data if present */
	if (tb[NL80211_ATTR_VENDOR_DATA]) {
		/* For now, we just note that vendor data is present
		 * Future enhancement: parse specific vendor data attributes
		 * based on the subcommand using nla_parse_nested()
		 */
		int data_len = nla_len(tb[NL80211_ATTR_VENDOR_DATA]);
		(void)data_len; /* Suppress unused warning */
		
		/* Example of how to parse nested attributes:
		 * struct nlattr *vendor_tb[QCA_WLAN_VENDOR_ATTR_MAX + 1];
		 * ret = nla_parse_nested(vendor_tb, QCA_WLAN_VENDOR_ATTR_MAX,
		 *                        tb[NL80211_ATTR_VENDOR_DATA], NULL);
		 * if (ret == 0) {
		 *     // Process vendor-specific attributes
		 * }
		 */
	}
	
	/* Extract interface index if present */
	if (tb[NL80211_ATTR_IFINDEX]) {
		int ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
		char ifname[IFNAMSIZ];
		if (if_indextoname(ifindex, ifname)) {
			strncpy(evt->interface, ifname, sizeof(evt->interface) - 1);
		}
	}
	
	/* Store in event */
	evt->netlink.data.generic = qca_info;
	
	return 0;
}
