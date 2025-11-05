/* nlmon_nl_limits.h - Resource limits and monitoring for netlink
 *
 * Configurable resource limits and monitoring for netlink subsystem.
 */

#ifndef NLMON_NL_LIMITS_H
#define NLMON_NL_LIMITS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/* Resource limits handle (opaque) */
struct nlmon_nl_limits;

/* Resource statistics */
struct nlmon_nl_resource_stats {
	/* Memory stats */
	size_t current_memory_bytes;
	size_t peak_memory_bytes;
	size_t max_memory_bytes;
	double memory_utilization;  /* Percentage */
	
	/* Message stats */
	uint64_t total_messages_processed;
	uint64_t total_messages_dropped;
	uint64_t total_bytes_processed;
	size_t messages_per_sec;
	size_t max_messages_per_sec;
	double drop_rate;  /* Percentage */
	
	/* Socket buffer stats */
	size_t socket_buffer_size;
	size_t socket_buffer_used;
	size_t socket_buffer_drops;
	double socket_buffer_utilization;  /* Percentage */
	
	/* Processing time stats */
	uint64_t avg_processing_time_ns;
	uint64_t min_processing_time_ns;
	uint64_t max_processing_time_ns;
};

/* Health status */
struct nlmon_nl_health_status {
	bool overall_healthy;
	bool memory_warning;
	bool memory_critical;
	bool rate_warning;
	bool rate_critical;
	bool buffer_warning;
	bool buffer_critical;
	bool drops_warning;
	bool drops_critical;
};

/**
 * nlmon_nl_limits_create() - Create resource limits tracker
 *
 * Returns: Limits handle or NULL on error
 */
struct nlmon_nl_limits *nlmon_nl_limits_create(void);

/**
 * nlmon_nl_limits_destroy() - Destroy resource limits tracker
 * @limits: Limits handle
 */
void nlmon_nl_limits_destroy(struct nlmon_nl_limits *limits);

/**
 * nlmon_nl_limits_set_memory() - Set memory limit
 * @limits: Limits handle
 * @max_mb: Maximum memory in megabytes
 */
void nlmon_nl_limits_set_memory(struct nlmon_nl_limits *limits, size_t max_mb);

/**
 * nlmon_nl_limits_set_rate() - Set message rate limit
 * @limits: Limits handle
 * @max_per_sec: Maximum messages per second
 */
void nlmon_nl_limits_set_rate(struct nlmon_nl_limits *limits, size_t max_per_sec);

/**
 * nlmon_nl_limits_enable_memory() - Enable/disable memory limit
 * @limits: Limits handle
 * @enable: true to enable, false to disable
 */
void nlmon_nl_limits_enable_memory(struct nlmon_nl_limits *limits, bool enable);

/**
 * nlmon_nl_limits_enable_rate() - Enable/disable rate limit
 * @limits: Limits handle
 * @enable: true to enable, false to disable
 */
void nlmon_nl_limits_enable_rate(struct nlmon_nl_limits *limits, bool enable);

/**
 * nlmon_nl_limits_track_alloc() - Track memory allocation
 * @limits: Limits handle
 * @size: Allocation size
 *
 * Returns: 0 on success, -ENOMEM if limit exceeded
 */
int nlmon_nl_limits_track_alloc(struct nlmon_nl_limits *limits, size_t size);

/**
 * nlmon_nl_limits_track_free() - Track memory deallocation
 * @limits: Limits handle
 * @size: Deallocation size
 */
void nlmon_nl_limits_track_free(struct nlmon_nl_limits *limits, size_t size);

/**
 * nlmon_nl_limits_can_process() - Check if message can be processed
 * @limits: Limits handle
 *
 * Checks rate limiting.
 *
 * Returns: true if message can be processed
 */
bool nlmon_nl_limits_can_process(struct nlmon_nl_limits *limits);

/**
 * nlmon_nl_limits_record_message() - Record message processed
 * @limits: Limits handle
 * @size: Message size
 * @processing_time_ns: Processing time in nanoseconds
 */
void nlmon_nl_limits_record_message(struct nlmon_nl_limits *limits,
                                   size_t size,
                                   uint64_t processing_time_ns);

/**
 * nlmon_nl_limits_update_socket_buffer() - Update socket buffer stats
 * @limits: Limits handle
 * @buffer_size: Total buffer size
 * @buffer_used: Used buffer size
 * @drops: Number of drops
 */
void nlmon_nl_limits_update_socket_buffer(struct nlmon_nl_limits *limits,
                                         size_t buffer_size,
                                         size_t buffer_used,
                                         size_t drops);

/**
 * nlmon_nl_limits_get_stats() - Get resource statistics
 * @limits: Limits handle
 * @stats: Output for statistics
 */
void nlmon_nl_limits_get_stats(struct nlmon_nl_limits *limits,
                               struct nlmon_nl_resource_stats *stats);

/**
 * nlmon_nl_limits_reset_stats() - Reset statistics
 * @limits: Limits handle
 */
void nlmon_nl_limits_reset_stats(struct nlmon_nl_limits *limits);

/**
 * nlmon_nl_limits_check_health() - Check if limits are being exceeded
 * @limits: Limits handle
 * @status: Output for health status
 *
 * Returns: true if healthy
 */
bool nlmon_nl_limits_check_health(struct nlmon_nl_limits *limits,
                                  struct nlmon_nl_health_status *status);

/**
 * nlmon_nl_limits_export_json() - Export metrics as JSON
 * @limits: Limits handle
 * @buffer: Output buffer
 * @buffer_size: Size of output buffer
 *
 * Returns: Number of bytes written or -1 on error
 */
ssize_t nlmon_nl_limits_export_json(struct nlmon_nl_limits *limits,
                                    char *buffer,
                                    size_t buffer_size);

/**
 * nlmon_nl_limits_get_memory_usage() - Get current memory usage
 * @limits: Limits handle
 *
 * Returns: Current memory usage in bytes
 */
size_t nlmon_nl_limits_get_memory_usage(struct nlmon_nl_limits *limits);

/**
 * nlmon_nl_limits_get_message_rate() - Get current message rate
 * @limits: Limits handle
 *
 * Returns: Current messages per second
 */
size_t nlmon_nl_limits_get_message_rate(struct nlmon_nl_limits *limits);

#endif /* NLMON_NL_LIMITS_H */

