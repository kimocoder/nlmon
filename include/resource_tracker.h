/* resource_tracker.h - System resource monitoring and metrics collection
 *
 * Provides comprehensive metrics collection including counters, gauges,
 * histograms, and system resource tracking (CPU, memory).
 */

#ifndef RESOURCE_TRACKER_H
#define RESOURCE_TRACKER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>

/* Metric types */
enum metric_type {
	METRIC_COUNTER,    /* Monotonically increasing counter */
	METRIC_GAUGE,      /* Value that can go up or down */
	METRIC_HISTOGRAM   /* Distribution of values */
};

/* Metric value union */
union metric_value {
	uint64_t counter;
	double gauge;
	struct {
		double sum;
		uint64_t count;
		double min;
		double max;
		uint64_t buckets[10];  /* Histogram buckets */
	} histogram;
};

/* Metric structure */
struct metric {
	char name[128];
	char labels[256];
	enum metric_type type;
	union metric_value value;
	time_t last_updated;
	bool in_use;
};

/* Resource usage statistics */
struct resource_stats {
	/* CPU metrics */
	double cpu_usage_percent;
	uint64_t cpu_time_user;
	uint64_t cpu_time_system;
	
	/* Memory metrics */
	uint64_t memory_rss_bytes;
	uint64_t memory_vms_bytes;
	uint64_t memory_peak_rss_bytes;
	
	/* Event processing metrics */
	uint64_t events_processed;
	uint64_t events_dropped;
	double event_rate;  /* Events per second */
	
	/* Performance metrics */
	double avg_processing_time_ms;
	double max_processing_time_ms;
	uint64_t queue_depth;
	
	/* Timestamp */
	time_t timestamp;
};

/* Resource tracker handle (opaque) */
struct resource_tracker;

/**
 * resource_tracker_create() - Create resource tracker
 * @max_metrics: Maximum number of metrics to track (0 for default)
 *
 * Returns: Resource tracker handle or NULL on error
 */
struct resource_tracker *resource_tracker_create(size_t max_metrics);

/**
 * resource_tracker_destroy() - Destroy resource tracker
 * @tracker: Resource tracker handle
 */
void resource_tracker_destroy(struct resource_tracker *tracker);

/**
 * resource_tracker_counter_inc() - Increment counter metric
 * @tracker: Resource tracker handle
 * @name: Metric name
 * @labels: Optional labels (e.g., "type=link")
 * @value: Value to add (typically 1)
 *
 * Returns: true on success
 */
bool resource_tracker_counter_inc(struct resource_tracker *tracker,
                                  const char *name,
                                  const char *labels,
                                  uint64_t value);

/**
 * resource_tracker_gauge_set() - Set gauge metric value
 * @tracker: Resource tracker handle
 * @name: Metric name
 * @labels: Optional labels
 * @value: Value to set
 *
 * Returns: true on success
 */
bool resource_tracker_gauge_set(struct resource_tracker *tracker,
                                const char *name,
                                const char *labels,
                                double value);

/**
 * resource_tracker_histogram_observe() - Add observation to histogram
 * @tracker: Resource tracker handle
 * @name: Metric name
 * @labels: Optional labels
 * @value: Observed value
 *
 * Returns: true on success
 */
bool resource_tracker_histogram_observe(struct resource_tracker *tracker,
                                        const char *name,
                                        const char *labels,
                                        double value);

/**
 * resource_tracker_get_metric() - Get metric value
 * @tracker: Resource tracker handle
 * @name: Metric name
 * @labels: Optional labels
 * @type: Output for metric type
 * @value: Output for metric value
 *
 * Returns: true if metric found
 */
bool resource_tracker_get_metric(struct resource_tracker *tracker,
                                 const char *name,
                                 const char *labels,
                                 enum metric_type *type,
                                 union metric_value *value);

/**
 * resource_tracker_update_system_metrics() - Update system resource metrics
 * @tracker: Resource tracker handle
 *
 * Reads CPU and memory usage from /proc and updates metrics
 */
void resource_tracker_update_system_metrics(struct resource_tracker *tracker);

/**
 * resource_tracker_get_stats() - Get comprehensive resource statistics
 * @tracker: Resource tracker handle
 * @stats: Output for statistics
 *
 * Returns: true on success
 */
bool resource_tracker_get_stats(struct resource_tracker *tracker,
                                struct resource_stats *stats);

/**
 * resource_tracker_reset_metric() - Reset a metric to zero
 * @tracker: Resource tracker handle
 * @name: Metric name
 * @labels: Optional labels
 *
 * Returns: true if metric was reset
 */
bool resource_tracker_reset_metric(struct resource_tracker *tracker,
                                   const char *name,
                                   const char *labels);

/**
 * resource_tracker_list_metrics() - List all metrics
 * @tracker: Resource tracker handle
 * @callback: Callback function for each metric
 * @user_data: User data passed to callback
 *
 * Callback signature: void callback(const struct metric *m, void *user_data)
 */
void resource_tracker_list_metrics(struct resource_tracker *tracker,
                                   void (*callback)(const struct metric *, void *),
                                   void *user_data);

/**
 * resource_tracker_export_prometheus() - Export metrics in Prometheus format
 * @tracker: Resource tracker handle
 * @buffer: Output buffer
 * @buffer_size: Size of output buffer
 *
 * Returns: Number of bytes written or -1 on error
 */
ssize_t resource_tracker_export_prometheus(struct resource_tracker *tracker,
                                           char *buffer,
                                           size_t buffer_size);

#endif /* RESOURCE_TRACKER_H */
