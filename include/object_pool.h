/* object_pool.h - Generic object pool for memory management
 *
 * Provides efficient object pooling with configurable size and statistics.
 * Reduces allocation overhead and memory fragmentation.
 */

#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

/* Object pool structure (opaque) */
struct object_pool;

/* Object pool statistics */
struct object_pool_stats {
	size_t capacity;          /* Total pool capacity */
	size_t allocated;         /* Currently allocated objects */
	size_t peak_usage;        /* Peak usage since creation */
	unsigned long total_allocs;   /* Total allocations */
	unsigned long total_frees;    /* Total frees */
	unsigned long pool_hits;      /* Allocations from pool */
	unsigned long pool_misses;    /* Allocations from heap */
};

/**
 * object_pool_create() - Create object pool
 * @object_size: Size of each object in bytes
 * @capacity: Maximum number of objects in pool
 *
 * Returns: Pointer to object pool or NULL on error
 */
struct object_pool *object_pool_create(size_t object_size, size_t capacity);

/**
 * object_pool_destroy() - Destroy object pool
 * @pool: Object pool
 *
 * Note: All allocated objects must be freed before destroying pool
 */
void object_pool_destroy(struct object_pool *pool);

/**
 * object_pool_alloc() - Allocate object from pool
 * @pool: Object pool
 *
 * Returns: Pointer to allocated object or NULL on error
 * Note: Object is zeroed before returning
 */
void *object_pool_alloc(struct object_pool *pool);

/**
 * object_pool_free() - Return object to pool
 * @pool: Object pool
 * @obj: Object to free
 *
 * Note: Object is zeroed before returning to pool
 */
void object_pool_free(struct object_pool *pool, void *obj);

/**
 * object_pool_get_stats() - Get pool statistics
 * @pool: Object pool
 * @stats: Output statistics structure
 */
void object_pool_get_stats(struct object_pool *pool, struct object_pool_stats *stats);

/**
 * object_pool_reset_stats() - Reset statistics counters
 * @pool: Object pool
 */
void object_pool_reset_stats(struct object_pool *pool);

/**
 * object_pool_get_usage() - Get current pool usage
 * @pool: Object pool
 *
 * Returns: Number of currently allocated objects
 */
size_t object_pool_get_usage(struct object_pool *pool);

/**
 * object_pool_get_capacity() - Get pool capacity
 * @pool: Object pool
 *
 * Returns: Maximum number of objects in pool
 */
size_t object_pool_get_capacity(struct object_pool *pool);

#endif /* OBJECT_POOL_H */
