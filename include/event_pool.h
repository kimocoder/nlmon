/* event_pool.h - Specialized object pool for network events
 *
 * Provides efficient pooling for nlmon_event structures.
 */

#ifndef EVENT_POOL_H
#define EVENT_POOL_H

#include <stddef.h>
#include <stdint.h>
#include "object_pool.h"

/* Event structure for pooling */
struct nlmon_event_pooled {
	uint64_t timestamp;
	uint64_t sequence;
	uint32_t event_type;
	uint16_t message_type;
	char interface[16];
	char namespace[256];
	void *data;           /* Event-specific data */
	size_t data_size;
	void *user_data;      /* User context */
};

/* Event pool wrapper */
struct event_pool {
	struct object_pool *pool;
};

/**
 * event_pool_create() - Create event pool
 * @capacity: Maximum number of events in pool
 *
 * Returns: Pointer to event pool or NULL on error
 */
struct event_pool *event_pool_create(size_t capacity);

/**
 * event_pool_destroy() - Destroy event pool
 * @pool: Event pool
 */
void event_pool_destroy(struct event_pool *pool);

/**
 * event_pool_alloc() - Allocate event from pool
 * @pool: Event pool
 *
 * Returns: Pointer to allocated event or NULL on error
 */
struct nlmon_event_pooled *event_pool_alloc(struct event_pool *pool);

/**
 * event_pool_free() - Return event to pool
 * @pool: Event pool
 * @event: Event to free
 *
 * Note: Frees event->data if present
 */
void event_pool_free(struct event_pool *pool, struct nlmon_event_pooled *event);

/**
 * event_pool_get_stats() - Get pool statistics
 * @pool: Event pool
 * @stats: Output statistics structure
 */
void event_pool_get_stats(struct event_pool *pool, struct object_pool_stats *stats);

/**
 * event_pool_get_usage() - Get current pool usage
 * @pool: Event pool
 *
 * Returns: Number of currently allocated events
 */
size_t event_pool_get_usage(struct event_pool *pool);

#endif /* EVENT_POOL_H */
