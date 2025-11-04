/* log_rotation.h - Log file rotation and compression
 *
 * Provides generic log file rotation with size-based and time-based
 * policies, and optional gzip compression of rotated files.
 */

#ifndef LOG_ROTATION_H
#define LOG_ROTATION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>

/* Rotation trigger types */
enum rotation_trigger {
	ROTATION_TRIGGER_SIZE,      /* Rotate when file reaches size limit */
	ROTATION_TRIGGER_TIME,      /* Rotate at specific time intervals */
	ROTATION_TRIGGER_BOTH       /* Rotate on either condition */
};

/* Time-based rotation intervals */
enum rotation_interval {
	ROTATION_HOURLY,
	ROTATION_DAILY,
	ROTATION_WEEKLY,
	ROTATION_MONTHLY
};

/* Log rotation policy */
struct log_rotation_policy {
	enum rotation_trigger trigger;
	
	/* Size-based rotation */
	size_t max_file_size;       /* Maximum file size in bytes */
	
	/* Time-based rotation */
	enum rotation_interval interval;
	int rotation_hour;          /* Hour of day for daily rotation (0-23) */
	int rotation_minute;        /* Minute of hour for rotation (0-59) */
	
	/* General settings */
	size_t max_rotations;       /* Maximum number of rotated files to keep */
	bool compress_rotated;      /* Compress rotated files with gzip */
	bool sync_writes;           /* Sync after each write */
};

/* Log rotator handle (opaque) */
struct log_rotator;

/**
 * log_rotator_create() - Create log rotator
 * @base_filename: Base filename for log files
 * @policy: Rotation policy
 *
 * Returns: Log rotator handle or NULL on error
 */
struct log_rotator *log_rotator_create(const char *base_filename,
                                       struct log_rotation_policy *policy);

/**
 * log_rotator_destroy() - Destroy log rotator
 * @rotator: Log rotator handle
 */
void log_rotator_destroy(struct log_rotator *rotator);

/**
 * log_rotator_write() - Write data to log file
 * @rotator: Log rotator handle
 * @data: Data to write
 * @len: Length of data
 *
 * Returns: Number of bytes written, or -1 on error
 */
ssize_t log_rotator_write(struct log_rotator *rotator,
                          const void *data,
                          size_t len);

/**
 * log_rotator_printf() - Write formatted string to log file
 * @rotator: Log rotator handle
 * @format: Printf-style format string
 * @...: Format arguments
 *
 * Returns: Number of bytes written, or -1 on error
 */
ssize_t log_rotator_printf(struct log_rotator *rotator,
                           const char *format, ...)
                           __attribute__((format(printf, 2, 3)));

/**
 * log_rotator_flush() - Flush pending writes
 * @rotator: Log rotator handle
 *
 * Returns: true on success, false on error
 */
bool log_rotator_flush(struct log_rotator *rotator);

/**
 * log_rotator_check_rotation() - Check if rotation is needed
 * @rotator: Log rotator handle
 *
 * Checks rotation conditions and rotates if necessary.
 * Returns: true if rotation occurred, false otherwise
 */
bool log_rotator_check_rotation(struct log_rotator *rotator);

/**
 * log_rotator_force_rotation() - Force immediate rotation
 * @rotator: Log rotator handle
 *
 * Returns: true on success, false on error
 */
bool log_rotator_force_rotation(struct log_rotator *rotator);

/**
 * log_rotator_get_stats() - Get rotator statistics
 * @rotator: Log rotator handle
 * @bytes_written: Output for total bytes written
 * @current_file_size: Output for current file size
 * @rotations: Output for number of rotations
 * @last_rotation_time: Output for last rotation timestamp
 *
 * Returns: true on success, false on error
 */
bool log_rotator_get_stats(struct log_rotator *rotator,
                           uint64_t *bytes_written,
                           uint64_t *current_file_size,
                           uint32_t *rotations,
                           time_t *last_rotation_time);

#endif /* LOG_ROTATION_H */
