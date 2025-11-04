/* storage_buffer.h - Thread-safe circular memory buffer for recent events
 *
 * Provides a circular buffer for storing recent network events with
 * thread-safe access, overflow handling, and query API for CLI display.
 */

#ifndef STORAGE_BUFFER_H
#define STORAGE_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* Forward declaration */
struct nlmon_event;

/* Storage buffer structure (opaque) */
struct storage_buffer;

/* Query filter for buffer queries */
struct buffer_query_filter {
	const char *interface_pattern;  /* Interface name pattern (NULL=any) */
	uint32_t event_type;            /* Event type (0=any) */
	uint16_t message_type;          /* Message type (0=any) */
	uint64_t start_time;            /* Start timestamp (0=any) */
	uint64_t end_time;              /* End timestamp (0=any) */
	size_t max_results;             /* Maximum results (0=unlimited) */
};

/* Query result callback */
typedef void (*buffer_query_callback_t)(struct nlmon_event *event, void *ctx);

/**
 * storage_buffer_create() - Create storage buffer
 * @capacity: Maximum number of events to store
 *
 * Returns: Pointer to storage buffer or NULL on error
 */
struct storage_buffer *storage_buffer_create(size_t capacity);

/**
 * storage_buffer_destroy() - Destroy storage buffer
 * @sb: Storage buffer
 */
void storage_buffer_destroy(struct storage_buffer *sb);

/**
 * storage_buffer_add() - Add event to buffer
 * @sb: Storage buffer
 * @event: Event to add (will be copied)
 *
 * Returns: true on success, false on error
 * Note: Oldest event is automatically removed if buffer is full
 */
bool storage_buffer_add(struct storage_buffer *sb, struct nlmon_event *event);

/**
 * storage_buffer_get() - Get event by index
 * @sb: Storage buffer
 * @index: Event index (0 = oldest, size-1 = newest)
 * @event: Output buffer for event
 *
 * Returns: true if event found, false otherwise
 */
bool storage_buffer_get(struct storage_buffer *sb, size_t index, 
                        struct nlmon_event *event);

/**
 * storage_buffer_get_latest() - Get most recent events
 * @sb: Storage buffer
 * @count: Number of events to retrieve
 * @events: Output array for events (must be allocated by caller)
 *
 * Returns: Number of events retrieved
 */
size_t storage_buffer_get_latest(struct storage_buffer *sb, size_t count,
                                 struct nlmon_event *events);

/**
 * storage_buffer_query() - Query buffer with filter
 * @sb: Storage buffer
 * @filter: Query filter (NULL for all events)
 * @callback: Callback for each matching event
 * @ctx: Context to pass to callback
 *
 * Returns: Number of matching events
 */
size_t storage_buffer_query(struct storage_buffer *sb,
                            struct buffer_query_filter *filter,
                            buffer_query_callback_t callback,
                            void *ctx);

/**
 * storage_buffer_size() - Get current number of events in buffer
 * @sb: Storage buffer
 *
 * Returns: Number of events
 */
size_t storage_buffer_size(struct storage_buffer *sb);

/**
 * storage_buffer_capacity() - Get buffer capacity
 * @sb: Storage buffer
 *
 * Returns: Buffer capacity
 */
size_t storage_buffer_capacity(struct storage_buffer *sb);

/**
 * storage_buffer_clear() - Clear all events from buffer
 * @sb: Storage buffer
 */
void storage_buffer_clear(struct storage_buffer *sb);

/**
 * storage_buffer_stats() - Get buffer statistics
 * @sb: Storage buffer
 * @total_added: Output for total events added
 * @total_overflows: Output for overflow count
 * @peak_usage: Output for peak usage
 */
void storage_buffer_stats(struct storage_buffer *sb,
                         unsigned long *total_added,
                         unsigned long *total_overflows,
                         unsigned long *peak_usage);

#endif /* STORAGE_BUFFER_H */
