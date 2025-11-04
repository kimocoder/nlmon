/* retention_policy.c - Retention policy enforcement implementation
 *
 * Implements time-based and size-based retention policies with
 * automatic cleanup and configurable enforcement schedules.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "retention_policy.h"
#include "storage_db.h"
#include "storage_buffer.h"

/* Retention policy structure */
struct retention_policy {
	struct retention_policy_config config;
	struct storage_db *db;
	struct storage_buffer *buffer;
	
	/* Background cleanup thread */
	pthread_t cleanup_thread;
	bool thread_running;
	bool thread_stop;
	
	/* Statistics */
	struct retention_stats stats;
	
	pthread_mutex_t lock;
};

/* Get current timestamp in seconds */
static uint64_t get_current_time(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec;
}

/* Enforce time-based retention on database */
static int enforce_time_retention_db(struct retention_policy *policy)
{
	uint64_t cutoff_time;
	int deleted;
	
	if (!policy->db || policy->config.max_age_seconds == 0)
		return 0;
	
	cutoff_time = get_current_time() - policy->config.max_age_seconds;
	
	deleted = storage_db_delete_before(policy->db, cutoff_time);
	
	return deleted;
}

/* Enforce size-based retention on database */
static int enforce_size_retention_db(struct retention_policy *policy)
{
	uint64_t total_events;
	uint64_t db_size;
	int deleted = 0;
	
	if (!policy->db)
		return 0;
	
	/* Get current database stats */
	if (!storage_db_get_stats(policy->db, &total_events, &db_size, NULL))
		return -1;
	
	/* Check event count limit */
	if (policy->config.max_events > 0 && total_events > policy->config.max_events) {
		size_t to_delete = total_events - policy->config.max_events;
		
		/* Delete in batches if configured */
		if (policy->config.batch_delete_size > 0) {
			to_delete = (to_delete < policy->config.batch_delete_size) ?
			           to_delete : policy->config.batch_delete_size;
		}
		
		int result = storage_db_delete_oldest(policy->db, policy->config.max_events);
		if (result > 0)
			deleted += result;
	}
	
	/* Check database size limit */
	if (policy->config.max_db_size_mb > 0) {
		uint64_t max_size = (uint64_t)policy->config.max_db_size_mb * 1024 * 1024;
		
		if (db_size > max_size) {
			/* Delete oldest 10% of events */
			size_t keep_count = (total_events * 9) / 10;
			int result = storage_db_delete_oldest(policy->db, keep_count);
			if (result > 0)
				deleted += result;
			
			/* Vacuum to reclaim space */
			storage_db_vacuum(policy->db);
		}
	}
	
	return deleted;
}

/* Cleanup thread function */
static void *cleanup_thread_func(void *arg)
{
	struct retention_policy *policy = arg;
	
	while (!policy->thread_stop) {
		/* Sleep for cleanup interval */
		sleep(policy->config.cleanup_interval);
		
		if (policy->thread_stop)
			break;
		
		/* Enforce policy */
		pthread_mutex_lock(&policy->lock);
		
		int deleted = 0;
		int result;
		
		/* Time-based retention */
		result = enforce_time_retention_db(policy);
		if (result > 0)
			deleted += result;
		
		/* Size-based retention */
		result = enforce_size_retention_db(policy);
		if (result > 0)
			deleted += result;
		
		/* Update statistics */
		if (deleted > 0) {
			policy->stats.total_cleanups++;
			policy->stats.total_deleted += deleted;
			policy->stats.last_cleanup_time = get_current_time();
			policy->stats.last_deleted_count = deleted;
		}
		
		/* Update current counts */
		if (policy->db) {
			uint64_t total_events, db_size;
			if (storage_db_get_stats(policy->db, &total_events, &db_size, NULL)) {
				policy->stats.current_event_count = total_events;
				policy->stats.current_db_size_bytes = db_size;
			}
		}
		
		pthread_mutex_unlock(&policy->lock);
	}
	
	return NULL;
}

struct retention_policy *retention_policy_create(
	struct retention_policy_config *config,
	struct storage_db *db,
	struct storage_buffer *buffer)
{
	struct retention_policy *policy;
	
	if (!config)
		return NULL;
	
	policy = calloc(1, sizeof(*policy));
	if (!policy)
		return NULL;
	
	policy->config = *config;
	policy->db = db;
	policy->buffer = buffer;
	policy->thread_running = false;
	policy->thread_stop = false;
	
	/* Set default cleanup interval if not specified */
	if (policy->config.cleanup_interval == 0)
		policy->config.cleanup_interval = 3600;  /* 1 hour */
	
	/* Set default batch size if not specified */
	if (policy->config.batch_delete_size == 0)
		policy->config.batch_delete_size = 1000;
	
	pthread_mutex_init(&policy->lock, NULL);
	
	/* Run initial cleanup if configured */
	if (config->cleanup_on_startup) {
		retention_policy_enforce(policy);
	}
	
	return policy;
}

void retention_policy_destroy(struct retention_policy *policy)
{
	if (!policy)
		return;
	
	/* Stop cleanup thread if running */
	if (policy->thread_running) {
		retention_policy_stop(policy);
	}
	
	pthread_mutex_destroy(&policy->lock);
	free(policy);
}

int retention_policy_enforce(struct retention_policy *policy)
{
	int deleted = 0;
	int result;
	
	if (!policy)
		return -1;
	
	pthread_mutex_lock(&policy->lock);
	
	/* Time-based retention */
	result = enforce_time_retention_db(policy);
	if (result > 0)
		deleted += result;
	else if (result < 0) {
		pthread_mutex_unlock(&policy->lock);
		return -1;
	}
	
	/* Size-based retention */
	result = enforce_size_retention_db(policy);
	if (result > 0)
		deleted += result;
	else if (result < 0) {
		pthread_mutex_unlock(&policy->lock);
		return -1;
	}
	
	/* Update statistics */
	if (deleted > 0) {
		policy->stats.total_cleanups++;
		policy->stats.total_deleted += deleted;
		policy->stats.last_cleanup_time = get_current_time();
		policy->stats.last_deleted_count = deleted;
	}
	
	/* Update current counts */
	if (policy->db) {
		uint64_t total_events, db_size;
		if (storage_db_get_stats(policy->db, &total_events, &db_size, NULL)) {
			policy->stats.current_event_count = total_events;
			policy->stats.current_db_size_bytes = db_size;
		}
	}
	
	pthread_mutex_unlock(&policy->lock);
	
	return deleted;
}

bool retention_policy_start(struct retention_policy *policy)
{
	int rc;
	
	if (!policy)
		return false;
	
	pthread_mutex_lock(&policy->lock);
	
	if (policy->thread_running) {
		pthread_mutex_unlock(&policy->lock);
		return true;
	}
	
	policy->thread_stop = false;
	
	rc = pthread_create(&policy->cleanup_thread, NULL, cleanup_thread_func, policy);
	if (rc != 0) {
		pthread_mutex_unlock(&policy->lock);
		return false;
	}
	
	policy->thread_running = true;
	
	pthread_mutex_unlock(&policy->lock);
	
	return true;
}

void retention_policy_stop(struct retention_policy *policy)
{
	if (!policy)
		return;
	
	pthread_mutex_lock(&policy->lock);
	
	if (!policy->thread_running) {
		pthread_mutex_unlock(&policy->lock);
		return;
	}
	
	policy->thread_stop = true;
	
	pthread_mutex_unlock(&policy->lock);
	
	/* Wait for thread to finish */
	pthread_join(policy->cleanup_thread, NULL);
	
	pthread_mutex_lock(&policy->lock);
	policy->thread_running = false;
	pthread_mutex_unlock(&policy->lock);
}

bool retention_policy_check_event(struct retention_policy *policy,
                                  uint64_t timestamp)
{
	bool should_retain = true;
	
	if (!policy)
		return true;
	
	pthread_mutex_lock(&policy->lock);
	
	/* Check time-based retention */
	if (policy->config.max_age_seconds > 0) {
		uint64_t cutoff_time = get_current_time() - policy->config.max_age_seconds;
		if (timestamp < cutoff_time) {
			should_retain = false;
		}
	}
	
	pthread_mutex_unlock(&policy->lock);
	
	return should_retain;
}

bool retention_policy_get_stats(struct retention_policy *policy,
                               struct retention_stats *stats)
{
	if (!policy || !stats)
		return false;
	
	pthread_mutex_lock(&policy->lock);
	
	*stats = policy->stats;
	
	/* Update current counts if database available */
	if (policy->db) {
		uint64_t total_events, db_size;
		if (storage_db_get_stats(policy->db, &total_events, &db_size, NULL)) {
			stats->current_event_count = total_events;
			stats->current_db_size_bytes = db_size;
		}
	}
	
	pthread_mutex_unlock(&policy->lock);
	
	return true;
}

bool retention_policy_update_config(struct retention_policy *policy,
                                   struct retention_policy_config *config)
{
	if (!policy || !config)
		return false;
	
	pthread_mutex_lock(&policy->lock);
	
	policy->config = *config;
	
	/* Set defaults if not specified */
	if (policy->config.cleanup_interval == 0)
		policy->config.cleanup_interval = 3600;
	
	if (policy->config.batch_delete_size == 0)
		policy->config.batch_delete_size = 1000;
	
	pthread_mutex_unlock(&policy->lock);
	
	return true;
}
