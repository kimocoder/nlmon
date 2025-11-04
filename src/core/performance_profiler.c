/* performance_profiler.c - Performance profiling and bottleneck detection */

#include "performance_profiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <math.h>

#define DEFAULT_MAX_SAMPLES 10000
#define DEFAULT_SLOW_THRESHOLD_MS 100.0
#define MAX_OPERATIONS 256
#define NS_PER_MS 1000000.0

/* Active sample (in-progress timing) */
struct active_sample {
	uint64_t id;
	char operation[128];
	char context[256];
	uint64_t start_ns;
	bool in_use;
};

/* Completed sample */
struct completed_sample {
	char operation[128];
	uint64_t duration_ns;
	time_t timestamp;
};

/* Operation statistics */
struct operation_stats {
	char name[128];
	uint64_t *durations;  /* Array of durations in ns */
	size_t count;
	size_t capacity;
	uint64_t total_ns;
	uint64_t min_ns;
	uint64_t max_ns;
	uint64_t slow_count;
	bool in_use;
};

struct performance_profiler {
	/* Active samples */
	struct active_sample *active_samples;
	size_t max_active;
	uint64_t next_sample_id;
	
	/* Operation statistics */
	struct operation_stats operations[MAX_OPERATIONS];
	size_t num_operations;
	
	/* Configuration */
	double slow_threshold_ms;
	
	/* Thread safety */
	pthread_mutex_t lock;
	
	/* Timing */
	struct timespec start_time;
};

static uint64_t get_time_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static double ns_to_ms(uint64_t ns)
{
	return (double)ns / NS_PER_MS;
}

static int compare_uint64(const void *a, const void *b)
{
	uint64_t ua = *(const uint64_t *)a;
	uint64_t ub = *(const uint64_t *)b;
	
	if (ua < ub) return -1;
	if (ua > ub) return 1;
	return 0;
}

struct performance_profiler *performance_profiler_create(size_t max_samples,
                                                         double slow_threshold_ms)
{
	struct performance_profiler *profiler;
	
	if (max_samples == 0)
		max_samples = DEFAULT_MAX_SAMPLES;
	if (slow_threshold_ms <= 0)
		slow_threshold_ms = DEFAULT_SLOW_THRESHOLD_MS;
	
	profiler = calloc(1, sizeof(*profiler));
	if (!profiler)
		return NULL;
	
	profiler->active_samples = calloc(max_samples, sizeof(struct active_sample));
	if (!profiler->active_samples) {
		free(profiler);
		return NULL;
	}
	
	profiler->max_active = max_samples;
	profiler->next_sample_id = 1;
	profiler->slow_threshold_ms = slow_threshold_ms;
	
	pthread_mutex_init(&profiler->lock, NULL);
	clock_gettime(CLOCK_MONOTONIC, &profiler->start_time);
	
	return profiler;
}

void performance_profiler_destroy(struct performance_profiler *profiler)
{
	size_t i;
	
	if (!profiler)
		return;
	
	/* Free operation duration arrays */
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (profiler->operations[i].in_use) {
			free(profiler->operations[i].durations);
		}
	}
	
	pthread_mutex_destroy(&profiler->lock);
	free(profiler->active_samples);
	free(profiler);
}

static struct operation_stats *find_or_create_operation(
	struct performance_profiler *profiler,
	const char *operation)
{
	size_t i;
	struct operation_stats *op;
	
	/* Find existing */
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (profiler->operations[i].in_use &&
		    strcmp(profiler->operations[i].name, operation) == 0) {
			return &profiler->operations[i];
		}
	}
	
	/* Create new */
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (!profiler->operations[i].in_use) {
			op = &profiler->operations[i];
			
			strncpy(op->name, operation, sizeof(op->name) - 1);
			op->name[sizeof(op->name) - 1] = '\0';
			
			op->capacity = 1000;
			op->durations = calloc(op->capacity, sizeof(uint64_t));
			if (!op->durations)
				return NULL;
			
			op->count = 0;
			op->total_ns = 0;
			op->min_ns = UINT64_MAX;
			op->max_ns = 0;
			op->slow_count = 0;
			op->in_use = true;
			
			profiler->num_operations++;
			return op;
		}
	}
	
	return NULL;
}

uint64_t performance_profiler_start(struct performance_profiler *profiler,
                                    const char *operation,
                                    const char *context)
{
	size_t i;
	uint64_t sample_id;
	
	if (!profiler || !operation)
		return 0;
	
	pthread_mutex_lock(&profiler->lock);
	
	/* Find free slot */
	for (i = 0; i < profiler->max_active; i++) {
		if (!profiler->active_samples[i].in_use) {
			struct active_sample *sample = &profiler->active_samples[i];
			
			sample->id = profiler->next_sample_id++;
			strncpy(sample->operation, operation, sizeof(sample->operation) - 1);
			sample->operation[sizeof(sample->operation) - 1] = '\0';
			
			if (context) {
				strncpy(sample->context, context, sizeof(sample->context) - 1);
				sample->context[sizeof(sample->context) - 1] = '\0';
			} else {
				sample->context[0] = '\0';
			}
			
			sample->start_ns = get_time_ns();
			sample->in_use = true;
			
			sample_id = sample->id;
			pthread_mutex_unlock(&profiler->lock);
			return sample_id;
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
	return 0;
}

uint64_t performance_profiler_end(struct performance_profiler *profiler,
                                  uint64_t sample_id)
{
	size_t i;
	uint64_t duration_ns = 0;
	
	if (!profiler || sample_id == 0)
		return 0;
	
	pthread_mutex_lock(&profiler->lock);
	
	/* Find active sample */
	for (i = 0; i < profiler->max_active; i++) {
		if (profiler->active_samples[i].in_use &&
		    profiler->active_samples[i].id == sample_id) {
			struct active_sample *sample = &profiler->active_samples[i];
			struct operation_stats *op;
			uint64_t end_ns = get_time_ns();
			
			duration_ns = end_ns - sample->start_ns;
			
			/* Record in operation stats */
			op = find_or_create_operation(profiler, sample->operation);
			if (op) {
				/* Expand array if needed */
				if (op->count >= op->capacity) {
					size_t new_capacity = op->capacity * 2;
					uint64_t *new_durations = realloc(op->durations,
					                                  new_capacity * sizeof(uint64_t));
					if (new_durations) {
						op->durations = new_durations;
						op->capacity = new_capacity;
					}
				}
				
				if (op->count < op->capacity) {
					op->durations[op->count++] = duration_ns;
				}
				
				op->total_ns += duration_ns;
				if (duration_ns < op->min_ns)
					op->min_ns = duration_ns;
				if (duration_ns > op->max_ns)
					op->max_ns = duration_ns;
				
				if (ns_to_ms(duration_ns) > profiler->slow_threshold_ms)
					op->slow_count++;
			}
			
			sample->in_use = false;
			break;
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
	
	return duration_ns;
}

void performance_profiler_record(struct performance_profiler *profiler,
                                 const char *operation,
                                 uint64_t duration_ns,
                                 const char *context)
{
	struct operation_stats *op;
	
	if (!profiler || !operation)
		return;
	
	pthread_mutex_lock(&profiler->lock);
	
	op = find_or_create_operation(profiler, operation);
	if (op) {
		/* Expand array if needed */
		if (op->count >= op->capacity) {
			size_t new_capacity = op->capacity * 2;
			uint64_t *new_durations = realloc(op->durations,
			                                  new_capacity * sizeof(uint64_t));
			if (new_durations) {
				op->durations = new_durations;
				op->capacity = new_capacity;
			}
		}
		
		if (op->count < op->capacity) {
			op->durations[op->count++] = duration_ns;
		}
		
		op->total_ns += duration_ns;
		if (duration_ns < op->min_ns)
			op->min_ns = duration_ns;
		if (duration_ns > op->max_ns)
			op->max_ns = duration_ns;
		
		if (ns_to_ms(duration_ns) > profiler->slow_threshold_ms)
			op->slow_count++;
	}
	
	pthread_mutex_unlock(&profiler->lock);
}

bool performance_profiler_get_stats(struct performance_profiler *profiler,
                                    const char *operation,
                                    struct profile_stats *stats)
{
	size_t i;
	bool found = false;
	
	if (!profiler || !operation || !stats)
		return false;
	
	pthread_mutex_lock(&profiler->lock);
	
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (profiler->operations[i].in_use &&
		    strcmp(profiler->operations[i].name, operation) == 0) {
			struct operation_stats *op = &profiler->operations[i];
			uint64_t *sorted_durations;
			
			strncpy(stats->operation, op->name, sizeof(stats->operation) - 1);
			stats->operation[sizeof(stats->operation) - 1] = '\0';
			
			stats->sample_count = op->count;
			stats->total_time_ms = ns_to_ms(op->total_ns);
			stats->avg_time_ms = op->count > 0 ? ns_to_ms(op->total_ns) / op->count : 0;
			stats->min_time_ms = ns_to_ms(op->min_ns);
			stats->max_time_ms = ns_to_ms(op->max_ns);
			stats->slow_count = op->slow_count;
			
			/* Calculate percentiles */
			if (op->count > 0) {
				sorted_durations = malloc(op->count * sizeof(uint64_t));
				if (sorted_durations) {
					memcpy(sorted_durations, op->durations,
					      op->count * sizeof(uint64_t));
					qsort(sorted_durations, op->count, sizeof(uint64_t),
					     compare_uint64);
					
					stats->p50_time_ms = ns_to_ms(sorted_durations[op->count / 2]);
					stats->p95_time_ms = ns_to_ms(sorted_durations[(op->count * 95) / 100]);
					stats->p99_time_ms = ns_to_ms(sorted_durations[(op->count * 99) / 100]);
					
					free(sorted_durations);
				}
			}
			
			found = true;
			break;
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
	
	return found;
}

size_t performance_profiler_detect_bottlenecks(struct performance_profiler *profiler,
                                               struct bottleneck_info *bottlenecks,
                                               size_t max_bottlenecks)
{
	size_t i, count = 0;
	uint64_t total_time = 0;
	double threshold_percentage = 10.0;  /* Operations taking >10% are bottlenecks */
	
	if (!profiler || !bottlenecks || max_bottlenecks == 0)
		return 0;
	
	pthread_mutex_lock(&profiler->lock);
	
	/* Calculate total time */
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (profiler->operations[i].in_use) {
			total_time += profiler->operations[i].total_ns;
		}
	}
	
	if (total_time == 0) {
		pthread_mutex_unlock(&profiler->lock);
		return 0;
	}
	
	/* Find bottlenecks */
	for (i = 0; i < MAX_OPERATIONS && count < max_bottlenecks; i++) {
		if (profiler->operations[i].in_use) {
			struct operation_stats *op = &profiler->operations[i];
			double percentage = (double)op->total_ns / total_time * 100.0;
			double avg_time_ms = op->count > 0 ? ns_to_ms(op->total_ns) / op->count : 0;
			
			if (percentage >= threshold_percentage || avg_time_ms > profiler->slow_threshold_ms) {
				strncpy(bottlenecks[count].operation, op->name,
				       sizeof(bottlenecks[count].operation) - 1);
				bottlenecks[count].operation[sizeof(bottlenecks[count].operation) - 1] = '\0';
				
				bottlenecks[count].avg_time_ms = avg_time_ms;
				bottlenecks[count].percentage_of_total = percentage;
				bottlenecks[count].sample_count = op->count;
				bottlenecks[count].is_bottleneck = true;
				
				count++;
			}
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
	
	return count;
}

ssize_t performance_profiler_export_json(struct performance_profiler *profiler,
                                         char *buffer,
                                         size_t buffer_size)
{
	size_t offset = 0;
	size_t i;
	bool first = true;
	
	if (!profiler || !buffer || buffer_size == 0)
		return -1;
	
	pthread_mutex_lock(&profiler->lock);
	
	offset += snprintf(buffer + offset, buffer_size - offset,
	                  "{\"operations\":[");
	
	for (i = 0; i < MAX_OPERATIONS && offset < buffer_size - 512; i++) {
		if (profiler->operations[i].in_use) {
			struct operation_stats *op = &profiler->operations[i];
			double avg_ms = op->count > 0 ? ns_to_ms(op->total_ns) / op->count : 0;
			
			if (!first)
				offset += snprintf(buffer + offset, buffer_size - offset, ",");
			first = false;
			
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "{\"name\":\"%s\",\"samples\":%lu,"
			                  "\"total_ms\":%.3f,\"avg_ms\":%.3f,"
			                  "\"min_ms\":%.3f,\"max_ms\":%.3f,"
			                  "\"slow_count\":%lu}",
			                  op->name, op->count,
			                  ns_to_ms(op->total_ns), avg_ms,
			                  ns_to_ms(op->min_ns), ns_to_ms(op->max_ns),
			                  op->slow_count);
		}
	}
	
	offset += snprintf(buffer + offset, buffer_size - offset, "]}");
	
	pthread_mutex_unlock(&profiler->lock);
	
	return offset;
}

ssize_t performance_profiler_export_csv(struct performance_profiler *profiler,
                                        char *buffer,
                                        size_t buffer_size)
{
	size_t offset = 0;
	size_t i;
	
	if (!profiler || !buffer || buffer_size == 0)
		return -1;
	
	pthread_mutex_lock(&profiler->lock);
	
	/* Header */
	offset += snprintf(buffer + offset, buffer_size - offset,
	                  "operation,samples,total_ms,avg_ms,min_ms,max_ms,slow_count\n");
	
	for (i = 0; i < MAX_OPERATIONS && offset < buffer_size - 256; i++) {
		if (profiler->operations[i].in_use) {
			struct operation_stats *op = &profiler->operations[i];
			double avg_ms = op->count > 0 ? ns_to_ms(op->total_ns) / op->count : 0;
			
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "%s,%lu,%.3f,%.3f,%.3f,%.3f,%lu\n",
			                  op->name, op->count,
			                  ns_to_ms(op->total_ns), avg_ms,
			                  ns_to_ms(op->min_ns), ns_to_ms(op->max_ns),
			                  op->slow_count);
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
	
	return offset;
}

void performance_profiler_reset(struct performance_profiler *profiler)
{
	size_t i;
	
	if (!profiler)
		return;
	
	pthread_mutex_lock(&profiler->lock);
	
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (profiler->operations[i].in_use) {
			profiler->operations[i].count = 0;
			profiler->operations[i].total_ns = 0;
			profiler->operations[i].min_ns = UINT64_MAX;
			profiler->operations[i].max_ns = 0;
			profiler->operations[i].slow_count = 0;
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
}

void performance_profiler_set_slow_threshold(struct performance_profiler *profiler,
                                             double threshold_ms)
{
	if (!profiler || threshold_ms <= 0)
		return;
	
	pthread_mutex_lock(&profiler->lock);
	profiler->slow_threshold_ms = threshold_ms;
	pthread_mutex_unlock(&profiler->lock);
}

double performance_profiler_get_total_time(struct performance_profiler *profiler)
{
	uint64_t total_ns = 0;
	size_t i;
	
	if (!profiler)
		return 0;
	
	pthread_mutex_lock(&profiler->lock);
	
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (profiler->operations[i].in_use) {
			total_ns += profiler->operations[i].total_ns;
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
	
	return ns_to_ms(total_ns);
}

void performance_profiler_list_operations(struct performance_profiler *profiler,
                                          void (*callback)(const struct profile_stats *, void *),
                                          void *user_data)
{
	size_t i;
	
	if (!profiler || !callback)
		return;
	
	pthread_mutex_lock(&profiler->lock);
	
	for (i = 0; i < MAX_OPERATIONS; i++) {
		if (profiler->operations[i].in_use) {
			struct profile_stats stats;
			struct operation_stats *op = &profiler->operations[i];
			
			strncpy(stats.operation, op->name, sizeof(stats.operation) - 1);
			stats.operation[sizeof(stats.operation) - 1] = '\0';
			
			stats.sample_count = op->count;
			stats.total_time_ms = ns_to_ms(op->total_ns);
			stats.avg_time_ms = op->count > 0 ? ns_to_ms(op->total_ns) / op->count : 0;
			stats.min_time_ms = ns_to_ms(op->min_ns);
			stats.max_time_ms = ns_to_ms(op->max_ns);
			stats.slow_count = op->slow_count;
			
			callback(&stats, user_data);
		}
	}
	
	pthread_mutex_unlock(&profiler->lock);
}
