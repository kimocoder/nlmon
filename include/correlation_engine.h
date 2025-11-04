/* correlation_engine.h - Event correlation and pattern detection
 *
 * Provides sliding time window management, correlation rule engine,
 * pattern detection, and anomaly detection for network events.
 */

#ifndef CORRELATION_ENGINE_H
#define CORRELATION_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Forward declarations */
struct nlmon_event;

/* Time window for event correlation */
struct time_window;

/* Correlation rule */
struct correlation_rule;

/* Correlation engine */
struct correlation_engine;

/* Pattern detector */
struct pattern_detector;

/* Anomaly detector */
struct anomaly_detector;

/* Correlation result */
struct correlation_result {
	char correlation_id[64];
	size_t event_count;
	struct nlmon_event **events;
	char rule_name[64];
	time_t first_timestamp;
	time_t last_timestamp;
};

/* Pattern detection result */
struct pattern_result {
	char pattern_name[64];
	uint32_t event_type;
	size_t frequency;
	time_t first_seen;
	time_t last_seen;
	double events_per_second;
};

/* Anomaly detection result */
struct anomaly_result {
	char anomaly_type[64];
	uint32_t event_type;
	double score;
	double baseline_mean;
	double baseline_stddev;
	double current_value;
	time_t detected_at;
};

/* Correlation rule condition */
enum correlation_condition {
	CORR_COND_EVENT_TYPE,
	CORR_COND_INTERFACE,
	CORR_COND_SAME_INTERFACE,
	CORR_COND_MESSAGE_TYPE,
	CORR_COND_COUNT,
	CORR_COND_RATE
};

/* Correlation rule definition */
struct correlation_rule_def {
	char name[64];
	size_t event_count;
	struct {
		enum correlation_condition type;
		union {
			uint32_t event_type;
			char interface[16];
			uint16_t message_type;
			size_t count;
			double rate;
		} value;
	} conditions[8];
	size_t condition_count;
	time_t time_window_sec;
	bool generate_alert;
};

/* Correlation engine configuration */
struct correlation_config {
	size_t max_window_size;
	time_t default_window_sec;
	size_t max_rules;
	bool enable_pattern_detection;
	bool enable_anomaly_detection;
	size_t pattern_min_frequency;
	double anomaly_threshold;
};

/**
 * time_window_create() - Create sliding time window
 * @window_sec: Window size in seconds
 * @max_events: Maximum events to store
 *
 * Returns: Pointer to time window or NULL on error
 */
struct time_window *time_window_create(time_t window_sec, size_t max_events);

/**
 * time_window_destroy() - Destroy time window
 * @window: Time window
 */
void time_window_destroy(struct time_window *window);

/**
 * time_window_add() - Add event to time window
 * @window: Time window
 * @event: Event to add (will be copied)
 *
 * Returns: true on success
 */
bool time_window_add(struct time_window *window, struct nlmon_event *event);

/**
 * time_window_expire() - Remove expired events from window
 * @window: Time window
 * @current_time: Current timestamp
 *
 * Returns: Number of events expired
 */
size_t time_window_expire(struct time_window *window, time_t current_time);

/**
 * time_window_query() - Query events in window
 * @window: Time window
 * @event_type: Event type filter (0 for all)
 * @interface: Interface filter (NULL for all)
 * @events: Output array for matching events
 * @max_events: Maximum events to return
 *
 * Returns: Number of events found
 */
size_t time_window_query(struct time_window *window,
                         uint32_t event_type,
                         const char *interface,
                         struct nlmon_event **events,
                         size_t max_events);

/**
 * time_window_count() - Get event count in window
 * @window: Time window
 *
 * Returns: Number of events in window
 */
size_t time_window_count(struct time_window *window);

/**
 * correlation_engine_create() - Create correlation engine
 * @config: Configuration parameters
 *
 * Returns: Pointer to correlation engine or NULL on error
 */
struct correlation_engine *correlation_engine_create(struct correlation_config *config);

/**
 * correlation_engine_destroy() - Destroy correlation engine
 * @engine: Correlation engine
 */
void correlation_engine_destroy(struct correlation_engine *engine);

/**
 * correlation_engine_add_rule() - Add correlation rule
 * @engine: Correlation engine
 * @rule_def: Rule definition
 *
 * Returns: Rule ID or -1 on error
 */
int correlation_engine_add_rule(struct correlation_engine *engine,
                                struct correlation_rule_def *rule_def);

/**
 * correlation_engine_remove_rule() - Remove correlation rule
 * @engine: Correlation engine
 * @rule_id: Rule ID
 */
void correlation_engine_remove_rule(struct correlation_engine *engine, int rule_id);

/**
 * correlation_engine_process() - Process event for correlations
 * @engine: Correlation engine
 * @event: Event to process
 * @results: Output array for correlation results
 * @max_results: Maximum results to return
 *
 * Returns: Number of correlations found
 */
size_t correlation_engine_process(struct correlation_engine *engine,
                                  struct nlmon_event *event,
                                  struct correlation_result *results,
                                  size_t max_results);

/**
 * correlation_engine_generate_id() - Generate correlation ID
 * @engine: Correlation engine
 * @rule_name: Rule name
 * @id_buf: Buffer for correlation ID
 * @buf_size: Buffer size
 *
 * Returns: true on success
 */
bool correlation_engine_generate_id(struct correlation_engine *engine,
                                    const char *rule_name,
                                    char *id_buf, size_t buf_size);

/**
 * pattern_detector_create() - Create pattern detector
 * @window_sec: Time window for pattern detection
 * @min_frequency: Minimum frequency to report
 *
 * Returns: Pointer to pattern detector or NULL on error
 */
struct pattern_detector *pattern_detector_create(time_t window_sec,
                                                 size_t min_frequency);

/**
 * pattern_detector_destroy() - Destroy pattern detector
 * @detector: Pattern detector
 */
void pattern_detector_destroy(struct pattern_detector *detector);

/**
 * pattern_detector_process() - Process event for pattern detection
 * @detector: Pattern detector
 * @event: Event to process
 * @results: Output array for pattern results
 * @max_results: Maximum results to return
 *
 * Returns: Number of patterns detected
 */
size_t pattern_detector_process(struct pattern_detector *detector,
                               struct nlmon_event *event,
                               struct pattern_result *results,
                               size_t max_results);

/**
 * anomaly_detector_create() - Create anomaly detector
 * @baseline_window_sec: Time window for baseline calculation
 * @threshold: Anomaly score threshold (standard deviations)
 *
 * Returns: Pointer to anomaly detector or NULL on error
 */
struct anomaly_detector *anomaly_detector_create(time_t baseline_window_sec,
                                                 double threshold);

/**
 * anomaly_detector_destroy() - Destroy anomaly detector
 * @detector: Anomaly detector
 */
void anomaly_detector_destroy(struct anomaly_detector *detector);

/**
 * anomaly_detector_process() - Process event for anomaly detection
 * @detector: Anomaly detector
 * @event: Event to process
 * @results: Output array for anomaly results
 * @max_results: Maximum results to return
 *
 * Returns: Number of anomalies detected
 */
size_t anomaly_detector_process(struct anomaly_detector *detector,
                               struct nlmon_event *event,
                               struct anomaly_result *results,
                               size_t max_results);

/**
 * anomaly_detector_update_baseline() - Update baseline statistics
 * @detector: Anomaly detector
 * @event_type: Event type to update
 * @value: Value to add to baseline
 */
void anomaly_detector_update_baseline(struct anomaly_detector *detector,
                                      uint32_t event_type,
                                      double value);

#endif /* CORRELATION_ENGINE_H */
