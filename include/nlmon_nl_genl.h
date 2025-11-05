#ifndef NLMON_NL_GENL_H
#define NLMON_NL_GENL_H

#include <stdint.h>
#include <net/if.h>
#include <linux/if_ether.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nlmsghdr;
struct nlmon_event;
struct nlmon_nl_manager;
struct nl_msg;

/**
 * nl80211 information structure
 */
struct nlmon_nl80211_info {
	uint8_t cmd;                     /* nl80211 command */
	int wiphy;                       /* Wireless PHY index */
	int ifindex;                     /* Interface index */
	char ifname[IFNAMSIZ];           /* Interface name */
	uint32_t iftype;                 /* Interface type */
	uint8_t mac[ETH_ALEN];           /* MAC address */
	uint32_t freq;                   /* Frequency (MHz) */
	uint32_t channel_type;           /* Channel type */
};

/**
 * QCA vendor information structure
 */
struct nlmon_qca_vendor_info {
	uint32_t subcmd;                 /* QCA vendor subcommand */
	char subcmd_name[64];            /* Subcommand name */
	uint32_t vendor_id;              /* Vendor ID (OUI) */
	/* Additional vendor-specific data can be added here */
};

/**
 * Generic netlink message callback handler
 * 
 * Processes NETLINK_GENERIC messages and routes them to appropriate parsers.
 * 
 * @param msg Netlink message
 * @param arg User data (nlmon_nl_manager)
 * @return NL_OK to continue, NL_STOP to stop, or negative error code
 */
int nlmon_genl_msg_handler(struct nl_msg *msg, void *arg);

/**
 * Parse nl80211 message
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_nl80211_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

/**
 * Parse QCA vendor command message
 * 
 * @param nlh Netlink message header
 * @param evt Event structure to populate
 * @return 0 on success, negative error code on failure
 */
int nlmon_parse_qca_vendor_msg(struct nlmsghdr *nlh, struct nlmon_event *evt);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NL_GENL_H */
