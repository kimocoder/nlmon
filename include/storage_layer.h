/* storage_layer.h - Unified storage layer abstraction
 *
 * Provides a unified interface to all storage backends including
 * memory buffer, database, audit log, and retention policy.
 */

#ifndef STORAGE_LAYER_H
#define STORAGE_LAYER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Forward declarations */
struct nlmon_event;
struct storage_buffer;
struct storage_db;
struct audit_log;
struct retention_policy;

/* Storage layer handle (opaque) */
struct storage_layer;

/* Storage layer configuration */
struct storage_layer_config {
	/* Memory buffer */
	bool enable_buffer;
	size_t buffer_capacity;
	
	/* Database */
	bool enable_database;
	const char *db_path;
	size_t db_batch_size;
	size_t db_cache_size_kb;
	bool db_enable_wal;
	
	/* Audit log */
	bool enable_audit_log;
	const char *audit_log_path;
	const char *security_log_path;
	size_t audit_max_file_size;
	size_t audit_max_rotations;
	bool audit_sync_writes;
	
	/* Retention policy */
	bool enable_retention;
	time_t retention_max_age_seconds;
	size_t retention_max_events;
	size_t retention_max_db_size_mb;
	time_t retention_cleanup_interval;
	bool retention_cleanup_on_startup;
};

/**
 * storage_layer_create() - Create storage layer
 * @config: Storage layer configuration
 *
 * Returns: Storage layer handle or NULL on error
 */
struct storage_layer *storage_layer_create(struct storage_layer_config *config);

/**
 * storage_layer_destroy() - Destroy storage layer
 * @sl: Storage layer handle
 */
void storage_layer_destroy(struct storage_layer *sl);

/**
 * storage_layer_store_event() - Store event in all enabled backends
 * @sl: Storage layer handle
 * @event: Event to store
 * @is_security_event: Whether this is a security event
 *
 * Returns: true on success, false on error
 */
bool storage_layer_store_event(struct storage_layer *sl,
                               struct nlmon_event *event,
                               bool is_security_event);

/**
 * storage_layer_get_buffer() - Get memory buffer handle
 * @sl: Storage layer handle
 *
 * Returns: Buffer handle or NULL if not enabled
 */
struct storage_buffer *storage_layer_get_buffer(struct storage_layer *sl);

/**
 * storage_layer_get_database() - Get database handle
 * @sl: Storage layer handle
 *
 * Returns: Database handle or NULL if not enabled
 */
struct storage_db *storage_layer_get_database(struct storage_layer *sl);

/**
 * storage_layer_get_audit_log() - Get audit log handle
 * @sl: Storage layer handle
 *
 * Returns: Audit log handle or NULL if not enabled
 */
struct audit_log *storage_layer_get_audit_log(struct storage_layer *sl);

/**
 * storage_layer_flush() - Flush all pending writes
 * @sl: Storage layer handle
 *
 * Returns: true on success, false on error
 */
bool storage_layer_flush(struct storage_layer *sl);

/**
 * storage_layer_enforce_retention() - Manually enforce retention policy
 * @sl: Storage layer handle
 *
 * Returns: Number of events deleted, or -1 on error
 */
int storage_layer_enforce_retention(struct storage_layer *sl);

/**
 * storage_layer_get_stats() - Get storage layer statistics
 * @sl: Storage layer handle
 * @buffer_size: Output for buffer size
 * @buffer_capacity: Output for buffer capacity
 * @db_event_count: Output for database event count
 * @db_size_bytes: Output for database size
 * @audit_entries: Output for audit log entries
 *
 * Returns: true on success, false on error
 */
bool storage_layer_get_stats(struct storage_layer *sl,
                            size_t *buffer_size,
                            size_t *buffer_capacity,
                            uint64_t *db_event_count,
                            uint64_t *db_size_bytes,
                            uint64_t *audit_entries);

#endif /* STORAGE_LAYER_H */
