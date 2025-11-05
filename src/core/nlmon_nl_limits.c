/* nlmon_nl_limits.c - Resource limits and monitoring for netlink
 *
 * Implements configurable resource limits and monitoring for netlink subsystem:
 * - Memory usage limits
 * - Message processing rate limits
 * - Socket buffer monitoring
 * - Performance metrics collection
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>

#include "nlmon_nl_limits.h"

#define DEFAULT_MAX_MEMORY_MB 100
#define DEFAULT_MAX_MSG_RATE 10000
#define DEFAULT_SAMPLE_INTERVAL_SEC 1

/* Resource limits structure */
struct nlmon_nl_limits {
	/* Memory limits */
	size_t max_memory_bytes;
	size_t current_memory_bytes;
	size_t peak_memory_bytes;
	
	/* Message rate limits */
	size_t max_messages_per_sec;
	size_t messages_this_second;
	time_t current_second;
	
	/* Message counters */
	uint64_t total_messages_processed;
	uint64_t total_messages_dropped;
	uint64_t total_bytes_processed;
	
	/* Socket buffer stats */
	size_t socket_buffer_size;
	size_t socket_buffer_used;
	size_t socket_buffer_drops;
	
	/* Performance metrics */
	uint64_t total_processing_time_ns;
	uint64_t min_processing_time_ns;
	uint64_t max_processing_time_ns;
	
	/* Sampling */
	time_t last_sample_time;
	size_t sample_interval_sec;
	
	/* Limits enabled flags */
	bool memory_limit_enabled;
	bool rate_limit_enabled;
	
	/* Thread safety */
	pthread_mutex_t lock;
};

/**
 * Create resource limits tracker
 */
struct nlmon_nl_limits *nlmon_nl_limits_create(void)
{
	struct nlmon_nl_limits *limits;
	
	limits = calloc(1, sizeof(*limits));
	if (!limits)
		return NULL;
	
	/* Set default limits */
	limits->max_memory_bytes = DEFAULT_MAX_MEMORY_MB * 1024 * 1024;
	limits->max_messages_per_sec = DEFAULT_MAX_MSG_RATE;
	limits->sample_interval_sec = DEFAULT_SAMPLE_INTERVAL_SEC;
	
	/* Initialize counters */
	limits->current_memory_bytes = 0;
	limits->peak_memory_bytes = 0;
	limits->messages_this_second = 0;
	limits->current_second = time(NULL);
	limits->total_messages_processed = 0;
	limits->total_messages_dropped = 0;
	limits->total_bytes_processed = 0;
	limits->socket_buffer_size = 0;
	limits->socket_buffer_used = 0;
	limits->socket_buffer_drops = 0;
	limits->total_processing_time_ns = 0;
	limits->min_processing_time_ns = UINT64_MAX;
	limits->max_processing_time_ns = 0;
	limits->last_sample_time = time(NULL);
	
	/* Enable limits by default */
	limits->memory_limit_enabled = true;
	limits->rate_limit_enabled = true;
	
	pthread_mutex_init(&limits->lock, NULL);
	
	return limits;
}

/**
 * Destroy resource limits tracker
 */
void nlmon_nl_limits_destroy(struct nlmon_nl_limits *limits)
{
	if (!limits)
		return;
	
	pthread_mutex_destroy(&limits->lock);
	free(limits);
}

/**
 * Set memory limit
 */
void nlmon_nl_limits_set_memory(struct nlmon_nl_limits *limits, size_t max_mb)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	limits->max_memory_bytes = max_mb * 1024 * 1024;
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Set message rate limit
 */
void nlmon_nl_limits_set_rate(struct nlmon_nl_limits *limits, size_t max_per_sec)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	limits->max_messages_per_sec = max_per_sec;
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Enable/disable memory limit
 */
void nlmon_nl_limits_enable_memory(struct nlmon_nl_limits *limits, bool enable)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	limits->memory_limit_enabled = enable;
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Enable/disable rate limit
 */
void nlmon_nl_limits_enable_rate(struct nlmon_nl_limits *limits, bool enable)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	limits->rate_limit_enabled = enable;
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Track memory allocation
 */
int nlmon_nl_limits_track_alloc(struct nlmon_nl_limits *limits, size_t size)
{
	int ret = 0;
	
	if (!limits)
		return -EINVAL;
	
	pthread_mutex_lock(&limits->lock);
	
	/* Check memory limit */
	if (limits->memory_limit_enabled) {
		if (limits->current_memory_bytes + size > limits->max_memory_bytes) {
			pthread_mutex_unlock(&limits->lock);
			return -ENOMEM;
		}
	}
	
	/* Track allocation */
	limits->current_memory_bytes += size;
	if (limits->current_memory_bytes > limits->peak_memory_bytes) {
		limits->peak_memory_bytes = limits->current_memory_bytes;
	}
	
	pthread_mutex_unlock(&limits->lock);
	
	return ret;
}

/**
 * Track memory deallocation
 */
void nlmon_nl_limits_track_free(struct nlmon_nl_limits *limits, size_t size)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	
	if (limits->current_memory_bytes >= size) {
		limits->current_memory_bytes -= size;
	} else {
		limits->current_memory_bytes = 0;
	}
	
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Check if message can be processed (rate limiting)
 */
bool nlmon_nl_limits_can_process(struct nlmon_nl_limits *limits)
{
	time_t now;
	bool can_process = true;
	
	if (!limits)
		return true;
	
	pthread_mutex_lock(&limits->lock);
	
	if (limits->rate_limit_enabled) {
		now = time(NULL);
		
		/* Reset counter if we're in a new second */
		if (now != limits->current_second) {
			limits->current_second = now;
			limits->messages_this_second = 0;
		}
		
		/* Check rate limit */
		if (limits->messages_this_second >= limits->max_messages_per_sec) {
			can_process = false;
			limits->total_messages_dropped++;
		}
	}
	
	pthread_mutex_unlock(&limits->lock);
	
	return can_process;
}

/**
 * Record message processed
 */
void nlmon_nl_limits_record_message(struct nlmon_nl_limits *limits,
                                   size_t size,
                                   uint64_t processing_time_ns)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	
	/* Update counters */
	limits->messages_this_second++;
	limits->total_messages_processed++;
	limits->total_bytes_processed += size;
	
	/* Update processing time stats */
	limits->total_processing_time_ns += processing_time_ns;
	if (processing_time_ns < limits->min_processing_time_ns) {
		limits->min_processing_time_ns = processing_time_ns;
	}
	if (processing_time_ns > limits->max_processing_time_ns) {
		limits->max_processing_time_ns = processing_time_ns;
	}
	
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Update socket buffer stats
 */
void nlmon_nl_limits_update_socket_buffer(struct nlmon_nl_limits *limits,
                                         size_t buffer_size,
                                         size_t buffer_used,
                                         size_t drops)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	
	limits->socket_buffer_size = buffer_size;
	limits->socket_buffer_used = buffer_used;
	limits->socket_buffer_drops = drops;
	
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Get resource statistics
 */
void nlmon_nl_limits_get_stats(struct nlmon_nl_limits *limits,
                               struct nlmon_nl_resource_stats *stats)
{
	if (!limits || !stats)
		return;
	
	pthread_mutex_lock(&limits->lock);
	
	/* Memory stats */
	stats->current_memory_bytes = limits->current_memory_bytes;
	stats->peak_memory_bytes = limits->peak_memory_bytes;
	stats->max_memory_bytes = limits->max_memory_bytes;
	stats->memory_utilization = limits->max_memory_bytes > 0 ?
		(double)limits->current_memory_bytes / limits->max_memory_bytes * 100.0 : 0.0;
	
	/* Message stats */
	stats->total_messages_processed = limits->total_messages_processed;
	stats->total_messages_dropped = limits->total_messages_dropped;
	stats->total_bytes_processed = limits->total_bytes_processed;
	stats->messages_per_sec = limits->messages_this_second;
	stats->max_messages_per_sec = limits->max_messages_per_sec;
	
	/* Socket buffer stats */
	stats->socket_buffer_size = limits->socket_buffer_size;
	stats->socket_buffer_used = limits->socket_buffer_used;
	stats->socket_buffer_drops = limits->socket_buffer_drops;
	stats->socket_buffer_utilization = limits->socket_buffer_size > 0 ?
		(double)limits->socket_buffer_used / limits->socket_buffer_size * 100.0 : 0.0;
	
	/* Processing time stats */
	stats->avg_processing_time_ns = limits->total_messages_processed > 0 ?
		limits->total_processing_time_ns / limits->total_messages_processed : 0;
	stats->min_processing_time_ns = limits->min_processing_time_ns != UINT64_MAX ?
		limits->min_processing_time_ns : 0;
	stats->max_processing_time_ns = limits->max_processing_time_ns;
	
	/* Drop rate */
	uint64_t total = limits->total_messages_processed + limits->total_messages_dropped;
	stats->drop_rate = total > 0 ?
		(double)limits->total_messages_dropped / total * 100.0 : 0.0;
	
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Reset statistics
 */
void nlmon_nl_limits_reset_stats(struct nlmon_nl_limits *limits)
{
	if (!limits)
		return;
	
	pthread_mutex_lock(&limits->lock);
	
	limits->total_messages_processed = 0;
	limits->total_messages_dropped = 0;
	limits->total_bytes_processed = 0;
	limits->total_processing_time_ns = 0;
	limits->min_processing_time_ns = UINT64_MAX;
	limits->max_processing_time_ns = 0;
	limits->peak_memory_bytes = limits->current_memory_bytes;
	
	pthread_mutex_unlock(&limits->lock);
}

/**
 * Check if limits are being exceeded
 */
bool nlmon_nl_limits_check_health(struct nlmon_nl_limits *limits,
                                  struct nlmon_nl_health_status *status)
{
	bool healthy = true;
	
	if (!limits || !status)
		return false;
	
	pthread_mutex_lock(&limits->lock);
	
	memset(status, 0, sizeof(*status));
	
	/* Check memory usage */
	if (limits->memory_limit_enabled) {
		double mem_usage = (double)limits->current_memory_bytes / limits->max_memory_bytes * 100.0;
		if (mem_usage > 90.0) {
			status->memory_critical = true;
			healthy = false;
		} else if (mem_usage > 75.0) {
			status->memory_warning = true;
		}
	}
	
	/* Check message rate */
	if (limits->rate_limit_enabled) {
		double rate_usage = (double)limits->messages_this_second / limits->max_messages_per_sec * 100.0;
		if (rate_usage > 90.0) {
			status->rate_critical = true;
			healthy = false;
		} else if (rate_usage > 75.0) {
			status->rate_warning = true;
		}
	}
	
	/* Check socket buffer */
	if (limits->socket_buffer_size > 0) {
		double buf_usage = (double)limits->socket_buffer_used / limits->socket_buffer_size * 100.0;
		if (buf_usage > 90.0) {
			status->buffer_critical = true;
			healthy = false;
		} else if (buf_usage > 75.0) {
			status->buffer_warning = true;
		}
	}
	
	/* Check drop rate */
	uint64_t total = limits->total_messages_processed + limits->total_messages_dropped;
	if (total > 0) {
		double drop_rate = (double)limits->total_messages_dropped / total * 100.0;
		if (drop_rate > 5.0) {
			status->drops_critical = true;
			healthy = false;
		} else if (drop_rate > 1.0) {
			status->drops_warning = true;
		}
	}
	
	status->overall_healthy = healthy;
	
	pthread_mutex_unlock(&limits->lock);
	
	return healthy;
}

/**
 * Export metrics as JSON
 */
ssize_t nlmon_nl_limits_export_json(struct nlmon_nl_limits *limits,
                                    char *buffer,
                                    size_t buffer_size)
{
	struct nlmon_nl_resource_stats stats;
	size_t offset = 0;
	
	if (!limits || !buffer || buffer_size == 0)
		return -1;
	
	nlmon_nl_limits_get_stats(limits, &stats);
	
	offset += snprintf(buffer + offset, buffer_size - offset,
	                  "{\"memory\":{\"current\":%zu,\"peak\":%zu,\"max\":%zu,\"utilization\":%.2f},"
	                  "\"messages\":{\"processed\":%lu,\"dropped\":%lu,\"bytes\":%lu,"
	                  "\"per_sec\":%zu,\"max_per_sec\":%zu,\"drop_rate\":%.2f},"
	                  "\"socket_buffer\":{\"size\":%zu,\"used\":%zu,\"drops\":%zu,\"utilization\":%.2f},"
	                  "\"processing_time\":{\"avg_ns\":%lu,\"min_ns\":%lu,\"max_ns\":%lu}}",
	                  stats.current_memory_bytes, stats.peak_memory_bytes, stats.max_memory_bytes,
	                  stats.memory_utilization,
	                  stats.total_messages_processed, stats.total_messages_dropped,
	                  stats.total_bytes_processed, stats.messages_per_sec,
	                  stats.max_messages_per_sec, stats.drop_rate,
	                  stats.socket_buffer_size, stats.socket_buffer_used,
	                  stats.socket_buffer_drops, stats.socket_buffer_utilization,
	                  stats.avg_processing_time_ns, stats.min_processing_time_ns,
	                  stats.max_processing_time_ns);
	
	return offset;
}

/**
 * Get current memory usage
 */
size_t nlmon_nl_limits_get_memory_usage(struct nlmon_nl_limits *limits)
{
	size_t usage;
	
	if (!limits)
		return 0;
	
	pthread_mutex_lock(&limits->lock);
	usage = limits->current_memory_bytes;
	pthread_mutex_unlock(&limits->lock);
	
	return usage;
}

/**
 * Get current message rate
 */
size_t nlmon_nl_limits_get_message_rate(struct nlmon_nl_limits *limits)
{
	size_t rate;
	
	if (!limits)
		return 0;
	
	pthread_mutex_lock(&limits->lock);
	rate = limits->messages_this_second;
	pthread_mutex_unlock(&limits->lock);
	
	return rate;
}

