/* storage_db.h - SQLite database backend for event storage
 *
 * Provides persistent storage for network events with indexing,
 * batched inserts, query API with filtering, and database maintenance.
 */

#ifndef STORAGE_DB_H
#define STORAGE_DB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declaration */
struct nlmon_event;

/* Database handle (opaque) */
struct storage_db;

/* Database configuration */
struct storage_db_config {
	const char *db_path;            /* Database file path */
	size_t batch_size;              /* Batch insert size (0=auto) */
	size_t cache_size_kb;           /* SQLite cache size in KB */
	bool enable_wal;                /* Enable Write-Ahead Logging */
	int busy_timeout_ms;            /* Busy timeout in milliseconds */
};

/* Query filter for database queries */
struct db_query_filter {
	const char *interface_pattern;  /* Interface name pattern (NULL=any) */
	uint32_t event_type;            /* Event type (0=any) */
	uint16_t message_type;          /* Message type (0=any) */
	const char *namespace;          /* Namespace (NULL=any) */
	uint64_t start_time;            /* Start timestamp (0=any) */
	uint64_t end_time;              /* End timestamp (0=any) */
	size_t limit;                   /* Result limit (0=unlimited) */
	size_t offset;                  /* Result offset */
	const char *order_by;           /* Order by field (NULL=timestamp) */
	bool descending;                /* Descending order */
};

/* Query result callback */
typedef void (*db_query_callback_t)(struct nlmon_event *event, void *ctx);

/**
 * storage_db_open() - Open or create database
 * @config: Database configuration
 *
 * Returns: Database handle or NULL on error
 */
struct storage_db *storage_db_open(struct storage_db_config *config);

/**
 * storage_db_close() - Close database
 * @db: Database handle
 */
void storage_db_close(struct storage_db *db);

/**
 * storage_db_insert() - Insert event into database
 * @db: Database handle
 * @event: Event to insert
 *
 * Returns: true on success, false on error
 * Note: May be batched for performance
 */
bool storage_db_insert(struct storage_db *db, struct nlmon_event *event);

/**
 * storage_db_flush() - Flush pending batched inserts
 * @db: Database handle
 *
 * Returns: true on success, false on error
 */
bool storage_db_flush(struct storage_db *db);

/**
 * storage_db_query() - Query events from database
 * @db: Database handle
 * @filter: Query filter (NULL for all events)
 * @callback: Callback for each matching event
 * @ctx: Context to pass to callback
 *
 * Returns: Number of matching events, or -1 on error
 */
int storage_db_query(struct storage_db *db,
                     struct db_query_filter *filter,
                     db_query_callback_t callback,
                     void *ctx);

/**
 * storage_db_count() - Count events matching filter
 * @db: Database handle
 * @filter: Query filter (NULL for all events)
 *
 * Returns: Number of matching events, or -1 on error
 */
int storage_db_count(struct storage_db *db, struct db_query_filter *filter);

/**
 * storage_db_delete_before() - Delete events before timestamp
 * @db: Database handle
 * @timestamp: Timestamp threshold
 *
 * Returns: Number of deleted events, or -1 on error
 */
int storage_db_delete_before(struct storage_db *db, uint64_t timestamp);

/**
 * storage_db_delete_oldest() - Delete oldest events to maintain size limit
 * @db: Database handle
 * @keep_count: Number of events to keep
 *
 * Returns: Number of deleted events, or -1 on error
 */
int storage_db_delete_oldest(struct storage_db *db, size_t keep_count);

/**
 * storage_db_vacuum() - Vacuum database to reclaim space
 * @db: Database handle
 *
 * Returns: true on success, false on error
 */
bool storage_db_vacuum(struct storage_db *db);

/**
 * storage_db_analyze() - Analyze database for query optimization
 * @db: Database handle
 *
 * Returns: true on success, false on error
 */
bool storage_db_analyze(struct storage_db *db);

/**
 * storage_db_get_stats() - Get database statistics
 * @db: Database handle
 * @total_events: Output for total event count
 * @db_size_bytes: Output for database file size
 * @page_count: Output for database page count
 *
 * Returns: true on success, false on error
 */
bool storage_db_get_stats(struct storage_db *db,
                         uint64_t *total_events,
                         uint64_t *db_size_bytes,
                         uint64_t *page_count);

#endif /* STORAGE_DB_H */
