/**
 * @file wmi_error.c
 * @brief WMI error handling implementation
 */

#include "wmi_error.h"
#include <string.h>
#include <stdarg.h>
#include <errno.h>

/**
 * Get human-readable error message for error code
 */
const char *wmi_error_string(int error_code)
{
	switch (error_code) {
	case WMI_SUCCESS:
		return "Success";
	
	/* Configuration errors */
	case WMI_ERR_INVALID_CONFIG:
		return "Invalid configuration";
	case WMI_ERR_INVALID_PARAM:
		return "Invalid parameter";
	case WMI_ERR_NULL_POINTER:
		return "NULL pointer provided";
	
	/* I/O errors */
	case WMI_ERR_FILE_NOT_FOUND:
		return "File not found";
	case WMI_ERR_PERMISSION_DENIED:
		return "Permission denied";
	case WMI_ERR_IO_ERROR:
		return "I/O error";
	case WMI_ERR_EOF:
		return "End of file";
	case WMI_ERR_FILE_ROTATION:
		return "File rotation detected";
	
	/* Parsing errors */
	case WMI_ERR_PARSE_FAILED:
		return "Parse failed";
	case WMI_ERR_UNKNOWN_FORMAT:
		return "Unknown log format";
	case WMI_ERR_INVALID_CMD_ID:
		return "Invalid command ID";
	case WMI_ERR_INVALID_MAC:
		return "Invalid MAC address";
	case WMI_ERR_TRUNCATED_LINE:
		return "Truncated log line";
	case WMI_ERR_MALFORMED_DATA:
		return "Malformed data";
	
	/* Resource errors */
	case WMI_ERR_NO_MEMORY:
		return "Out of memory";
	case WMI_ERR_BUFFER_FULL:
		return "Buffer full";
	case WMI_ERR_QUEUE_FULL:
		return "Queue full";
	case WMI_ERR_RESOURCE_LIMIT:
		return "Resource limit reached";
	
	/* State errors */
	case WMI_ERR_NOT_INITIALIZED:
		return "Not initialized";
	case WMI_ERR_ALREADY_RUNNING:
		return "Already running";
	case WMI_ERR_NOT_RUNNING:
		return "Not running";
	case WMI_ERR_INVALID_STATE:
		return "Invalid state";
	
	/* Callback errors */
	case WMI_ERR_CALLBACK_FAILED:
		return "Callback failed";
	case WMI_ERR_NO_CALLBACK:
		return "No callback registered";
	
	default:
		return "Unknown error";
	}
}

/**
 * Get error category name
 */
const char *wmi_error_category(int error_code)
{
	if (error_code >= -10 && error_code <= -1)
		return "Configuration";
	else if (error_code >= -20 && error_code <= -11)
		return "I/O";
	else if (error_code >= -30 && error_code <= -21)
		return "Parsing";
	else if (error_code >= -40 && error_code <= -31)
		return "Resource";
	else if (error_code >= -50 && error_code <= -41)
		return "State";
	else if (error_code >= -60 && error_code <= -51)
		return "Callback";
	else if (error_code == 0)
		return "Success";
	else
		return "Unknown";
}

/**
 * Get severity string
 */
static const char *severity_string(enum wmi_error_severity severity)
{
	switch (severity) {
	case WMI_SEV_DEBUG:    return "DEBUG";
	case WMI_SEV_INFO:     return "INFO";
	case WMI_SEV_WARNING:  return "WARNING";
	case WMI_SEV_ERROR:    return "ERROR";
	case WMI_SEV_CRITICAL: return "CRITICAL";
	default:               return "UNKNOWN";
	}
}

/**
 * Log error with context
 */
void wmi_log_error(enum wmi_error_severity severity, int error_code,
                   const char *context, const char *file, int line)
{
	FILE *out = (severity >= WMI_SEV_ERROR) ? stderr : stdout;
	
	fprintf(out, "[WMI %s] %s: %s",
	        severity_string(severity),
	        wmi_error_category(error_code),
	        wmi_error_string(error_code));
	
	if (context) {
		fprintf(out, " - %s", context);
	}
	
	if (severity >= WMI_SEV_ERROR && file) {
		fprintf(out, " (%s:%d)", file, line);
	}
	
	/* Add errno if it's an I/O error */
	if (error_code >= -20 && error_code <= -11 && errno != 0) {
		fprintf(out, " [errno=%d: %s]", errno, strerror(errno));
	}
	
	fprintf(out, "\n");
	fflush(out);
}

/**
 * Log error with formatted context
 */
void wmi_log_error_fmt(enum wmi_error_severity severity, int error_code,
                       const char *file, int line, const char *fmt, ...)
{
	char context[512];
	va_list args;
	
	va_start(args, fmt);
	vsnprintf(context, sizeof(context), fmt, args);
	va_end(args);
	
	wmi_log_error(severity, error_code, context, file, line);
}

/**
 * Initialize error statistics
 */
void wmi_error_stats_init(struct wmi_error_stats *stats)
{
	if (!stats)
		return;
	
	memset(stats, 0, sizeof(*stats));
}

/**
 * Record an error in statistics
 */
void wmi_error_stats_record(struct wmi_error_stats *stats, int error_code,
                            const char *context)
{
	if (!stats)
		return;
	
	time_t now = time(NULL);
	
	/* Update total counts */
	stats->total_errors++;
	stats->total_operations++;
	
	/* Track timing */
	if (stats->first_error_time == 0) {
		stats->first_error_time = now;
	}
	stats->last_error_time = now;
	
	/* Update category counts */
	if (error_code >= -10 && error_code <= -1) {
		stats->config_errors++;
	} else if (error_code >= -20 && error_code <= -11) {
		stats->io_errors++;
	} else if (error_code >= -30 && error_code <= -21) {
		stats->parse_errors++;
	} else if (error_code >= -40 && error_code <= -31) {
		stats->resource_errors++;
	} else if (error_code >= -50 && error_code <= -41) {
		stats->state_errors++;
	} else if (error_code >= -60 && error_code <= -51) {
		stats->callback_errors++;
	}
	
	/* Update specific error counts */
	switch (error_code) {
	case WMI_ERR_FILE_NOT_FOUND:
		stats->file_not_found++;
		break;
	case WMI_ERR_PERMISSION_DENIED:
		stats->permission_denied++;
		break;
	case WMI_ERR_PARSE_FAILED:
	case WMI_ERR_UNKNOWN_FORMAT:
	case WMI_ERR_TRUNCATED_LINE:
	case WMI_ERR_MALFORMED_DATA:
		stats->malformed_lines++;
		break;
	case WMI_ERR_INVALID_CMD_ID:
		stats->unknown_cmd_ids++;
		break;
	case WMI_ERR_INVALID_MAC:
		stats->invalid_macs++;
		break;
	case WMI_ERR_BUFFER_FULL:
	case WMI_ERR_QUEUE_FULL:
		stats->buffer_overflows++;
		stats->lines_dropped++;
		break;
	}
	
	/* Store last error info */
	stats->last_error_code = error_code;
	snprintf(stats->last_error_msg, sizeof(stats->last_error_msg),
	         "%s", wmi_error_string(error_code));
	
	if (context) {
		snprintf(stats->last_error_context, sizeof(stats->last_error_context),
		         "%s", context);
	} else {
		stats->last_error_context[0] = '\0';
	}
}

/**
 * Get error rate (errors per operation)
 */
double wmi_error_stats_get_rate(const struct wmi_error_stats *stats)
{
	if (!stats || stats->total_operations == 0)
		return 0.0;
	
	return (double)stats->total_errors / (double)stats->total_operations;
}

/**
 * Print error statistics
 */
void wmi_error_stats_print(const struct wmi_error_stats *stats, FILE *fp)
{
	if (!stats || !fp)
		return;
	
	fprintf(fp, "\n=== WMI Error Statistics ===\n");
	fprintf(fp, "Total Operations: %llu\n", (unsigned long long)stats->total_operations);
	fprintf(fp, "Total Errors: %llu\n", (unsigned long long)stats->total_errors);
	fprintf(fp, "Error Rate: %.2f%%\n", wmi_error_stats_get_rate(stats) * 100.0);
	
	fprintf(fp, "\nErrors by Category:\n");
	fprintf(fp, "  Configuration: %llu\n", (unsigned long long)stats->config_errors);
	fprintf(fp, "  I/O: %llu\n", (unsigned long long)stats->io_errors);
	fprintf(fp, "  Parsing: %llu\n", (unsigned long long)stats->parse_errors);
	fprintf(fp, "  Resource: %llu\n", (unsigned long long)stats->resource_errors);
	fprintf(fp, "  State: %llu\n", (unsigned long long)stats->state_errors);
	fprintf(fp, "  Callback: %llu\n", (unsigned long long)stats->callback_errors);
	
	fprintf(fp, "\nSpecific Errors:\n");
	fprintf(fp, "  File Not Found: %llu\n", (unsigned long long)stats->file_not_found);
	fprintf(fp, "  Permission Denied: %llu\n", (unsigned long long)stats->permission_denied);
	fprintf(fp, "  Malformed Lines: %llu\n", (unsigned long long)stats->malformed_lines);
	fprintf(fp, "  Unknown Command IDs: %llu\n", (unsigned long long)stats->unknown_cmd_ids);
	fprintf(fp, "  Invalid MAC Addresses: %llu\n", (unsigned long long)stats->invalid_macs);
	fprintf(fp, "  Buffer Overflows: %llu\n", (unsigned long long)stats->buffer_overflows);
	fprintf(fp, "  Lines Dropped: %llu\n", (unsigned long long)stats->lines_dropped);
	
	if (stats->last_error_code != 0) {
		fprintf(fp, "\nLast Error:\n");
		fprintf(fp, "  Code: %d\n", stats->last_error_code);
		fprintf(fp, "  Message: %s\n", stats->last_error_msg);
		if (stats->last_error_context[0]) {
			fprintf(fp, "  Context: %s\n", stats->last_error_context);
		}
		if (stats->last_error_time > 0) {
			char time_buf[64];
			struct tm *tm_info = localtime(&stats->last_error_time);
			strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
			fprintf(fp, "  Time: %s\n", time_buf);
		}
	}
	
	if (stats->first_error_time > 0 && stats->last_error_time > 0) {
		time_t duration = stats->last_error_time - stats->first_error_time;
		fprintf(fp, "\nError Duration: %lld seconds\n", (long long)duration);
		if (duration > 0) {
			double errors_per_sec = (double)stats->total_errors / (double)duration;
			fprintf(fp, "Errors per Second: %.2f\n", errors_per_sec);
		}
	}
	
	fprintf(fp, "============================\n\n");
	fflush(fp);
}

/**
 * Reset error statistics
 */
void wmi_error_stats_reset(struct wmi_error_stats *stats)
{
	if (!stats)
		return;
	
	memset(stats, 0, sizeof(*stats));
}
