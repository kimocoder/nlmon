/* test_ring_buffer.c - Unit tests for ring buffer */

#include "test_framework.h"
#include "ring_buffer.h"
#include <pthread.h>
#include <stdlib.h>

TEST(ring_buffer_create_destroy)
{
	struct ring_buffer *rb = ring_buffer_create(10);
	ASSERT_NOT_NULL(rb);
	ASSERT_TRUE(ring_buffer_capacity(rb) >= 10);
	ASSERT_EQ(ring_buffer_size(rb), 0);
	ASSERT_TRUE(ring_buffer_is_empty(rb));
	ASSERT_FALSE(ring_buffer_is_full(rb));
	ring_buffer_destroy(rb);
}

TEST(ring_buffer_enqueue_dequeue)
{
	struct ring_buffer *rb = ring_buffer_create(5);
	ASSERT_NOT_NULL(rb);
	
	int *value = malloc(sizeof(int));
	*value = 42;
	
	ASSERT_TRUE(ring_buffer_enqueue(rb, value));
	ASSERT_EQ(ring_buffer_size(rb), 1);
	ASSERT_FALSE(ring_buffer_is_empty(rb));
	
	int *result = (int *)ring_buffer_dequeue(rb);
	ASSERT_NOT_NULL(result);
	ASSERT_EQ(*result, 42);
	ASSERT_EQ(ring_buffer_size(rb), 0);
	ASSERT_TRUE(ring_buffer_is_empty(rb));
	
	free(result);
	ring_buffer_destroy(rb);
}

TEST(ring_buffer_full)
{
	struct ring_buffer *rb = ring_buffer_create(3);
	ASSERT_NOT_NULL(rb);
	
	int *values[3];
	for (int i = 0; i < 3; i++) {
		values[i] = malloc(sizeof(int));
		*values[i] = i;
		ASSERT_TRUE(ring_buffer_enqueue(rb, values[i]));
	}
	
	ASSERT_TRUE(ring_buffer_is_full(rb));
	ASSERT_EQ(ring_buffer_size(rb), 3);
	
	int *extra = malloc(sizeof(int));
	*extra = 99;
	ASSERT_FALSE(ring_buffer_enqueue(rb, extra));
	free(extra);
	
	/* Cleanup */
	for (int i = 0; i < 3; i++) {
		int *val = (int *)ring_buffer_dequeue(rb);
		free(val);
	}
	
	ring_buffer_destroy(rb);
}

TEST(ring_buffer_wrap_around)
{
	struct ring_buffer *rb = ring_buffer_create(3);
	ASSERT_NOT_NULL(rb);
	
	/* Fill buffer */
	int *values[3];
	for (int i = 0; i < 3; i++) {
		values[i] = malloc(sizeof(int));
		*values[i] = i;
		ASSERT_TRUE(ring_buffer_enqueue(rb, values[i]));
	}
	
	/* Pop one */
	int *result = (int *)ring_buffer_dequeue(rb);
	ASSERT_NOT_NULL(result);
	ASSERT_EQ(*result, 0);
	free(result);
	
	/* Push another (should wrap) */
	int *value = malloc(sizeof(int));
	*value = 10;
	ASSERT_TRUE(ring_buffer_enqueue(rb, value));
	ASSERT_EQ(ring_buffer_size(rb), 3);
	
	/* Verify order */
	result = (int *)ring_buffer_dequeue(rb);
	ASSERT_EQ(*result, 1);
	free(result);
	
	result = (int *)ring_buffer_dequeue(rb);
	ASSERT_EQ(*result, 2);
	free(result);
	
	result = (int *)ring_buffer_dequeue(rb);
	ASSERT_EQ(*result, 10);
	free(result);
	
	ring_buffer_destroy(rb);
}

TEST(ring_buffer_stats)
{
	struct ring_buffer *rb = ring_buffer_create(5);
	ASSERT_NOT_NULL(rb);
	
	unsigned long enqueued, dequeued, overflows, peak;
	
	/* Enqueue some items */
	for (int i = 0; i < 3; i++) {
		int *val = malloc(sizeof(int));
		*val = i;
		ring_buffer_enqueue(rb, val);
	}
	
	ring_buffer_stats(rb, &enqueued, &dequeued, &overflows, &peak);
	ASSERT_EQ(enqueued, 3);
	ASSERT_EQ(dequeued, 0);
	ASSERT_TRUE(peak >= 3);
	
	/* Dequeue items */
	for (int i = 0; i < 3; i++) {
		int *val = (int *)ring_buffer_dequeue(rb);
		free(val);
	}
	
	ring_buffer_stats(rb, &enqueued, &dequeued, &overflows, &peak);
	ASSERT_EQ(dequeued, 3);
	
	ring_buffer_destroy(rb);
}

/* Thread function for concurrent test */
static void *producer_thread(void *arg)
{
	struct ring_buffer *rb = (struct ring_buffer *)arg;
	
	for (int i = 0; i < 100; i++) {
		int *val = malloc(sizeof(int));
		*val = i;
		while (!ring_buffer_enqueue(rb, val)) {
			/* Spin until space available */
		}
	}
	
	return NULL;
}

static void *consumer_thread(void *arg)
{
	struct ring_buffer *rb = (struct ring_buffer *)arg;
	int count = 0;
	
	while (count < 100) {
		int *val = (int *)ring_buffer_dequeue(rb);
		if (val) {
			free(val);
			count++;
		}
	}
	
	return NULL;
}

TEST(ring_buffer_concurrent)
{
	struct ring_buffer *rb = ring_buffer_create(10);
	ASSERT_NOT_NULL(rb);
	
	pthread_t prod, cons;
	
	pthread_create(&prod, NULL, producer_thread, rb);
	pthread_create(&cons, NULL, consumer_thread, rb);
	
	pthread_join(prod, NULL);
	pthread_join(cons, NULL);
	
	/* All items should be consumed */
	ASSERT_TRUE(ring_buffer_is_empty(rb));
	
	ring_buffer_destroy(rb);
}

TEST_SUITE_BEGIN("Ring Buffer")
	RUN_TEST(ring_buffer_create_destroy);
	RUN_TEST(ring_buffer_enqueue_dequeue);
	RUN_TEST(ring_buffer_full);
	RUN_TEST(ring_buffer_wrap_around);
	RUN_TEST(ring_buffer_stats);
	RUN_TEST(ring_buffer_concurrent);
TEST_SUITE_END()
