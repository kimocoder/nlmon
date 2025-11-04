/* object_pool.c - Generic object pool implementation
 *
 * Thread-safe object pool with statistics tracking.
 */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "object_pool.h"

/* Object pool structure */
struct object_pool {
	void **objects;           /* Array of object pointers */
	size_t object_size;       /* Size of each object */
	size_t capacity;          /* Maximum pool size */
	
	/* Free list management */
	atomic_size_t free_count; /* Number of free objects */
	pthread_mutex_t mutex;    /* Protects free list operations */
	
	/* Statistics */
	atomic_size_t allocated;
	atomic_size_t peak_usage;
	atomic_ulong total_allocs;
	atomic_ulong total_frees;
	atomic_ulong pool_hits;
	atomic_ulong pool_misses;
};

struct object_pool *object_pool_create(size_t object_size, size_t capacity)
{
	struct object_pool *pool;
	size_t i;
	
	if (object_size == 0 || capacity == 0)
		return NULL;
	
	pool = calloc(1, sizeof(*pool));
	if (!pool)
		return NULL;
	
	pool->object_size = object_size;
	pool->capacity = capacity;
	
	/* Allocate object pointer array */
	pool->objects = calloc(capacity, sizeof(void *));
	if (!pool->objects) {
		free(pool);
		return NULL;
	}
	
	/* Pre-allocate all objects */
	for (i = 0; i < capacity; i++) {
		pool->objects[i] = calloc(1, object_size);
		if (!pool->objects[i]) {
			/* Cleanup on failure */
			for (size_t j = 0; j < i; j++)
				free(pool->objects[j]);
			free(pool->objects);
			free(pool);
			return NULL;
		}
	}
	
	/* Initialize atomics */
	atomic_init(&pool->free_count, capacity);
	atomic_init(&pool->allocated, 0);
	atomic_init(&pool->peak_usage, 0);
	atomic_init(&pool->total_allocs, 0);
	atomic_init(&pool->total_frees, 0);
	atomic_init(&pool->pool_hits, 0);
	atomic_init(&pool->pool_misses, 0);
	
	/* Initialize mutex */
	if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
		for (i = 0; i < capacity; i++)
			free(pool->objects[i]);
		free(pool->objects);
		free(pool);
		return NULL;
	}
	
	return pool;
}

void object_pool_destroy(struct object_pool *pool)
{
	size_t i;
	
	if (!pool)
		return;
	
	/* Free all objects */
	for (i = 0; i < pool->capacity; i++) {
		if (pool->objects[i])
			free(pool->objects[i]);
	}
	
	free(pool->objects);
	pthread_mutex_destroy(&pool->mutex);
	free(pool);
}

void *object_pool_alloc(struct object_pool *pool)
{
	void *obj = NULL;
	size_t free_count, allocated, peak;
	
	if (!pool)
		return NULL;
	
	pthread_mutex_lock(&pool->mutex);
	
	/* Try to get object from pool */
	free_count = atomic_load_explicit(&pool->free_count, memory_order_acquire);
	if (free_count > 0) {
		/* Get object from free list */
		free_count--;
		obj = pool->objects[free_count];
		pool->objects[free_count] = NULL;
		atomic_store_explicit(&pool->free_count, free_count, memory_order_release);
		
		/* Update statistics */
		atomic_fetch_add_explicit(&pool->pool_hits, 1, memory_order_relaxed);
	} else {
		/* Pool empty, allocate from heap */
		obj = calloc(1, pool->object_size);
		atomic_fetch_add_explicit(&pool->pool_misses, 1, memory_order_relaxed);
	}
	
	pthread_mutex_unlock(&pool->mutex);
	
	if (obj) {
		/* Update allocation statistics */
		allocated = atomic_fetch_add_explicit(&pool->allocated, 1, memory_order_relaxed) + 1;
		atomic_fetch_add_explicit(&pool->total_allocs, 1, memory_order_relaxed);
		
		/* Update peak usage */
		peak = atomic_load_explicit(&pool->peak_usage, memory_order_relaxed);
		while (allocated > peak) {
			if (atomic_compare_exchange_weak_explicit(&pool->peak_usage, &peak, allocated,
			                                          memory_order_relaxed,
			                                          memory_order_relaxed))
				break;
		}
		
		/* Zero the object */
		memset(obj, 0, pool->object_size);
	}
	
	return obj;
}

void object_pool_free(struct object_pool *pool, void *obj)
{
	size_t free_count;
	
	if (!pool || !obj)
		return;
	
	/* Zero the object */
	memset(obj, 0, pool->object_size);
	
	pthread_mutex_lock(&pool->mutex);
	
	/* Try to return object to pool */
	free_count = atomic_load_explicit(&pool->free_count, memory_order_acquire);
	if (free_count < pool->capacity) {
		/* Return to pool */
		pool->objects[free_count] = obj;
		free_count++;
		atomic_store_explicit(&pool->free_count, free_count, memory_order_release);
	} else {
		/* Pool full, free to heap */
		free(obj);
	}
	
	pthread_mutex_unlock(&pool->mutex);
	
	/* Update statistics */
	atomic_fetch_sub_explicit(&pool->allocated, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&pool->total_frees, 1, memory_order_relaxed);
}

void object_pool_get_stats(struct object_pool *pool, struct object_pool_stats *stats)
{
	if (!pool || !stats)
		return;
	
	stats->capacity = pool->capacity;
	stats->allocated = atomic_load_explicit(&pool->allocated, memory_order_relaxed);
	stats->peak_usage = atomic_load_explicit(&pool->peak_usage, memory_order_relaxed);
	stats->total_allocs = atomic_load_explicit(&pool->total_allocs, memory_order_relaxed);
	stats->total_frees = atomic_load_explicit(&pool->total_frees, memory_order_relaxed);
	stats->pool_hits = atomic_load_explicit(&pool->pool_hits, memory_order_relaxed);
	stats->pool_misses = atomic_load_explicit(&pool->pool_misses, memory_order_relaxed);
}

void object_pool_reset_stats(struct object_pool *pool)
{
	if (!pool)
		return;
	
	/* Reset counters but preserve current allocated count and peak */
	atomic_store_explicit(&pool->total_allocs, 0, memory_order_relaxed);
	atomic_store_explicit(&pool->total_frees, 0, memory_order_relaxed);
	atomic_store_explicit(&pool->pool_hits, 0, memory_order_relaxed);
	atomic_store_explicit(&pool->pool_misses, 0, memory_order_relaxed);
}

size_t object_pool_get_usage(struct object_pool *pool)
{
	if (!pool)
		return 0;
	
	return atomic_load_explicit(&pool->allocated, memory_order_relaxed);
}

size_t object_pool_get_capacity(struct object_pool *pool)
{
	if (!pool)
		return 0;
	
	return pool->capacity;
}
