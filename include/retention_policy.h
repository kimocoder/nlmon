/* retention_policy.h - Event retention policy enforcement
 *
 * Provides time-based and size-based retention policy enforcement
 * for event storage with configurable policies and automatic cleanup.
 */

#ifndef RETENTION_POLICY_H
#define RETENTION_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Forward declarations */
struct storage_db;
struct storage_buffer;

/* Retention policy structure (opaque) */
struct retention_policy;

/* Retention policy configuration */
struct retention_policy_config {
	/* Time-based retention */
	time_t max_age_seconds;         /* Max age of events (0=unlimited) */
	
	/* Size-based retention */
	size_t max_events;              /* Max number of events (0=unlimited) */
	size_t max_db_size_mb;          /* Max database size in MB (0=unlimited) */
	
	/* Cleanup schedule */
	time_t cleanup_interval;        /* Cleanup interval in seconds */
	bool cleanup_on_startup;        /* Run cleanup on startup */
	
	/* Policy behavior */
	bool delete_oldest_first;       /* Delete oldest when limit reached */
	size_t batch_delete_size;       /* Number of events to delete per batch */
};

/* Retention statistics */
struct retention_stats {
	uint64_t total_cleanups;
	uint64_t total_deleted;
	uint64_t last_cleanup_time;
	uint64_t last_deleted_count;
	uint64_t current_event_count;
	uint64_t current_db_size_bytes;
};

/**
 * retention_policy_create() - Create retention policy
 * @config: Policy configuration
 * @db: Database handle (optional, can be NULL)
 * @buffer: Memory buffer handle (optional, can be NULL)
 *
 * Returns: Retention policy handle or NULL on error
 */
struct retention_policy *retention_policy_create(
	struct retention_policy_config *config,
	struct storage_db *db,
	struct storage_buffer *buffer);

/**
 * retention_policy_destroy() - Destroy retention policy
 * @policy: Retention policy handle
 */
void retention_policy_destroy(struct retention_policy *policy);

/**
 * retention_policy_enforce() - Manually enforce retention policy
 * @policy: Retention policy handle
 *
 * Returns: Number of events deleted, or -1 on error
 */
int retention_policy_enforce(struct retention_policy *policy);

/**
 * retention_policy_start() - Start automatic policy enforcement
 * @policy: Retention policy handle
 *
 * Returns: true on success, false on error
 * Note: Starts background thread for periodic cleanup
 */
bool retention_policy_start(struct retention_policy *policy);

/**
 * retention_policy_stop() - Stop automatic policy enforcement
 * @policy: Retention policy handle
 */
void retention_policy_stop(struct retention_policy *policy);

/**
 * retention_policy_check_event() - Check if event should be retained
 * @policy: Retention policy handle
 * @timestamp: Event timestamp
 *
 * Returns: true if event should be retained, false if it should be deleted
 */
bool retention_policy_check_event(struct retention_policy *policy,
                                  uint64_t timestamp);

/**
 * retention_policy_get_stats() - Get retention statistics
 * @policy: Retention policy handle
 * @stats: Output for statistics
 *
 * Returns: true on success, false on error
 */
bool retention_policy_get_stats(struct retention_policy *policy,
                               struct retention_stats *stats);

/**
 * retention_policy_update_config() - Update policy configuration
 * @policy: Retention policy handle
 * @config: New configuration
 *
 * Returns: true on success, false on error
 */
bool retention_policy_update_config(struct retention_policy *policy,
                                   struct retention_policy_config *config);

#endif /* RETENTION_POLICY_H */
