/* bench_memory_usage.c - Memory usage benchmark */

#include "benchmark_framework.h"
#include "object_pool.h"
#include "ring_buffer.h"
#include "event_processor.h"
#include <stdlib.h>
#include <string.h>

MEMORY_BENCHMARK(object_pool_overhead)
{
	size_t capacity = 1000;
	size_t object_size = 64;
	
	struct object_pool *pool = object_pool_create(capacity, object_size, NULL, NULL);
	if (!pool)
		return 0;
	
	/* Calculate overhead */
	size_t total_memory = sizeof(struct object_pool);
	total_memory += capacity * (object_size + sizeof(void *)); /* Objects + free list */
	
	object_pool_destroy(pool);
	
	return total_memory;
}

MEMORY_BENCHMARK(ring_buffer_overhead)
{
	size_t capacity = 1000;
	size_t element_size = 128;
	
	struct ring_buffer *rb = ring_buffer_create(capacity, element_size);
	if (!rb)
		return 0;
	
	size_t total_memory = sizeof(struct ring_buffer);
	total_memory += capacity * element_size;
	
	ring_buffer_destroy(rb);
	
	return total_memory;
}

MEMORY_BENCHMARK(event_storage_1k)
{
	size_t num_events = 1000;
	size_t event_size = sizeof(struct nlmon_event);
	
	return num_events * event_size;
}

MEMORY_BENCHMARK(event_storage_10k)
{
	size_t num_events = 10000;
	size_t event_size = sizeof(struct nlmon_event);
	
	return num_events * event_size;
}

MEMORY_BENCHMARK(event_storage_100k)
{
	size_t num_events = 100000;
	size_t event_size = sizeof(struct nlmon_event);
	
	return num_events * event_size;
}

BENCHMARK(memory_allocation, 10000)
{
	void *ptr = malloc(1024);
	if (ptr) {
		memset(ptr, 0, 1024);
		free(ptr);
	}
}

BENCHMARK(object_pool_allocation, 10000)
{
	static struct object_pool *pool = NULL;
	
	if (!pool) {
		pool = object_pool_create(100, 1024, NULL, NULL);
	}
	
	if (pool) {
		void *obj = object_pool_alloc(pool);
		if (obj) {
			memset(obj, 0, 1024);
			object_pool_free(pool, obj);
		}
	}
}

THROUGHPUT_BENCHMARK(allocation_throughput, 3.0)
{
	void *ptr = malloc(128);
	if (ptr) {
		free(ptr);
		return 1;
	}
	return 0;
}

THROUGHPUT_BENCHMARK(pool_allocation_throughput, 3.0)
{
	static struct object_pool *pool = NULL;
	
	if (!pool) {
		pool = object_pool_create(1000, 128, NULL, NULL);
	}
	
	if (pool) {
		void *obj = object_pool_alloc(pool);
		if (obj) {
			object_pool_free(pool, obj);
			return 1;
		}
	}
	return 0;
}

BENCHMARK_SUITE_BEGIN("Memory Usage")
	RUN_MEMORY_BENCHMARK(object_pool_overhead);
	RUN_MEMORY_BENCHMARK(ring_buffer_overhead);
	RUN_MEMORY_BENCHMARK(event_storage_1k);
	RUN_MEMORY_BENCHMARK(event_storage_10k);
	RUN_MEMORY_BENCHMARK(event_storage_100k);
	
	printf("\n=== Allocation Performance ===\n");
	RUN_BENCHMARK(memory_allocation);
	RUN_BENCHMARK(object_pool_allocation);
	RUN_THROUGHPUT_BENCHMARK(allocation_throughput);
	RUN_THROUGHPUT_BENCHMARK(pool_allocation_throughput);
BENCHMARK_SUITE_END()
