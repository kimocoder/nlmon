/* resource_tracker.c - System resource monitoring and metrics collection */

#include "resource_tracker.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>

#define DEFAULT_MAX_METRICS 1024
#define HISTOGRAM_BUCKETS 10

/* Predefined histogram bucket boundaries */
static const double histogram_boundaries[HISTOGRAM_BUCKETS] = {
	0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0, +INFINITY
};

struct resource_tracker {
	struct metric *metrics;
	size_t max_metrics;
	size_t num_metrics;
	pthread_mutex_t lock;
	
	/* Cached system metrics */
	struct resource_stats cached_stats;
	time_t last_system_update;
	
	/* CPU tracking */
	uint64_t last_cpu_user;
	uint64_t last_cpu_system;
	time_t last_cpu_time;
	
	/* Event processing tracking */
	uint64_t events_processed_total;
	uint64_t events_dropped_total;
	time_t tracking_start_time;
	
	/* Performance profiling */
	double total_processing_time;
	double max_processing_time;
	uint64_t processing_samples;
};

struct resource_tracker *resource_tracker_create(size_t max_metrics)
{
	struct resource_tracker *tracker;
	
	if (max_metrics == 0)
		max_metrics = DEFAULT_MAX_METRICS;
	
	tracker = calloc(1, sizeof(*tracker));
	if (!tracker)
		return NULL;
	
	tracker->metrics = calloc(max_metrics, sizeof(struct metric));
	if (!tracker->metrics) {
		free(tracker);
		return NULL;
	}
	
	tracker->max_metrics = max_metrics;
	tracker->num_metrics = 0;
	pthread_mutex_init(&tracker->lock, NULL);
	
	tracker->tracking_start_time = time(NULL);
	tracker->last_system_update = 0;
	
	return tracker;
}

void resource_tracker_destroy(struct resource_tracker *tracker)
{
	if (!tracker)
		return;
	
	pthread_mutex_destroy(&tracker->lock);
	free(tracker->metrics);
	free(tracker);
}

static struct metric *find_metric(struct resource_tracker *tracker,
                                  const char *name,
                                  const char *labels)
{
	size_t i;
	const char *label_str = labels ? labels : "";
	
	for (i = 0; i < tracker->max_metrics; i++) {
		if (tracker->metrics[i].in_use &&
		    strcmp(tracker->metrics[i].name, name) == 0 &&
		    strcmp(tracker->metrics[i].labels, label_str) == 0) {
			return &tracker->metrics[i];
		}
	}
	
	return NULL;
}

static struct metric *create_metric(struct resource_tracker *tracker,
                                    const char *name,
                                    const char *labels,
                                    enum metric_type type)
{
	size_t i;
	struct metric *m;
	const char *label_str = labels ? labels : "";
	
	/* Find free slot */
	for (i = 0; i < tracker->max_metrics; i++) {
		if (!tracker->metrics[i].in_use) {
			m = &tracker->metrics[i];
			
			strncpy(m->name, name, sizeof(m->name) - 1);
			m->name[sizeof(m->name) - 1] = '\0';
			
			strncpy(m->labels, label_str, sizeof(m->labels) - 1);
			m->labels[sizeof(m->labels) - 1] = '\0';
			
			m->type = type;
			memset(&m->value, 0, sizeof(m->value));
			
			if (type == METRIC_HISTOGRAM) {
				m->value.histogram.min = INFINITY;
				m->value.histogram.max = -INFINITY;
			}
			
			m->last_updated = time(NULL);
			m->in_use = true;
			
			tracker->num_metrics++;
			return m;
		}
	}
	
	return NULL;
}

bool resource_tracker_counter_inc(struct resource_tracker *tracker,
                                  const char *name,
                                  const char *labels,
                                  uint64_t value)
{
	struct metric *m;
	bool success = false;
	
	if (!tracker || !name)
		return false;
	
	pthread_mutex_lock(&tracker->lock);
	
	m = find_metric(tracker, name, labels);
	if (!m)
		m = create_metric(tracker, name, labels, METRIC_COUNTER);
	
	if (m && m->type == METRIC_COUNTER) {
		m->value.counter += value;
		m->last_updated = time(NULL);
		success = true;
	}
	
	pthread_mutex_unlock(&tracker->lock);
	
	return success;
}

bool resource_tracker_gauge_set(struct resource_tracker *tracker,
                                const char *name,
                                const char *labels,
                                double value)
{
	struct metric *m;
	bool success = false;
	
	if (!tracker || !name)
		return false;
	
	pthread_mutex_lock(&tracker->lock);
	
	m = find_metric(tracker, name, labels);
	if (!m)
		m = create_metric(tracker, name, labels, METRIC_GAUGE);
	
	if (m && m->type == METRIC_GAUGE) {
		m->value.gauge = value;
		m->last_updated = time(NULL);
		success = true;
	}
	
	pthread_mutex_unlock(&tracker->lock);
	
	return success;
}

bool resource_tracker_histogram_observe(struct resource_tracker *tracker,
                                        const char *name,
                                        const char *labels,
                                        double value)
{
	struct metric *m;
	bool success = false;
	int i;
	
	if (!tracker || !name)
		return false;
	
	pthread_mutex_lock(&tracker->lock);
	
	m = find_metric(tracker, name, labels);
	if (!m)
		m = create_metric(tracker, name, labels, METRIC_HISTOGRAM);
	
	if (m && m->type == METRIC_HISTOGRAM) {
		m->value.histogram.sum += value;
		m->value.histogram.count++;
		
		if (value < m->value.histogram.min)
			m->value.histogram.min = value;
		if (value > m->value.histogram.max)
			m->value.histogram.max = value;
		
		/* Update histogram buckets */
		for (i = 0; i < HISTOGRAM_BUCKETS; i++) {
			if (value <= histogram_boundaries[i]) {
				m->value.histogram.buckets[i]++;
			}
		}
		
		m->last_updated = time(NULL);
		success = true;
	}
	
	pthread_mutex_unlock(&tracker->lock);
	
	return success;
}

bool resource_tracker_get_metric(struct resource_tracker *tracker,
                                 const char *name,
                                 const char *labels,
                                 enum metric_type *type,
                                 union metric_value *value)
{
	struct metric *m;
	bool found = false;
	
	if (!tracker || !name)
		return false;
	
	pthread_mutex_lock(&tracker->lock);
	
	m = find_metric(tracker, name, labels);
	if (m) {
		if (type)
			*type = m->type;
		if (value)
			*value = m->value;
		found = true;
	}
	
	pthread_mutex_unlock(&tracker->lock);
	
	return found;
}

void resource_tracker_update_system_metrics(struct resource_tracker *tracker)
{
	FILE *fp;
	char line[512];
	uint64_t utime, stime;
	time_t now;
	
	if (!tracker)
		return;
	
	now = time(NULL);
	
	/* Don't update too frequently (max once per second) */
	if (now == tracker->last_system_update)
		return;
	
	pthread_mutex_lock(&tracker->lock);
	
	/* Read CPU usage from /proc/self/stat */
	fp = fopen("/proc/self/stat", "r");
	if (fp) {
		if (fgets(line, sizeof(line), fp)) {
			/* Parse: pid comm state ... utime stime ... */
			if (sscanf(line, "%*d %*s %*c %*d %*d %*d %*d %*d %*u "
			          "%*u %*u %*u %*u %lu %lu",
			          &utime, &stime) == 2) {
				
				if (tracker->last_cpu_time > 0) {
					double cpu_delta = (utime + stime) - 
					                  (tracker->last_cpu_user + tracker->last_cpu_system);
					double time_delta = now - tracker->last_cpu_time;
					
					if (time_delta > 0) {
						double cpu_percent = (cpu_delta / sysconf(_SC_CLK_TCK)) / 
						                    time_delta * 100.0;
						tracker->cached_stats.cpu_usage_percent = cpu_percent;
						
						/* Update gauge metric */
						resource_tracker_gauge_set(tracker, "nlmon_cpu_usage_percent",
						                          NULL, cpu_percent);
					}
				}
				
				tracker->last_cpu_user = utime;
				tracker->last_cpu_system = stime;
				tracker->last_cpu_time = now;
				tracker->cached_stats.cpu_time_user = utime;
				tracker->cached_stats.cpu_time_system = stime;
			}
		}
		fclose(fp);
	}
	
	/* Read memory usage from /proc/self/status */
	fp = fopen("/proc/self/status", "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			unsigned long value;
			
			if (sscanf(line, "VmRSS: %lu kB", &value) == 1) {
				tracker->cached_stats.memory_rss_bytes = value * 1024;
				resource_tracker_gauge_set(tracker, "nlmon_memory_rss_bytes",
				                          NULL, value * 1024);
			} else if (sscanf(line, "VmSize: %lu kB", &value) == 1) {
				tracker->cached_stats.memory_vms_bytes = value * 1024;
				resource_tracker_gauge_set(tracker, "nlmon_memory_vms_bytes",
				                          NULL, value * 1024);
			} else if (sscanf(line, "VmPeak: %lu kB", &value) == 1) {
				tracker->cached_stats.memory_peak_rss_bytes = value * 1024;
				resource_tracker_gauge_set(tracker, "nlmon_memory_peak_rss_bytes",
				                          NULL, value * 1024);
			}
		}
		fclose(fp);
	}
	
	tracker->last_system_update = now;
	tracker->cached_stats.timestamp = now;
	
	pthread_mutex_unlock(&tracker->lock);
}

bool resource_tracker_get_stats(struct resource_tracker *tracker,
                                struct resource_stats *stats)
{
	if (!tracker || !stats)
		return false;
	
	/* Update system metrics first */
	resource_tracker_update_system_metrics(tracker);
	
	pthread_mutex_lock(&tracker->lock);
	
	*stats = tracker->cached_stats;
	
	/* Calculate event rate */
	time_t uptime = time(NULL) - tracker->tracking_start_time;
	if (uptime > 0) {
		stats->event_rate = (double)tracker->events_processed_total / uptime;
	}
	
	stats->events_processed = tracker->events_processed_total;
	stats->events_dropped = tracker->events_dropped_total;
	
	/* Calculate average processing time */
	if (tracker->processing_samples > 0) {
		stats->avg_processing_time_ms = 
			tracker->total_processing_time / tracker->processing_samples;
	}
	stats->max_processing_time_ms = tracker->max_processing_time;
	
	pthread_mutex_unlock(&tracker->lock);
	
	return true;
}

bool resource_tracker_reset_metric(struct resource_tracker *tracker,
                                   const char *name,
                                   const char *labels)
{
	struct metric *m;
	bool found = false;
	
	if (!tracker || !name)
		return false;
	
	pthread_mutex_lock(&tracker->lock);
	
	m = find_metric(tracker, name, labels);
	if (m) {
		memset(&m->value, 0, sizeof(m->value));
		if (m->type == METRIC_HISTOGRAM) {
			m->value.histogram.min = INFINITY;
			m->value.histogram.max = -INFINITY;
		}
		m->last_updated = time(NULL);
		found = true;
	}
	
	pthread_mutex_unlock(&tracker->lock);
	
	return found;
}

void resource_tracker_list_metrics(struct resource_tracker *tracker,
                                   void (*callback)(const struct metric *, void *),
                                   void *user_data)
{
	size_t i;
	
	if (!tracker || !callback)
		return;
	
	pthread_mutex_lock(&tracker->lock);
	
	for (i = 0; i < tracker->max_metrics; i++) {
		if (tracker->metrics[i].in_use) {
			callback(&tracker->metrics[i], user_data);
		}
	}
	
	pthread_mutex_unlock(&tracker->lock);
}

ssize_t resource_tracker_export_prometheus(struct resource_tracker *tracker,
                                           char *buffer,
                                           size_t buffer_size)
{
	size_t offset = 0;
	size_t i, j;
	
	if (!tracker || !buffer || buffer_size == 0)
		return -1;
	
	pthread_mutex_lock(&tracker->lock);
	
	for (i = 0; i < tracker->max_metrics && offset < buffer_size - 512; i++) {
		struct metric *m = &tracker->metrics[i];
		
		if (!m->in_use)
			continue;
		
		switch (m->type) {
		case METRIC_COUNTER:
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# HELP %s Counter metric\n", m->name);
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# TYPE %s counter\n", m->name);
			if (m->labels[0]) {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s{%s} %lu\n", m->name, m->labels,
				                  m->value.counter);
			} else {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s %lu\n", m->name, m->value.counter);
			}
			break;
			
		case METRIC_GAUGE:
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# HELP %s Gauge metric\n", m->name);
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# TYPE %s gauge\n", m->name);
			if (m->labels[0]) {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s{%s} %.6f\n", m->name, m->labels,
				                  m->value.gauge);
			} else {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s %.6f\n", m->name, m->value.gauge);
			}
			break;
			
		case METRIC_HISTOGRAM:
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# HELP %s Histogram metric\n", m->name);
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# TYPE %s histogram\n", m->name);
			
			/* Buckets */
			for (j = 0; j < HISTOGRAM_BUCKETS; j++) {
				char le_str[32];
				if (j == HISTOGRAM_BUCKETS - 1) {
					snprintf(le_str, sizeof(le_str), "+Inf");
				} else {
					snprintf(le_str, sizeof(le_str), "%.3f",
					        histogram_boundaries[j]);
				}
				
				if (m->labels[0]) {
					offset += snprintf(buffer + offset, buffer_size - offset,
					                  "%s_bucket{%s,le=\"%s\"} %lu\n",
					                  m->name, m->labels, le_str,
					                  m->value.histogram.buckets[j]);
				} else {
					offset += snprintf(buffer + offset, buffer_size - offset,
					                  "%s_bucket{le=\"%s\"} %lu\n",
					                  m->name, le_str,
					                  m->value.histogram.buckets[j]);
				}
			}
			
			/* Sum and count */
			if (m->labels[0]) {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_sum{%s} %.6f\n", m->name, m->labels,
				                  m->value.histogram.sum);
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_count{%s} %lu\n", m->name, m->labels,
				                  m->value.histogram.count);
			} else {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_sum %.6f\n", m->name,
				                  m->value.histogram.sum);
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_count %lu\n", m->name,
				                  m->value.histogram.count);
			}
			break;
		}
	}
	
	pthread_mutex_unlock(&tracker->lock);
	
	return offset;
}
