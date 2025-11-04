/*
 * Example nlmon plugin demonstrating the plugin API
 * 
 * This plugin logs all network events to a file and provides
 * a custom command to query statistics.
 */

#include "plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Plugin state */
static struct {
    FILE *logfile;
    uint64_t event_count;
    uint64_t link_events;
    uint64_t addr_events;
    uint64_t route_events;
    nlmon_plugin_context_t *ctx;
} plugin_state;

/* Initialize plugin */
static int example_init(nlmon_plugin_context_t *ctx) {
    memset(&plugin_state, 0, sizeof(plugin_state));
    plugin_state.ctx = ctx;
    
    /* Get log file path from configuration */
    const char *log_path = ctx->get_config("example_plugin.log_path");
    if (!log_path) {
        log_path = "/tmp/nlmon_example.log";
    }
    
    /* Open log file */
    plugin_state.logfile = fopen(log_path, "a");
    if (!plugin_state.logfile) {
        ctx->log(NLMON_LOG_ERROR, "Failed to open log file: %s", log_path);
        return -1;
    }
    
    ctx->log(NLMON_LOG_INFO, "Example plugin initialized, logging to: %s", log_path);
    
    /* Register custom command */
    ctx->register_command("example_stats", NULL, "Show example plugin statistics");
    
    /* Write header to log file */
    time_t now = time(NULL);
    fprintf(plugin_state.logfile, "\n=== Example Plugin Started: %s", ctime(&now));
    fflush(plugin_state.logfile);
    
    return 0;
}

/* Cleanup plugin */
static void example_cleanup(void) {
    if (plugin_state.logfile) {
        time_t now = time(NULL);
        fprintf(plugin_state.logfile, "=== Example Plugin Stopped: %s", ctime(&now));
        fprintf(plugin_state.logfile, "Total events processed: %lu\n", plugin_state.event_count);
        fclose(plugin_state.logfile);
        plugin_state.logfile = NULL;
    }
    
    if (plugin_state.ctx) {
        plugin_state.ctx->log(NLMON_LOG_INFO, "Example plugin cleaned up");
    }
}

/* Process event */
static int example_on_event(struct nlmon_event *event) {
    if (!plugin_state.logfile || !event) {
        return -1;
    }
    
    plugin_state.event_count++;
    
    /* Count event types (simplified - would need actual event structure) */
    /* This is just an example showing how to process events */
    
    /* Log event to file */
    fprintf(plugin_state.logfile, "[%lu] Event #%lu processed\n",
            (unsigned long)time(NULL),
            plugin_state.event_count);
    
    /* Flush periodically */
    if (plugin_state.event_count % 100 == 0) {
        fflush(plugin_state.logfile);
    }
    
    return 0;  /* Success */
}

/* Handle custom command */
static int example_on_command(const char *cmd, const char *args, char *response, size_t resp_len) {
    if (!cmd || !response) {
        return -1;
    }
    
    if (strcmp(cmd, "example_stats") == 0) {
        snprintf(response, resp_len,
                "Example Plugin Statistics:\n"
                "  Total events: %lu\n"
                "  Link events: %lu\n"
                "  Addr events: %lu\n"
                "  Route events: %lu\n",
                plugin_state.event_count,
                plugin_state.link_events,
                plugin_state.addr_events,
                plugin_state.route_events);
        return 0;
    }
    
    snprintf(response, resp_len, "Unknown command: %s", cmd);
    return -1;
}

/* Handle config reload */
static int example_on_config_reload(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "Example plugin: configuration reloaded");
    
    /* Could re-read configuration and adjust behavior */
    const char *log_path = ctx->get_config("example_plugin.log_path");
    if (log_path) {
        ctx->log(NLMON_LOG_INFO, "Example plugin: log path = %s", log_path);
    }
    
    return 0;
}

/* Event filter - only process link events */
static int example_event_filter(struct nlmon_event *event) {
    /* This is an example - actual implementation would check event type */
    /* For now, process all events */
    return 1;
}

/* Plugin descriptor */
static nlmon_plugin_t example_plugin = {
    .name = "example_plugin",
    .version = "1.0.0",
    .description = "Example plugin demonstrating the plugin API",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = example_init,
        .cleanup = example_cleanup,
        .on_event = example_on_event,
        .on_command = example_on_command,
        .on_config_reload = example_on_config_reload,
    },
    .event_filter = example_event_filter,
    .flags = NLMON_PLUGIN_FLAG_NONE,
    .dependencies = NULL,
};

/* Plugin registration */
NLMON_PLUGIN_DEFINE(example_plugin);
