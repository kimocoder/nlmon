/* storage_buffer.c - Thread-safe circular memory buffer implementation
 *
 * Implements a circular buffer for storing recent network events with
 * thread-safe access using read-write locks for concurrent readers.
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fnmatch.h>
#include "storage_buffer.h"
#include "event_processor.h"

/* Storage buffer structure */
struct storage_buffer {
	struct nlmon_event *events;     /* Circular array of events */
	size_t capacity;                /* Maximum capacity */
	size_t head;                    /* Write position */
	size_t count;                   /* Current count */
	pthread_rwlock_t lock;          /* Read-write lock */
	
	/* Statistics */
	unsigned long total_added;
	unsigned long total_overflows;
	unsigned long peak_usage;
};

struct storage_buffer *storage_buffer_create(size_t capacity)
{
	struct storage_buffer *sb;
	
	if (capacity == 0)
		return NULL;
	
	sb = calloc(1, sizeof(*sb));
	if (!sb)
		return NULL;
	
	sb->events = calloc(capacity, sizeof(struct nlmon_event));
	if (!sb->events) {
		free(sb);
		return NULL;
	}
	
	sb->capacity = capacity;
	sb->head = 0;
	sb->count = 0;
	
	if (pthread_rwlock_init(&sb->lock, NULL) != 0) {
		free(sb->events);
		free(sb);
		return NULL;
	}
	
	return sb;
}

void storage_buffer_destroy(struct storage_buffer *sb)
{
	if (!sb)
		return;
	
	pthread_rwlock_destroy(&sb->lock);
	
	/* Free event data */
	for (size_t i = 0; i < sb->count; i++) {
		size_t idx = (sb->head + sb->capacity - sb->count + i) % sb->capacity;
		if (sb->events[idx].data)
			free(sb->events[idx].data);
	}
	
	free(sb->events);
	free(sb);
}

bool storage_buffer_add(struct storage_buffer *sb, struct nlmon_event *event)
{
	if (!sb || !event)
		return false;
	
	pthread_rwlock_wrlock(&sb->lock);
	
	/* If buffer is full, free the oldest event's data */
	if (sb->count == sb->capacity) {
		size_t oldest_idx = (sb->head + sb->capacity - sb->count) % sb->capacity;
		if (sb->events[oldest_idx].data) {
			free(sb->events[oldest_idx].data);
			sb->events[oldest_idx].data = NULL;
		}
		sb->total_overflows++;
	} else {
		sb->count++;
	}
	
	/* Copy event */
	sb->events[sb->head] = *event;
	
	/* Deep copy event data if present */
	if (event->data && event->data_size > 0) {
		sb->events[sb->head].data = malloc(event->data_size);
		if (sb->events[sb->head].data) {
			memcpy(sb->events[sb->head].data, event->data, event->data_size);
		}
	} else {
		sb->events[sb->head].data = NULL;
	}
	
	/* Advance head */
	sb->head = (sb->head + 1) % sb->capacity;
	
	/* Update statistics */
	sb->total_added++;
	if (sb->count > sb->peak_usage)
		sb->peak_usage = sb->count;
	
	pthread_rwlock_unlock(&sb->lock);
	
	return true;
}

bool storage_buffer_get(struct storage_buffer *sb, size_t index,
                        struct nlmon_event *event)
{
	bool found = false;
	
	if (!sb || !event)
		return false;
	
	pthread_rwlock_rdlock(&sb->lock);
	
	if (index < sb->count) {
		size_t actual_idx = (sb->head + sb->capacity - sb->count + index) % sb->capacity;
		*event = sb->events[actual_idx];
		
		/* Deep copy event data */
		if (sb->events[actual_idx].data && sb->events[actual_idx].data_size > 0) {
			event->data = malloc(sb->events[actual_idx].data_size);
			if (event->data) {
				memcpy(event->data, sb->events[actual_idx].data,
				       sb->events[actual_idx].data_size);
			}
		}
		
		found = true;
	}
	
	pthread_rwlock_unlock(&sb->lock);
	
	return found;
}

size_t storage_buffer_get_latest(struct storage_buffer *sb, size_t count,
                                 struct nlmon_event *events)
{
	size_t retrieved = 0;
	
	if (!sb || !events)
		return 0;
	
	pthread_rwlock_rdlock(&sb->lock);
	
	size_t to_retrieve = (count < sb->count) ? count : sb->count;
	
	for (size_t i = 0; i < to_retrieve; i++) {
		/* Get from newest to oldest */
		size_t idx = (sb->head + sb->capacity - 1 - i) % sb->capacity;
		events[i] = sb->events[idx];
		
		/* Deep copy event data */
		if (sb->events[idx].data && sb->events[idx].data_size > 0) {
			events[i].data = malloc(sb->events[idx].data_size);
			if (events[i].data) {
				memcpy(events[i].data, sb->events[idx].data,
				       sb->events[idx].data_size);
			}
		}
		
		retrieved++;
	}
	
	pthread_rwlock_unlock(&sb->lock);
	
	return retrieved;
}

/* Helper function to check if event matches filter */
static bool event_matches_filter(struct nlmon_event *event,
                                 struct buffer_query_filter *filter)
{
	if (!filter)
		return true;
	
	/* Check interface pattern */
	if (filter->interface_pattern) {
		if (fnmatch(filter->interface_pattern, event->interface, 0) != 0)
			return false;
	}
	
	/* Check event type */
	if (filter->event_type != 0 && event->event_type != filter->event_type)
		return false;
	
	/* Check message type */
	if (filter->message_type != 0 && event->message_type != filter->message_type)
		return false;
	
	/* Check time range */
	if (filter->start_time != 0 && event->timestamp < filter->start_time)
		return false;
	
	if (filter->end_time != 0 && event->timestamp > filter->end_time)
		return false;
	
	return true;
}

size_t storage_buffer_query(struct storage_buffer *sb,
                            struct buffer_query_filter *filter,
                            buffer_query_callback_t callback,
                            void *ctx)
{
	size_t matches = 0;
	
	if (!sb || !callback)
		return 0;
	
	pthread_rwlock_rdlock(&sb->lock);
	
	size_t max_results = filter && filter->max_results > 0 ? 
	                     filter->max_results : sb->count;
	
	/* Iterate from oldest to newest */
	for (size_t i = 0; i < sb->count && matches < max_results; i++) {
		size_t idx = (sb->head + sb->capacity - sb->count + i) % sb->capacity;
		
		if (event_matches_filter(&sb->events[idx], filter)) {
			callback(&sb->events[idx], ctx);
			matches++;
		}
	}
	
	pthread_rwlock_unlock(&sb->lock);
	
	return matches;
}

size_t storage_buffer_size(struct storage_buffer *sb)
{
	size_t size;
	
	if (!sb)
		return 0;
	
	pthread_rwlock_rdlock(&sb->lock);
	size = sb->count;
	pthread_rwlock_unlock(&sb->lock);
	
	return size;
}

size_t storage_buffer_capacity(struct storage_buffer *sb)
{
	if (!sb)
		return 0;
	
	return sb->capacity;
}

void storage_buffer_clear(struct storage_buffer *sb)
{
	if (!sb)
		return;
	
	pthread_rwlock_wrlock(&sb->lock);
	
	/* Free all event data */
	for (size_t i = 0; i < sb->count; i++) {
		size_t idx = (sb->head + sb->capacity - sb->count + i) % sb->capacity;
		if (sb->events[idx].data) {
			free(sb->events[idx].data);
			sb->events[idx].data = NULL;
		}
	}
	
	sb->head = 0;
	sb->count = 0;
	
	pthread_rwlock_unlock(&sb->lock);
}

void storage_buffer_stats(struct storage_buffer *sb,
                         unsigned long *total_added,
                         unsigned long *total_overflows,
                         unsigned long *peak_usage)
{
	if (!sb)
		return;
	
	pthread_rwlock_rdlock(&sb->lock);
	
	if (total_added)
		*total_added = sb->total_added;
	if (total_overflows)
		*total_overflows = sb->total_overflows;
	if (peak_usage)
		*peak_usage = sb->peak_usage;
	
	pthread_rwlock_unlock(&sb->lock);
}
