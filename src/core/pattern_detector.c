/* pattern_detector.c - Pattern detection for network events
 *
 * Detects repeating patterns in network events, analyzes frequency,
 * and generates alerts for significant patterns.
 */

#include "correlation_engine.h"
#include "event_processor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define MAX_PATTERN_TYPES 256

/* Pattern statistics */
struct pattern_stats {
	uint32_t event_type;
	char interface[16];
	size_t count;
	time_t first_seen;
	time_t last_seen;
	double events_per_second;
	bool alert_triggered;
};

/* Pattern detector structure */
struct pattern_detector {
	struct time_window *window;
	struct pattern_stats patterns[MAX_PATTERN_TYPES];
	size_t pattern_count;
	size_t min_frequency;
	time_t window_sec;
	pthread_mutex_t lock;
};

struct pattern_detector *pattern_detector_create(time_t window_sec,
                                                 size_t min_frequency)
{
	struct pattern_detector *detector;

	if (window_sec <= 0 || min_frequency == 0)
		return NULL;

	detector = calloc(1, sizeof(*detector));
	if (!detector)
		return NULL;

	detector->window = time_window_create(window_sec, 10000);
	if (!detector->window) {
		free(detector);
		return NULL;
	}

	detector->window_sec = window_sec;
	detector->min_frequency = min_frequency;
	detector->pattern_count = 0;

	if (pthread_mutex_init(&detector->lock, NULL) != 0) {
		time_window_destroy(detector->window);
		free(detector);
		return NULL;
	}

	return detector;
}

void pattern_detector_destroy(struct pattern_detector *detector)
{
	if (!detector)
		return;

	time_window_destroy(detector->window);
	pthread_mutex_destroy(&detector->lock);
	free(detector);
}

/* Find or create pattern stats for event type and interface */
static struct pattern_stats *find_pattern(struct pattern_detector *detector,
                                          uint32_t event_type,
                                          const char *interface)
{
	size_t i;

	/* Search for existing pattern */
	for (i = 0; i < detector->pattern_count; i++) {
		if (detector->patterns[i].event_type == event_type &&
		    strcmp(detector->patterns[i].interface, interface) == 0) {
			return &detector->patterns[i];
		}
	}

	/* Create new pattern if space available */
	if (detector->pattern_count < MAX_PATTERN_TYPES) {
		struct pattern_stats *pattern = &detector->patterns[detector->pattern_count];
		pattern->event_type = event_type;
		strncpy(pattern->interface, interface, sizeof(pattern->interface) - 1);
		pattern->count = 0;
		pattern->first_seen = 0;
		pattern->last_seen = 0;
		pattern->events_per_second = 0.0;
		pattern->alert_triggered = false;
		detector->pattern_count++;
		return pattern;
	}

	return NULL;
}

/* Update pattern statistics */
static void update_pattern_stats(struct pattern_stats *pattern,
                                 time_t timestamp,
                                 time_t window_sec)
{
	if (pattern->count == 0) {
		pattern->first_seen = timestamp;
	}

	pattern->last_seen = timestamp;
	pattern->count++;

	/* Calculate events per second */
	time_t duration = pattern->last_seen - pattern->first_seen;
	if (duration > 0) {
		pattern->events_per_second = (double)pattern->count / duration;
	} else {
		pattern->events_per_second = pattern->count;
	}
}

/* Check if pattern should trigger alert */
static bool should_alert(struct pattern_stats *pattern, size_t min_frequency)
{
	if (pattern->alert_triggered)
		return false;

	if (pattern->count >= min_frequency) {
		pattern->alert_triggered = true;
		return true;
	}

	return false;
}

size_t pattern_detector_process(struct pattern_detector *detector,
                               struct nlmon_event *event,
                               struct pattern_result *results,
                               size_t max_results)
{
	struct pattern_stats *pattern;
	size_t found = 0;
	time_t current_time;

	if (!detector || !event || !results || max_results == 0)
		return 0;

	current_time = event->timestamp;

	pthread_mutex_lock(&detector->lock);

	/* Add event to window */
	time_window_add(detector->window, event);
	time_window_expire(detector->window, current_time);

	/* Find or create pattern for this event */
	pattern = find_pattern(detector, event->event_type, event->interface);
	if (!pattern) {
		pthread_mutex_unlock(&detector->lock);
		return 0;
	}

	/* Update pattern statistics */
	update_pattern_stats(pattern, current_time, detector->window_sec);

	/* Check if pattern should trigger alert */
	if (should_alert(pattern, detector->min_frequency)) {
		if (found < max_results) {
			snprintf(results[found].pattern_name,
			        sizeof(results[found].pattern_name),
			        "pattern_%u_%s", pattern->event_type, pattern->interface);
			results[found].event_type = pattern->event_type;
			results[found].frequency = pattern->count;
			results[found].first_seen = pattern->first_seen;
			results[found].last_seen = pattern->last_seen;
			results[found].events_per_second = pattern->events_per_second;
			found++;
		}
	}

	/* Reset patterns that have expired from window */
	size_t i;
	for (i = 0; i < detector->pattern_count; i++) {
		struct pattern_stats *p = &detector->patterns[i];
		if (current_time - p->last_seen > detector->window_sec) {
			/* Reset pattern */
			p->count = 0;
			p->first_seen = 0;
			p->last_seen = 0;
			p->events_per_second = 0.0;
			p->alert_triggered = false;
		}
	}

	pthread_mutex_unlock(&detector->lock);
	return found;
}
