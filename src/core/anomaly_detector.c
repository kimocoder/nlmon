/* anomaly_detector.c - Statistical anomaly detection for network events
 *
 * Implements baseline statistics collection and anomaly detection using
 * standard deviation-based scoring to identify unusual network behavior.
 */

#include "correlation_engine.h"
#include "event_processor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>

#define MAX_EVENT_TYPES 256

/* Baseline statistics for an event type */
struct baseline_stats {
	uint32_t event_type;
	double *values;
	size_t value_count;
	size_t value_capacity;
	double mean;
	double stddev;
	double sum;
	double sum_squares;
	time_t last_update;
	bool initialized;
};

/* Anomaly detector structure */
struct anomaly_detector {
	struct baseline_stats baselines[MAX_EVENT_TYPES];
	size_t baseline_count;
	time_t baseline_window_sec;
	double threshold;
	struct time_window *window;
	pthread_mutex_t lock;
};

struct anomaly_detector *anomaly_detector_create(time_t baseline_window_sec,
                                                 double threshold)
{
	struct anomaly_detector *detector;

	if (baseline_window_sec <= 0 || threshold <= 0)
		return NULL;

	detector = calloc(1, sizeof(*detector));
	if (!detector)
		return NULL;

	detector->baseline_window_sec = baseline_window_sec;
	detector->threshold = threshold;
	detector->baseline_count = 0;

	detector->window = time_window_create(baseline_window_sec, 10000);
	if (!detector->window) {
		free(detector);
		return NULL;
	}

	if (pthread_mutex_init(&detector->lock, NULL) != 0) {
		time_window_destroy(detector->window);
		free(detector);
		return NULL;
	}

	return detector;
}

void anomaly_detector_destroy(struct anomaly_detector *detector)
{
	size_t i;

	if (!detector)
		return;

	/* Free baseline value arrays */
	for (i = 0; i < detector->baseline_count; i++) {
		free(detector->baselines[i].values);
	}

	time_window_destroy(detector->window);
	pthread_mutex_destroy(&detector->lock);
	free(detector);
}

/* Find or create baseline for event type */
static struct baseline_stats *find_baseline(struct anomaly_detector *detector,
                                            uint32_t event_type)
{
	size_t i;

	/* Search for existing baseline */
	for (i = 0; i < detector->baseline_count; i++) {
		if (detector->baselines[i].event_type == event_type) {
			return &detector->baselines[i];
		}
	}

	/* Create new baseline if space available */
	if (detector->baseline_count < MAX_EVENT_TYPES) {
		struct baseline_stats *baseline = &detector->baselines[detector->baseline_count];
		baseline->event_type = event_type;
		baseline->value_capacity = 1000;
		baseline->values = calloc(baseline->value_capacity, sizeof(double));
		if (!baseline->values)
			return NULL;
		baseline->value_count = 0;
		baseline->mean = 0.0;
		baseline->stddev = 0.0;
		baseline->sum = 0.0;
		baseline->sum_squares = 0.0;
		baseline->last_update = 0;
		baseline->initialized = false;
		detector->baseline_count++;
		return baseline;
	}

	return NULL;
}

/* Calculate mean and standard deviation */
static void calculate_statistics(struct baseline_stats *baseline)
{
	if (baseline->value_count == 0) {
		baseline->mean = 0.0;
		baseline->stddev = 0.0;
		return;
	}

	/* Calculate mean */
	baseline->mean = baseline->sum / baseline->value_count;

	/* Calculate standard deviation */
	double variance = (baseline->sum_squares / baseline->value_count) -
	                  (baseline->mean * baseline->mean);
	baseline->stddev = sqrt(fabs(variance));

	baseline->initialized = true;
}

void anomaly_detector_update_baseline(struct anomaly_detector *detector,
                                      uint32_t event_type,
                                      double value)
{
	struct baseline_stats *baseline;

	if (!detector)
		return;

	pthread_mutex_lock(&detector->lock);

	baseline = find_baseline(detector, event_type);
	if (!baseline) {
		pthread_mutex_unlock(&detector->lock);
		return;
	}

	/* Add value to baseline */
	if (baseline->value_count < baseline->value_capacity) {
		baseline->values[baseline->value_count] = value;
		baseline->value_count++;
		baseline->sum += value;
		baseline->sum_squares += value * value;
	} else {
		/* Remove oldest value and add new one */
		double old_value = baseline->values[0];
		memmove(baseline->values, baseline->values + 1,
		        (baseline->value_capacity - 1) * sizeof(double));
		baseline->values[baseline->value_capacity - 1] = value;
		baseline->sum = baseline->sum - old_value + value;
		baseline->sum_squares = baseline->sum_squares -
		                        (old_value * old_value) +
		                        (value * value);
	}

	/* Recalculate statistics */
	calculate_statistics(baseline);

	pthread_mutex_unlock(&detector->lock);
}

/* Calculate anomaly score (number of standard deviations from mean) */
static double calculate_anomaly_score(struct baseline_stats *baseline,
                                      double value)
{
	if (!baseline->initialized || baseline->stddev == 0.0)
		return 0.0;

	return fabs(value - baseline->mean) / baseline->stddev;
}

/* Count events of a specific type in time window */
static size_t count_events_in_window(struct time_window *window,
                                     uint32_t event_type,
                                     time_t window_sec)
{
	struct nlmon_event *events[10000];
	size_t count;

	count = time_window_query(window, event_type, NULL, events, 10000);
	return count;
}

size_t anomaly_detector_process(struct anomaly_detector *detector,
                               struct nlmon_event *event,
                               struct anomaly_result *results,
                               size_t max_results)
{
	struct baseline_stats *baseline;
	size_t found = 0;
	time_t current_time;
	double current_value;
	double score;

	if (!detector || !event || !results || max_results == 0)
		return 0;

	current_time = event->timestamp;

	pthread_mutex_lock(&detector->lock);

	/* Add event to window */
	time_window_add(detector->window, event);
	time_window_expire(detector->window, current_time);

	/* Find baseline for this event type */
	baseline = find_baseline(detector, event->event_type);
	if (!baseline) {
		pthread_mutex_unlock(&detector->lock);
		return 0;
	}

	/* Calculate current event rate */
	size_t event_count = count_events_in_window(detector->window,
	                                            event->event_type,
	                                            detector->baseline_window_sec);
	current_value = (double)event_count / detector->baseline_window_sec;

	/* Update baseline if enough time has passed */
	if (current_time - baseline->last_update >= 60) {
		anomaly_detector_update_baseline(detector, event->event_type, current_value);
		baseline->last_update = current_time;
	}

	/* Calculate anomaly score */
	score = calculate_anomaly_score(baseline, current_value);

	/* Check if anomaly detected */
	if (baseline->initialized && score >= detector->threshold) {
		if (found < max_results) {
			snprintf(results[found].anomaly_type,
			        sizeof(results[found].anomaly_type),
			        "rate_anomaly_%u", event->event_type);
			results[found].event_type = event->event_type;
			results[found].score = score;
			results[found].baseline_mean = baseline->mean;
			results[found].baseline_stddev = baseline->stddev;
			results[found].current_value = current_value;
			results[found].detected_at = current_time;
			found++;
		}
	}

	pthread_mutex_unlock(&detector->lock);
	return found;
}
