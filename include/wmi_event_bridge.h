/**
 * @file wmi_event_bridge.h
 * @brief Bridge between WMI log entries and nlmon event system
 *
 * This module converts WMI log entries into nlmon events for unified
 * processing through the event processor pipeline.
 */

#ifndef WMI_EVENT_BRIDGE_H
#define WMI_EVENT_BRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include "qca_wmi.h"
#include "event_processor.h"

/**
 * WMI event metadata structure
 * Contains WMI-specific information attached to nlmon events
 */
struct wmi_event_metadata {
    uint32_t cmd_id;             /**< WMI command ID */
    char command_name[64];       /**< Decoded command name */
    uint32_t vdev_id;            /**< Virtual device ID */
    uint32_t pdev_id;            /**< Physical device ID */
    uint32_t stats_id;           /**< Statistics type ID */
    char stats_type[32];         /**< Decoded stats type */
    uint32_t req_id;             /**< Request ID */
    char peer_mac[18];           /**< Peer MAC address */
    uint32_t htc_tag;            /**< HTC tag */
    char thread_name[16];        /**< Thread/process name */
    uint64_t thread_id;          /**< Thread ID */
    uint8_t has_stats:1;         /**< Has statistics information */
    uint8_t has_peer:1;          /**< Has peer information */
    uint8_t reserved:6;
};

/**
 * WMI event bridge configuration
 */
struct wmi_bridge_config {
    struct event_processor *event_processor;  /**< Target event processor */
    int verbose;                              /**< Verbose output mode */
    void *user_data;                          /**< User context data */
};

/**
 * Initialize WMI event bridge
 *
 * @param config Bridge configuration
 * @return 0 on success, negative error code on failure
 *         -1: Invalid configuration
 *         -2: Event processor not available
 */
int wmi_bridge_init(struct wmi_bridge_config *config);

/**
 * Convert WMI log entry to nlmon event
 *
 * Creates an nlmon event from a WMI log entry, populating all relevant
 * fields and metadata. The event can then be submitted to the event processor.
 *
 * @param wmi_entry Source WMI log entry
 * @param event Target nlmon event structure (must be allocated by caller)
 * @return 0 on success, negative error code on failure
 *         -1: Invalid input parameters
 *         -2: Memory allocation failure
 */
int wmi_to_nlmon_event(const struct wmi_log_entry *wmi_entry,
                       struct nlmon_event *event);

/**
 * Register WMI event handler with event processor
 *
 * Registers a callback that will be invoked for WMI events submitted
 * to the event processor. This allows WMI events to be processed
 * alongside other nlmon events.
 *
 * @return Handler ID on success, negative error code on failure
 *         -1: Bridge not initialized
 *         -2: Failed to register handler
 */
int wmi_register_event_handler(void);

/**
 * Submit WMI log entry as event
 *
 * Convenience function that converts a WMI log entry to an nlmon event
 * and submits it to the event processor in one call.
 *
 * @param wmi_entry WMI log entry to submit
 * @return 0 on success, negative error code on failure
 *         -1: Bridge not initialized
 *         -2: Conversion failed
 *         -3: Event submission failed
 */
int wmi_bridge_submit(const struct wmi_log_entry *wmi_entry);

/**
 * Get WMI bridge statistics
 *
 * @param events_converted Output for total events converted
 * @param events_submitted Output for total events submitted
 * @param conversion_errors Output for conversion errors
 * @return 0 on success, -1 if bridge not initialized
 */
int wmi_bridge_get_stats(uint64_t *events_converted,
                         uint64_t *events_submitted,
                         uint64_t *conversion_errors);

/**
 * Start WMI log monitoring with event bridge integration
 *
 * Sets up the WMI log reader to feed events into the event processor
 * through the bridge. This function blocks until monitoring is stopped
 * or the log source is exhausted.
 *
 * @param log_source Log file path or "-" for stdin
 * @param follow_mode Enable tail -f style following (1=enabled, 0=disabled)
 * @return 0 on success, negative error code on failure
 *         -1: Bridge not initialized or invalid parameters
 *         -2: Failed to initialize log reader
 *         -3: I/O error during reading
 */
int wmi_bridge_start_monitoring(const char *log_source, int follow_mode);

/**
 * Stop WMI log monitoring
 *
 * Signals the monitoring thread to stop. Safe to call from signal
 * handlers or other threads.
 */
void wmi_bridge_stop_monitoring(void);

/**
 * Cleanup WMI event bridge
 *
 * Stops monitoring, unregisters handlers and frees resources.
 * Safe to call multiple times.
 */
void wmi_bridge_cleanup(void);

#endif /* WMI_EVENT_BRIDGE_H */
