/* memory_tracker.h - Memory usage tracking and reporting
 *
 * Provides memory usage statistics and tracking.
 */

#ifndef MEMORY_TRACKER_H
#define MEMORY_TRACKER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Memory statistics */
struct memory_stats {
	size_t rss;              /* Resident set size (bytes) */
	size_t vms;              /* Virtual memory size (bytes) */
	size_t peak_rss;         /* Peak RSS (bytes) */
	size_t allocated;        /* Tracked allocations (bytes) */
	size_t freed;            /* Tracked frees (bytes) */
	size_t current_usage;    /* Current tracked usage (bytes) */
	size_t peak_usage;       /* Peak tracked usage (bytes) */
	unsigned long alloc_count;   /* Number of allocations */
	unsigned long free_count;    /* Number of frees */
};

/* Memory tracker structure (opaque) */
struct memory_tracker;

/**
 * memory_tracker_create() - Create memory tracker
 * @enable_tracking: Enable allocation tracking (adds overhead)
 *
 * Returns: Pointer to memory tracker or NULL on error
 */
struct memory_tracker *memory_tracker_create(bool enable_tracking);

/**
 * memory_tracker_destroy() - Destroy memory tracker
 * @tracker: Memory tracker
 */
void memory_tracker_destroy(struct memory_tracker *tracker);

/**
 * memory_tracker_alloc() - Track memory allocation
 * @tracker: Memory tracker
 * @size: Size of allocation
 * @ptr: Pointer to allocated memory
 */
void memory_tracker_alloc(struct memory_tracker *tracker, size_t size, void *ptr);

/**
 * memory_tracker_free() - Track memory free
 * @tracker: Memory tracker
 * @ptr: Pointer to freed memory
 */
void memory_tracker_free(struct memory_tracker *tracker, void *ptr);

/**
 * memory_tracker_get_stats() - Get memory statistics
 * @tracker: Memory tracker
 * @stats: Output statistics structure
 *
 * Returns: true on success
 */
bool memory_tracker_get_stats(struct memory_tracker *tracker,
                              struct memory_stats *stats);

/**
 * memory_tracker_update_system_stats() - Update system memory stats
 * @tracker: Memory tracker
 *
 * Updates RSS and VMS from /proc/self/status
 * Returns: true on success
 */
bool memory_tracker_update_system_stats(struct memory_tracker *tracker);

/**
 * memory_tracker_reset_stats() - Reset statistics counters
 * @tracker: Memory tracker
 */
void memory_tracker_reset_stats(struct memory_tracker *tracker);

/**
 * memory_tracker_dump() - Dump memory statistics
 * @tracker: Memory tracker
 * @fd: File descriptor to write to
 */
void memory_tracker_dump(struct memory_tracker *tracker, int fd);

#endif /* MEMORY_TRACKER_H */
