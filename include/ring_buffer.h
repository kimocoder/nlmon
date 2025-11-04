/* ring_buffer.h - Lock-free ring buffer for event processing
 *
 * This implements a single-producer, single-consumer (SPSC) lock-free
 * ring buffer using atomic operations for high-performance event queuing.
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

/* Ring buffer structure */
struct ring_buffer {
	void **data;                    /* Array of pointers to events */
	size_t capacity;                /* Buffer capacity (power of 2) */
	size_t mask;                    /* Capacity - 1 (for fast modulo) */
	atomic_size_t head;             /* Write position */
	atomic_size_t tail;             /* Read position */
	
	/* Statistics */
	atomic_ulong enqueue_count;     /* Total enqueued */
	atomic_ulong dequeue_count;     /* Total dequeued */
	atomic_ulong overflow_count;    /* Dropped due to full buffer */
	atomic_ulong peak_usage;        /* Peak buffer usage */
};

/**
 * ring_buffer_create() - Create a new ring buffer
 * @capacity: Buffer capacity (will be rounded up to next power of 2)
 *
 * Returns: Pointer to ring buffer or NULL on error
 */
struct ring_buffer *ring_buffer_create(size_t capacity);

/**
 * ring_buffer_destroy() - Destroy ring buffer
 * @rb: Ring buffer to destroy
 *
 * Note: Does not free the data items, caller must drain buffer first
 */
void ring_buffer_destroy(struct ring_buffer *rb);

/**
 * ring_buffer_enqueue() - Add item to ring buffer
 * @rb: Ring buffer
 * @item: Item to add (must not be NULL)
 *
 * Returns: true on success, false if buffer is full
 */
bool ring_buffer_enqueue(struct ring_buffer *rb, void *item);

/**
 * ring_buffer_dequeue() - Remove item from ring buffer
 * @rb: Ring buffer
 *
 * Returns: Item pointer or NULL if buffer is empty
 */
void *ring_buffer_dequeue(struct ring_buffer *rb);

/**
 * ring_buffer_is_empty() - Check if buffer is empty
 * @rb: Ring buffer
 *
 * Returns: true if empty
 */
bool ring_buffer_is_empty(struct ring_buffer *rb);

/**
 * ring_buffer_is_full() - Check if buffer is full
 * @rb: Ring buffer
 *
 * Returns: true if full
 */
bool ring_buffer_is_full(struct ring_buffer *rb);

/**
 * ring_buffer_size() - Get current number of items in buffer
 * @rb: Ring buffer
 *
 * Returns: Number of items
 */
size_t ring_buffer_size(struct ring_buffer *rb);

/**
 * ring_buffer_capacity() - Get buffer capacity
 * @rb: Ring buffer
 *
 * Returns: Buffer capacity
 */
size_t ring_buffer_capacity(struct ring_buffer *rb);

/**
 * ring_buffer_stats() - Get buffer statistics
 * @rb: Ring buffer
 * @enqueued: Output for total enqueued count
 * @dequeued: Output for total dequeued count
 * @overflows: Output for overflow count
 * @peak: Output for peak usage
 */
void ring_buffer_stats(struct ring_buffer *rb, 
                       unsigned long *enqueued,
                       unsigned long *dequeued,
                       unsigned long *overflows,
                       unsigned long *peak);

#endif /* RING_BUFFER_H */
