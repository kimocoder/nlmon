/*
 * QCA Driver Control Module
 * 
 * Provides functions to send vendor commands to QCA drivers
 * Integrated into nlmon for seamless driver control
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>

#include "qca_control.h"

/**
 * Send QCA vendor command
 */
int qca_send_vendor_command(const char *ifname,
                            uint32_t subcmd,
                            const void *data,
                            size_t data_len,
                            qca_response_cb_t response_cb,
                            void *user_data)
{
	struct nl_sock *sock = NULL;
	struct nl_msg *msg = NULL;
	struct nl_cb *cb = NULL;
	int nl80211_id, ifindex;
	int ret = -1;
	int err = 1;
	
	/* Create socket */
	sock = nl_socket_alloc();
	if (!sock)
		goto out;
	
	if (genl_connect(sock) < 0)
		goto out;
	
	/* Resolve nl80211 */
	nl80211_id = genl_ctrl_resolve(sock, "nl80211");
	if (nl80211_id < 0)
		goto out;
	
	/* Get interface index */
	ifindex = if_nametoindex(ifname);
	if (ifindex == 0)
		goto out;
	
	/* Build message */
	msg = nlmsg_alloc();
	if (!msg)
		goto out;
	
	genlmsg_put(msg, 0, 0, nl80211_id, 0, 0, NL80211_CMD_VENDOR, 0);
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);
	nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, QCA_NL80211_VENDOR_ID);
	nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD, subcmd);
	
	if (data && data_len > 0)
		nla_put(msg, NL80211_ATTR_VENDOR_DATA, data_len, data);
	
	/* Set up callback if provided */
	if (response_cb) {
		cb = nl_cb_alloc(NL_CB_DEFAULT);
		if (!cb)
			goto out;
		
		nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, response_cb, user_data);
		nl_cb_err(cb, NL_CB_CUSTOM, NULL, &err);
	}
	
	/* Send */
	ret = nl_send_auto_complete(sock, msg);
	if (ret < 0)
		goto out;
	
	/* Wait for response if callback provided */
	if (response_cb) {
		while (err > 0)
			nl_recvmsgs(sock, cb);
	}
	
	ret = 0;
	
out:
	if (cb)
		nl_cb_put(cb);
	if (msg)
		nlmsg_free(msg);
	if (sock)
		nl_socket_free(sock);
	
	return ret;
}

/**
 * Get link layer statistics
 */
int qca_get_link_stats(const char *ifname,
                       qca_response_cb_t callback,
                       void *user_data)
{
	struct {
		uint32_t req_id;
		uint32_t req_mask;
	} __attribute__((packed)) req = {
		.req_id = 1,
		.req_mask = 0x7  /* Radio + Iface + Peer */
	};
	
	return qca_send_vendor_command(ifname,
	                               QCA_NL80211_VENDOR_SUBCMD_LL_STATS_GET,
	                               &req, sizeof(req),
	                               callback, user_data);
}

/**
 * Clear link layer statistics
 */
int qca_clear_link_stats(const char *ifname)
{
	uint32_t stats_clear_req_mask = 0xFFFFFFFF;  /* Clear all stats */
	uint32_t stop_req = 0;  /* Don't stop collection */
	
	struct {
		uint32_t stats_clear_req_mask;
		uint32_t stop_req;
	} __attribute__((packed)) req = {
		.stats_clear_req_mask = stats_clear_req_mask,
		.stop_req = stop_req
	};
	
	return qca_send_vendor_command(ifname,
	                               QCA_NL80211_VENDOR_SUBCMD_LL_STATS_CLR,
	                               &req, sizeof(req),
	                               NULL, NULL);
}

/**
 * Get WiFi driver info
 */
int qca_get_wifi_info(const char *ifname,
                      qca_response_cb_t callback,
                      void *user_data)
{
	return qca_send_vendor_command(ifname,
	                               QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO,
	                               NULL, 0,
	                               callback, user_data);
}

/**
 * Set roaming RSSI threshold
 */
int qca_set_roam_rssi(const char *ifname, int rssi_threshold)
{
	/* Note: This is simplified. Real implementation needs proper
	 * nested attribute construction */
	uint32_t threshold = (uint32_t)rssi_threshold;
	
	return qca_send_vendor_command(ifname,
	                               QCA_NL80211_VENDOR_SUBCMD_ROAM,
	                               &threshold, sizeof(threshold),
	                               NULL, NULL);
}

/**
 * Send raw vendor command
 */
int qca_send_raw_command(const char *ifname,
                         uint32_t subcmd,
                         const void *data,
                         size_t data_len,
                         qca_response_cb_t callback,
                         void *user_data)
{
	return qca_send_vendor_command(ifname, subcmd, data, data_len,
	                               callback, user_data);
}
