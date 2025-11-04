/* rate_limiter.c - Token bucket rate limiter implementation
 *
 * Implements token bucket algorithm for rate limiting with support for
 * per-event-type limits and statistics tracking.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdatomic.h>
#include "rate_limiter.h"

/* Get current time in seconds with microsecond precision */
static double get_time_seconds(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1000000.0;
}

/* Rate limiter structure */
struct rate_limiter {
	double rate;              /* Tokens per second */
	double tokens;            /* Current token count */
	size_t burst;             /* Maximum tokens (bucket capacity) */
	double last_update;       /* Last update time */
	
	pthread_mutex_t mutex;
	
	/* Statistics */
	atomic_ulong allowed_count;
	atomic_ulong denied_count;
	
	/* Rate calculation */
	double rate_window_start;
	atomic_ulong rate_window_count;
};

struct rate_limiter *rate_limiter_create(double rate, size_t burst)
{
	struct rate_limiter *rl;
	
	if (rate <= 0 || burst == 0)
		return NULL;
	
	rl = calloc(1, sizeof(*rl));
	if (!rl)
		return NULL;
	
	rl->rate = rate;
	rl->burst = burst;
	rl->tokens = burst;  /* Start with full bucket */
	rl->last_update = get_time_seconds();
	rl->rate_window_start = rl->last_update;
	
	if (pthread_mutex_init(&rl->mutex, NULL) != 0) {
		free(rl);
		return NULL;
	}
	
	atomic_init(&rl->allowed_count, 0);
	atomic_init(&rl->denied_count, 0);
	atomic_init(&rl->rate_window_count, 0);
	
	return rl;
}

void rate_limiter_destroy(struct rate_limiter *rl)
{
	if (!rl)
		return;
	
	pthread_mutex_destroy(&rl->mutex);
	free(rl);
}

/* Refill tokens based on elapsed time */
static void refill_tokens(struct rate_limiter *rl)
{
	double now, elapsed, new_tokens;
	
	now = get_time_seconds();
	elapsed = now - rl->last_update;
	
	if (elapsed > 0) {
		new_tokens = elapsed * rl->rate;
		rl->tokens += new_tokens;
		
		/* Cap at burst size */
		if (rl->tokens > rl->burst)
			rl->tokens = rl->burst;
		
		rl->last_update = now;
	}
}

bool rate_limiter_allow(struct rate_limiter *rl)
{
	bool allowed;
	
	if (!rl)
		return true;
	
	pthread_mutex_lock(&rl->mutex);
	
	refill_tokens(rl);
	
	if (rl->tokens >= 1.0) {
		rl->tokens -= 1.0;
		allowed = true;
		atomic_fetch_add_explicit(&rl->allowed_count, 1, memory_order_relaxed);
		atomic_fetch_add_explicit(&rl->rate_window_count, 1, memory_order_relaxed);
	} else {
		allowed = false;
		atomic_fetch_add_explicit(&rl->denied_count, 1, memory_order_relaxed);
	}
	
	pthread_mutex_unlock(&rl->mutex);
	
	return allowed;
}

bool rate_limiter_allow_n(struct rate_limiter *rl, size_t n)
{
	bool allowed;
	
	if (!rl)
		return true;
	
	if (n == 0)
		return true;
	
	pthread_mutex_lock(&rl->mutex);
	
	refill_tokens(rl);
	
	if (rl->tokens >= n) {
		rl->tokens -= n;
		allowed = true;
		atomic_fetch_add_explicit(&rl->allowed_count, n, memory_order_relaxed);
		atomic_fetch_add_explicit(&rl->rate_window_count, n, memory_order_relaxed);
	} else {
		allowed = false;
		atomic_fetch_add_explicit(&rl->denied_count, n, memory_order_relaxed);
	}
	
	pthread_mutex_unlock(&rl->mutex);
	
	return allowed;
}

void rate_limiter_reset(struct rate_limiter *rl)
{
	if (!rl)
		return;
	
	pthread_mutex_lock(&rl->mutex);
	rl->tokens = rl->burst;
	rl->last_update = get_time_seconds();
	rl->rate_window_start = rl->last_update;
	atomic_store_explicit(&rl->rate_window_count, 0, memory_order_relaxed);
	pthread_mutex_unlock(&rl->mutex);
}

void rate_limiter_set_rate(struct rate_limiter *rl, double rate)
{
	if (!rl || rate <= 0)
		return;
	
	pthread_mutex_lock(&rl->mutex);
	refill_tokens(rl);  /* Update with old rate first */
	rl->rate = rate;
	pthread_mutex_unlock(&rl->mutex);
}

void rate_limiter_set_burst(struct rate_limiter *rl, size_t burst)
{
	if (!rl || burst == 0)
		return;
	
	pthread_mutex_lock(&rl->mutex);
	rl->burst = burst;
	if (rl->tokens > burst)
		rl->tokens = burst;
	pthread_mutex_unlock(&rl->mutex);
}

double rate_limiter_get_tokens(struct rate_limiter *rl)
{
	double tokens;
	
	if (!rl)
		return 0;
	
	pthread_mutex_lock(&rl->mutex);
	refill_tokens(rl);
	tokens = rl->tokens;
	pthread_mutex_unlock(&rl->mutex);
	
	return tokens;
}

void rate_limiter_stats(struct rate_limiter *rl,
                        unsigned long *allowed,
                        unsigned long *denied,
                        double *current_rate)
{
	if (!rl)
		return;
	
	if (allowed)
		*allowed = atomic_load_explicit(&rl->allowed_count, memory_order_relaxed);
	if (denied)
		*denied = atomic_load_explicit(&rl->denied_count, memory_order_relaxed);
	
	if (current_rate) {
		pthread_mutex_lock(&rl->mutex);
		double now = get_time_seconds();
		double elapsed = now - rl->rate_window_start;
		
		if (elapsed >= 1.0) {
			unsigned long count = atomic_load_explicit(&rl->rate_window_count, 
			                                           memory_order_relaxed);
			*current_rate = count / elapsed;
			
			/* Reset window */
			rl->rate_window_start = now;
			atomic_store_explicit(&rl->rate_window_count, 0, memory_order_relaxed);
		} else {
			unsigned long count = atomic_load_explicit(&rl->rate_window_count,
			                                           memory_order_relaxed);
			*current_rate = (elapsed > 0) ? (count / elapsed) : 0;
		}
		pthread_mutex_unlock(&rl->mutex);
	}
}

/* Per-event-type rate limiter map */

#define RATE_LIMITER_MAP_SIZE 256

struct rate_limiter_entry {
	uint32_t event_type;
	struct rate_limiter *limiter;
	struct rate_limiter_entry *next;
};

struct rate_limiter_map {
	struct rate_limiter_entry *buckets[RATE_LIMITER_MAP_SIZE];
	struct rate_limiter *default_limiter;
	pthread_mutex_t mutex;
	double default_rate;
	size_t default_burst;
};

static uint32_t hash_event_type(uint32_t event_type)
{
	/* Simple hash function */
	event_type ^= event_type >> 16;
	event_type *= 0x85ebca6b;
	event_type ^= event_type >> 13;
	event_type *= 0xc2b2ae35;
	event_type ^= event_type >> 16;
	return event_type % RATE_LIMITER_MAP_SIZE;
}

struct rate_limiter_map *rate_limiter_map_create(double default_rate,
                                                  size_t default_burst)
{
	struct rate_limiter_map *map;
	
	map = calloc(1, sizeof(*map));
	if (!map)
		return NULL;
	
	map->default_rate = default_rate;
	map->default_burst = default_burst;
	
	map->default_limiter = rate_limiter_create(default_rate, default_burst);
	if (!map->default_limiter) {
		free(map);
		return NULL;
	}
	
	if (pthread_mutex_init(&map->mutex, NULL) != 0) {
		rate_limiter_destroy(map->default_limiter);
		free(map);
		return NULL;
	}
	
	return map;
}

void rate_limiter_map_destroy(struct rate_limiter_map *map)
{
	struct rate_limiter_entry *entry, *next;
	size_t i;
	
	if (!map)
		return;
	
	for (i = 0; i < RATE_LIMITER_MAP_SIZE; i++) {
		entry = map->buckets[i];
		while (entry) {
			next = entry->next;
			rate_limiter_destroy(entry->limiter);
			free(entry);
			entry = next;
		}
	}
	
	rate_limiter_destroy(map->default_limiter);
	pthread_mutex_destroy(&map->mutex);
	free(map);
}

bool rate_limiter_map_set(struct rate_limiter_map *map, uint32_t event_type,
                          double rate, size_t burst)
{
	struct rate_limiter_entry *entry;
	struct rate_limiter *limiter;
	uint32_t bucket;
	
	if (!map)
		return false;
	
	limiter = rate_limiter_create(rate, burst);
	if (!limiter)
		return false;
	
	bucket = hash_event_type(event_type);
	
	pthread_mutex_lock(&map->mutex);
	
	/* Check if entry exists */
	for (entry = map->buckets[bucket]; entry; entry = entry->next) {
		if (entry->event_type == event_type) {
			/* Update existing */
			rate_limiter_destroy(entry->limiter);
			entry->limiter = limiter;
			pthread_mutex_unlock(&map->mutex);
			return true;
		}
	}
	
	/* Create new entry */
	entry = malloc(sizeof(*entry));
	if (!entry) {
		rate_limiter_destroy(limiter);
		pthread_mutex_unlock(&map->mutex);
		return false;
	}
	
	entry->event_type = event_type;
	entry->limiter = limiter;
	entry->next = map->buckets[bucket];
	map->buckets[bucket] = entry;
	
	pthread_mutex_unlock(&map->mutex);
	return true;
}

bool rate_limiter_map_allow(struct rate_limiter_map *map, uint32_t event_type)
{
	struct rate_limiter_entry *entry;
	struct rate_limiter *limiter = NULL;
	uint32_t bucket;
	bool allowed;
	
	if (!map)
		return true;
	
	bucket = hash_event_type(event_type);
	
	pthread_mutex_lock(&map->mutex);
	
	/* Find limiter for event type */
	for (entry = map->buckets[bucket]; entry; entry = entry->next) {
		if (entry->event_type == event_type) {
			limiter = entry->limiter;
			break;
		}
	}
	
	/* Use default if not found */
	if (!limiter)
		limiter = map->default_limiter;
	
	pthread_mutex_unlock(&map->mutex);
	
	allowed = rate_limiter_allow(limiter);
	return allowed;
}

void rate_limiter_map_reset(struct rate_limiter_map *map)
{
	struct rate_limiter_entry *entry;
	size_t i;
	
	if (!map)
		return;
	
	pthread_mutex_lock(&map->mutex);
	
	rate_limiter_reset(map->default_limiter);
	
	for (i = 0; i < RATE_LIMITER_MAP_SIZE; i++) {
		for (entry = map->buckets[i]; entry; entry = entry->next)
			rate_limiter_reset(entry->limiter);
	}
	
	pthread_mutex_unlock(&map->mutex);
}

void rate_limiter_map_stats(struct rate_limiter_map *map, uint32_t event_type,
                            unsigned long *allowed,
                            unsigned long *denied,
                            double *current_rate)
{
	struct rate_limiter_entry *entry;
	struct rate_limiter *limiter = NULL;
	uint32_t bucket;
	
	if (!map)
		return;
	
	bucket = hash_event_type(event_type);
	
	pthread_mutex_lock(&map->mutex);
	
	/* Find limiter for event type */
	for (entry = map->buckets[bucket]; entry; entry = entry->next) {
		if (entry->event_type == event_type) {
			limiter = entry->limiter;
			break;
		}
	}
	
	/* Use default if not found */
	if (!limiter)
		limiter = map->default_limiter;
	
	pthread_mutex_unlock(&map->mutex);
	
	rate_limiter_stats(limiter, allowed, denied, current_rate);
}
