/* time_window.c - Sliding time window for event correlation
 *
 * Implements a circular buffer-based sliding time window that automatically
 * expires old events and provides efficient query operations.
 */

#include "correlation_engine.h"
#include "event_processor.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Time window entry */
struct window_entry {
	struct nlmon_event event;
	time_t timestamp;
	bool valid;
};

/* Time window structure */
struct time_window {
	struct window_entry *entries;
	size_t capacity;
	size_t head;
	size_t tail;
	size_t count;
	time_t window_sec;
	pthread_mutex_t lock;
};

struct time_window *time_window_create(time_t window_sec, size_t max_events)
{
	struct time_window *window;

	if (window_sec <= 0 || max_events == 0)
		return NULL;

	window = calloc(1, sizeof(*window));
	if (!window)
		return NULL;

	window->entries = calloc(max_events, sizeof(struct window_entry));
	if (!window->entries) {
		free(window);
		return NULL;
	}

	window->capacity = max_events;
	window->window_sec = window_sec;
	window->head = 0;
	window->tail = 0;
	window->count = 0;

	if (pthread_mutex_init(&window->lock, NULL) != 0) {
		free(window->entries);
		free(window);
		return NULL;
	}

	return window;
}

void time_window_destroy(struct time_window *window)
{
	if (!window)
		return;

	pthread_mutex_destroy(&window->lock);
	free(window->entries);
	free(window);
}

bool time_window_add(struct time_window *window, struct nlmon_event *event)
{
	struct window_entry *entry;

	if (!window || !event)
		return false;

	pthread_mutex_lock(&window->lock);

	/* Check if window is full */
	if (window->count >= window->capacity) {
		/* Remove oldest entry */
		window->entries[window->head].valid = false;
		window->head = (window->head + 1) % window->capacity;
		window->count--;
	}

	/* Add new entry at tail */
	entry = &window->entries[window->tail];
	memcpy(&entry->event, event, sizeof(struct nlmon_event));
	entry->timestamp = event->timestamp;
	entry->valid = true;

	window->tail = (window->tail + 1) % window->capacity;
	window->count++;

	pthread_mutex_unlock(&window->lock);
	return true;
}

size_t time_window_expire(struct time_window *window, time_t current_time)
{
	size_t expired = 0;
	time_t cutoff_time;

	if (!window)
		return 0;

	cutoff_time = current_time - window->window_sec;

	pthread_mutex_lock(&window->lock);

	/* Remove expired entries from head */
	while (window->count > 0) {
		struct window_entry *entry = &window->entries[window->head];

		if (!entry->valid || entry->timestamp >= cutoff_time)
			break;

		entry->valid = false;
		window->head = (window->head + 1) % window->capacity;
		window->count--;
		expired++;
	}

	pthread_mutex_unlock(&window->lock);
	return expired;
}

size_t time_window_query(struct time_window *window,
                         uint32_t event_type,
                         const char *interface,
                         struct nlmon_event **events,
                         size_t max_events)
{
	size_t found = 0;
	size_t i, idx;

	if (!window || !events || max_events == 0)
		return 0;

	pthread_mutex_lock(&window->lock);

	/* Iterate through valid entries */
	for (i = 0; i < window->count && found < max_events; i++) {
		idx = (window->head + i) % window->capacity;
		struct window_entry *entry = &window->entries[idx];

		if (!entry->valid)
			continue;

		/* Apply filters */
		if (event_type != 0 && entry->event.event_type != event_type)
			continue;

		if (interface && strcmp(entry->event.interface, interface) != 0)
			continue;

		/* Add to results */
		events[found++] = &entry->event;
	}

	pthread_mutex_unlock(&window->lock);
	return found;
}

size_t time_window_count(struct time_window *window)
{
	size_t count;

	if (!window)
		return 0;

	pthread_mutex_lock(&window->lock);
	count = window->count;
	pthread_mutex_unlock(&window->lock);

	return count;
}
