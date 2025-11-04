/* storage_db.c - SQLite database backend implementation
 *
 * Implements persistent storage for network events using SQLite with
 * batched inserts, indexing, and query optimization.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include "storage_db.h"
#include "event_processor.h"

/* Database schema version */
#define SCHEMA_VERSION 1

/* Default batch size */
#define DEFAULT_BATCH_SIZE 100

/* Database structure */
struct storage_db {
	sqlite3 *db;
	sqlite3_stmt *insert_stmt;
	size_t batch_size;
	size_t batch_count;
	bool in_transaction;
};

/* Database schema */
static const char *schema_sql = 
	"CREATE TABLE IF NOT EXISTS events ("
	"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"  timestamp INTEGER NOT NULL,"
	"  sequence INTEGER NOT NULL,"
	"  event_type INTEGER NOT NULL,"
	"  message_type INTEGER NOT NULL,"
	"  interface TEXT,"
	"  namespace TEXT,"
	"  details BLOB"
	");"
	"CREATE INDEX IF NOT EXISTS idx_timestamp ON events(timestamp);"
	"CREATE INDEX IF NOT EXISTS idx_event_type ON events(event_type);"
	"CREATE INDEX IF NOT EXISTS idx_interface ON events(interface);"
	"CREATE INDEX IF NOT EXISTS idx_namespace ON events(namespace);"
	"CREATE TABLE IF NOT EXISTS metadata ("
	"  key TEXT PRIMARY KEY,"
	"  value TEXT"
	");"
	"INSERT OR REPLACE INTO metadata (key, value) VALUES ('schema_version', '1');";

/* Prepared statement SQL */
static const char *insert_sql =
	"INSERT INTO events (timestamp, sequence, event_type, message_type, "
	"interface, namespace, details) VALUES (?, ?, ?, ?, ?, ?, ?)";

/* Initialize database schema */
static bool init_schema(sqlite3 *db)
{
	char *err_msg = NULL;
	int rc;
	
	rc = sqlite3_exec(db, schema_sql, NULL, NULL, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to initialize schema: %s\n", err_msg);
		sqlite3_free(err_msg);
		return false;
	}
	
	return true;
}

/* Configure database for performance */
static bool configure_db(sqlite3 *db, struct storage_db_config *config)
{
	char sql[256];
	char *err_msg = NULL;
	int rc;
	
	/* Set cache size */
	if (config->cache_size_kb > 0) {
		snprintf(sql, sizeof(sql), "PRAGMA cache_size = -%zu", config->cache_size_kb);
		rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Failed to set cache size: %s\n", err_msg);
			sqlite3_free(err_msg);
			return false;
		}
	}
	
	/* Enable WAL mode for better concurrency */
	if (config->enable_wal) {
		rc = sqlite3_exec(db, "PRAGMA journal_mode = WAL", NULL, NULL, &err_msg);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Failed to enable WAL: %s\n", err_msg);
			sqlite3_free(err_msg);
			return false;
		}
	}
	
	/* Set busy timeout */
	if (config->busy_timeout_ms > 0) {
		rc = sqlite3_busy_timeout(db, config->busy_timeout_ms);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Failed to set busy timeout\n");
			return false;
		}
	}
	
	/* Enable foreign keys */
	rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to enable foreign keys: %s\n", err_msg);
		sqlite3_free(err_msg);
		return false;
	}
	
	return true;
}

struct storage_db *storage_db_open(struct storage_db_config *config)
{
	struct storage_db *sdb;
	int rc;
	
	if (!config || !config->db_path)
		return NULL;
	
	sdb = calloc(1, sizeof(*sdb));
	if (!sdb)
		return NULL;
	
	/* Open database */
	rc = sqlite3_open(config->db_path, &sdb->db);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(sdb->db));
		sqlite3_close(sdb->db);
		free(sdb);
		return NULL;
	}
	
	/* Initialize schema */
	if (!init_schema(sdb->db)) {
		sqlite3_close(sdb->db);
		free(sdb);
		return NULL;
	}
	
	/* Configure database */
	if (!configure_db(sdb->db, config)) {
		sqlite3_close(sdb->db);
		free(sdb);
		return NULL;
	}
	
	/* Prepare insert statement */
	rc = sqlite3_prepare_v2(sdb->db, insert_sql, -1, &sdb->insert_stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare insert statement: %s\n",
		        sqlite3_errmsg(sdb->db));
		sqlite3_close(sdb->db);
		free(sdb);
		return NULL;
	}
	
	sdb->batch_size = config->batch_size > 0 ? config->batch_size : DEFAULT_BATCH_SIZE;
	sdb->batch_count = 0;
	sdb->in_transaction = false;
	
	return sdb;
}

void storage_db_close(struct storage_db *db)
{
	if (!db)
		return;
	
	/* Flush any pending inserts */
	storage_db_flush(db);
	
	/* Finalize prepared statement */
	if (db->insert_stmt)
		sqlite3_finalize(db->insert_stmt);
	
	/* Close database */
	if (db->db)
		sqlite3_close(db->db);
	
	free(db);
}

bool storage_db_insert(struct storage_db *db, struct nlmon_event *event)
{
	int rc;
	
	if (!db || !event)
		return false;
	
	/* Start transaction if not already in one */
	if (!db->in_transaction) {
		rc = sqlite3_exec(db->db, "BEGIN TRANSACTION", NULL, NULL, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Failed to begin transaction: %s\n",
			        sqlite3_errmsg(db->db));
			return false;
		}
		db->in_transaction = true;
	}
	
	/* Bind parameters */
	sqlite3_reset(db->insert_stmt);
	sqlite3_bind_int64(db->insert_stmt, 1, event->timestamp);
	sqlite3_bind_int64(db->insert_stmt, 2, event->sequence);
	sqlite3_bind_int(db->insert_stmt, 3, event->event_type);
	sqlite3_bind_int(db->insert_stmt, 4, event->message_type);
	sqlite3_bind_text(db->insert_stmt, 5, event->interface, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(db->insert_stmt, 6, "", -1, SQLITE_TRANSIENT);  /* namespace placeholder */
	
	/* Bind event data as blob */
	if (event->data && event->data_size > 0) {
		sqlite3_bind_blob(db->insert_stmt, 7, event->data, event->data_size, SQLITE_TRANSIENT);
	} else {
		sqlite3_bind_null(db->insert_stmt, 7);
	}
	
	/* Execute insert */
	rc = sqlite3_step(db->insert_stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Failed to insert event: %s\n", sqlite3_errmsg(db->db));
		return false;
	}
	
	db->batch_count++;
	
	/* Commit transaction if batch is full */
	if (db->batch_count >= db->batch_size) {
		return storage_db_flush(db);
	}
	
	return true;
}

bool storage_db_flush(struct storage_db *db)
{
	int rc;
	
	if (!db || !db->in_transaction)
		return true;
	
	rc = sqlite3_exec(db->db, "COMMIT", NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to commit transaction: %s\n",
		        sqlite3_errmsg(db->db));
		sqlite3_exec(db->db, "ROLLBACK", NULL, NULL, NULL);
		db->in_transaction = false;
		db->batch_count = 0;
		return false;
	}
	
	db->in_transaction = false;
	db->batch_count = 0;
	
	return true;
}

/* Build WHERE clause from filter */
static void build_where_clause(struct db_query_filter *filter, char *where, size_t size)
{
	char *p = where;
	size_t remaining = size;
	int n;
	bool first = true;
	
	*p = '\0';
	
	if (!filter)
		return;
	
	if (filter->interface_pattern) {
		n = snprintf(p, remaining, "%sinterface LIKE '%s'",
		            first ? "WHERE " : " AND ", filter->interface_pattern);
		p += n;
		remaining -= n;
		first = false;
	}
	
	if (filter->event_type != 0) {
		n = snprintf(p, remaining, "%sevent_type = %u",
		            first ? "WHERE " : " AND ", filter->event_type);
		p += n;
		remaining -= n;
		first = false;
	}
	
	if (filter->message_type != 0) {
		n = snprintf(p, remaining, "%smessage_type = %u",
		            first ? "WHERE " : " AND ", filter->message_type);
		p += n;
		remaining -= n;
		first = false;
	}
	
	if (filter->namespace) {
		n = snprintf(p, remaining, "%snamespace = '%s'",
		            first ? "WHERE " : " AND ", filter->namespace);
		p += n;
		remaining -= n;
		first = false;
	}
	
	if (filter->start_time != 0) {
		n = snprintf(p, remaining, "%stimestamp >= %lu",
		            first ? "WHERE " : " AND ", filter->start_time);
		p += n;
		remaining -= n;
		first = false;
	}
	
	if (filter->end_time != 0) {
		n = snprintf(p, remaining, "%stimestamp <= %lu",
		            first ? "WHERE " : " AND ", filter->end_time);
		p += n;
		remaining -= n;
		first = false;
	}
}

int storage_db_query(struct storage_db *db,
                     struct db_query_filter *filter,
                     db_query_callback_t callback,
                     void *ctx)
{
	char sql[1024];
	char where[512];
	sqlite3_stmt *stmt;
	int rc;
	int count = 0;
	
	if (!db || !callback)
		return -1;
	
	/* Build query */
	build_where_clause(filter, where, sizeof(where));
	
	snprintf(sql, sizeof(sql),
	        "SELECT timestamp, sequence, event_type, message_type, "
	        "interface, namespace, details FROM events %s ORDER BY %s %s",
	        where,
	        filter && filter->order_by ? filter->order_by : "timestamp",
	        filter && filter->descending ? "DESC" : "ASC");
	
	if (filter && filter->limit > 0) {
		char limit_clause[64];
		snprintf(limit_clause, sizeof(limit_clause), " LIMIT %zu", filter->limit);
		strncat(sql, limit_clause, sizeof(sql) - strlen(sql) - 1);
		
		if (filter->offset > 0) {
			char offset_clause[64];
			snprintf(offset_clause, sizeof(offset_clause), " OFFSET %zu", filter->offset);
			strncat(sql, offset_clause, sizeof(sql) - strlen(sql) - 1);
		}
	}
	
	/* Prepare statement */
	rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare query: %s\n", sqlite3_errmsg(db->db));
		return -1;
	}
	
	/* Execute query */
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		struct nlmon_event event = {0};
		
		event.timestamp = sqlite3_column_int64(stmt, 0);
		event.sequence = sqlite3_column_int64(stmt, 1);
		event.event_type = sqlite3_column_int(stmt, 2);
		event.message_type = sqlite3_column_int(stmt, 3);
		
		const char *interface = (const char *)sqlite3_column_text(stmt, 4);
		if (interface)
			strncpy(event.interface, interface, sizeof(event.interface) - 1);
		
		/* Get blob data */
		const void *blob = sqlite3_column_blob(stmt, 6);
		int blob_size = sqlite3_column_bytes(stmt, 6);
		
		if (blob && blob_size > 0) {
			event.data = malloc(blob_size);
			if (event.data) {
				memcpy(event.data, blob, blob_size);
				event.data_size = blob_size;
			}
		}
		
		callback(&event, ctx);
		
		if (event.data)
			free(event.data);
		
		count++;
	}
	
	sqlite3_finalize(stmt);
	
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Query execution failed: %s\n", sqlite3_errmsg(db->db));
		return -1;
	}
	
	return count;
}

int storage_db_count(struct storage_db *db, struct db_query_filter *filter)
{
	char sql[1024];
	char where[512];
	sqlite3_stmt *stmt;
	int rc;
	int count = -1;
	
	if (!db)
		return -1;
	
	build_where_clause(filter, where, sizeof(where));
	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM events %s", where);
	
	rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare count query: %s\n", sqlite3_errmsg(db->db));
		return -1;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
	}
	
	sqlite3_finalize(stmt);
	
	return count;
}

int storage_db_delete_before(struct storage_db *db, uint64_t timestamp)
{
	char sql[256];
	int rc;
	
	if (!db)
		return -1;
	
	snprintf(sql, sizeof(sql), "DELETE FROM events WHERE timestamp < %lu", timestamp);
	
	rc = sqlite3_exec(db->db, sql, NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to delete events: %s\n", sqlite3_errmsg(db->db));
		return -1;
	}
	
	return sqlite3_changes(db->db);
}

int storage_db_delete_oldest(struct storage_db *db, size_t keep_count)
{
	char sql[256];
	int rc;
	
	if (!db)
		return -1;
	
	snprintf(sql, sizeof(sql),
	        "DELETE FROM events WHERE id IN ("
	        "  SELECT id FROM events ORDER BY timestamp DESC LIMIT -1 OFFSET %zu"
	        ")", keep_count);
	
	rc = sqlite3_exec(db->db, sql, NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to delete oldest events: %s\n", sqlite3_errmsg(db->db));
		return -1;
	}
	
	return sqlite3_changes(db->db);
}

bool storage_db_vacuum(struct storage_db *db)
{
	int rc;
	
	if (!db)
		return false;
	
	rc = sqlite3_exec(db->db, "VACUUM", NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to vacuum database: %s\n", sqlite3_errmsg(db->db));
		return false;
	}
	
	return true;
}

bool storage_db_analyze(struct storage_db *db)
{
	int rc;
	
	if (!db)
		return false;
	
	rc = sqlite3_exec(db->db, "ANALYZE", NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to analyze database: %s\n", sqlite3_errmsg(db->db));
		return false;
	}
	
	return true;
}

bool storage_db_get_stats(struct storage_db *db,
                         uint64_t *total_events,
                         uint64_t *db_size_bytes,
                         uint64_t *page_count)
{
	sqlite3_stmt *stmt;
	int rc;
	struct stat st;
	
	if (!db)
		return false;
	
	/* Get total events */
	if (total_events) {
		rc = sqlite3_prepare_v2(db->db, "SELECT COUNT(*) FROM events", -1, &stmt, NULL);
		if (rc == SQLITE_OK) {
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				*total_events = sqlite3_column_int64(stmt, 0);
			}
			sqlite3_finalize(stmt);
		}
	}
	
	/* Get page count */
	if (page_count) {
		rc = sqlite3_prepare_v2(db->db, "PRAGMA page_count", -1, &stmt, NULL);
		if (rc == SQLITE_OK) {
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				*page_count = sqlite3_column_int64(stmt, 0);
			}
			sqlite3_finalize(stmt);
		}
	}
	
	/* Get file size */
	if (db_size_bytes) {
		const char *db_path = sqlite3_db_filename(db->db, "main");
		if (db_path && stat(db_path, &st) == 0) {
			*db_size_bytes = st.st_size;
		}
	}
	
	return true;
}
