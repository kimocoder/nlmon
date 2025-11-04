/* prometheus_exporter.h - Prometheus metrics exporter
 *
 * Provides HTTP endpoint for Prometheus metrics scraping with
 * event counters, performance metrics, and system resource usage.
 */

#ifndef PROMETHEUS_EXPORTER_H
#define PROMETHEUS_EXPORTER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Prometheus exporter handle (opaque) */
struct prometheus_exporter;

/* Metric types */
enum metric_type {
	METRIC_TYPE_COUNTER,
	METRIC_TYPE_GAUGE,
	METRIC_TYPE_HISTOGRAM
};

/**
 * prometheus_exporter_create() - Create Prometheus exporter
 * @port: HTTP port to listen on
 * @path: HTTP path for metrics endpoint (e.g., "/metrics")
 *
 * Returns: Prometheus exporter handle or NULL on error
 */
struct prometheus_exporter *prometheus_exporter_create(uint16_t port,
                                                       const char *path);

/**
 * prometheus_exporter_destroy() - Destroy Prometheus exporter
 * @exporter: Prometheus exporter handle
 */
void prometheus_exporter_destroy(struct prometheus_exporter *exporter);

/**
 * prometheus_exporter_inc_counter() - Increment a counter metric
 * @exporter: Prometheus exporter handle
 * @name: Metric name
 * @labels: Label string (e.g., "type=\"link\"") or NULL
 * @value: Value to add (typically 1)
 */
void prometheus_exporter_inc_counter(struct prometheus_exporter *exporter,
                                     const char *name,
                                     const char *labels,
                                     uint64_t value);

/**
 * prometheus_exporter_set_gauge() - Set a gauge metric
 * @exporter: Prometheus exporter handle
 * @name: Metric name
 * @labels: Label string or NULL
 * @value: Value to set
 */
void prometheus_exporter_set_gauge(struct prometheus_exporter *exporter,
                                   const char *name,
                                   const char *labels,
                                   double value);

/**
 * prometheus_exporter_observe_histogram() - Add observation to histogram
 * @exporter: Prometheus exporter handle
 * @name: Metric name
 * @labels: Label string or NULL
 * @value: Observed value
 */
void prometheus_exporter_observe_histogram(struct prometheus_exporter *exporter,
                                          const char *name,
                                          const char *labels,
                                          double value);

/**
 * prometheus_exporter_update_system_metrics() - Update system resource metrics
 * @exporter: Prometheus exporter handle
 *
 * Updates CPU and memory usage metrics from /proc
 */
void prometheus_exporter_update_system_metrics(struct prometheus_exporter *exporter);

/**
 * prometheus_exporter_get_stats() - Get exporter statistics
 * @exporter: Prometheus exporter handle
 * @requests_served: Output for number of HTTP requests served
 * @last_scrape_time: Output for last scrape timestamp
 *
 * Returns: true on success, false on error
 */
bool prometheus_exporter_get_stats(struct prometheus_exporter *exporter,
                                   uint64_t *requests_served,
                                   uint64_t *last_scrape_time);

#endif /* PROMETHEUS_EXPORTER_H */
