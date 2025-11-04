/**
 * @file wmi_error.h
 * @brief WMI error handling framework
 *
 * Provides comprehensive error codes, logging, and statistics tracking
 * for WMI log parsing and processing operations.
 */

#ifndef WMI_ERROR_H
#define WMI_ERROR_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

/**
 * WMI error codes
 */
enum wmi_error {
	WMI_SUCCESS = 0,                    /**< Operation successful */
	
	/* Configuration errors (-1 to -10) */
	WMI_ERR_INVALID_CONFIG = -1,        /**< Invalid configuration */
	WMI_ERR_INVALID_PARAM = -2,         /**< Invalid parameter */
	WMI_ERR_NULL_POINTER = -3,          /**< NULL pointer provided */
	
	/* I/O errors (-11 to -20) */
	WMI_ERR_FILE_NOT_FOUND = -11,       /**< File not found */
	WMI_ERR_PERMISSION_DENIED = -12,    /**< Permission denied */
	WMI_ERR_IO_ERROR = -13,             /**< General I/O error */
	WMI_ERR_EOF = -14,                  /**< End of file reached */
	WMI_ERR_FILE_ROTATION = -15,        /**< File rotation detected */
	
	/* Parsing errors (-21 to -30) */
	WMI_ERR_PARSE_FAILED = -21,         /**< Failed to parse log line */
	WMI_ERR_UNKNOWN_FORMAT = -22,       /**< Unknown log format */
	WMI_ERR_INVALID_CMD_ID = -23,       /**< Invalid command ID */
	WMI_ERR_INVALID_MAC = -24,          /**< Invalid MAC address */
	WMI_ERR_TRUNCATED_LINE = -25,       /**< Truncated log line */
	WMI_ERR_MALFORMED_DATA = -26,       /**< Malformed data in log */
	
	/* Resource errors (-31 to -40) */
	WMI_ERR_NO_MEMORY = -31,            /**< Memory allocation failed */
	WMI_ERR_BUFFER_FULL = -32,          /**< Buffer is full */
	WMI_ERR_QUEUE_FULL = -33,           /**< Queue is full */
	WMI_ERR_RESOURCE_LIMIT = -34,       /**< Resource limit reached */
	
	/* State errors (-41 to -50) */
	WMI_ERR_NOT_INITIALIZED = -41,      /**< Component not initialized */
	WMI_ERR_ALREADY_RUNNING = -42,      /**< Already running */
	WMI_ERR_NOT_RUNNING = -43,          /**< Not running */
	WMI_ERR_INVALID_STATE = -44,        /**< Invalid state */
	
	/* Callback errors (-51 to -60) */
	WMI_ERR_CALLBACK_FAILED = -51,      /**< Callback function failed */
	WMI_ERR_NO_CALLBACK = -52,          /**< No callback registered */
};

/**
 * Error severity levels
 */
enum wmi_error_severity {
	WMI_SEV_DEBUG = 0,      /**< Debug information */
	WMI_SEV_INFO = 1,       /**< Informational message */
	WMI_SEV_WARNING = 2,    /**< Warning condition */
	WMI_SEV_ERROR = 3,      /**< Error condition */
	WMI_SEV_CRITICAL = 4,   /**< Critical error */
};

/**
 * Error statistics structure
 */
struct wmi_error_stats {
	/* Error counts by category */
	uint64_t config_errors;         /**< Configuration errors */
	uint64_t io_errors;             /**< I/O errors */
	uint64_t parse_errors;          /**< Parsing errors */
	uint64_t resource_errors;       /**< Resource errors */
	uint64_t state_errors;          /**< State errors */
	uint64_t callback_errors;       /**< Callback errors */
	
	/* Specific error counts */
	uint64_t file_not_found;        /**< File not found count */
	uint64_t permission_denied;     /**< Permission denied count */
	uint64_t malformed_lines;       /**< Malformed log lines */
	uint64_t unknown_cmd_ids;       /**< Unknown command IDs */
	uint64_t invalid_macs;          /**< Invalid MAC addresses */
	uint64_t buffer_overflows;      /**< Buffer overflow count */
	uint64_t lines_dropped;         /**< Lines dropped */
	
	/* Error rate tracking */
	uint64_t total_errors;          /**< Total error count */
	uint64_t total_operations;      /**< Total operations attempted */
	time_t first_error_time;        /**< Time of first error */
	time_t last_error_time;         /**< Time of last error */
	
	/* Last error info */
	int last_error_code;            /**< Last error code */
	char last_error_msg[256];       /**< Last error message */
	char last_error_context[512];   /**< Last error context */
};

/**
 * Get human-readable error message for error code
 *
 * @param error_code WMI error code
 * @return Error message string (never NULL)
 */
const char *wmi_error_string(int error_code);

/**
 * Get error category name
 *
 * @param error_code WMI error code
 * @return Category name string
 */
const char *wmi_error_category(int error_code);

/**
 * Log error with context
 *
 * @param severity Error severity level
 * @param error_code WMI error code
 * @param context Context string (can be NULL)
 * @param file Source file name
 * @param line Source line number
 */
void wmi_log_error(enum wmi_error_severity severity, int error_code,
                   const char *context, const char *file, int line);

/**
 * Log error with formatted context
 *
 * @param severity Error severity level
 * @param error_code WMI error code
 * @param file Source file name
 * @param line Source line number
 * @param fmt Format string
 * @param ... Format arguments
 */
void wmi_log_error_fmt(enum wmi_error_severity severity, int error_code,
                       const char *file, int line, const char *fmt, ...)
                       __attribute__((format(printf, 5, 6)));

/**
 * Initialize error statistics
 *
 * @param stats Pointer to statistics structure
 */
void wmi_error_stats_init(struct wmi_error_stats *stats);

/**
 * Record an error in statistics
 *
 * @param stats Pointer to statistics structure
 * @param error_code WMI error code
 * @param context Context string (can be NULL)
 */
void wmi_error_stats_record(struct wmi_error_stats *stats, int error_code,
                            const char *context);

/**
 * Get error rate (errors per operation)
 *
 * @param stats Pointer to statistics structure
 * @return Error rate (0.0 to 1.0)
 */
double wmi_error_stats_get_rate(const struct wmi_error_stats *stats);

/**
 * Print error statistics
 *
 * @param stats Pointer to statistics structure
 * @param fp File pointer to print to
 */
void wmi_error_stats_print(const struct wmi_error_stats *stats, FILE *fp);

/**
 * Reset error statistics
 *
 * @param stats Pointer to statistics structure
 */
void wmi_error_stats_reset(struct wmi_error_stats *stats);

/* Convenience macros for error logging */
#define WMI_LOG_DEBUG(code, ctx) \
	wmi_log_error(WMI_SEV_DEBUG, code, ctx, __FILE__, __LINE__)

#define WMI_LOG_INFO(code, ctx) \
	wmi_log_error(WMI_SEV_INFO, code, ctx, __FILE__, __LINE__)

#define WMI_LOG_WARNING(code, ctx) \
	wmi_log_error(WMI_SEV_WARNING, code, ctx, __FILE__, __LINE__)

#define WMI_LOG_ERROR(code, ctx) \
	wmi_log_error(WMI_SEV_ERROR, code, ctx, __FILE__, __LINE__)

#define WMI_LOG_CRITICAL(code, ctx) \
	wmi_log_error(WMI_SEV_CRITICAL, code, ctx, __FILE__, __LINE__)

#define WMI_LOG_ERROR_FMT(code, fmt, ...) \
	wmi_log_error_fmt(WMI_SEV_ERROR, code, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define WMI_LOG_WARNING_FMT(code, fmt, ...) \
	wmi_log_error_fmt(WMI_SEV_WARNING, code, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif /* WMI_ERROR_H */
