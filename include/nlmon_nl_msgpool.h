/* nlmon_nl_msgpool.h - Message buffer pool for netlink
 *
 * Memory pool for netlink message buffers to reduce allocation overhead.
 */

#ifndef NLMON_NL_MSGPOOL_H
#define NLMON_NL_MSGPOOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Message pool handle (opaque) */
struct nlmon_nl_msgpool;

/* Pool statistics */
struct nlmon_msgpool_stats {
	size_t pool_size;          /* Total pool size */
	size_t buffer_size;        /* Size of each buffer */
	size_t allocated_count;    /* Currently allocated buffers */
	size_t free_count;         /* Currently free buffers */
	size_t alloc_requests;     /* Total allocation requests */
	size_t pool_hits;          /* Allocations served from pool */
	size_t pool_misses;        /* Allocations that bypassed pool */
	double hit_rate;           /* Pool hit rate percentage */
};

/**
 * nlmon_nl_msgpool_create() - Create message pool
 * @pool_size: Number of buffers in pool (0 for default)
 * @buffer_size: Size of each buffer (0 for default)
 *
 * Creates a pool of pre-allocated message buffers.
 *
 * Returns: Pool handle or NULL on error
 */
struct nlmon_nl_msgpool *nlmon_nl_msgpool_create(size_t pool_size, size_t buffer_size);

/**
 * nlmon_nl_msgpool_destroy() - Destroy message pool
 * @pool: Pool handle
 *
 * Frees all pool resources. Buffers must be returned before calling.
 */
void nlmon_nl_msgpool_destroy(struct nlmon_nl_msgpool *pool);

/**
 * nlmon_nl_msgpool_alloc() - Allocate buffer from pool
 * @pool: Pool handle
 * @size: Requested size
 *
 * Allocates a buffer from the pool. If pool is exhausted or size is too large,
 * falls back to malloc().
 *
 * Returns: Buffer pointer or NULL on error
 */
void *nlmon_nl_msgpool_alloc(struct nlmon_nl_msgpool *pool, size_t size);

/**
 * nlmon_nl_msgpool_free() - Free buffer back to pool
 * @pool: Pool handle
 * @ptr: Buffer pointer
 *
 * Returns buffer to pool if it came from pool, otherwise calls free().
 */
void nlmon_nl_msgpool_free(struct nlmon_nl_msgpool *pool, void *ptr);

/**
 * nlmon_nl_msgpool_get_stats() - Get pool statistics
 * @pool: Pool handle
 * @stats: Output for statistics
 *
 * Retrieves current pool statistics.
 */
void nlmon_nl_msgpool_get_stats(struct nlmon_nl_msgpool *pool,
                                struct nlmon_msgpool_stats *stats);

/**
 * nlmon_nl_msgpool_reset_stats() - Reset pool statistics
 * @pool: Pool handle
 *
 * Resets allocation counters.
 */
void nlmon_nl_msgpool_reset_stats(struct nlmon_nl_msgpool *pool);

/**
 * nlmon_nl_msgpool_resize() - Resize buffer
 * @pool: Pool handle
 * @ptr: Buffer pointer
 * @new_size: New size
 *
 * Attempts to resize a pooled buffer. Only succeeds if new size fits
 * in existing capacity.
 *
 * Returns: 0 on success, negative error code on failure
 */
int nlmon_nl_msgpool_resize(struct nlmon_nl_msgpool *pool, void *ptr, size_t new_size);

/**
 * nlmon_nl_msgpool_get_capacity() - Get buffer capacity
 * @pool: Pool handle
 * @ptr: Buffer pointer
 *
 * Returns: Buffer capacity or 0 if not found
 */
size_t nlmon_nl_msgpool_get_capacity(struct nlmon_nl_msgpool *pool, void *ptr);

/**
 * nlmon_nl_msgpool_preallocate() - Preallocate all buffers
 * @pool: Pool handle
 *
 * Ensures all pool buffers are allocated (warm up).
 *
 * Returns: 0 on success, negative error code on failure
 */
int nlmon_nl_msgpool_preallocate(struct nlmon_nl_msgpool *pool);

/**
 * nlmon_nl_msgpool_shrink() - Shrink pool
 * @pool: Pool handle
 * @target_free: Target number of free buffers
 *
 * Attempts to reduce pool size (not currently implemented).
 *
 * Returns: 0 on success, negative error code on failure
 */
int nlmon_nl_msgpool_shrink(struct nlmon_nl_msgpool *pool, size_t target_free);

/**
 * nlmon_nl_msgpool_is_pooled() - Check if pointer is from pool
 * @pool: Pool handle
 * @ptr: Buffer pointer
 *
 * Returns: true if buffer is from pool
 */
bool nlmon_nl_msgpool_is_pooled(struct nlmon_nl_msgpool *pool, void *ptr);

/**
 * nlmon_nl_msgpool_get_utilization() - Get pool utilization
 * @pool: Pool handle
 *
 * Returns: Utilization percentage (0-100)
 */
double nlmon_nl_msgpool_get_utilization(struct nlmon_nl_msgpool *pool);

#endif /* NLMON_NL_MSGPOOL_H */

