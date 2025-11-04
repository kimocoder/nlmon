/*
 * Event Logger Plugin
 * 
 * A simple plugin that logs all network events to a file with timestamps
 * and provides statistics about logged events.
 * 
 * Configuration:
 *   event_logger.log_path: Path to log file (default: /var/log/nlmon/events.log)
 *   event_logger.log_format: Format (text|json) (default: text)
 *   event_logger.flush_interval: Flush after N events (default: 10)
 */

#include "plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* Plugin state */
static struct {
    FILE *logfile;
    char log_path[256];
    char log_format[16];
    uint64_t event_count;
    uint64_t bytes_written;
    int flush_interval;
    nlmon_plugin_context_t *ctx;
} state;

/* Get timestamp string */
static void get_timestamp(char *buf, size_t len) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    struct tm *tm_info = localtime(&tv.tv_sec);
    size_t written = strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
    snprintf(buf + written, len - written, ".%03ld", tv.tv_usec / 1000);
}

/* Initialize plugin */
static int event_logger_init(nlmon_plugin_context_t *ctx) {
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    
    /* Get configuration */
    const char *log_path = ctx->get_config("event_logger.log_path");
    if (!log_path) {
        log_path = "/var/log/nlmon/events.log";
    }
    strncpy(state.log_path, log_path, sizeof(state.log_path) - 1);
    
    const char *log_format = ctx->get_config("event_logger.log_format");
    if (!log_format) {
        log_format = "text";
    }
    strncpy(state.log_format, log_format, sizeof(state.log_format) - 1);
    
    const char *flush_str = ctx->get_config("event_logger.flush_interval");
    state.flush_interval = flush_str ? atoi(flush_str) : 10;
    
    /* Open log file */
    state.logfile = fopen(state.log_path, "a");
    if (!state.logfile) {
        ctx->log(NLMON_LOG_ERROR, "Failed to open log file: %s", state.log_path);
        return -1;
    }
    
    ctx->log(NLMON_LOG_INFO, "Event logger initialized: %s (format: %s)", 
             state.log_path, state.log_format);
    
    /* Register custom command */
    ctx->register_command("logger_stats", NULL, "Show event logger statistics");
    
    /* Write header */
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(state.logfile, "\n=== Event Logger Started: %s ===\n", timestamp);
    fflush(state.logfile);
    
    return 0;
}

/* Cleanup plugin */
static void event_logger_cleanup(void) {
    if (state.logfile) {
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        
        fprintf(state.logfile, "=== Event Logger Stopped: %s ===\n", timestamp);
        fprintf(state.logfile, "Total events logged: %lu\n", state.event_count);
        fprintf(state.logfile, "Total bytes written: %lu\n", state.bytes_written);
        
        fclose(state.logfile);
        state.logfile = NULL;
    }
    
    if (state.ctx) {
        state.ctx->log(NLMON_LOG_INFO, "Event logger cleaned up (%lu events)", 
                      state.event_count);
    }
}

/* Log event in text format */
static void log_event_text(struct nlmon_event *event) {
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    int written = fprintf(state.logfile, "[%s] Event #%lu\n", 
                         timestamp, state.event_count);
    
    if (written > 0) {
        state.bytes_written += written;
    }
}

/* Log event in JSON format */
static void log_event_json(struct nlmon_event *event) {
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    int written = fprintf(state.logfile, 
                         "{\"timestamp\":\"%s\",\"sequence\":%lu}\n",
                         timestamp, state.event_count);
    
    if (written > 0) {
        state.bytes_written += written;
    }
}

/* Process event */
static int event_logger_on_event(struct nlmon_event *event) {
    if (!state.logfile || !event) {
        return -1;
    }
    
    state.event_count++;
    
    /* Log based on format */
    if (strcmp(state.log_format, "json") == 0) {
        log_event_json(event);
    } else {
        log_event_text(event);
    }
    
    /* Flush periodically */
    if (state.event_count % state.flush_interval == 0) {
        fflush(state.logfile);
    }
    
    return 0;
}

/* Handle custom command */
static int event_logger_on_command(const char *cmd, const char *args, 
                                   char *response, size_t resp_len) {
    if (!cmd || !response) {
        return -1;
    }
    
    if (strcmp(cmd, "logger_stats") == 0) {
        snprintf(response, resp_len,
                "Event Logger Statistics:\n"
                "  Log file: %s\n"
                "  Format: %s\n"
                "  Events logged: %lu\n"
                "  Bytes written: %lu\n"
                "  Flush interval: %d events\n",
                state.log_path,
                state.log_format,
                state.event_count,
                state.bytes_written,
                state.flush_interval);
        return 0;
    }
    
    snprintf(response, resp_len, "Unknown command: %s", cmd);
    return -1;
}

/* Handle config reload */
static int event_logger_on_config_reload(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "Event logger: configuration reloaded");
    
    /* Check if log path changed */
    const char *new_path = ctx->get_config("event_logger.log_path");
    if (new_path && strcmp(new_path, state.log_path) != 0) {
        ctx->log(NLMON_LOG_INFO, "Event logger: switching to new log file: %s", new_path);
        
        /* Close old file */
        if (state.logfile) {
            fclose(state.logfile);
        }
        
        /* Open new file */
        strncpy(state.log_path, new_path, sizeof(state.log_path) - 1);
        state.logfile = fopen(state.log_path, "a");
        
        if (!state.logfile) {
            ctx->log(NLMON_LOG_ERROR, "Failed to open new log file: %s", state.log_path);
            return -1;
        }
    }
    
    /* Update flush interval */
    const char *flush_str = ctx->get_config("event_logger.flush_interval");
    if (flush_str) {
        state.flush_interval = atoi(flush_str);
        ctx->log(NLMON_LOG_INFO, "Event logger: flush interval = %d", state.flush_interval);
    }
    
    return 0;
}

/* Plugin descriptor */
static nlmon_plugin_t event_logger_plugin = {
    .name = "event_logger",
    .version = "1.0.0",
    .description = "Logs all network events to a file",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = event_logger_init,
        .cleanup = event_logger_cleanup,
        .on_event = event_logger_on_event,
        .on_command = event_logger_on_command,
        .on_config_reload = event_logger_on_config_reload,
    },
    .event_filter = NULL,  /* Process all events */
    .flags = NLMON_PLUGIN_FLAG_PROCESS_ALL,
    .dependencies = NULL,
};

/* Plugin registration */
NLMON_PLUGIN_DEFINE(event_logger_plugin);
