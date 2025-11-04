/* filter_pool.h - Specialized object pool for filter structures
 *
 * Provides efficient pooling for filter evaluation contexts.
 */

#ifndef FILTER_POOL_H
#define FILTER_POOL_H

#include <stddef.h>
#include "object_pool.h"

/* Filter evaluation context */
struct filter_context {
	char *expression;         /* Filter expression string */
	void *compiled;           /* Compiled filter bytecode */
	size_t compiled_size;     /* Size of compiled data */
	int ref_count;            /* Reference count */
};

/* Filter pool wrapper */
struct filter_pool {
	struct object_pool *pool;
};

/**
 * filter_pool_create() - Create filter pool
 * @capacity: Maximum number of filters in pool
 *
 * Returns: Pointer to filter pool or NULL on error
 */
struct filter_pool *filter_pool_create(size_t capacity);

/**
 * filter_pool_destroy() - Destroy filter pool
 * @pool: Filter pool
 */
void filter_pool_destroy(struct filter_pool *pool);

/**
 * filter_pool_alloc() - Allocate filter context from pool
 * @pool: Filter pool
 *
 * Returns: Pointer to allocated filter context or NULL on error
 */
struct filter_context *filter_pool_alloc(struct filter_pool *pool);

/**
 * filter_pool_free() - Return filter context to pool
 * @pool: Filter pool
 * @filter: Filter context to free
 *
 * Note: Frees expression and compiled data if present
 */
void filter_pool_free(struct filter_pool *pool, struct filter_context *filter);

/**
 * filter_pool_get_stats() - Get pool statistics
 * @pool: Filter pool
 * @stats: Output statistics structure
 */
void filter_pool_get_stats(struct filter_pool *pool, struct object_pool_stats *stats);

/**
 * filter_pool_get_usage() - Get current pool usage
 * @pool: Filter pool
 *
 * Returns: Number of currently allocated filters
 */
size_t filter_pool_get_usage(struct filter_pool *pool);

#endif /* FILTER_POOL_H */
