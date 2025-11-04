/* event_processor.c - Enhanced event processing engine implementation
 *
 * Integrates ring buffer, thread pool, rate limiting, and object pooling
 * for high-performance parallel event processing.
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <unistd.h>
#include "event_processor.h"
#include "ring_buffer.h"
#include "thread_pool.h"
#include "rate_limiter.h"

/* Object pool for event structures */
struct event_pool {
	struct nlmon_event **objects;
	size_t capacity;
	atomic_size_t head;
	atomic_size_t tail;
	pthread_mutex_t mutex;
};

/* Event handler entry */
struct event_handler_entry {
	int id;
	event_handler_t handler;
	void *ctx;
	struct event_handler_entry *next;
};

/* Event processor structure */
struct event_processor {
	struct ring_buffer *ring_buffer;
	struct thread_pool *thread_pool;
	struct rate_limiter_map *rate_limiter;
	struct event_pool *event_pool;
	
	/* Event handlers */
	struct event_handler_entry *handlers;
	pthread_mutex_t handlers_mutex;
	int next_handler_id;
	
	/* Configuration */
	struct event_processor_config config;
	
	/* State */
	atomic_bool running;
	pthread_t dispatcher_thread;
	
	/* Statistics */
	atomic_ulong submitted_count;
	atomic_ulong processed_count;
	atomic_ulong dropped_count;
	atomic_ulong rate_limited_count;
	atomic_ullong sequence_counter;
};

/* Object pool functions */
static struct event_pool *event_pool_create(size_t capacity)
{
	struct event_pool *pool;
	size_t i;
	
	pool = calloc(1, sizeof(*pool));
	if (!pool)
		return NULL;
	
	pool->capacity = capacity;
	pool->objects = calloc(capacity, sizeof(struct nlmon_event *));
	if (!pool->objects) {
		free(pool);
		return NULL;
	}
	
	/* Pre-allocate event objects */
	for (i = 0; i < capacity; i++) {
		pool->objects[i] = calloc(1, sizeof(struct nlmon_event));
		if (!pool->objects[i]) {
			/* Cleanup on failure */
			for (size_t j = 0; j < i; j++)
				free(pool->objects[j]);
			free(pool->objects);
			free(pool);
			return NULL;
		}
	}
	
	atomic_init(&pool->head, 0);
	atomic_init(&pool->tail, capacity);
	
	if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
		for (i = 0; i < capacity; i++)
			free(pool->objects[i]);
		free(pool->objects);
		free(pool);
		return NULL;
	}
	
	return pool;
}

static void event_pool_destroy(struct event_pool *pool)
{
	size_t i;
	
	if (!pool)
		return;
	
	for (i = 0; i < pool->capacity; i++)
		free(pool->objects[i]);
	
	free(pool->objects);
	pthread_mutex_destroy(&pool->mutex);
	free(pool);
}

static struct nlmon_event *event_pool_alloc(struct event_pool *pool)
{
	struct nlmon_event *event = NULL;
	size_t tail;
	
	if (!pool)
		return calloc(1, sizeof(struct nlmon_event));
	
	pthread_mutex_lock(&pool->mutex);
	
	tail = atomic_load_explicit(&pool->tail, memory_order_acquire);
	if (tail > 0) {
		tail--;
		event = pool->objects[tail];
		atomic_store_explicit(&pool->tail, tail, memory_order_release);
	}
	
	pthread_mutex_unlock(&pool->mutex);
	
	if (!event)
		event = calloc(1, sizeof(struct nlmon_event));
	
	return event;
}

static void event_pool_free(struct event_pool *pool, struct nlmon_event *event)
{
	size_t tail;
	
	if (!event)
		return;
	
	if (!pool) {
		free(event);
		return;
	}
	
	/* Clear event data */
	if (event->data) {
		free(event->data);
		event->data = NULL;
	}
	memset(event, 0, sizeof(*event));
	
	pthread_mutex_lock(&pool->mutex);
	
	tail = atomic_load_explicit(&pool->tail, memory_order_acquire);
	if (tail < pool->capacity) {
		pool->objects[tail] = event;
		tail++;
		atomic_store_explicit(&pool->tail, tail, memory_order_release);
	} else {
		/* Pool full, just free it */
		free(event);
	}
	
	pthread_mutex_unlock(&pool->mutex);
}

static size_t event_pool_usage(struct event_pool *pool)
{
	size_t tail;
	
	if (!pool)
		return 0;
	
	tail = atomic_load_explicit(&pool->tail, memory_order_acquire);
	return pool->capacity - tail;
}

/* Worker function for processing events */
static void process_event_work(void *arg)
{
	struct event_processor *ep = arg;
	struct nlmon_event *event;
	struct event_handler_entry *handler;
	
	/* Dequeue event from ring buffer */
	event = ring_buffer_dequeue(ep->ring_buffer);
	if (!event)
		return;
	
	/* Call all registered handlers */
	pthread_mutex_lock(&ep->handlers_mutex);
	for (handler = ep->handlers; handler; handler = handler->next) {
		if (handler->handler)
			handler->handler(event, handler->ctx);
	}
	pthread_mutex_unlock(&ep->handlers_mutex);
	
	/* Update statistics */
	atomic_fetch_add_explicit(&ep->processed_count, 1, memory_order_relaxed);
	
	/* Return event to pool */
	event_pool_free(ep->event_pool, event);
}

/* Dispatcher thread - moves events from ring buffer to thread pool */
static void *dispatcher_thread_func(void *arg)
{
	struct event_processor *ep = arg;
	
	while (atomic_load_explicit(&ep->running, memory_order_acquire)) {
		/* Check if there are events in the ring buffer */
		if (!ring_buffer_is_empty(ep->ring_buffer)) {
			/* Submit work to thread pool */
			if (!thread_pool_submit(ep->thread_pool, process_event_work,
			                       ep, PRIORITY_NORMAL)) {
				/* Thread pool queue full, wait a bit */
				usleep(1000);
			}
		} else {
			/* No events, sleep briefly */
			usleep(100);
		}
	}
	
	return NULL;
}

struct event_processor *event_processor_create(struct event_processor_config *config)
{
	struct event_processor *ep;
	
	if (!config)
		return NULL;
	
	ep = calloc(1, sizeof(*ep));
	if (!ep)
		return NULL;
	
	ep->config = *config;
	
	/* Set defaults */
	if (ep->config.ring_buffer_size == 0)
		ep->config.ring_buffer_size = 10000;
	if (ep->config.rate_burst == 0)
		ep->config.rate_burst = 100;
	if (ep->config.object_pool_size == 0)
		ep->config.object_pool_size = 1000;
	
	/* Create ring buffer */
	ep->ring_buffer = ring_buffer_create(ep->config.ring_buffer_size);
	if (!ep->ring_buffer) {
		free(ep);
		return NULL;
	}
	
	/* Create thread pool */
	ep->thread_pool = thread_pool_create(ep->config.thread_pool_size,
	                                     ep->config.work_queue_size);
	if (!ep->thread_pool) {
		ring_buffer_destroy(ep->ring_buffer);
		free(ep);
		return NULL;
	}
	
	/* Create rate limiter if enabled */
	if (ep->config.rate_limit > 0) {
		ep->rate_limiter = rate_limiter_map_create(ep->config.rate_limit,
		                                           ep->config.rate_burst);
		if (!ep->rate_limiter) {
			thread_pool_destroy(ep->thread_pool, false);
			ring_buffer_destroy(ep->ring_buffer);
			free(ep);
			return NULL;
		}
	}
	
	/* Create object pool if enabled */
	if (ep->config.enable_object_pool) {
		ep->event_pool = event_pool_create(ep->config.object_pool_size);
		if (!ep->event_pool) {
			if (ep->rate_limiter)
				rate_limiter_map_destroy(ep->rate_limiter);
			thread_pool_destroy(ep->thread_pool, false);
			ring_buffer_destroy(ep->ring_buffer);
			free(ep);
			return NULL;
		}
	}
	
	/* Initialize handlers mutex */
	if (pthread_mutex_init(&ep->handlers_mutex, NULL) != 0) {
		if (ep->event_pool)
			event_pool_destroy(ep->event_pool);
		if (ep->rate_limiter)
			rate_limiter_map_destroy(ep->rate_limiter);
		thread_pool_destroy(ep->thread_pool, false);
		ring_buffer_destroy(ep->ring_buffer);
		free(ep);
		return NULL;
	}
	
	/* Initialize atomics */
	atomic_init(&ep->running, true);
	atomic_init(&ep->submitted_count, 0);
	atomic_init(&ep->processed_count, 0);
	atomic_init(&ep->dropped_count, 0);
	atomic_init(&ep->rate_limited_count, 0);
	atomic_init(&ep->sequence_counter, 0);
	
	ep->next_handler_id = 1;
	
	/* Start dispatcher thread */
	if (pthread_create(&ep->dispatcher_thread, NULL, dispatcher_thread_func, ep) != 0) {
		pthread_mutex_destroy(&ep->handlers_mutex);
		if (ep->event_pool)
			event_pool_destroy(ep->event_pool);
		if (ep->rate_limiter)
			rate_limiter_map_destroy(ep->rate_limiter);
		thread_pool_destroy(ep->thread_pool, false);
		ring_buffer_destroy(ep->ring_buffer);
		free(ep);
		return NULL;
	}
	
	return ep;
}

void event_processor_destroy(struct event_processor *ep, bool wait)
{
	struct event_handler_entry *handler, *next;
	
	if (!ep)
		return;
	
	/* Stop dispatcher */
	atomic_store_explicit(&ep->running, false, memory_order_release);
	pthread_join(ep->dispatcher_thread, NULL);
	
	/* Wait for pending work if requested */
	if (wait) {
		thread_pool_wait(ep->thread_pool);
	}
	
	/* Cleanup */
	thread_pool_destroy(ep->thread_pool, wait);
	
	/* Drain ring buffer */
	while (!ring_buffer_is_empty(ep->ring_buffer)) {
		struct nlmon_event *event = ring_buffer_dequeue(ep->ring_buffer);
		event_pool_free(ep->event_pool, event);
	}
	
	ring_buffer_destroy(ep->ring_buffer);
	
	if (ep->rate_limiter)
		rate_limiter_map_destroy(ep->rate_limiter);
	
	if (ep->event_pool)
		event_pool_destroy(ep->event_pool);
	
	/* Free handlers */
	handler = ep->handlers;
	while (handler) {
		next = handler->next;
		free(handler);
		handler = next;
	}
	
	pthread_mutex_destroy(&ep->handlers_mutex);
	free(ep);
}

int event_processor_register_handler(struct event_processor *ep,
                                     event_handler_t handler, void *ctx)
{
	struct event_handler_entry *entry;
	int id;
	
	if (!ep || !handler)
		return -1;
	
	entry = malloc(sizeof(*entry));
	if (!entry)
		return -1;
	
	pthread_mutex_lock(&ep->handlers_mutex);
	
	id = ep->next_handler_id++;
	entry->id = id;
	entry->handler = handler;
	entry->ctx = ctx;
	entry->next = ep->handlers;
	ep->handlers = entry;
	
	pthread_mutex_unlock(&ep->handlers_mutex);
	
	return id;
}

void event_processor_unregister_handler(struct event_processor *ep, int handler_id)
{
	struct event_handler_entry *entry, *prev;
	
	if (!ep)
		return;
	
	pthread_mutex_lock(&ep->handlers_mutex);
	
	prev = NULL;
	for (entry = ep->handlers; entry; entry = entry->next) {
		if (entry->id == handler_id) {
			if (prev)
				prev->next = entry->next;
			else
				ep->handlers = entry->next;
			free(entry);
			break;
		}
		prev = entry;
	}
	
	pthread_mutex_unlock(&ep->handlers_mutex);
}

bool event_processor_submit(struct event_processor *ep, struct nlmon_event *event)
{
	struct nlmon_event *queued_event;
	
	if (!ep || !event)
		return false;
	
	/* Check rate limit */
	if (ep->rate_limiter) {
		if (!rate_limiter_map_allow(ep->rate_limiter, event->event_type)) {
			atomic_fetch_add_explicit(&ep->rate_limited_count, 1, memory_order_relaxed);
			return false;
		}
	}
	
	/* Allocate event from pool or copy */
	queued_event = event_pool_alloc(ep->event_pool);
	if (!queued_event) {
		atomic_fetch_add_explicit(&ep->dropped_count, 1, memory_order_relaxed);
		return false;
	}
	
	/* Copy event data */
	memcpy(queued_event, event, sizeof(*queued_event));
	
	/* Copy event-specific data if present */
	if (event->data && event->data_size > 0) {
		queued_event->data = malloc(event->data_size);
		if (queued_event->data) {
			memcpy(queued_event->data, event->data, event->data_size);
		} else {
			event_pool_free(ep->event_pool, queued_event);
			atomic_fetch_add_explicit(&ep->dropped_count, 1, memory_order_relaxed);
			return false;
		}
	}
	
	/* Assign sequence number */
	queued_event->sequence = atomic_fetch_add_explicit(&ep->sequence_counter, 1,
	                                                   memory_order_relaxed);
	
	/* Enqueue to ring buffer */
	if (!ring_buffer_enqueue(ep->ring_buffer, queued_event)) {
		event_pool_free(ep->event_pool, queued_event);
		atomic_fetch_add_explicit(&ep->dropped_count, 1, memory_order_relaxed);
		return false;
	}
	
	atomic_fetch_add_explicit(&ep->submitted_count, 1, memory_order_relaxed);
	return true;
}

bool event_processor_set_rate_limit(struct event_processor *ep,
                                    uint32_t event_type,
                                    double rate, size_t burst)
{
	if (!ep || !ep->rate_limiter)
		return false;
	
	return rate_limiter_map_set(ep->rate_limiter, event_type, rate, burst);
}

void event_processor_wait(struct event_processor *ep)
{
	if (!ep)
		return;
	
	/* Wait for ring buffer to drain */
	while (!ring_buffer_is_empty(ep->ring_buffer))
		usleep(1000);
	
	/* Wait for thread pool */
	thread_pool_wait(ep->thread_pool);
}

void event_processor_stats(struct event_processor *ep,
                          unsigned long *submitted,
                          unsigned long *processed,
                          unsigned long *dropped,
                          unsigned long *rate_limited,
                          size_t *queue_size,
                          size_t *pool_usage)
{
	if (!ep)
		return;
	
	if (submitted)
		*submitted = atomic_load_explicit(&ep->submitted_count, memory_order_relaxed);
	if (processed)
		*processed = atomic_load_explicit(&ep->processed_count, memory_order_relaxed);
	if (dropped)
		*dropped = atomic_load_explicit(&ep->dropped_count, memory_order_relaxed);
	if (rate_limited)
		*rate_limited = atomic_load_explicit(&ep->rate_limited_count, memory_order_relaxed);
	if (queue_size)
		*queue_size = ring_buffer_size(ep->ring_buffer);
	if (pool_usage)
		*pool_usage = event_pool_usage(ep->event_pool);
}
