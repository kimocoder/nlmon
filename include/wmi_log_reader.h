/**
 * @file wmi_log_reader.h
 * @brief WMI log reader interface for parsing device logs
 *
 * This module provides functionality to read WMI command logs from files
 * or stdin, parse them line by line, and invoke callbacks for processing.
 */

#ifndef WMI_LOG_READER_H
#define WMI_LOG_READER_H

#include <stddef.h>
#include <stdint.h>
#include "wmi_error.h"

/**
 * WMI log reader configuration structure
 */
struct wmi_log_config {
    const char *log_source;      /**< File path or "-" for stdin */
    int follow_mode;             /**< Enable tail -f style following (1=enabled, 0=disabled) */
    size_t buffer_size;          /**< Line buffer size in bytes (default: 4096) */
    
    /**
     * Callback function invoked for each complete log line
     * @param line The complete log line (null-terminated)
     * @param user_data User-provided context data
     * @return 0 on success, negative on error
     */
    int (*callback)(const char *line, void *user_data);
    
    void *user_data;             /**< User data passed to callback */
};

/**
 * WMI log reader statistics
 */
struct wmi_log_stats {
    uint64_t lines_read;         /**< Total lines successfully read */
    uint64_t lines_dropped;      /**< Lines dropped due to buffer overflow */
    uint64_t parse_errors;       /**< Number of parsing errors */
    uint64_t bytes_read;         /**< Total bytes read from source */
};

/**
 * Initialize WMI log reader with configuration
 *
 * @param config Configuration structure with log source and options
 * @return 0 on success, negative error code on failure
 *         -1: Invalid configuration
 *         -2: Failed to open log source
 *         -3: Memory allocation failure
 */
int wmi_log_reader_init(struct wmi_log_config *config);

/**
 * Start reading WMI logs
 *
 * This function begins reading from the configured log source and
 * invoking the callback for each complete line. It blocks until
 * the log source is exhausted (file EOF) or an error occurs.
 * In follow mode, it continues monitoring the file for new data.
 *
 * @return 0 on normal completion, negative error code on failure
 *         -1: Reader not initialized
 *         -2: I/O error during reading
 *         -3: Callback error
 */
int wmi_log_reader_start(void);

/**
 * Stop reading WMI logs
 *
 * Signals the reader to stop processing. Safe to call from signal
 * handlers or other threads.
 */
void wmi_log_reader_stop(void);

/**
 * Get current reader statistics
 *
 * @param stats Pointer to statistics structure to fill
 * @return 0 on success, -1 if reader not initialized
 */
int wmi_log_reader_get_stats(struct wmi_log_stats *stats);

/**
 * Get current error statistics
 *
 * @param stats Pointer to error statistics structure to fill
 * @return 0 on success, -1 if reader not initialized
 */
int wmi_log_reader_get_error_stats(struct wmi_error_stats *stats);

/**
 * Cleanup WMI log reader and free resources
 *
 * Closes file descriptors, frees buffers, and resets state.
 * Safe to call multiple times.
 */
void wmi_log_reader_cleanup(void);

#endif /* WMI_LOG_READER_H */
