/* event_processor.h - Enhanced event processing engine
 *
 * Integrates lock-free ring buffer, worker thread pool, rate limiting,
 * and object pooling for high-performance event processing.
 */

#ifndef EVENT_PROCESSOR_H
#define EVENT_PROCESSOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct ring_buffer;
struct thread_pool;
struct rate_limiter_map;

/* Forward declarations for netlink data structures */
struct nlmon_link_info;
struct nlmon_addr_info;
struct nlmon_route_info;
struct nlmon_neigh_info;
struct nlmon_diag_info;
struct nlmon_ct_info;
struct nlmon_nl80211_info;
struct nlmon_qca_vendor_info;

/* Event structure for processing */
struct nlmon_event {
	uint64_t timestamp;
	uint64_t sequence;
	uint32_t event_type;
	uint16_t message_type;
	char interface[16];
	void *data;           /* Event-specific data */
	size_t data_size;
	void *user_data;      /* User context */
	
	/* Netlink-specific fields */
	struct {
		int protocol;                /* NETLINK_ROUTE, NETLINK_GENERIC, etc. */
		uint16_t msg_type;           /* RTM_NEWLINK, etc. */
		uint16_t msg_flags;          /* NLM_F_* flags */
		uint32_t seq;                /* Sequence number */
		uint32_t pid;                /* Port ID */
		
		/* Generic netlink specific */
		uint8_t genl_cmd;
		uint8_t genl_version;
		uint16_t genl_family_id;
		char genl_family_name[32];
		
		/* Parsed attributes (protocol-specific) */
		union {
			struct nlmon_link_info *link;
			struct nlmon_addr_info *addr;
			struct nlmon_route_info *route;
			struct nlmon_neigh_info *neigh;
			struct nlmon_diag_info *diag;
			struct nlmon_ct_info *conntrack;
			struct nlmon_nl80211_info *nl80211;
			struct nlmon_qca_vendor_info *qca_vendor;
			void *generic;
		} data;
	} netlink;
	
	/* Raw message (optional, for debugging) */
	struct nlmsghdr *raw_msg;
	size_t raw_msg_len;
};

/* Event handler callback */
typedef void (*event_handler_t)(struct nlmon_event *event, void *ctx);

/* Event processor configuration */
struct event_processor_config {
	size_t ring_buffer_size;      /* Ring buffer capacity */
	size_t thread_pool_size;      /* Number of worker threads (0=auto) */
	size_t work_queue_size;       /* Work queue size (0=unlimited) */
	double rate_limit;            /* Global rate limit (events/sec, 0=none) */
	size_t rate_burst;            /* Rate limit burst size */
	bool enable_object_pool;      /* Enable event object pooling */
	size_t object_pool_size;      /* Object pool size */
};

/* Event processor structure (opaque) */
struct event_processor;

/**
 * event_processor_create() - Create event processor
 * @config: Configuration parameters
 *
 * Returns: Pointer to event processor or NULL on error
 */
struct event_processor *event_processor_create(struct event_processor_config *config);

/**
 * event_processor_destroy() - Destroy event processor
 * @ep: Event processor
 * @wait: If true, wait for pending events to complete
 */
void event_processor_destroy(struct event_processor *ep, bool wait);

/**
 * event_processor_register_handler() - Register event handler
 * @ep: Event processor
 * @handler: Handler function
 * @ctx: Context to pass to handler
 *
 * Returns: Handler ID or -1 on error
 */
int event_processor_register_handler(struct event_processor *ep,
                                     event_handler_t handler, void *ctx);

/**
 * event_processor_unregister_handler() - Unregister event handler
 * @ep: Event processor
 * @handler_id: Handler ID returned by register
 */
void event_processor_unregister_handler(struct event_processor *ep, int handler_id);

/**
 * event_processor_submit() - Submit event for processing
 * @ep: Event processor
 * @event: Event to process (will be copied if object pool enabled)
 *
 * Returns: true on success, false if queue full or rate limited
 */
bool event_processor_submit(struct event_processor *ep, struct nlmon_event *event);

/**
 * event_processor_set_rate_limit() - Set rate limit for event type
 * @ep: Event processor
 * @event_type: Event type (0 for global)
 * @rate: Rate limit (events/sec)
 * @burst: Burst size
 *
 * Returns: true on success
 */
bool event_processor_set_rate_limit(struct event_processor *ep,
                                    uint32_t event_type,
                                    double rate, size_t burst);

/**
 * event_processor_wait() - Wait for all pending events to complete
 * @ep: Event processor
 */
void event_processor_wait(struct event_processor *ep);

/**
 * event_processor_stats() - Get processor statistics
 * @ep: Event processor
 * @submitted: Output for total submitted events
 * @processed: Output for total processed events
 * @dropped: Output for dropped events (queue full)
 * @rate_limited: Output for rate limited events
 * @queue_size: Output for current queue size
 * @pool_usage: Output for object pool usage
 */
void event_processor_stats(struct event_processor *ep,
                          unsigned long *submitted,
                          unsigned long *processed,
                          unsigned long *dropped,
                          unsigned long *rate_limited,
                          size_t *queue_size,
                          size_t *pool_usage);

#endif /* EVENT_PROCESSOR_H */
