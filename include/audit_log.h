/* audit_log.h - Cryptographic audit log for security events
 *
 * Provides tamper-evident audit logging with cryptographic hash chaining,
 * separate security event log, and log rotation.
 */

#ifndef AUDIT_LOG_H
#define AUDIT_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declaration */
struct nlmon_event;

/* Audit log handle (opaque) */
struct audit_log;

/* Audit log configuration */
struct audit_log_config {
	const char *log_path;           /* Audit log file path */
	const char *security_log_path;  /* Security events log path (NULL=same) */
	size_t max_file_size;           /* Max file size before rotation (bytes) */
	size_t max_rotations;           /* Max number of rotated files to keep */
	bool sync_writes;               /* Sync after each write */
	bool verify_on_open;            /* Verify chain integrity on open */
};

/* Audit log entry severity */
enum audit_severity {
	AUDIT_INFO = 0,
	AUDIT_WARNING = 1,
	AUDIT_SECURITY = 2,
	AUDIT_CRITICAL = 3
};

/**
 * audit_log_open() - Open or create audit log
 * @config: Audit log configuration
 *
 * Returns: Audit log handle or NULL on error
 */
struct audit_log *audit_log_open(struct audit_log_config *config);

/**
 * audit_log_close() - Close audit log
 * @log: Audit log handle
 */
void audit_log_close(struct audit_log *log);

/**
 * audit_log_write() - Write event to audit log
 * @log: Audit log handle
 * @event: Event to log
 * @severity: Event severity
 * @message: Optional message (NULL for default)
 *
 * Returns: true on success, false on error
 */
bool audit_log_write(struct audit_log *log,
                     struct nlmon_event *event,
                     enum audit_severity severity,
                     const char *message);

/**
 * audit_log_write_message() - Write custom message to audit log
 * @log: Audit log handle
 * @severity: Message severity
 * @format: Printf-style format string
 * @...: Format arguments
 *
 * Returns: true on success, false on error
 */
bool audit_log_write_message(struct audit_log *log,
                             enum audit_severity severity,
                             const char *format, ...)
                             __attribute__((format(printf, 3, 4)));

/**
 * audit_log_verify() - Verify audit log integrity
 * @log_path: Path to audit log file
 * @error_line: Output for line number of first error (if any)
 *
 * Returns: true if log is valid, false if tampered or error
 */
bool audit_log_verify(const char *log_path, size_t *error_line);

/**
 * audit_log_rotate() - Manually rotate audit log
 * @log: Audit log handle
 *
 * Returns: true on success, false on error
 */
bool audit_log_rotate(struct audit_log *log);

/**
 * audit_log_get_stats() - Get audit log statistics
 * @log: Audit log handle
 * @total_entries: Output for total entries written
 * @security_entries: Output for security entries written
 * @file_size: Output for current file size
 * @rotations: Output for number of rotations performed
 *
 * Returns: true on success, false on error
 */
bool audit_log_get_stats(struct audit_log *log,
                        uint64_t *total_entries,
                        uint64_t *security_entries,
                        uint64_t *file_size,
                        uint64_t *rotations);

#endif /* AUDIT_LOG_H */
