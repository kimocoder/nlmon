/* security_detector.h - Security event detection system
 *
 * Detects suspicious network activities including promiscuous mode,
 * ARP floods, route hijacking, and suspicious interface changes.
 */

#ifndef SECURITY_DETECTOR_H
#define SECURITY_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Forward declarations */
struct nlmon_event;

/* Security event severity levels */
enum security_severity {
	SECURITY_INFO = 0,
	SECURITY_LOW = 1,
	SECURITY_MEDIUM = 2,
	SECURITY_HIGH = 3,
	SECURITY_CRITICAL = 4
};

/* Security event types */
enum security_event_type {
	SECURITY_PROMISCUOUS_MODE,
	SECURITY_ARP_FLOOD,
	SECURITY_ROUTE_HIJACK,
	SECURITY_SUSPICIOUS_INTERFACE,
	SECURITY_NEIGHBOR_FLOOD,
	SECURITY_INTERFACE_STORM
};

/* Security event structure */
struct security_event {
	enum security_event_type type;
	enum security_severity severity;
	time_t timestamp;
	char interface[16];
	char description[256];
	void *details;  /* Type-specific details */
};

/* Security event callback */
typedef void (*security_event_callback_t)(struct security_event *event, void *ctx);

/* Security detector configuration */
struct security_detector_config {
	/* ARP flood detection */
	bool enable_arp_flood_detection;
	double arp_rate_threshold;        /* ARP entries per second */
	size_t arp_time_window;           /* Time window in seconds */
	
	/* Promiscuous mode detection */
	bool enable_promisc_detection;
	
	/* Route hijack detection */
	bool enable_route_hijack_detection;
	
	/* Suspicious interface detection */
	bool enable_suspicious_interface_detection;
	
	/* Neighbor flood detection */
	bool enable_neighbor_flood_detection;
	double neighbor_rate_threshold;
	size_t neighbor_time_window;
	
	/* Interface storm detection */
	bool enable_interface_storm_detection;
	size_t interface_storm_threshold;  /* Events per second */
	size_t interface_storm_window;     /* Time window in seconds */
};

/* Security detector structure (opaque) */
struct security_detector;

/**
 * security_detector_create() - Create security detector
 * @config: Configuration parameters
 *
 * Returns: Pointer to security detector or NULL on error
 */
struct security_detector *security_detector_create(struct security_detector_config *config);

/**
 * security_detector_destroy() - Destroy security detector
 * @sd: Security detector
 */
void security_detector_destroy(struct security_detector *sd);

/**
 * security_detector_register_callback() - Register security event callback
 * @sd: Security detector
 * @callback: Callback function
 * @ctx: Context to pass to callback
 *
 * Returns: Callback ID or -1 on error
 */
int security_detector_register_callback(struct security_detector *sd,
                                        security_event_callback_t callback,
                                        void *ctx);

/**
 * security_detector_unregister_callback() - Unregister callback
 * @sd: Security detector
 * @callback_id: Callback ID returned by register
 */
void security_detector_unregister_callback(struct security_detector *sd, int callback_id);

/**
 * security_detector_process_event() - Process network event for security analysis
 * @sd: Security detector
 * @event: Network event to analyze
 *
 * Returns: true if security event was detected
 */
bool security_detector_process_event(struct security_detector *sd,
                                     struct nlmon_event *event);

/**
 * security_detector_stats() - Get detector statistics
 * @sd: Security detector
 * @events_processed: Output for total events processed
 * @security_events: Output for total security events detected
 * @promisc_events: Output for promiscuous mode events
 * @arp_flood_events: Output for ARP flood events
 * @route_hijack_events: Output for route hijack events
 */
void security_detector_stats(struct security_detector *sd,
                            unsigned long *events_processed,
                            unsigned long *security_events,
                            unsigned long *promisc_events,
                            unsigned long *arp_flood_events,
                            unsigned long *route_hijack_events);

/**
 * security_detector_reset() - Reset detector state
 * @sd: Security detector
 */
void security_detector_reset(struct security_detector *sd);

/* Helper functions for security event details */

/**
 * security_event_severity_string() - Get string representation of severity
 * @severity: Severity level
 *
 * Returns: String representation
 */
const char *security_event_severity_string(enum security_severity severity);

/**
 * security_event_type_string() - Get string representation of event type
 * @type: Event type
 *
 * Returns: String representation
 */
const char *security_event_type_string(enum security_event_type type);

#endif /* SECURITY_DETECTOR_H */
