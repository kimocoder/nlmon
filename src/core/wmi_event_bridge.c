/**
 * @file wmi_event_bridge.c
 * @brief WMI to nlmon event bridge implementation
 *
 * Converts WMI log entries into nlmon events for unified processing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include "../../include/wmi_event_bridge.h"
#include "../../include/qca_wmi.h"
#include "../../include/event_processor.h"
#include "../../include/wmi_log_reader.h"

/* Event type for WMI events */
#define NLMON_EVENT_WMI 0x1000

/* Bridge state */
static struct {
    struct event_processor *event_processor;
    int handler_id;
    int verbose;
    void *user_data;
    atomic_bool initialized;
    pthread_mutex_t mutex;
    
    /* Statistics */
    atomic_ullong events_converted;
    atomic_ullong events_submitted;
    atomic_ullong conversion_errors;
} bridge_state = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

/**
 * Initialize WMI event bridge
 */
int wmi_bridge_init(struct wmi_bridge_config *config)
{
    if (!config) {
        return -1;
    }
    
    /* Event processor is optional - can be NULL for simple logging mode */
    
    pthread_mutex_lock(&bridge_state.mutex);
    
    bridge_state.event_processor = config->event_processor;
    bridge_state.verbose = config->verbose;
    bridge_state.user_data = config->user_data;
    bridge_state.handler_id = -1;
    
    /* Initialize atomic variables */
    atomic_init(&bridge_state.initialized, false);
    atomic_init(&bridge_state.events_converted, 0);
    atomic_init(&bridge_state.events_submitted, 0);
    atomic_init(&bridge_state.conversion_errors, 0);
    
    atomic_store(&bridge_state.initialized, true);
    
    pthread_mutex_unlock(&bridge_state.mutex);
    
    return 0;
}

/**
 * Convert WMI log entry to nlmon event
 */
int wmi_to_nlmon_event(const struct wmi_log_entry *wmi_entry,
                       struct nlmon_event *event)
{
    struct wmi_event_metadata *metadata;
    char display_buf[512];
    char *display_str;
    
    if (!wmi_entry || !event) {
        return -1;
    }
    
    /* Allocate metadata structure */
    metadata = calloc(1, sizeof(*metadata));
    if (!metadata) {
        atomic_fetch_add(&bridge_state.conversion_errors, 1);
        return -2;
    }
    
    /* Populate metadata from WMI entry */
    metadata->cmd_id = wmi_entry->cmd_id;
    strncpy(metadata->command_name, wmi_entry->command_name,
            sizeof(metadata->command_name) - 1);
    metadata->vdev_id = wmi_entry->vdev_id;
    metadata->pdev_id = wmi_entry->pdev_id;
    metadata->stats_id = wmi_entry->stats_id;
    strncpy(metadata->stats_type, wmi_entry->stats_type,
            sizeof(metadata->stats_type) - 1);
    metadata->req_id = wmi_entry->req_id;
    strncpy(metadata->peer_mac, wmi_entry->peer_mac,
            sizeof(metadata->peer_mac) - 1);
    metadata->htc_tag = wmi_entry->htc_tag;
    strncpy(metadata->thread_name, wmi_entry->thread_name,
            sizeof(metadata->thread_name) - 1);
    metadata->thread_id = wmi_entry->thread_id;
    metadata->has_stats = wmi_entry->has_stats;
    metadata->has_peer = wmi_entry->has_peer;
    
    /* Format display string with all relevant information */
    wmi_format_entry(wmi_entry, display_buf, sizeof(display_buf));
    
    display_str = strdup(display_buf);
    if (!display_str) {
        free(metadata);
        atomic_fetch_add(&bridge_state.conversion_errors, 1);
        return -2;
    }
    
    /* Populate nlmon event structure */
    memset(event, 0, sizeof(*event));
    event->timestamp = wmi_entry->timestamp;
    event->event_type = NLMON_EVENT_WMI;
    event->message_type = wmi_entry->cmd_id & 0xFFFF;
    strncpy(event->interface, "wmi0", sizeof(event->interface) - 1);
    event->data = metadata;
    event->data_size = sizeof(*metadata);
    event->user_data = display_str;
    
    atomic_fetch_add(&bridge_state.events_converted, 1);
    
    return 0;
}

/**
 * WMI event handler callback
 */
static void wmi_event_handler(struct nlmon_event *event, void *ctx)
{
    struct wmi_event_metadata *metadata;
    char *display_str;
    
    if (!event || event->event_type != NLMON_EVENT_WMI) {
        return;
    }
    
    metadata = (struct wmi_event_metadata *)event->data;
    display_str = (char *)event->user_data;
    
    if (bridge_state.verbose) {
        printf("[WMI] %s\n", display_str ? display_str : "");
    }
    
    /* Free allocated resources */
    if (metadata) {
        free(metadata);
        event->data = NULL;
    }
    
    if (display_str) {
        free(display_str);
        event->user_data = NULL;
    }
}

/**
 * Register WMI event handler with event processor
 */
int wmi_register_event_handler(void)
{
    int handler_id;
    
    if (!atomic_load(&bridge_state.initialized)) {
        return -1;
    }
    
    /* If no event processor, return success without registering */
    if (!bridge_state.event_processor) {
        return 0;
    }
    
    pthread_mutex_lock(&bridge_state.mutex);
    
    if (bridge_state.handler_id >= 0) {
        /* Already registered */
        handler_id = bridge_state.handler_id;
        pthread_mutex_unlock(&bridge_state.mutex);
        return handler_id;
    }
    
    handler_id = event_processor_register_handler(
        bridge_state.event_processor,
        wmi_event_handler,
        bridge_state.user_data
    );
    
    if (handler_id < 0) {
        pthread_mutex_unlock(&bridge_state.mutex);
        return -2;
    }
    
    bridge_state.handler_id = handler_id;
    
    pthread_mutex_unlock(&bridge_state.mutex);
    
    return handler_id;
}

/**
 * Submit WMI log entry as event
 */
int wmi_bridge_submit(const struct wmi_log_entry *wmi_entry)
{
    struct nlmon_event event;
    bool submitted;
    int ret;
    
    if (!atomic_load(&bridge_state.initialized)) {
        return -1;
    }
    
    if (!wmi_entry) {
        return -1;
    }
    
    /* If no event processor, just count the conversion and return success */
    if (!bridge_state.event_processor) {
        atomic_fetch_add(&bridge_state.events_converted, 1);
        return 0;
    }
    
    /* Convert WMI entry to nlmon event */
    ret = wmi_to_nlmon_event(wmi_entry, &event);
    if (ret < 0) {
        return -2;
    }
    
    /* Submit to event processor */
    submitted = event_processor_submit(bridge_state.event_processor, &event);
    if (!submitted) {
        /* Cleanup on submission failure */
        if (event.data) {
            free(event.data);
        }
        if (event.user_data) {
            free(event.user_data);
        }
        return -3;
    }
    
    atomic_fetch_add(&bridge_state.events_submitted, 1);
    
    return 0;
}

/**
 * Get WMI bridge statistics
 */
int wmi_bridge_get_stats(uint64_t *events_converted,
                         uint64_t *events_submitted,
                         uint64_t *conversion_errors)
{
    if (!atomic_load(&bridge_state.initialized)) {
        return -1;
    }
    
    if (events_converted) {
        *events_converted = atomic_load(&bridge_state.events_converted);
    }
    
    if (events_submitted) {
        *events_submitted = atomic_load(&bridge_state.events_submitted);
    }
    
    if (conversion_errors) {
        *conversion_errors = atomic_load(&bridge_state.conversion_errors);
    }
    
    return 0;
}

/**
 * WMI log reader callback for event bridge integration
 * 
 * This callback is invoked by the WMI log reader for each parsed line.
 * It parses the line into a WMI log entry and submits it as an event.
 */
static int wmi_bridge_log_callback(const char *line, void *user_data)
{
    struct wmi_log_entry entry;
    int ret;
    
    if (!line) {
        return -1;
    }
    
    /* Parse the log line into a WMI entry */
    memset(&entry, 0, sizeof(entry));
    ret = wmi_parse_log_line(line, &entry);
    if (ret < 0) {
        /* Parsing failed - not a WMI line or malformed */
        return 0; /* Continue processing other lines */
    }
    
    /* Submit the parsed entry as an event */
    ret = wmi_bridge_submit(&entry);
    if (ret < 0) {
        /* Event submission failed */
        return ret;
    }
    
    return 0;
}

/**
 * Start WMI log monitoring with event bridge integration
 * 
 * This function sets up the WMI log reader to feed events into the
 * event processor through the bridge. It's thread-safe and can be
 * called from any thread.
 *
 * @param log_source Log file path or "-" for stdin
 * @param follow_mode Enable tail -f style following
 * @return 0 on success, negative error code on failure
 */
int wmi_bridge_start_monitoring(const char *log_source, int follow_mode)
{
    struct wmi_log_config log_config;
    int ret;
    
    if (!atomic_load(&bridge_state.initialized)) {
        return -1;
    }
    
    if (!log_source) {
        return -1;
    }
    
    /* Configure WMI log reader */
    memset(&log_config, 0, sizeof(log_config));
    log_config.log_source = log_source;
    log_config.follow_mode = follow_mode;
    log_config.buffer_size = 4096;
    log_config.callback = wmi_bridge_log_callback;
    log_config.user_data = bridge_state.user_data;
    
    /* Initialize log reader */
    ret = wmi_log_reader_init(&log_config);
    if (ret < 0) {
        return ret;
    }
    
    /* Start reading logs (this will block until stopped or EOF) */
    ret = wmi_log_reader_start();
    
    return ret;
}

/**
 * Stop WMI log monitoring
 */
void wmi_bridge_stop_monitoring(void)
{
    wmi_log_reader_stop();
}

/**
 * Cleanup WMI event bridge
 */
void wmi_bridge_cleanup(void)
{
    if (!atomic_load(&bridge_state.initialized)) {
        return;
    }
    
    /* Stop monitoring if active */
    wmi_bridge_stop_monitoring();
    wmi_log_reader_cleanup();
    
    pthread_mutex_lock(&bridge_state.mutex);
    
    /* Unregister handler if registered */
    if (bridge_state.handler_id >= 0 && bridge_state.event_processor) {
        event_processor_unregister_handler(
            bridge_state.event_processor,
            bridge_state.handler_id
        );
        bridge_state.handler_id = -1;
    }
    
    bridge_state.event_processor = NULL;
    bridge_state.user_data = NULL;
    
    atomic_store(&bridge_state.initialized, false);
    
    pthread_mutex_unlock(&bridge_state.mutex);
}
