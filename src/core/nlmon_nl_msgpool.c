/* nlmon_nl_msgpool.c - Message buffer pool for netlink
 *
 * Implements a memory pool for netlink message buffers to reduce
 * allocation overhead in hot paths.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "nlmon_nl_msgpool.h"

#define DEFAULT_POOL_SIZE 256
#define DEFAULT_MSG_SIZE 4096
#define MAX_MSG_SIZE 65536

/* Message buffer structure */
struct msg_buffer {
	void *data;
	size_t size;
	size_t capacity;
	bool in_use;
	struct msg_buffer *next;
};

/* Message pool structure */
struct nlmon_nl_msgpool {
	struct msg_buffer *buffers;
	struct msg_buffer *free_list;
	size_t pool_size;
	size_t buffer_size;
	size_t allocated_count;
	size_t free_count;
	size_t alloc_requests;
	size_t pool_hits;
	size_t pool_misses;
	pthread_mutex_t lock;
};

/**
 * Create message pool
 */
struct nlmon_nl_msgpool *nlmon_nl_msgpool_create(size_t pool_size, size_t buffer_size)
{
	struct nlmon_nl_msgpool *pool;
	size_t i;
	
	if (pool_size == 0)
		pool_size = DEFAULT_POOL_SIZE;
	if (buffer_size == 0)
		buffer_size = DEFAULT_MSG_SIZE;
	if (buffer_size > MAX_MSG_SIZE)
		buffer_size = MAX_MSG_SIZE;
	
	pool = calloc(1, sizeof(*pool));
	if (!pool)
		return NULL;
	
	pool->buffers = calloc(pool_size, sizeof(struct msg_buffer));
	if (!pool->buffers) {
		free(pool);
		return NULL;
	}
	
	pool->pool_size = pool_size;
	pool->buffer_size = buffer_size;
	pool->allocated_count = 0;
	pool->free_count = pool_size;
	pool->alloc_requests = 0;
	pool->pool_hits = 0;
	pool->pool_misses = 0;
	
	/* Initialize buffers and build free list */
	pool->free_list = NULL;
	for (i = 0; i < pool_size; i++) {
		struct msg_buffer *buf = &pool->buffers[i];
		
		buf->data = malloc(buffer_size);
		if (!buf->data) {
			/* Cleanup on failure */
			for (size_t j = 0; j < i; j++) {
				free(pool->buffers[j].data);
			}
			free(pool->buffers);
			free(pool);
			return NULL;
		}
		
		buf->size = 0;
		buf->capacity = buffer_size;
		buf->in_use = false;
		
		/* Add to free list */
		buf->next = pool->free_list;
		pool->free_list = buf;
	}
	
	pthread_mutex_init(&pool->lock, NULL);
	
	return pool;
}

/**
 * Destroy message pool
 */
void nlmon_nl_msgpool_destroy(struct nlmon_nl_msgpool *pool)
{
	size_t i;
	
	if (!pool)
		return;
	
	if (pool->buffers) {
		for (i = 0; i < pool->pool_size; i++) {
			free(pool->buffers[i].data);
		}
		free(pool->buffers);
	}
	
	pthread_mutex_destroy(&pool->lock);
	free(pool);
}

/**
 * Allocate message buffer from pool
 */
void *nlmon_nl_msgpool_alloc(struct nlmon_nl_msgpool *pool, size_t size)
{
	struct msg_buffer *buf;
	void *data = NULL;
	
	if (!pool)
		return NULL;
	
	pthread_mutex_lock(&pool->lock);
	
	pool->alloc_requests++;
	
	/* Check if requested size fits in pool buffers */
	if (size > pool->buffer_size) {
		/* Size too large for pool, allocate directly */
		pthread_mutex_unlock(&pool->lock);
		pool->pool_misses++;
		return malloc(size);
	}
	
	/* Try to get buffer from free list */
	if (pool->free_list) {
		buf = pool->free_list;
		pool->free_list = buf->next;
		buf->next = NULL;
		buf->in_use = true;
		buf->size = size;
		
		pool->free_count--;
		pool->allocated_count++;
		pool->pool_hits++;
		
		data = buf->data;
	} else {
		/* Pool exhausted, allocate directly */
		pool->pool_misses++;
		data = malloc(size);
	}
	
	pthread_mutex_unlock(&pool->lock);
	
	return data;
}

/**
 * Free message buffer back to pool
 */
void nlmon_nl_msgpool_free(struct nlmon_nl_msgpool *pool, void *ptr)
{
	struct msg_buffer *buf;
	size_t i;
	bool found = false;
	
	if (!pool || !ptr)
		return;
	
	pthread_mutex_lock(&pool->lock);
	
	/* Check if this buffer belongs to the pool */
	for (i = 0; i < pool->pool_size; i++) {
		buf = &pool->buffers[i];
		if (buf->data == ptr && buf->in_use) {
			/* Return to free list */
			buf->in_use = false;
			buf->size = 0;
			buf->next = pool->free_list;
			pool->free_list = buf;
			
			pool->free_count++;
			pool->allocated_count--;
			
			found = true;
			break;
		}
	}
	
	pthread_mutex_unlock(&pool->lock);
	
	/* If not from pool, free directly */
	if (!found) {
		free(ptr);
	}
}

/**
 * Get pool statistics
 */
void nlmon_nl_msgpool_get_stats(struct nlmon_nl_msgpool *pool,
                                struct nlmon_msgpool_stats *stats)
{
	if (!pool || !stats)
		return;
	
	pthread_mutex_lock(&pool->lock);
	
	stats->pool_size = pool->pool_size;
	stats->buffer_size = pool->buffer_size;
	stats->allocated_count = pool->allocated_count;
	stats->free_count = pool->free_count;
	stats->alloc_requests = pool->alloc_requests;
	stats->pool_hits = pool->pool_hits;
	stats->pool_misses = pool->pool_misses;
	
	if (pool->alloc_requests > 0) {
		stats->hit_rate = (double)pool->pool_hits / pool->alloc_requests * 100.0;
	} else {
		stats->hit_rate = 0.0;
	}
	
	pthread_mutex_unlock(&pool->lock);
}

/**
 * Reset pool statistics
 */
void nlmon_nl_msgpool_reset_stats(struct nlmon_nl_msgpool *pool)
{
	if (!pool)
		return;
	
	pthread_mutex_lock(&pool->lock);
	
	pool->alloc_requests = 0;
	pool->pool_hits = 0;
	pool->pool_misses = 0;
	
	pthread_mutex_unlock(&pool->lock);
}

/**
 * Resize buffer if needed
 */
int nlmon_nl_msgpool_resize(struct nlmon_nl_msgpool *pool, void *ptr, size_t new_size)
{
	struct msg_buffer *buf;
	size_t i;
	
	if (!pool || !ptr)
		return -EINVAL;
	
	pthread_mutex_lock(&pool->lock);
	
	/* Find buffer in pool */
	for (i = 0; i < pool->pool_size; i++) {
		buf = &pool->buffers[i];
		if (buf->data == ptr && buf->in_use) {
			if (new_size <= buf->capacity) {
				/* Fits in existing buffer */
				buf->size = new_size;
				pthread_mutex_unlock(&pool->lock);
				return 0;
			} else {
				/* Need larger buffer - not supported for pooled buffers */
				pthread_mutex_unlock(&pool->lock);
				return -ENOMEM;
			}
		}
	}
	
	pthread_mutex_unlock(&pool->lock);
	
	/* Not a pooled buffer */
	return -EINVAL;
}

/**
 * Get buffer capacity
 */
size_t nlmon_nl_msgpool_get_capacity(struct nlmon_nl_msgpool *pool, void *ptr)
{
	struct msg_buffer *buf;
	size_t i;
	size_t capacity = 0;
	
	if (!pool || !ptr)
		return 0;
	
	pthread_mutex_lock(&pool->lock);
	
	/* Find buffer in pool */
	for (i = 0; i < pool->pool_size; i++) {
		buf = &pool->buffers[i];
		if (buf->data == ptr && buf->in_use) {
			capacity = buf->capacity;
			break;
		}
	}
	
	pthread_mutex_unlock(&pool->lock);
	
	return capacity;
}

/**
 * Preallocate all buffers (warm up pool)
 */
int nlmon_nl_msgpool_preallocate(struct nlmon_nl_msgpool *pool)
{
	if (!pool)
		return -EINVAL;
	
	/* Buffers are already allocated during pool creation */
	/* This function is a no-op but provided for API completeness */
	
	return 0;
}

/**
 * Shrink pool (free unused buffers)
 */
int nlmon_nl_msgpool_shrink(struct nlmon_nl_msgpool *pool, size_t target_free)
{
	if (!pool)
		return -EINVAL;
	
	/* Not implemented - pool maintains fixed size */
	/* Could be enhanced to support dynamic sizing */
	
	return 0;
}

/**
 * Check if pointer is from pool
 */
bool nlmon_nl_msgpool_is_pooled(struct nlmon_nl_msgpool *pool, void *ptr)
{
	struct msg_buffer *buf;
	size_t i;
	bool is_pooled = false;
	
	if (!pool || !ptr)
		return false;
	
	pthread_mutex_lock(&pool->lock);
	
	for (i = 0; i < pool->pool_size; i++) {
		buf = &pool->buffers[i];
		if (buf->data == ptr) {
			is_pooled = true;
			break;
		}
	}
	
	pthread_mutex_unlock(&pool->lock);
	
	return is_pooled;
}

/**
 * Get pool utilization percentage
 */
double nlmon_nl_msgpool_get_utilization(struct nlmon_nl_msgpool *pool)
{
	double utilization;
	
	if (!pool || pool->pool_size == 0)
		return 0.0;
	
	pthread_mutex_lock(&pool->lock);
	utilization = (double)pool->allocated_count / pool->pool_size * 100.0;
	pthread_mutex_unlock(&pool->lock);
	
	return utilization;
}

