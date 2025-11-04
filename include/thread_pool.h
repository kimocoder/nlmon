/* thread_pool.h - Worker thread pool for parallel event processing
 *
 * Provides a configurable thread pool with work queue and priority support
 * for efficient parallel processing of network events.
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>
#include <stdbool.h>

/* Work item priority levels */
enum work_priority {
	PRIORITY_LOW = 0,
	PRIORITY_NORMAL = 1,
	PRIORITY_HIGH = 2,
	PRIORITY_MAX = 3
};

/* Work function signature */
typedef void (*work_func_t)(void *arg);

/* Thread pool structure (opaque) */
struct thread_pool;

/**
 * thread_pool_create() - Create a new thread pool
 * @num_threads: Number of worker threads (0 = auto-detect CPU count)
 * @queue_size: Maximum work queue size (0 = unlimited)
 *
 * Returns: Pointer to thread pool or NULL on error
 */
struct thread_pool *thread_pool_create(size_t num_threads, size_t queue_size);

/**
 * thread_pool_destroy() - Destroy thread pool
 * @pool: Thread pool to destroy
 * @wait: If true, wait for all pending work to complete
 *
 * If wait is false, pending work items are discarded.
 */
void thread_pool_destroy(struct thread_pool *pool, bool wait);

/**
 * thread_pool_submit() - Submit work to thread pool
 * @pool: Thread pool
 * @func: Work function to execute
 * @arg: Argument to pass to work function
 * @priority: Work priority level
 *
 * Returns: true on success, false if queue is full
 */
bool thread_pool_submit(struct thread_pool *pool, work_func_t func, 
                        void *arg, enum work_priority priority);

/**
 * thread_pool_wait() - Wait for all pending work to complete
 * @pool: Thread pool
 */
void thread_pool_wait(struct thread_pool *pool);

/**
 * thread_pool_get_thread_count() - Get number of worker threads
 * @pool: Thread pool
 *
 * Returns: Number of worker threads
 */
size_t thread_pool_get_thread_count(struct thread_pool *pool);

/**
 * thread_pool_get_pending_count() - Get number of pending work items
 * @pool: Thread pool
 *
 * Returns: Number of pending work items
 */
size_t thread_pool_get_pending_count(struct thread_pool *pool);

/**
 * thread_pool_stats() - Get thread pool statistics
 * @pool: Thread pool
 * @submitted: Output for total submitted work items
 * @completed: Output for total completed work items
 * @rejected: Output for rejected work items (queue full)
 */
void thread_pool_stats(struct thread_pool *pool,
                       unsigned long *submitted,
                       unsigned long *completed,
                       unsigned long *rejected);

#endif /* THREAD_POOL_H */
