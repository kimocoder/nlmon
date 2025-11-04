/* performance_profiler.h - Performance profiling and bottleneck detection
 *
 * Provides event processing time measurement, bottleneck detection,
 * and profiling data export capabilities.
 */

#ifndef PERFORMANCE_PROFILER_H
#define PERFORMANCE_PROFILER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>

/* Profiling sample structure */
struct profile_sample {
	const char *operation;
	uint64_t start_ns;
	uint64_t end_ns;
	uint64_t duration_ns;
	const char *context;
};

/* Profiling statistics for an operation */
struct profile_stats {
	char operation[128];
	uint64_t sample_count;
	double total_time_ms;
	double avg_time_ms;
	double min_time_ms;
	double max_time_ms;
	double p50_time_ms;  /* Median */
	double p95_time_ms;
	double p99_time_ms;
	uint64_t slow_count;  /* Count of samples exceeding threshold */
};

/* Bottleneck information */
struct bottleneck_info {
	char operation[128];
	double avg_time_ms;
	double percentage_of_total;
	uint64_t sample_count;
	bool is_bottleneck;
};

/* Performance profiler handle (opaque) */
struct performance_profiler;

/**
 * performance_profiler_create() - Create performance profiler
 * @max_samples: Maximum number of samples to keep (0 for default)
 * @slow_threshold_ms: Threshold for "slow" operations in milliseconds
 *
 * Returns: Performance profiler handle or NULL on error
 */
struct performance_profiler *performance_profiler_create(size_t max_samples,
                                                         double slow_threshold_ms);

/**
 * performance_profiler_destroy() - Destroy performance profiler
 * @profiler: Performance profiler handle
 */
void performance_profiler_destroy(struct performance_profiler *profiler);

/**
 * performance_profiler_start() - Start timing an operation
 * @profiler: Performance profiler handle
 * @operation: Operation name
 * @context: Optional context string
 *
 * Returns: Sample ID for use with _end(), or 0 on error
 */
uint64_t performance_profiler_start(struct performance_profiler *profiler,
                                    const char *operation,
                                    const char *context);

/**
 * performance_profiler_end() - End timing an operation
 * @profiler: Performance profiler handle
 * @sample_id: Sample ID returned by _start()
 *
 * Returns: Duration in nanoseconds, or 0 on error
 */
uint64_t performance_profiler_end(struct performance_profiler *profiler,
                                  uint64_t sample_id);

/**
 * performance_profiler_record() - Record a completed operation
 * @profiler: Performance profiler handle
 * @operation: Operation name
 * @duration_ns: Duration in nanoseconds
 * @context: Optional context string
 *
 * Use this for operations where you already have the duration
 */
void performance_profiler_record(struct performance_profiler *profiler,
                                 const char *operation,
                                 uint64_t duration_ns,
                                 const char *context);

/**
 * performance_profiler_get_stats() - Get statistics for an operation
 * @profiler: Performance profiler handle
 * @operation: Operation name
 * @stats: Output for statistics
 *
 * Returns: true if operation found
 */
bool performance_profiler_get_stats(struct performance_profiler *profiler,
                                    const char *operation,
                                    struct profile_stats *stats);

/**
 * performance_profiler_detect_bottlenecks() - Detect performance bottlenecks
 * @profiler: Performance profiler handle
 * @bottlenecks: Output array for bottleneck information
 * @max_bottlenecks: Size of bottlenecks array
 *
 * Returns: Number of bottlenecks detected
 */
size_t performance_profiler_detect_bottlenecks(struct performance_profiler *profiler,
                                               struct bottleneck_info *bottlenecks,
                                               size_t max_bottlenecks);

/**
 * performance_profiler_export_json() - Export profiling data as JSON
 * @profiler: Performance profiler handle
 * @buffer: Output buffer
 * @buffer_size: Size of output buffer
 *
 * Returns: Number of bytes written or -1 on error
 */
ssize_t performance_profiler_export_json(struct performance_profiler *profiler,
                                         char *buffer,
                                         size_t buffer_size);

/**
 * performance_profiler_export_csv() - Export profiling data as CSV
 * @profiler: Performance profiler handle
 * @buffer: Output buffer
 * @buffer_size: Size of output buffer
 *
 * Returns: Number of bytes written or -1 on error
 */
ssize_t performance_profiler_export_csv(struct performance_profiler *profiler,
                                        char *buffer,
                                        size_t buffer_size);

/**
 * performance_profiler_reset() - Reset all profiling data
 * @profiler: Performance profiler handle
 */
void performance_profiler_reset(struct performance_profiler *profiler);

/**
 * performance_profiler_set_slow_threshold() - Set slow operation threshold
 * @profiler: Performance profiler handle
 * @threshold_ms: Threshold in milliseconds
 */
void performance_profiler_set_slow_threshold(struct performance_profiler *profiler,
                                             double threshold_ms);

/**
 * performance_profiler_get_total_time() - Get total profiled time
 * @profiler: Performance profiler handle
 *
 * Returns: Total time in milliseconds across all operations
 */
double performance_profiler_get_total_time(struct performance_profiler *profiler);

/**
 * performance_profiler_list_operations() - List all profiled operations
 * @profiler: Performance profiler handle
 * @callback: Callback function for each operation
 * @user_data: User data passed to callback
 *
 * Callback signature: void callback(const struct profile_stats *stats, void *user_data)
 */
void performance_profiler_list_operations(struct performance_profiler *profiler,
                                          void (*callback)(const struct profile_stats *, void *),
                                          void *user_data);

/* Helper macros for easy profiling */
#define PROFILE_START(profiler, op) \
	performance_profiler_start(profiler, op, NULL)

#define PROFILE_START_CTX(profiler, op, ctx) \
	performance_profiler_start(profiler, op, ctx)

#define PROFILE_END(profiler, id) \
	performance_profiler_end(profiler, id)

#endif /* PERFORMANCE_PROFILER_H */
