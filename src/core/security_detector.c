/* security_detector.c - Security event detection implementation
 *
 * Implements detection of suspicious network activities including
 * promiscuous mode, ARP floods, route hijacking, and interface anomalies.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include "security_detector.h"
#include "event_processor.h"

/* Time window entry for rate tracking */
struct time_window_entry {
	time_t timestamp;
	char interface[16];
	struct time_window_entry *next;
};

/* Interface tracking for storm detection */
struct interface_tracker {
	char interface[16];
	size_t event_count;
	time_t window_start;
	struct interface_tracker *next;
};

/* Route tracking for hijack detection */
struct route_entry {
	uint32_t dst;
	uint32_t gateway;
	char interface[16];
	time_t timestamp;
	struct route_entry *next;
};

/* Security event callback entry */
struct callback_entry {
	int id;
	security_event_callback_t callback;
	void *ctx;
	struct callback_entry *next;
};

/* Security detector structure */
struct security_detector {
	struct security_detector_config config;
	
	/* ARP flood tracking */
	struct time_window_entry *arp_window;
	pthread_mutex_t arp_mutex;
	
	/* Neighbor flood tracking */
	struct time_window_entry *neighbor_window;
	pthread_mutex_t neighbor_mutex;
	
	/* Interface storm tracking */
	struct interface_tracker *interface_trackers;
	pthread_mutex_t interface_mutex;
	
	/* Route tracking */
	struct route_entry *routes;
	pthread_mutex_t route_mutex;
	
	/* Callbacks */
	struct callback_entry *callbacks;
	pthread_mutex_t callback_mutex;
	int next_callback_id;
	
	/* Statistics */
	atomic_ulong events_processed;
	atomic_ulong security_events;
	atomic_ulong promisc_events;
	atomic_ulong arp_flood_events;
	atomic_ulong route_hijack_events;
	atomic_ulong neighbor_flood_events;
	atomic_ulong interface_storm_events;
};

/* Get current time */
static time_t get_current_time(void)
{
	return time(NULL);
}

/* Emit security event to all callbacks */
static void emit_security_event(struct security_detector *sd,
                                struct security_event *event)
{
	struct callback_entry *cb;
	
	pthread_mutex_lock(&sd->callback_mutex);
	for (cb = sd->callbacks; cb; cb = cb->next) {
		if (cb->callback)
			cb->callback(event, cb->ctx);
	}
	pthread_mutex_unlock(&sd->callback_mutex);
	
	atomic_fetch_add_explicit(&sd->security_events, 1, memory_order_relaxed);
}

/* Clean old entries from time window */
static void clean_time_window(struct time_window_entry **head,
                              time_t cutoff_time)
{
	struct time_window_entry *entry, *prev, *next;
	
	prev = NULL;
	entry = *head;
	
	while (entry) {
		next = entry->next;
		
		if (entry->timestamp < cutoff_time) {
			if (prev)
				prev->next = next;
			else
				*head = next;
			free(entry);
		} else {
			prev = entry;
		}
		
		entry = next;
	}
}

/* Count entries in time window */
static size_t count_time_window(struct time_window_entry *head)
{
	size_t count = 0;
	struct time_window_entry *entry;
	
	for (entry = head; entry; entry = entry->next)
		count++;
	
	return count;
}

/* Add entry to time window */
static bool add_time_window_entry(struct time_window_entry **head,
                                  const char *interface)
{
	struct time_window_entry *entry;
	
	entry = malloc(sizeof(*entry));
	if (!entry)
		return false;
	
	entry->timestamp = get_current_time();
	strncpy(entry->interface, interface, sizeof(entry->interface) - 1);
	entry->interface[sizeof(entry->interface) - 1] = '\0';
	entry->next = *head;
	*head = entry;
	
	return true;
}

/* Detect promiscuous mode */
static bool detect_promiscuous_mode(struct security_detector *sd,
                                    struct nlmon_event *event)
{
	struct security_event sec_event;
	
	if (!sd->config.enable_promisc_detection)
		return false;
	
	/* Check if this is a link change event with IFF_PROMISC flag */
	if (event->message_type != RTM_NEWLINK)
		return false;
	
	/* We would need to parse the netlink message to check IFF_PROMISC flag
	 * For now, we'll use a simplified check based on event data */
	if (event->data && event->data_size >= sizeof(uint32_t)) {
		uint32_t flags = *(uint32_t *)event->data;
		
		if (flags & IFF_PROMISC) {
			memset(&sec_event, 0, sizeof(sec_event));
			sec_event.type = SECURITY_PROMISCUOUS_MODE;
			sec_event.severity = SECURITY_HIGH;
			sec_event.timestamp = get_current_time();
			strncpy(sec_event.interface, event->interface,
			        sizeof(sec_event.interface) - 1);
			snprintf(sec_event.description, sizeof(sec_event.description),
			         "Interface %s entered promiscuous mode",
			         event->interface);
			
			emit_security_event(sd, &sec_event);
			atomic_fetch_add_explicit(&sd->promisc_events, 1,
			                          memory_order_relaxed);
			return true;
		}
	}
	
	return false;
}

/* Detect ARP flood */
static bool detect_arp_flood(struct security_detector *sd,
                             struct nlmon_event *event)
{
	struct security_event sec_event;
	time_t now, cutoff;
	size_t count;
	double rate;
	
	if (!sd->config.enable_arp_flood_detection)
		return false;
	
	/* Check if this is a new neighbor (ARP) event */
	if (event->message_type != RTM_NEWNEIGH)
		return false;
	
	pthread_mutex_lock(&sd->arp_mutex);
	
	now = get_current_time();
	cutoff = now - sd->config.arp_time_window;
	
	/* Clean old entries */
	clean_time_window(&sd->arp_window, cutoff);
	
	/* Add new entry */
	add_time_window_entry(&sd->arp_window, event->interface);
	
	/* Count entries in window */
	count = count_time_window(sd->arp_window);
	
	pthread_mutex_unlock(&sd->arp_mutex);
	
	/* Calculate rate */
	rate = (double)count / sd->config.arp_time_window;
	
	/* Check threshold */
	if (rate > sd->config.arp_rate_threshold) {
		memset(&sec_event, 0, sizeof(sec_event));
		sec_event.type = SECURITY_ARP_FLOOD;
		sec_event.severity = SECURITY_MEDIUM;
		sec_event.timestamp = now;
		strncpy(sec_event.interface, event->interface,
		        sizeof(sec_event.interface) - 1);
		snprintf(sec_event.description, sizeof(sec_event.description),
		         "ARP flood detected on %s: %.1f entries/sec (threshold: %.1f)",
		         event->interface, rate, sd->config.arp_rate_threshold);
		
		emit_security_event(sd, &sec_event);
		atomic_fetch_add_explicit(&sd->arp_flood_events, 1,
		                          memory_order_relaxed);
		return true;
	}
	
	return false;
}

/* Detect neighbor flood (similar to ARP but for all neighbor events) */
static bool detect_neighbor_flood(struct security_detector *sd,
                                  struct nlmon_event *event)
{
	struct security_event sec_event;
	time_t now, cutoff;
	size_t count;
	double rate;
	
	if (!sd->config.enable_neighbor_flood_detection)
		return false;
	
	/* Check if this is a neighbor event */
	if (event->message_type != RTM_NEWNEIGH &&
	    event->message_type != RTM_DELNEIGH)
		return false;
	
	pthread_mutex_lock(&sd->neighbor_mutex);
	
	now = get_current_time();
	cutoff = now - sd->config.neighbor_time_window;
	
	/* Clean old entries */
	clean_time_window(&sd->neighbor_window, cutoff);
	
	/* Add new entry */
	add_time_window_entry(&sd->neighbor_window, event->interface);
	
	/* Count entries in window */
	count = count_time_window(sd->neighbor_window);
	
	pthread_mutex_unlock(&sd->neighbor_mutex);
	
	/* Calculate rate */
	rate = (double)count / sd->config.neighbor_time_window;
	
	/* Check threshold */
	if (rate > sd->config.neighbor_rate_threshold) {
		memset(&sec_event, 0, sizeof(sec_event));
		sec_event.type = SECURITY_NEIGHBOR_FLOOD;
		sec_event.severity = SECURITY_MEDIUM;
		sec_event.timestamp = now;
		strncpy(sec_event.interface, event->interface,
		        sizeof(sec_event.interface) - 1);
		snprintf(sec_event.description, sizeof(sec_event.description),
		         "Neighbor flood detected on %s: %.1f events/sec (threshold: %.1f)",
		         event->interface, rate, sd->config.neighbor_rate_threshold);
		
		emit_security_event(sd, &sec_event);
		atomic_fetch_add_explicit(&sd->neighbor_flood_events, 1,
		                          memory_order_relaxed);
		return true;
	}
	
	return false;
}

/* Detect route hijacking */
static bool detect_route_hijack(struct security_detector *sd,
                                struct nlmon_event *event)
{
	struct security_event sec_event;
	struct route_entry *route;
	bool hijack_detected = false;
	
	if (!sd->config.enable_route_hijack_detection)
		return false;
	
	/* Check if this is a route change event */
	if (event->message_type != RTM_NEWROUTE &&
	    event->message_type != RTM_DELROUTE)
		return false;
	
	/* For route hijack detection, we would need to parse the route data
	 * and check for suspicious changes (e.g., default route changes,
	 * gateway changes for existing routes). This is a simplified version. */
	
	pthread_mutex_lock(&sd->route_mutex);
	
	/* Check for suspicious route changes */
	if (event->message_type == RTM_NEWROUTE && event->data) {
		/* In a real implementation, we would parse the route attributes
		 * and check for suspicious patterns like:
		 * - Default route (0.0.0.0/0) changes
		 * - Gateway changes for existing routes
		 * - Routes to private networks via unexpected gateways
		 */
		
		/* For now, just track route additions */
		route = malloc(sizeof(*route));
		if (route) {
			memset(route, 0, sizeof(*route));
			route->timestamp = get_current_time();
			strncpy(route->interface, event->interface,
			        sizeof(route->interface) - 1);
			route->next = sd->routes;
			sd->routes = route;
		}
	}
	
	pthread_mutex_unlock(&sd->route_mutex);
	
	if (hijack_detected) {
		memset(&sec_event, 0, sizeof(sec_event));
		sec_event.type = SECURITY_ROUTE_HIJACK;
		sec_event.severity = SECURITY_CRITICAL;
		sec_event.timestamp = get_current_time();
		strncpy(sec_event.interface, event->interface,
		        sizeof(sec_event.interface) - 1);
		snprintf(sec_event.description, sizeof(sec_event.description),
		         "Suspicious route change detected on %s",
		         event->interface);
		
		emit_security_event(sd, &sec_event);
		atomic_fetch_add_explicit(&sd->route_hijack_events, 1,
		                          memory_order_relaxed);
		return true;
	}
	
	return false;
}

/* Detect interface storm (rapid interface state changes) */
static bool detect_interface_storm(struct security_detector *sd,
                                   struct nlmon_event *event)
{
	struct security_event sec_event;
	struct interface_tracker *tracker;
	time_t now;
	bool storm_detected = false;
	
	if (!sd->config.enable_interface_storm_detection)
		return false;
	
	/* Check if this is a link event */
	if (event->message_type != RTM_NEWLINK &&
	    event->message_type != RTM_DELLINK)
		return false;
	
	pthread_mutex_lock(&sd->interface_mutex);
	
	now = get_current_time();
	
	/* Find or create tracker for this interface */
	for (tracker = sd->interface_trackers; tracker; tracker = tracker->next) {
		if (strcmp(tracker->interface, event->interface) == 0)
			break;
	}
	
	if (!tracker) {
		tracker = malloc(sizeof(*tracker));
		if (tracker) {
			memset(tracker, 0, sizeof(*tracker));
			strncpy(tracker->interface, event->interface,
			        sizeof(tracker->interface) - 1);
			tracker->window_start = now;
			tracker->event_count = 0;
			tracker->next = sd->interface_trackers;
			sd->interface_trackers = tracker;
		}
	}
	
	if (tracker) {
		/* Reset window if expired */
		if (now - tracker->window_start >= (time_t)sd->config.interface_storm_window) {
			tracker->window_start = now;
			tracker->event_count = 0;
		}
		
		tracker->event_count++;
		
		/* Check threshold */
		if (tracker->event_count >= sd->config.interface_storm_threshold) {
			storm_detected = true;
			tracker->event_count = 0;  /* Reset to avoid repeated alerts */
			tracker->window_start = now;
		}
	}
	
	pthread_mutex_unlock(&sd->interface_mutex);
	
	if (storm_detected) {
		memset(&sec_event, 0, sizeof(sec_event));
		sec_event.type = SECURITY_INTERFACE_STORM;
		sec_event.severity = SECURITY_MEDIUM;
		sec_event.timestamp = now;
		strncpy(sec_event.interface, event->interface,
		        sizeof(sec_event.interface) - 1);
		snprintf(sec_event.description, sizeof(sec_event.description),
		         "Interface storm detected on %s: %zu events in %zu seconds",
		         event->interface, sd->config.interface_storm_threshold,
		         sd->config.interface_storm_window);
		
		emit_security_event(sd, &sec_event);
		atomic_fetch_add_explicit(&sd->interface_storm_events, 1,
		                          memory_order_relaxed);
		return true;
	}
	
	return false;
}

/* Detect suspicious interface (new interfaces with unusual names) */
static bool detect_suspicious_interface(struct security_detector *sd,
                                        struct nlmon_event *event)
{
	struct security_event sec_event;
	bool suspicious = false;
	
	if (!sd->config.enable_suspicious_interface_detection)
		return false;
	
	/* Check if this is a new link event */
	if (event->message_type != RTM_NEWLINK)
		return false;
	
	/* Check for suspicious interface name patterns
	 * This is a simplified check - in production, you'd want more
	 * sophisticated pattern matching */
	if (strstr(event->interface, "tmp") ||
	    strstr(event->interface, "test") ||
	    strstr(event->interface, "hack")) {
		suspicious = true;
	}
	
	if (suspicious) {
		memset(&sec_event, 0, sizeof(sec_event));
		sec_event.type = SECURITY_SUSPICIOUS_INTERFACE;
		sec_event.severity = SECURITY_LOW;
		sec_event.timestamp = get_current_time();
		strncpy(sec_event.interface, event->interface,
		        sizeof(sec_event.interface) - 1);
		snprintf(sec_event.description, sizeof(sec_event.description),
		         "Suspicious interface name detected: %s",
		         event->interface);
		
		emit_security_event(sd, &sec_event);
		return true;
	}
	
	return false;
}

struct security_detector *security_detector_create(struct security_detector_config *config)
{
	struct security_detector *sd;
	
	if (!config)
		return NULL;
	
	sd = calloc(1, sizeof(*sd));
	if (!sd)
		return NULL;
	
	sd->config = *config;
	
	/* Set defaults */
	if (sd->config.arp_rate_threshold == 0)
		sd->config.arp_rate_threshold = 100.0;
	if (sd->config.arp_time_window == 0)
		sd->config.arp_time_window = 10;
	if (sd->config.neighbor_rate_threshold == 0)
		sd->config.neighbor_rate_threshold = 100.0;
	if (sd->config.neighbor_time_window == 0)
		sd->config.neighbor_time_window = 10;
	if (sd->config.interface_storm_threshold == 0)
		sd->config.interface_storm_threshold = 10;
	if (sd->config.interface_storm_window == 0)
		sd->config.interface_storm_window = 5;
	
	/* Initialize mutexes */
	if (pthread_mutex_init(&sd->arp_mutex, NULL) != 0 ||
	    pthread_mutex_init(&sd->neighbor_mutex, NULL) != 0 ||
	    pthread_mutex_init(&sd->interface_mutex, NULL) != 0 ||
	    pthread_mutex_init(&sd->route_mutex, NULL) != 0 ||
	    pthread_mutex_init(&sd->callback_mutex, NULL) != 0) {
		pthread_mutex_destroy(&sd->arp_mutex);
		pthread_mutex_destroy(&sd->neighbor_mutex);
		pthread_mutex_destroy(&sd->interface_mutex);
		pthread_mutex_destroy(&sd->route_mutex);
		pthread_mutex_destroy(&sd->callback_mutex);
		free(sd);
		return NULL;
	}
	
	/* Initialize atomics */
	atomic_init(&sd->events_processed, 0);
	atomic_init(&sd->security_events, 0);
	atomic_init(&sd->promisc_events, 0);
	atomic_init(&sd->arp_flood_events, 0);
	atomic_init(&sd->route_hijack_events, 0);
	atomic_init(&sd->neighbor_flood_events, 0);
	atomic_init(&sd->interface_storm_events, 0);
	
	sd->next_callback_id = 1;
	
	return sd;
}

void security_detector_destroy(struct security_detector *sd)
{
	struct time_window_entry *tw_entry, *tw_next;
	struct interface_tracker *if_tracker, *if_next;
	struct route_entry *route, *route_next;
	struct callback_entry *cb, *cb_next;
	
	if (!sd)
		return;
	
	/* Free ARP window */
	tw_entry = sd->arp_window;
	while (tw_entry) {
		tw_next = tw_entry->next;
		free(tw_entry);
		tw_entry = tw_next;
	}
	
	/* Free neighbor window */
	tw_entry = sd->neighbor_window;
	while (tw_entry) {
		tw_next = tw_entry->next;
		free(tw_entry);
		tw_entry = tw_next;
	}
	
	/* Free interface trackers */
	if_tracker = sd->interface_trackers;
	while (if_tracker) {
		if_next = if_tracker->next;
		free(if_tracker);
		if_tracker = if_next;
	}
	
	/* Free routes */
	route = sd->routes;
	while (route) {
		route_next = route->next;
		free(route);
		route = route_next;
	}
	
	/* Free callbacks */
	cb = sd->callbacks;
	while (cb) {
		cb_next = cb->next;
		free(cb);
		cb = cb_next;
	}
	
	/* Destroy mutexes */
	pthread_mutex_destroy(&sd->arp_mutex);
	pthread_mutex_destroy(&sd->neighbor_mutex);
	pthread_mutex_destroy(&sd->interface_mutex);
	pthread_mutex_destroy(&sd->route_mutex);
	pthread_mutex_destroy(&sd->callback_mutex);
	
	free(sd);
}

int security_detector_register_callback(struct security_detector *sd,
                                        security_event_callback_t callback,
                                        void *ctx)
{
	struct callback_entry *entry;
	int id;
	
	if (!sd || !callback)
		return -1;
	
	entry = malloc(sizeof(*entry));
	if (!entry)
		return -1;
	
	pthread_mutex_lock(&sd->callback_mutex);
	
	id = sd->next_callback_id++;
	entry->id = id;
	entry->callback = callback;
	entry->ctx = ctx;
	entry->next = sd->callbacks;
	sd->callbacks = entry;
	
	pthread_mutex_unlock(&sd->callback_mutex);
	
	return id;
}

void security_detector_unregister_callback(struct security_detector *sd, int callback_id)
{
	struct callback_entry *entry, *prev;
	
	if (!sd)
		return;
	
	pthread_mutex_lock(&sd->callback_mutex);
	
	prev = NULL;
	for (entry = sd->callbacks; entry; entry = entry->next) {
		if (entry->id == callback_id) {
			if (prev)
				prev->next = entry->next;
			else
				sd->callbacks = entry->next;
			free(entry);
			break;
		}
		prev = entry;
	}
	
	pthread_mutex_unlock(&sd->callback_mutex);
}

bool security_detector_process_event(struct security_detector *sd,
                                     struct nlmon_event *event)
{
	bool detected = false;
	
	if (!sd || !event)
		return false;
	
	atomic_fetch_add_explicit(&sd->events_processed, 1, memory_order_relaxed);
	
	/* Run all enabled detectors */
	if (detect_promiscuous_mode(sd, event))
		detected = true;
	
	if (detect_arp_flood(sd, event))
		detected = true;
	
	if (detect_neighbor_flood(sd, event))
		detected = true;
	
	if (detect_route_hijack(sd, event))
		detected = true;
	
	if (detect_interface_storm(sd, event))
		detected = true;
	
	if (detect_suspicious_interface(sd, event))
		detected = true;
	
	return detected;
}

void security_detector_stats(struct security_detector *sd,
                            unsigned long *events_processed,
                            unsigned long *security_events,
                            unsigned long *promisc_events,
                            unsigned long *arp_flood_events,
                            unsigned long *route_hijack_events)
{
	if (!sd)
		return;
	
	if (events_processed)
		*events_processed = atomic_load_explicit(&sd->events_processed,
		                                         memory_order_relaxed);
	if (security_events)
		*security_events = atomic_load_explicit(&sd->security_events,
		                                        memory_order_relaxed);
	if (promisc_events)
		*promisc_events = atomic_load_explicit(&sd->promisc_events,
		                                       memory_order_relaxed);
	if (arp_flood_events)
		*arp_flood_events = atomic_load_explicit(&sd->arp_flood_events,
		                                         memory_order_relaxed);
	if (route_hijack_events)
		*route_hijack_events = atomic_load_explicit(&sd->route_hijack_events,
		                                            memory_order_relaxed);
}

void security_detector_reset(struct security_detector *sd)
{
	struct time_window_entry *tw_entry, *tw_next;
	struct interface_tracker *if_tracker, *if_next;
	
	if (!sd)
		return;
	
	/* Clear ARP window */
	pthread_mutex_lock(&sd->arp_mutex);
	tw_entry = sd->arp_window;
	while (tw_entry) {
		tw_next = tw_entry->next;
		free(tw_entry);
		tw_entry = tw_next;
	}
	sd->arp_window = NULL;
	pthread_mutex_unlock(&sd->arp_mutex);
	
	/* Clear neighbor window */
	pthread_mutex_lock(&sd->neighbor_mutex);
	tw_entry = sd->neighbor_window;
	while (tw_entry) {
		tw_next = tw_entry->next;
		free(tw_entry);
		tw_entry = tw_next;
	}
	sd->neighbor_window = NULL;
	pthread_mutex_unlock(&sd->neighbor_mutex);
	
	/* Clear interface trackers */
	pthread_mutex_lock(&sd->interface_mutex);
	if_tracker = sd->interface_trackers;
	while (if_tracker) {
		if_next = if_tracker->next;
		free(if_tracker);
		if_tracker = if_next;
	}
	sd->interface_trackers = NULL;
	pthread_mutex_unlock(&sd->interface_mutex);
	
	/* Reset statistics */
	atomic_store_explicit(&sd->events_processed, 0, memory_order_relaxed);
	atomic_store_explicit(&sd->security_events, 0, memory_order_relaxed);
	atomic_store_explicit(&sd->promisc_events, 0, memory_order_relaxed);
	atomic_store_explicit(&sd->arp_flood_events, 0, memory_order_relaxed);
	atomic_store_explicit(&sd->route_hijack_events, 0, memory_order_relaxed);
	atomic_store_explicit(&sd->neighbor_flood_events, 0, memory_order_relaxed);
	atomic_store_explicit(&sd->interface_storm_events, 0, memory_order_relaxed);
}

const char *security_event_severity_string(enum security_severity severity)
{
	switch (severity) {
	case SECURITY_INFO:
		return "INFO";
	case SECURITY_LOW:
		return "LOW";
	case SECURITY_MEDIUM:
		return "MEDIUM";
	case SECURITY_HIGH:
		return "HIGH";
	case SECURITY_CRITICAL:
		return "CRITICAL";
	default:
		return "UNKNOWN";
	}
}

const char *security_event_type_string(enum security_event_type type)
{
	switch (type) {
	case SECURITY_PROMISCUOUS_MODE:
		return "PROMISCUOUS_MODE";
	case SECURITY_ARP_FLOOD:
		return "ARP_FLOOD";
	case SECURITY_ROUTE_HIJACK:
		return "ROUTE_HIJACK";
	case SECURITY_SUSPICIOUS_INTERFACE:
		return "SUSPICIOUS_INTERFACE";
	case SECURITY_NEIGHBOR_FLOOD:
		return "NEIGHBOR_FLOOD";
	case SECURITY_INTERFACE_STORM:
		return "INTERFACE_STORM";
	default:
		return "UNKNOWN";
	}
}
