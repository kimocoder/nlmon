/*
 * CSV Exporter Plugin
 * 
 * Exports network events to CSV format for analysis in spreadsheet applications.
 * Supports custom field selection and automatic file rotation.
 * 
 * Configuration:
 *   csv_exporter.output_path: Path to CSV file (default: /var/log/nlmon/events.csv)
 *   csv_exporter.fields: Comma-separated list of fields (default: timestamp,interface,event_type)
 *   csv_exporter.rotate_size: Rotate after N MB (default: 100)
 *   csv_exporter.include_header: Include CSV header (default: true)
 */

#include "plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* Plugin state */
static struct {
    FILE *csvfile;
    char output_path[256];
    char fields[256];
    uint64_t event_count;
    uint64_t bytes_written;
    uint64_t rotate_size;
    int include_header;
    int file_number;
    nlmon_plugin_context_t *ctx;
} state;

/* Get file size */
static long get_file_size(FILE *fp) {
    if (!fp) return 0;
    
    long current = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, current, SEEK_SET);
    
    return size;
}

/* Rotate CSV file */
static int rotate_csv_file(void) {
    if (!state.csvfile) return -1;
    
    fclose(state.csvfile);
    
    /* Rename current file */
    char rotated_path[512];
    snprintf(rotated_path, sizeof(rotated_path), "%s.%d", 
             state.output_path, state.file_number++);
    
    rename(state.output_path, rotated_path);
    
    state.ctx->log(NLMON_LOG_INFO, "CSV exporter: rotated to %s", rotated_path);
    
    /* Open new file */
    state.csvfile = fopen(state.output_path, "w");
    if (!state.csvfile) {
        state.ctx->log(NLMON_LOG_ERROR, "Failed to open new CSV file: %s", 
                      state.output_path);
        return -1;
    }
    
    /* Write header if enabled */
    if (state.include_header) {
        fprintf(state.csvfile, "%s\n", state.fields);
        fflush(state.csvfile);
    }
    
    return 0;
}

/* Write CSV header */
static void write_csv_header(void) {
    if (!state.csvfile || !state.include_header) return;
    
    fprintf(state.csvfile, "%s\n", state.fields);
    fflush(state.csvfile);
}

/* Initialize plugin */
static int csv_exporter_init(nlmon_plugin_context_t *ctx) {
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    
    /* Get configuration */
    const char *output_path = ctx->get_config("csv_exporter.output_path");
    if (!output_path) {
        output_path = "/var/log/nlmon/events.csv";
    }
    strncpy(state.output_path, output_path, sizeof(state.output_path) - 1);
    
    const char *fields = ctx->get_config("csv_exporter.fields");
    if (!fields) {
        fields = "timestamp,interface,event_type";
    }
    strncpy(state.fields, fields, sizeof(state.fields) - 1);
    
    const char *rotate_str = ctx->get_config("csv_exporter.rotate_size");
    state.rotate_size = rotate_str ? atoi(rotate_str) * 1024 * 1024 : 100 * 1024 * 1024;
    
    const char *header_str = ctx->get_config("csv_exporter.include_header");
    state.include_header = (!header_str || strcmp(header_str, "true") == 0) ? 1 : 0;
    
    /* Open CSV file */
    state.csvfile = fopen(state.output_path, "a");
    if (!state.csvfile) {
        ctx->log(NLMON_LOG_ERROR, "Failed to open CSV file: %s", state.output_path);
        return -1;
    }
    
    /* Write header if file is empty */
    if (get_file_size(state.csvfile) == 0) {
        write_csv_header();
    }
    
    ctx->log(NLMON_LOG_INFO, "CSV exporter initialized: %s (fields: %s)", 
             state.output_path, state.fields);
    
    /* Register custom command */
    ctx->register_command("csv_stats", NULL, "Show CSV exporter statistics");
    ctx->register_command("csv_rotate", NULL, "Manually rotate CSV file");
    
    return 0;
}

/* Cleanup plugin */
static void csv_exporter_cleanup(void) {
    if (state.csvfile) {
        fclose(state.csvfile);
        state.csvfile = NULL;
    }
    
    if (state.ctx) {
        state.ctx->log(NLMON_LOG_INFO, "CSV exporter cleaned up (%lu events)", 
                      state.event_count);
    }
}

/* Export event to CSV */
static int csv_exporter_on_event(struct nlmon_event *event) {
    if (!state.csvfile || !event) {
        return -1;
    }
    
    state.event_count++;
    
    /* Write CSV row - simplified example */
    time_t now = time(NULL);
    int written = fprintf(state.csvfile, "%ld,event_%lu,network_event\n",
                         now, state.event_count);
    
    if (written > 0) {
        state.bytes_written += written;
    }
    
    /* Check if rotation needed */
    if (get_file_size(state.csvfile) >= (long)state.rotate_size) {
        rotate_csv_file();
    }
    
    /* Flush every 100 events */
    if (state.event_count % 100 == 0) {
        fflush(state.csvfile);
    }
    
    return 0;
}

/* Handle custom command */
static int csv_exporter_on_command(const char *cmd, const char *args, 
                                   char *response, size_t resp_len) {
    if (!cmd || !response) {
        return -1;
    }
    
    if (strcmp(cmd, "csv_stats") == 0) {
        long file_size = get_file_size(state.csvfile);
        snprintf(response, resp_len,
                "CSV Exporter Statistics:\n"
                "  Output file: %s\n"
                "  Fields: %s\n"
                "  Events exported: %lu\n"
                "  Bytes written: %lu\n"
                "  Current file size: %ld bytes\n"
                "  Rotate size: %lu MB\n"
                "  Files rotated: %d\n",
                state.output_path,
                state.fields,
                state.event_count,
                state.bytes_written,
                file_size,
                state.rotate_size / (1024 * 1024),
                state.file_number);
        return 0;
    }
    
    if (strcmp(cmd, "csv_rotate") == 0) {
        if (rotate_csv_file() == 0) {
            snprintf(response, resp_len, "CSV file rotated successfully");
            return 0;
        } else {
            snprintf(response, resp_len, "Failed to rotate CSV file");
            return -1;
        }
    }
    
    snprintf(response, resp_len, "Unknown command: %s", cmd);
    return -1;
}

/* Handle config reload */
static int csv_exporter_on_config_reload(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "CSV exporter: configuration reloaded");
    
    /* Update rotate size */
    const char *rotate_str = ctx->get_config("csv_exporter.rotate_size");
    if (rotate_str) {
        state.rotate_size = atoi(rotate_str) * 1024 * 1024;
        ctx->log(NLMON_LOG_INFO, "CSV exporter: rotate size = %lu MB", 
                state.rotate_size / (1024 * 1024));
    }
    
    return 0;
}

/* Plugin descriptor */
static nlmon_plugin_t csv_exporter_plugin = {
    .name = "csv_exporter",
    .version = "1.0.0",
    .description = "Exports network events to CSV format",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = csv_exporter_init,
        .cleanup = csv_exporter_cleanup,
        .on_event = csv_exporter_on_event,
        .on_command = csv_exporter_on_command,
        .on_config_reload = csv_exporter_on_config_reload,
    },
    .event_filter = NULL,  /* Process all events */
    .flags = NLMON_PLUGIN_FLAG_NONE,
    .dependencies = NULL,
};

/* Plugin registration */
NLMON_PLUGIN_DEFINE(csv_exporter_plugin);
