/* storage_layer.c - Unified storage layer implementation
 *
 * Provides a unified interface to all storage backends with
 * coordinated event storage and management.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "storage_layer.h"
#include "storage_buffer.h"
#include "storage_db.h"
#include "audit_log.h"
#include "retention_policy.h"
#include "event_processor.h"

/* Storage layer structure */
struct storage_layer {
	struct storage_buffer *buffer;
	struct storage_db *db;
	struct audit_log *audit;
	struct retention_policy *retention;
	
	struct storage_layer_config config;
};

struct storage_layer *storage_layer_create(struct storage_layer_config *config)
{
	struct storage_layer *sl;
	
	if (!config)
		return NULL;
	
	sl = calloc(1, sizeof(*sl));
	if (!sl)
		return NULL;
	
	sl->config = *config;
	
	/* Create memory buffer if enabled */
	if (config->enable_buffer) {
		sl->buffer = storage_buffer_create(config->buffer_capacity);
		if (!sl->buffer) {
			fprintf(stderr, "Failed to create storage buffer\n");
			storage_layer_destroy(sl);
			return NULL;
		}
	}
	
	/* Create database if enabled */
	if (config->enable_database && config->db_path) {
		struct storage_db_config db_config = {
			.db_path = config->db_path,
			.batch_size = config->db_batch_size,
			.cache_size_kb = config->db_cache_size_kb,
			.enable_wal = config->db_enable_wal,
			.busy_timeout_ms = 5000
		};
		
		sl->db = storage_db_open(&db_config);
		if (!sl->db) {
			fprintf(stderr, "Failed to open database\n");
			storage_layer_destroy(sl);
			return NULL;
		}
	}
	
	/* Create audit log if enabled */
	if (config->enable_audit_log && config->audit_log_path) {
		struct audit_log_config audit_config = {
			.log_path = config->audit_log_path,
			.security_log_path = config->security_log_path,
			.max_file_size = config->audit_max_file_size,
			.max_rotations = config->audit_max_rotations,
			.sync_writes = config->audit_sync_writes,
			.verify_on_open = false
		};
		
		sl->audit = audit_log_open(&audit_config);
		if (!sl->audit) {
			fprintf(stderr, "Failed to open audit log\n");
			storage_layer_destroy(sl);
			return NULL;
		}
	}
	
	/* Create retention policy if enabled */
	if (config->enable_retention) {
		struct retention_policy_config retention_config = {
			.max_age_seconds = config->retention_max_age_seconds,
			.max_events = config->retention_max_events,
			.max_db_size_mb = config->retention_max_db_size_mb,
			.cleanup_interval = config->retention_cleanup_interval,
			.cleanup_on_startup = config->retention_cleanup_on_startup,
			.delete_oldest_first = true,
			.batch_delete_size = 1000
		};
		
		sl->retention = retention_policy_create(&retention_config, sl->db, sl->buffer);
		if (!sl->retention) {
			fprintf(stderr, "Failed to create retention policy\n");
			storage_layer_destroy(sl);
			return NULL;
		}
		
		/* Start automatic enforcement */
		if (config->retention_cleanup_interval > 0) {
			retention_policy_start(sl->retention);
		}
	}
	
	return sl;
}

void storage_layer_destroy(struct storage_layer *sl)
{
	if (!sl)
		return;
	
	/* Stop and destroy retention policy */
	if (sl->retention) {
		retention_policy_stop(sl->retention);
		retention_policy_destroy(sl->retention);
	}
	
	/* Close audit log */
	if (sl->audit)
		audit_log_close(sl->audit);
	
	/* Close database */
	if (sl->db)
		storage_db_close(sl->db);
	
	/* Destroy buffer */
	if (sl->buffer)
		storage_buffer_destroy(sl->buffer);
	
	free(sl);
}

bool storage_layer_store_event(struct storage_layer *sl,
                               struct nlmon_event *event,
                               bool is_security_event)
{
	bool success = true;
	
	if (!sl || !event)
		return false;
	
	/* Check retention policy */
	if (sl->retention) {
		if (!retention_policy_check_event(sl->retention, event->timestamp)) {
			/* Event is too old, don't store */
			return true;
		}
	}
	
	/* Store in memory buffer */
	if (sl->buffer) {
		if (!storage_buffer_add(sl->buffer, event)) {
			fprintf(stderr, "Failed to add event to buffer\n");
			success = false;
		}
	}
	
	/* Store in database */
	if (sl->db) {
		if (!storage_db_insert(sl->db, event)) {
			fprintf(stderr, "Failed to insert event into database\n");
			success = false;
		}
	}
	
	/* Write to audit log */
	if (sl->audit) {
		enum audit_severity severity = is_security_event ? 
		                               AUDIT_SECURITY : AUDIT_INFO;
		
		if (!audit_log_write(sl->audit, event, severity, NULL)) {
			fprintf(stderr, "Failed to write to audit log\n");
			success = false;
		}
	}
	
	return success;
}

struct storage_buffer *storage_layer_get_buffer(struct storage_layer *sl)
{
	return sl ? sl->buffer : NULL;
}

struct storage_db *storage_layer_get_database(struct storage_layer *sl)
{
	return sl ? sl->db : NULL;
}

struct audit_log *storage_layer_get_audit_log(struct storage_layer *sl)
{
	return sl ? sl->audit : NULL;
}

bool storage_layer_flush(struct storage_layer *sl)
{
	bool success = true;
	
	if (!sl)
		return false;
	
	/* Flush database */
	if (sl->db) {
		if (!storage_db_flush(sl->db)) {
			fprintf(stderr, "Failed to flush database\n");
			success = false;
		}
	}
	
	return success;
}

int storage_layer_enforce_retention(struct storage_layer *sl)
{
	if (!sl || !sl->retention)
		return 0;
	
	return retention_policy_enforce(sl->retention);
}

bool storage_layer_get_stats(struct storage_layer *sl,
                            size_t *buffer_size,
                            size_t *buffer_capacity,
                            uint64_t *db_event_count,
                            uint64_t *db_size_bytes,
                            uint64_t *audit_entries)
{
	if (!sl)
		return false;
	
	/* Get buffer stats */
	if (sl->buffer) {
		if (buffer_size)
			*buffer_size = storage_buffer_size(sl->buffer);
		if (buffer_capacity)
			*buffer_capacity = storage_buffer_capacity(sl->buffer);
	}
	
	/* Get database stats */
	if (sl->db) {
		storage_db_get_stats(sl->db, db_event_count, db_size_bytes, NULL);
	}
	
	/* Get audit log stats */
	if (sl->audit) {
		audit_log_get_stats(sl->audit, audit_entries, NULL, NULL, NULL);
	}
	
	return true;
}
