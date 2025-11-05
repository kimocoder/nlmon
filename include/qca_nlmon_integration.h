/*
 * QCA-nlmon Integration Module Header
 * 
 * Integrates QCA vendor command control with nlmon event monitoring
 */

#ifndef QCA_NLMON_INTEGRATION_H
#define QCA_NLMON_INTEGRATION_H

#include <stdint.h>

/* Forward declaration */
struct nlmon_event;

/**
 * Initialize QCA-nlmon integration
 * 
 * @param interface: Target WiFi interface (e.g., "wlan0"), NULL for default
 * @param verbose: Enable verbose logging
 * @return: 0 on success, -1 on error
 */
int qca_nlmon_init(const char *interface, int verbose);

/**
 * Enable/disable automatic roaming adjustment
 * 
 * When enabled, nlmon will automatically adjust roaming parameters
 * based on connection events (connect, disconnect, roam)
 * 
 * @param enable: 1 to enable, 0 to disable
 */
void qca_nlmon_enable_auto_roaming(int enable);

/**
 * Set roaming RSSI thresholds
 * 
 * @param weak: RSSI threshold for weak signal (aggressive roaming)
 * @param normal: RSSI threshold for normal operation
 * @param strong: RSSI threshold for strong signal (conservative roaming)
 */
void qca_nlmon_set_roaming_thresholds(int weak, int normal, int strong);

/**
 * Enable/disable stats collection on roam events
 * 
 * @param enable: 1 to enable, 0 to disable
 */
void qca_nlmon_enable_stats_on_roam(int enable);

/**
 * Process nl80211 event from nlmon
 * 
 * This should be called from nlmon's event callback when
 * an nl80211 event is received
 * 
 * @param evt: nlmon event structure
 */
void qca_nlmon_process_nl80211_event(struct nlmon_event *evt);

/**
 * Send QCA vendor command to configured interface
 * 
 * @param subcmd: Vendor subcommand ID
 * @param data: Command data (can be NULL)
 * @param data_len: Length of command data
 * @return: 0 on success, -1 on error
 */
int qca_nlmon_send_command(uint32_t subcmd, const void *data, size_t data_len);

/**
 * Get link layer statistics for configured interface
 * 
 * @return: 0 on success, -1 on error
 */
int qca_nlmon_get_stats(void);

/**
 * Clear link layer statistics for configured interface
 * 
 * @return: 0 on success, -1 on error
 */
int qca_nlmon_clear_stats(void);

/**
 * Get WiFi driver info for configured interface
 * 
 * @return: 0 on success, -1 on error
 */
int qca_nlmon_get_wifi_info(void);

/**
 * Cleanup QCA-nlmon integration
 */
void qca_nlmon_cleanup(void);

#endif /* QCA_NLMON_INTEGRATION_H */
