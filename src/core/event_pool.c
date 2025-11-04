/* event_pool.c - Specialized object pool for network events */

#include <stdlib.h>
#include <string.h>
#include "event_pool.h"

struct event_pool *event_pool_create(size_t capacity)
{
	struct event_pool *pool;
	
	pool = malloc(sizeof(*pool));
	if (!pool)
		return NULL;
	
	pool->pool = object_pool_create(sizeof(struct nlmon_event_pooled), capacity);
	if (!pool->pool) {
		free(pool);
		return NULL;
	}
	
	return pool;
}

void event_pool_destroy(struct event_pool *pool)
{
	if (!pool)
		return;
	
	object_pool_destroy(pool->pool);
	free(pool);
}

struct nlmon_event_pooled *event_pool_alloc(struct event_pool *pool)
{
	if (!pool)
		return NULL;
	
	return (struct nlmon_event_pooled *)object_pool_alloc(pool->pool);
}

void event_pool_free(struct event_pool *pool, struct nlmon_event_pooled *event)
{
	if (!pool || !event)
		return;
	
	/* Free event-specific data if present */
	if (event->data) {
		free(event->data);
		event->data = NULL;
	}
	
	object_pool_free(pool->pool, event);
}

void event_pool_get_stats(struct event_pool *pool, struct object_pool_stats *stats)
{
	if (!pool)
		return;
	
	object_pool_get_stats(pool->pool, stats);
}

size_t event_pool_get_usage(struct event_pool *pool)
{
	if (!pool)
		return 0;
	
	return object_pool_get_usage(pool->pool);
}
