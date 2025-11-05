/*
 * QCA Driver Control Module Header
 * 
 * Provides functions to send vendor commands to QCA drivers
 * Integrated into nlmon for seamless driver control
 */

#ifndef QCA_CONTROL_H
#define QCA_CONTROL_H

#include <stdint.h>
#include <stddef.h>

/* QCA vendor ID */
#define QCA_NL80211_VENDOR_ID  0x001374

/* Common QCA vendor subcommands */
#define QCA_NL80211_VENDOR_SUBCMD_LL_STATS_GET   15
#define QCA_NL80211_VENDOR_SUBCMD_LL_STATS_CLR   16
#define QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO  61
#define QCA_NL80211_VENDOR_SUBCMD_ROAM           64

/**
 * Callback for vendor command responses
 * Returns: NL_SKIP to continue, NL_STOP to stop processing
 */
typedef int (*qca_response_cb_t)(struct nl_msg *msg, void *arg);

/**
 * Send QCA vendor command
 * 
 * @param ifname: Interface name (e.g., "wlan0")
 * @param subcmd: Vendor subcommand ID
 * @param data: Command data (can be NULL)
 * @param data_len: Length of command data
 * @param response_cb: Callback for response (can be NULL)
 * @param user_data: User data passed to callback
 * @return: 0 on success, -1 on error
 */
int qca_send_vendor_command(const char *ifname,
                            uint32_t subcmd,
                            const void *data,
                            size_t data_len,
                            qca_response_cb_t response_cb,
                            void *user_data);

/**
 * Get link layer statistics
 * 
 * @param ifname: Interface name
 * @param callback: Callback for response
 * @param user_data: User data passed to callback
 * @return: 0 on success, -1 on error
 */
int qca_get_link_stats(const char *ifname,
                       qca_response_cb_t callback,
                       void *user_data);

/**
 * Clear link layer statistics
 * 
 * @param ifname: Interface name
 * @return: 0 on success, -1 on error
 */
int qca_clear_link_stats(const char *ifname);

/**
 * Get WiFi driver info
 * 
 * @param ifname: Interface name
 * @param callback: Callback for response
 * @param user_data: User data passed to callback
 * @return: 0 on success, -1 on error
 */
int qca_get_wifi_info(const char *ifname,
                      qca_response_cb_t callback,
                      void *user_data);

/**
 * Set roaming RSSI threshold
 * 
 * @param ifname: Interface name
 * @param rssi_threshold: RSSI threshold in dBm (e.g., 70 for -70 dBm)
 * @return: 0 on success, -1 on error
 */
int qca_set_roam_rssi(const char *ifname, int rssi_threshold);

/**
 * Send raw vendor command
 * 
 * @param ifname: Interface name
 * @param subcmd: Vendor subcommand ID
 * @param data: Command data (can be NULL)
 * @param data_len: Length of command data
 * @param callback: Callback for response (can be NULL)
 * @param user_data: User data passed to callback
 * @return: 0 on success, -1 on error
 */
int qca_send_raw_command(const char *ifname,
                         uint32_t subcmd,
                         const void *data,
                         size_t data_len,
                         qca_response_cb_t callback,
                         void *user_data);

#endif /* QCA_CONTROL_H */
