/* bench_event_processing.c - Event processing throughput benchmark */

#include "benchmark_framework.h"
#include "event_processor.h"
#include "ring_buffer.h"
#include <string.h>

static struct event_processor *g_processor = NULL;
static struct ring_buffer *g_ring_buffer = NULL;
static volatile uint64_t g_events_processed = 0;

static void event_callback(struct nlmon_event *event, void *ctx)
{
	__sync_fetch_and_add(&g_events_processed, 1);
}

THROUGHPUT_BENCHMARK(event_submission, 5.0)
{
	struct nlmon_event event = {0};
	
	event.message_type = 16;
	event.sequence = g_events_processed;
	strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
	
	if (event_processor_submit(g_processor, &event) == 0) {
		return 1;
	}
	
	return 0;
}

THROUGHPUT_BENCHMARK(ring_buffer_ops, 5.0)
{
	int value = 42;
	int result;
	
	if (ring_buffer_push(g_ring_buffer, &value)) {
		ring_buffer_pop(g_ring_buffer, &result);
		return 1;
	}
	
	return 0;
}

BENCHMARK(event_creation, 100000)
{
	struct nlmon_event event = {0};
	
	event.message_type = 16;
	event.sequence = stats->iterations;
	strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
	event.event_type = 1;
}

BENCHMARK(event_copy, 100000)
{
	struct nlmon_event src = {0};
	struct nlmon_event dst;
	
	src.message_type = 16;
	strncpy(src.interface, "eth0", sizeof(src.interface) - 1);
	
	memcpy(&dst, &src, sizeof(dst));
}

MEMORY_BENCHMARK(event_processor_memory)
{
	struct event_processor_config config = {0};
	struct event_processor *ep;
	
	config.buffer_size = 1000;
	config.worker_threads = 4;
	
	ep = event_processor_create(&config);
	if (!ep)
		return 0;
	
	/* Estimate memory usage */
	size_t memory = sizeof(struct event_processor);
	memory += config.buffer_size * sizeof(struct nlmon_event);
	memory += config.worker_threads * 8192; /* Stack per thread */
	
	event_processor_destroy(ep);
	
	return memory;
}

MEMORY_BENCHMARK(ring_buffer_memory)
{
	size_t capacity = 1000;
	size_t element_size = sizeof(struct nlmon_event);
	
	struct ring_buffer *rb = ring_buffer_create(capacity, element_size);
	if (!rb)
		return 0;
	
	size_t memory = sizeof(struct ring_buffer);
	memory += capacity * element_size;
	
	ring_buffer_destroy(rb);
	
	return memory;
}

BENCHMARK_SUITE_BEGIN("Event Processing")
	/* Setup */
	struct event_processor_config config = {0};
	config.buffer_size = 10000;
	config.worker_threads = 4;
	config.rate_limit = 0; /* No rate limiting for benchmark */
	
	g_processor = event_processor_create(&config);
	if (g_processor) {
		event_processor_register_callback(g_processor, event_callback, NULL);
		event_processor_start(g_processor);
	}
	
	g_ring_buffer = ring_buffer_create(1000, sizeof(int));
	
	/* Run benchmarks */
	RUN_BENCHMARK(event_creation);
	RUN_BENCHMARK(event_copy);
	
	if (g_processor) {
		RUN_THROUGHPUT_BENCHMARK(event_submission);
		printf("Events processed: %lu\n", g_events_processed);
	}
	
	if (g_ring_buffer) {
		RUN_THROUGHPUT_BENCHMARK(ring_buffer_ops);
	}
	
	RUN_MEMORY_BENCHMARK(event_processor_memory);
	RUN_MEMORY_BENCHMARK(ring_buffer_memory);
	
	/* Cleanup */
	if (g_processor) {
		event_processor_stop(g_processor);
		event_processor_destroy(g_processor);
	}
	
	if (g_ring_buffer) {
		ring_buffer_destroy(g_ring_buffer);
	}
BENCHMARK_SUITE_END()
