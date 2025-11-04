/* memory_tracker.c - Memory usage tracking implementation */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "memory_tracker.h"

/* Allocation entry (for tracking) */
struct alloc_entry {
	void *ptr;
	size_t size;
	struct alloc_entry *next;
};

/* Memory tracker structure */
struct memory_tracker {
	bool tracking_enabled;
	
	/* Allocation tracking */
	struct alloc_entry *allocations;
	pthread_mutex_t mutex;
	
	/* System stats */
	atomic_size_t rss;
	atomic_size_t vms;
	atomic_size_t peak_rss;
	
	/* Tracked stats */
	atomic_size_t allocated;
	atomic_size_t freed;
	atomic_size_t current_usage;
	atomic_size_t peak_usage;
	atomic_ulong alloc_count;
	atomic_ulong free_count;
};

struct memory_tracker *memory_tracker_create(bool enable_tracking)
{
	struct memory_tracker *tracker;
	
	tracker = calloc(1, sizeof(*tracker));
	if (!tracker)
		return NULL;
	
	tracker->tracking_enabled = enable_tracking;
	
	if (enable_tracking) {
		if (pthread_mutex_init(&tracker->mutex, NULL) != 0) {
			free(tracker);
			return NULL;
		}
	}
	
	/* Initialize atomics */
	atomic_init(&tracker->rss, 0);
	atomic_init(&tracker->vms, 0);
	atomic_init(&tracker->peak_rss, 0);
	atomic_init(&tracker->allocated, 0);
	atomic_init(&tracker->freed, 0);
	atomic_init(&tracker->current_usage, 0);
	atomic_init(&tracker->peak_usage, 0);
	atomic_init(&tracker->alloc_count, 0);
	atomic_init(&tracker->free_count, 0);
	
	/* Get initial system stats */
	memory_tracker_update_system_stats(tracker);
	
	return tracker;
}

void memory_tracker_destroy(struct memory_tracker *tracker)
{
	struct alloc_entry *entry, *next;
	
	if (!tracker)
		return;
	
	if (tracker->tracking_enabled) {
		/* Free allocation entries */
		entry = tracker->allocations;
		while (entry) {
			next = entry->next;
			free(entry);
			entry = next;
		}
		
		pthread_mutex_destroy(&tracker->mutex);
	}
	
	free(tracker);
}

void memory_tracker_alloc(struct memory_tracker *tracker, size_t size, void *ptr)
{
	struct alloc_entry *entry;
	size_t current, peak;
	
	if (!tracker || !ptr)
		return;
	
	/* Update statistics */
	atomic_fetch_add_explicit(&tracker->allocated, size, memory_order_relaxed);
	atomic_fetch_add_explicit(&tracker->alloc_count, 1, memory_order_relaxed);
	current = atomic_fetch_add_explicit(&tracker->current_usage, size,
	                                    memory_order_relaxed) + size;
	
	/* Update peak usage */
	peak = atomic_load_explicit(&tracker->peak_usage, memory_order_relaxed);
	while (current > peak) {
		if (atomic_compare_exchange_weak_explicit(&tracker->peak_usage, &peak, current,
		                                          memory_order_relaxed,
		                                          memory_order_relaxed))
			break;
	}
	
	/* Track allocation if enabled */
	if (tracker->tracking_enabled) {
		entry = malloc(sizeof(*entry));
		if (entry) {
			entry->ptr = ptr;
			entry->size = size;
			
			pthread_mutex_lock(&tracker->mutex);
			entry->next = tracker->allocations;
			tracker->allocations = entry;
			pthread_mutex_unlock(&tracker->mutex);
		}
	}
}

void memory_tracker_free(struct memory_tracker *tracker, void *ptr)
{
	struct alloc_entry *entry, *prev;
	size_t size = 0;
	
	if (!tracker || !ptr)
		return;
	
	/* Find and remove allocation entry if tracking */
	if (tracker->tracking_enabled) {
		pthread_mutex_lock(&tracker->mutex);
		
		prev = NULL;
		for (entry = tracker->allocations; entry; entry = entry->next) {
			if (entry->ptr == ptr) {
				size = entry->size;
				
				if (prev)
					prev->next = entry->next;
				else
					tracker->allocations = entry->next;
				
				free(entry);
				break;
			}
			prev = entry;
		}
		
		pthread_mutex_unlock(&tracker->mutex);
	}
	
	/* Update statistics */
	if (size > 0) {
		atomic_fetch_add_explicit(&tracker->freed, size, memory_order_relaxed);
		atomic_fetch_sub_explicit(&tracker->current_usage, size, memory_order_relaxed);
	}
	atomic_fetch_add_explicit(&tracker->free_count, 1, memory_order_relaxed);
}

bool memory_tracker_get_stats(struct memory_tracker *tracker,
                              struct memory_stats *stats)
{
	if (!tracker || !stats)
		return false;
	
	stats->rss = atomic_load_explicit(&tracker->rss, memory_order_relaxed);
	stats->vms = atomic_load_explicit(&tracker->vms, memory_order_relaxed);
	stats->peak_rss = atomic_load_explicit(&tracker->peak_rss, memory_order_relaxed);
	stats->allocated = atomic_load_explicit(&tracker->allocated, memory_order_relaxed);
	stats->freed = atomic_load_explicit(&tracker->freed, memory_order_relaxed);
	stats->current_usage = atomic_load_explicit(&tracker->current_usage, memory_order_relaxed);
	stats->peak_usage = atomic_load_explicit(&tracker->peak_usage, memory_order_relaxed);
	stats->alloc_count = atomic_load_explicit(&tracker->alloc_count, memory_order_relaxed);
	stats->free_count = atomic_load_explicit(&tracker->free_count, memory_order_relaxed);
	
	return true;
}

bool memory_tracker_update_system_stats(struct memory_tracker *tracker)
{
	FILE *fp;
	char line[256];
	size_t rss = 0, vms = 0, peak_rss;
	
	if (!tracker)
		return false;
	
	fp = fopen("/proc/self/status", "r");
	if (!fp)
		return false;
	
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "VmRSS:", 6) == 0) {
			sscanf(line + 6, "%zu", &rss);
			rss *= 1024;  /* Convert from KB to bytes */
		} else if (strncmp(line, "VmSize:", 7) == 0) {
			sscanf(line + 7, "%zu", &vms);
			vms *= 1024;  /* Convert from KB to bytes */
		}
	}
	
	fclose(fp);
	
	/* Update stats */
	atomic_store_explicit(&tracker->rss, rss, memory_order_relaxed);
	atomic_store_explicit(&tracker->vms, vms, memory_order_relaxed);
	
	/* Update peak RSS */
	peak_rss = atomic_load_explicit(&tracker->peak_rss, memory_order_relaxed);
	while (rss > peak_rss) {
		if (atomic_compare_exchange_weak_explicit(&tracker->peak_rss, &peak_rss, rss,
		                                          memory_order_relaxed,
		                                          memory_order_relaxed))
			break;
	}
	
	return true;
}

void memory_tracker_reset_stats(struct memory_tracker *tracker)
{
	if (!tracker)
		return;
	
	atomic_store_explicit(&tracker->allocated, 0, memory_order_relaxed);
	atomic_store_explicit(&tracker->freed, 0, memory_order_relaxed);
	atomic_store_explicit(&tracker->alloc_count, 0, memory_order_relaxed);
	atomic_store_explicit(&tracker->free_count, 0, memory_order_relaxed);
}

void memory_tracker_dump(struct memory_tracker *tracker, int fd)
{
	struct memory_stats stats;
	
	if (!tracker)
		return;
	
	memory_tracker_update_system_stats(tracker);
	memory_tracker_get_stats(tracker, &stats);
	
	dprintf(fd, "=== Memory Statistics ===\n");
	dprintf(fd, "System Memory:\n");
	dprintf(fd, "  RSS:      %zu bytes (%.2f MB)\n", stats.rss, stats.rss / 1024.0 / 1024.0);
	dprintf(fd, "  VMS:      %zu bytes (%.2f MB)\n", stats.vms, stats.vms / 1024.0 / 1024.0);
	dprintf(fd, "  Peak RSS: %zu bytes (%.2f MB)\n", stats.peak_rss, stats.peak_rss / 1024.0 / 1024.0);
	
	if (tracker->tracking_enabled) {
		dprintf(fd, "\nTracked Allocations:\n");
		dprintf(fd, "  Allocated:     %zu bytes (%.2f MB)\n", stats.allocated, stats.allocated / 1024.0 / 1024.0);
		dprintf(fd, "  Freed:         %zu bytes (%.2f MB)\n", stats.freed, stats.freed / 1024.0 / 1024.0);
		dprintf(fd, "  Current usage: %zu bytes (%.2f MB)\n", stats.current_usage, stats.current_usage / 1024.0 / 1024.0);
		dprintf(fd, "  Peak usage:    %zu bytes (%.2f MB)\n", stats.peak_usage, stats.peak_usage / 1024.0 / 1024.0);
		dprintf(fd, "  Alloc count:   %lu\n", stats.alloc_count);
		dprintf(fd, "  Free count:    %lu\n", stats.free_count);
	}
	
	dprintf(fd, "========================\n");
}
