/* filter_pool.c - Specialized object pool for filter structures */

#include <stdlib.h>
#include <string.h>
#include "filter_pool.h"

struct filter_pool *filter_pool_create(size_t capacity)
{
	struct filter_pool *pool;
	
	pool = malloc(sizeof(*pool));
	if (!pool)
		return NULL;
	
	pool->pool = object_pool_create(sizeof(struct filter_context), capacity);
	if (!pool->pool) {
		free(pool);
		return NULL;
	}
	
	return pool;
}

void filter_pool_destroy(struct filter_pool *pool)
{
	if (!pool)
		return;
	
	object_pool_destroy(pool->pool);
	free(pool);
}

struct filter_context *filter_pool_alloc(struct filter_pool *pool)
{
	if (!pool)
		return NULL;
	
	return (struct filter_context *)object_pool_alloc(pool->pool);
}

void filter_pool_free(struct filter_pool *pool, struct filter_context *filter)
{
	if (!pool || !filter)
		return;
	
	/* Free expression string if present */
	if (filter->expression) {
		free(filter->expression);
		filter->expression = NULL;
	}
	
	/* Free compiled data if present */
	if (filter->compiled) {
		free(filter->compiled);
		filter->compiled = NULL;
	}
	
	object_pool_free(pool->pool, filter);
}

void filter_pool_get_stats(struct filter_pool *pool, struct object_pool_stats *stats)
{
	if (!pool)
		return;
	
	object_pool_get_stats(pool->pool, stats);
}

size_t filter_pool_get_usage(struct filter_pool *pool)
{
	if (!pool)
		return 0;
	
	return object_pool_get_usage(pool->pool);
}
