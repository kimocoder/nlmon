/* thread_pool.c - Worker thread pool implementation
 *
 * Implements a thread pool with configurable size, work queue with priority
 * support, and graceful shutdown mechanism.
 */

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include "thread_pool.h"

/* Work item structure */
struct work_item {
	work_func_t func;
	void *arg;
	enum work_priority priority;
	struct work_item *next;
};

/* Thread pool structure */
struct thread_pool {
	pthread_t *threads;
	size_t num_threads;
	
	/* Work queue (priority-based) */
	struct work_item *queue_head[PRIORITY_MAX];
	struct work_item *queue_tail[PRIORITY_MAX];
	size_t queue_size;
	size_t max_queue_size;
	
	/* Synchronization */
	pthread_mutex_t queue_mutex;
	pthread_cond_t work_available;
	pthread_cond_t work_done;
	
	/* State */
	atomic_bool shutdown;
	atomic_bool immediate_shutdown;
	atomic_size_t active_threads;
	
	/* Statistics */
	atomic_ulong submitted_count;
	atomic_ulong completed_count;
	atomic_ulong rejected_count;
};

/* Get number of CPU cores */
static size_t get_cpu_count(void)
{
	long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	if (nprocs <= 0)
		return 1;
	return (size_t)nprocs;
}

/* Worker thread function */
static void *worker_thread(void *arg)
{
	struct thread_pool *pool = arg;
	struct work_item *work;
	int priority;
	
	while (1) {
		pthread_mutex_lock(&pool->queue_mutex);
		
		/* Wait for work or shutdown */
		while (pool->queue_size == 0 && 
		       !atomic_load_explicit(&pool->shutdown, memory_order_acquire)) {
			pthread_cond_wait(&pool->work_available, &pool->queue_mutex);
		}
		
		/* Check for shutdown */
		if (atomic_load_explicit(&pool->immediate_shutdown, memory_order_acquire)) {
			pthread_mutex_unlock(&pool->queue_mutex);
			break;
		}
		
		if (atomic_load_explicit(&pool->shutdown, memory_order_acquire) && 
		    pool->queue_size == 0) {
			pthread_mutex_unlock(&pool->queue_mutex);
			break;
		}
		
		/* Get work item from highest priority queue */
		work = NULL;
		for (priority = PRIORITY_MAX - 1; priority >= 0; priority--) {
			if (pool->queue_head[priority]) {
				work = pool->queue_head[priority];
				pool->queue_head[priority] = work->next;
				if (!pool->queue_head[priority])
					pool->queue_tail[priority] = NULL;
				pool->queue_size--;
				break;
			}
		}
		
		pthread_mutex_unlock(&pool->queue_mutex);
		
		/* Execute work */
		if (work) {
			atomic_fetch_add_explicit(&pool->active_threads, 1, memory_order_relaxed);
			
			work->func(work->arg);
			free(work);
			
			atomic_fetch_sub_explicit(&pool->active_threads, 1, memory_order_relaxed);
			atomic_fetch_add_explicit(&pool->completed_count, 1, memory_order_relaxed);
			
			/* Signal work done */
			pthread_mutex_lock(&pool->queue_mutex);
			pthread_cond_broadcast(&pool->work_done);
			pthread_mutex_unlock(&pool->queue_mutex);
		}
	}
	
	return NULL;
}

struct thread_pool *thread_pool_create(size_t num_threads, size_t queue_size)
{
	struct thread_pool *pool;
	size_t i;
	
	pool = calloc(1, sizeof(*pool));
	if (!pool)
		return NULL;
	
	/* Determine thread count */
	if (num_threads == 0)
		num_threads = get_cpu_count();
	
	pool->num_threads = num_threads;
	pool->max_queue_size = queue_size;
	
	/* Initialize synchronization primitives */
	if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
		free(pool);
		return NULL;
	}
	
	if (pthread_cond_init(&pool->work_available, NULL) != 0) {
		pthread_mutex_destroy(&pool->queue_mutex);
		free(pool);
		return NULL;
	}
	
	if (pthread_cond_init(&pool->work_done, NULL) != 0) {
		pthread_cond_destroy(&pool->work_available);
		pthread_mutex_destroy(&pool->queue_mutex);
		free(pool);
		return NULL;
	}
	
	/* Initialize atomics */
	atomic_init(&pool->shutdown, false);
	atomic_init(&pool->immediate_shutdown, false);
	atomic_init(&pool->active_threads, 0);
	atomic_init(&pool->submitted_count, 0);
	atomic_init(&pool->completed_count, 0);
	atomic_init(&pool->rejected_count, 0);
	
	/* Create worker threads */
	pool->threads = calloc(num_threads, sizeof(pthread_t));
	if (!pool->threads) {
		pthread_cond_destroy(&pool->work_done);
		pthread_cond_destroy(&pool->work_available);
		pthread_mutex_destroy(&pool->queue_mutex);
		free(pool);
		return NULL;
	}
	
	for (i = 0; i < num_threads; i++) {
		if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
			/* Cleanup on failure */
			atomic_store_explicit(&pool->immediate_shutdown, true, memory_order_release);
			pthread_cond_broadcast(&pool->work_available);
			
			for (size_t j = 0; j < i; j++)
				pthread_join(pool->threads[j], NULL);
			
			free(pool->threads);
			pthread_cond_destroy(&pool->work_done);
			pthread_cond_destroy(&pool->work_available);
			pthread_mutex_destroy(&pool->queue_mutex);
			free(pool);
			return NULL;
		}
	}
	
	return pool;
}

void thread_pool_destroy(struct thread_pool *pool, bool wait)
{
	size_t i;
	struct work_item *work, *next;
	
	if (!pool)
		return;
	
	/* Signal shutdown */
	if (wait)
		atomic_store_explicit(&pool->shutdown, true, memory_order_release);
	else
		atomic_store_explicit(&pool->immediate_shutdown, true, memory_order_release);
	
	/* Wake up all threads */
	pthread_mutex_lock(&pool->queue_mutex);
	pthread_cond_broadcast(&pool->work_available);
	pthread_mutex_unlock(&pool->queue_mutex);
	
	/* Wait for threads to finish */
	for (i = 0; i < pool->num_threads; i++)
		pthread_join(pool->threads[i], NULL);
	
	/* Free remaining work items */
	for (i = 0; i < PRIORITY_MAX; i++) {
		work = pool->queue_head[i];
		while (work) {
			next = work->next;
			free(work);
			work = next;
		}
	}
	
	/* Cleanup */
	free(pool->threads);
	pthread_cond_destroy(&pool->work_done);
	pthread_cond_destroy(&pool->work_available);
	pthread_mutex_destroy(&pool->queue_mutex);
	free(pool);
}

bool thread_pool_submit(struct thread_pool *pool, work_func_t func,
                        void *arg, enum work_priority priority)
{
	struct work_item *work;
	
	if (!pool || !func)
		return false;
	
	if (priority >= PRIORITY_MAX)
		priority = PRIORITY_NORMAL;
	
	/* Check if shutting down */
	if (atomic_load_explicit(&pool->shutdown, memory_order_acquire))
		return false;
	
	/* Check queue size limit */
	pthread_mutex_lock(&pool->queue_mutex);
	if (pool->max_queue_size > 0 && pool->queue_size >= pool->max_queue_size) {
		pthread_mutex_unlock(&pool->queue_mutex);
		atomic_fetch_add_explicit(&pool->rejected_count, 1, memory_order_relaxed);
		return false;
	}
	pthread_mutex_unlock(&pool->queue_mutex);
	
	/* Create work item */
	work = malloc(sizeof(*work));
	if (!work) {
		atomic_fetch_add_explicit(&pool->rejected_count, 1, memory_order_relaxed);
		return false;
	}
	
	work->func = func;
	work->arg = arg;
	work->priority = priority;
	work->next = NULL;
	
	/* Add to queue */
	pthread_mutex_lock(&pool->queue_mutex);
	
	if (pool->queue_tail[priority])
		pool->queue_tail[priority]->next = work;
	else
		pool->queue_head[priority] = work;
	
	pool->queue_tail[priority] = work;
	pool->queue_size++;
	
	atomic_fetch_add_explicit(&pool->submitted_count, 1, memory_order_relaxed);
	
	/* Signal worker threads */
	pthread_cond_signal(&pool->work_available);
	pthread_mutex_unlock(&pool->queue_mutex);
	
	return true;
}

void thread_pool_wait(struct thread_pool *pool)
{
	if (!pool)
		return;
	
	pthread_mutex_lock(&pool->queue_mutex);
	while (pool->queue_size > 0 || 
	       atomic_load_explicit(&pool->active_threads, memory_order_relaxed) > 0) {
		pthread_cond_wait(&pool->work_done, &pool->queue_mutex);
	}
	pthread_mutex_unlock(&pool->queue_mutex);
}

size_t thread_pool_get_thread_count(struct thread_pool *pool)
{
	if (!pool)
		return 0;
	return pool->num_threads;
}

size_t thread_pool_get_pending_count(struct thread_pool *pool)
{
	size_t count;
	
	if (!pool)
		return 0;
	
	pthread_mutex_lock(&pool->queue_mutex);
	count = pool->queue_size;
	pthread_mutex_unlock(&pool->queue_mutex);
	
	return count;
}

void thread_pool_stats(struct thread_pool *pool,
                       unsigned long *submitted,
                       unsigned long *completed,
                       unsigned long *rejected)
{
	if (!pool)
		return;
	
	if (submitted)
		*submitted = atomic_load_explicit(&pool->submitted_count, memory_order_relaxed);
	if (completed)
		*completed = atomic_load_explicit(&pool->completed_count, memory_order_relaxed);
	if (rejected)
		*rejected = atomic_load_explicit(&pool->rejected_count, memory_order_relaxed);
}
