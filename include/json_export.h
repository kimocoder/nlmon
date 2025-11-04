/* json_export.h - JSON export format with rotation support
 *
 * Provides JSON event serialization with streaming mode and
 * file rotation capabilities.
 */

#ifndef JSON_EXPORT_H
#define JSON_EXPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* JSON formatting options */
enum json_format {
	JSON_FORMAT_COMPACT,    /* Compact single-line format */
	JSON_FORMAT_PRETTY      /* Pretty-printed with indentation */
};

/* JSON rotation policy */
struct json_rotation_policy {
	size_t max_file_size;      /* Maximum file size before rotation (bytes) */
	size_t max_rotations;      /* Maximum number of rotated files to keep */
	bool compress_rotated;     /* Compress rotated files with gzip */
};

/* JSON exporter handle (opaque) */
struct json_exporter;

/* Event structure for JSON export */
struct json_event {
	uint64_t timestamp_sec;
	uint64_t timestamp_usec;
	uint64_t sequence;
	const char *event_type;
	uint16_t message_type;
	const char *message_type_str;
	const char *interface;
	const char *namespace;
	const char *details;       /* JSON string with additional details */
	const char *correlation_id;
};

/**
 * json_exporter_create() - Create JSON exporter
 * @filename: Output filename (NULL for stdout)
 * @format: JSON formatting option
 * @streaming: Enable streaming mode (one event per line)
 * @policy: Rotation policy (NULL for no rotation)
 *
 * Returns: JSON exporter handle or NULL on error
 */
struct json_exporter *json_exporter_create(const char *filename,
                                           enum json_format format,
                                           bool streaming,
                                           struct json_rotation_policy *policy);

/**
 * json_exporter_destroy() - Destroy JSON exporter
 * @exporter: JSON exporter handle
 */
void json_exporter_destroy(struct json_exporter *exporter);

/**
 * json_exporter_write_event() - Write event to JSON output
 * @exporter: JSON exporter handle
 * @event: Event to write
 *
 * Returns: true on success, false on error
 */
bool json_exporter_write_event(struct json_exporter *exporter,
                               struct json_event *event);

/**
 * json_exporter_flush() - Flush pending writes
 * @exporter: JSON exporter handle
 *
 * Returns: true on success, false on error
 */
bool json_exporter_flush(struct json_exporter *exporter);

/**
 * json_exporter_get_stats() - Get exporter statistics
 * @exporter: JSON exporter handle
 * @events_written: Output for events written
 * @bytes_written: Output for bytes written
 * @current_file_size: Output for current file size
 * @rotations: Output for number of rotations
 *
 * Returns: true on success, false on error
 */
bool json_exporter_get_stats(struct json_exporter *exporter,
                             uint64_t *events_written,
                             uint64_t *bytes_written,
                             uint64_t *current_file_size,
                             uint32_t *rotations);

#endif /* JSON_EXPORT_H */
