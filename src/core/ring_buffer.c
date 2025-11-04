/* ring_buffer.c - Lock-free ring buffer implementation
 *
 * Single-producer, single-consumer lock-free ring buffer using atomic operations.
 * Provides high-performance event queuing with overflow handling and backpressure.
 */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "ring_buffer.h"

/* Round up to next power of 2 */
static size_t next_power_of_2(size_t n)
{
	if (n == 0)
		return 1;
	
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	if (sizeof(size_t) > 4)
		n |= n >> 32;
	n++;
	
	return n;
}

struct ring_buffer *ring_buffer_create(size_t capacity)
{
	struct ring_buffer *rb;
	
	if (capacity == 0)
		return NULL;
	
	rb = calloc(1, sizeof(*rb));
	if (!rb)
		return NULL;
	
	/* Round capacity to next power of 2 for fast modulo */
	rb->capacity = next_power_of_2(capacity);
	rb->mask = rb->capacity - 1;
	
	/* Allocate data array */
	rb->data = calloc(rb->capacity, sizeof(void *));
	if (!rb->data) {
		free(rb);
		return NULL;
	}
	
	/* Initialize atomics */
	atomic_init(&rb->head, 0);
	atomic_init(&rb->tail, 0);
	atomic_init(&rb->enqueue_count, 0);
	atomic_init(&rb->dequeue_count, 0);
	atomic_init(&rb->overflow_count, 0);
	atomic_init(&rb->peak_usage, 0);
	
	return rb;
}

void ring_buffer_destroy(struct ring_buffer *rb)
{
	if (!rb)
		return;
	
	free(rb->data);
	free(rb);
}

bool ring_buffer_enqueue(struct ring_buffer *rb, void *item)
{
	size_t head, tail, next_head, size;
	
	if (!rb || !item)
		return false;
	
	/* Load current positions with acquire semantics */
	head = atomic_load_explicit(&rb->head, memory_order_acquire);
	tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
	
	/* Calculate next head position */
	next_head = (head + 1) & rb->mask;
	
	/* Check if buffer is full */
	if (next_head == tail) {
		atomic_fetch_add_explicit(&rb->overflow_count, 1, memory_order_relaxed);
		return false;
	}
	
	/* Store item */
	rb->data[head] = item;
	
	/* Update head with release semantics to ensure item is visible */
	atomic_store_explicit(&rb->head, next_head, memory_order_release);
	
	/* Update statistics */
	atomic_fetch_add_explicit(&rb->enqueue_count, 1, memory_order_relaxed);
	
	/* Update peak usage */
	size = (head >= tail) ? (head - tail) : (rb->capacity - tail + head);
	size++;  /* Include the item we just added */
	
	unsigned long current_peak = atomic_load_explicit(&rb->peak_usage, memory_order_relaxed);
	while (size > current_peak) {
		if (atomic_compare_exchange_weak_explicit(&rb->peak_usage, &current_peak, size,
		                                          memory_order_relaxed, memory_order_relaxed))
			break;
	}
	
	return true;
}

void *ring_buffer_dequeue(struct ring_buffer *rb)
{
	size_t head, tail, next_tail;
	void *item;
	
	if (!rb)
		return NULL;
	
	/* Load current positions with acquire semantics */
	head = atomic_load_explicit(&rb->head, memory_order_acquire);
	tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
	
	/* Check if buffer is empty */
	if (head == tail)
		return NULL;
	
	/* Get item */
	item = rb->data[tail];
	
	/* Calculate next tail position */
	next_tail = (tail + 1) & rb->mask;
	
	/* Update tail with release semantics */
	atomic_store_explicit(&rb->tail, next_tail, memory_order_release);
	
	/* Update statistics */
	atomic_fetch_add_explicit(&rb->dequeue_count, 1, memory_order_relaxed);
	
	return item;
}

bool ring_buffer_is_empty(struct ring_buffer *rb)
{
	size_t head, tail;
	
	if (!rb)
		return true;
	
	head = atomic_load_explicit(&rb->head, memory_order_acquire);
	tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
	
	return head == tail;
}

bool ring_buffer_is_full(struct ring_buffer *rb)
{
	size_t head, tail, next_head;
	
	if (!rb)
		return false;
	
	head = atomic_load_explicit(&rb->head, memory_order_acquire);
	tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
	
	next_head = (head + 1) & rb->mask;
	
	return next_head == tail;
}

size_t ring_buffer_size(struct ring_buffer *rb)
{
	size_t head, tail;
	
	if (!rb)
		return 0;
	
	head = atomic_load_explicit(&rb->head, memory_order_acquire);
	tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
	
	if (head >= tail)
		return head - tail;
	else
		return rb->capacity - tail + head;
}

size_t ring_buffer_capacity(struct ring_buffer *rb)
{
	if (!rb)
		return 0;
	
	return rb->capacity - 1;  /* One slot is reserved for full/empty detection */
}

void ring_buffer_stats(struct ring_buffer *rb,
                       unsigned long *enqueued,
                       unsigned long *dequeued,
                       unsigned long *overflows,
                       unsigned long *peak)
{
	if (!rb)
		return;
	
	if (enqueued)
		*enqueued = atomic_load_explicit(&rb->enqueue_count, memory_order_relaxed);
	if (dequeued)
		*dequeued = atomic_load_explicit(&rb->dequeue_count, memory_order_relaxed);
	if (overflows)
		*overflows = atomic_load_explicit(&rb->overflow_count, memory_order_relaxed);
	if (peak)
		*peak = atomic_load_explicit(&rb->peak_usage, memory_order_relaxed);
}
