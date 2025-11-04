/*
 * nlmon Plugin Template
 * 
 * Use this template as a starting point for creating new nlmon plugins.
 * 
 * Instructions:
 * 1. Copy this file to your plugin name: cp plugin_template.c my_plugin.c
 * 2. Replace "template" with your plugin name throughout
 * 3. Update plugin metadata (name, version, description)
 * 4. Implement the TODO sections
 * 5. Build: gcc -shared -fPIC -o my_plugin.so my_plugin.c -I../../../include
 * 6. Install: sudo cp my_plugin.so /usr/lib/nlmon/plugins/
 * 7. Enable in /etc/nlmon/nlmon.yaml
 */

#include "plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Plugin State
 * ============================================================================
 * Define your plugin's state variables here. This structure persists for the
 * lifetime of the plugin.
 */
static struct {
    nlmon_plugin_context_t *ctx;
    uint64_t event_count;
    
    /* TODO: Add your state variables here
     * Examples:
     *   FILE *logfile;
     *   char server_url[256];
     *   int connection_fd;
     *   pthread_mutex_t mutex;
     */
    
} plugin_state;

/* ============================================================================
 * Helper Functions
 * ============================================================================
 * Add your helper functions here.
 */

/* TODO: Add helper functions
 * Examples:
 *   static int connect_to_server(const char *host, int port);
 *   static void process_event_data(struct nlmon_event *event);
 *   static void write_to_log(const char *message);
 */

/* ============================================================================
 * Plugin Callbacks
 * ============================================================================
 */

/**
 * Initialize plugin
 * 
 * Called once when the plugin is loaded. Use this to:
 * - Initialize plugin state
 * - Allocate resources
 * - Read configuration
 * - Register commands
 * - Open files/connections
 * 
 * @param ctx Plugin context providing API access
 * @return 0 on success, -1 on error
 */
static int template_init(nlmon_plugin_context_t *ctx) {
    /* Initialize state */
    memset(&plugin_state, 0, sizeof(plugin_state));
    plugin_state.ctx = ctx;
    
    ctx->log(NLMON_LOG_INFO, "Template plugin initializing...");
    
    /* TODO: Read configuration
     * Example:
     *   const char *setting = ctx->get_config("template.setting");
     *   if (!setting) {
     *       ctx->log(NLMON_LOG_ERROR, "Missing required config: template.setting");
     *       return -1;
     *   }
     */
    
    /* TODO: Allocate resources
     * Example:
     *   plugin_state.logfile = fopen("/var/log/nlmon/template.log", "a");
     *   if (!plugin_state.logfile) {
     *       ctx->log(NLMON_LOG_ERROR, "Failed to open log file");
     *       return -1;
     *   }
     */
    
    /* TODO: Register commands
     * Example:
     *   ctx->register_command("template_stats", NULL, "Show template plugin statistics");
     */
    
    ctx->log(NLMON_LOG_INFO, "Template plugin initialized successfully");
    return 0;
}

/**
 * Cleanup plugin
 * 
 * Called once when the plugin is unloaded. Use this to:
 * - Free allocated memory
 * - Close files/connections
 * - Save state if needed
 * - Release resources
 */
static void template_cleanup(void) {
    if (plugin_state.ctx) {
        plugin_state.ctx->log(NLMON_LOG_INFO, 
                             "Template plugin cleaning up (%lu events processed)",
                             plugin_state.event_count);
    }
    
    /* TODO: Cleanup resources
     * Example:
     *   if (plugin_state.logfile) {
     *       fclose(plugin_state.logfile);
     *       plugin_state.logfile = NULL;
     *   }
     *   if (plugin_state.connection_fd >= 0) {
     *       close(plugin_state.connection_fd);
     *   }
     */
}

/**
 * Process network event
 * 
 * Called for each network event (if plugin subscribes to events).
 * Keep this function fast - avoid blocking operations.
 * 
 * @param event Network event to process
 * @return 0 on success, -1 on error, 1 to filter event
 */
static int template_on_event(struct nlmon_event *event) {
    /* Validate input */
    if (!event) {
        if (plugin_state.ctx) {
            plugin_state.ctx->log(NLMON_LOG_ERROR, "Null event received");
        }
        return -1;
    }
    
    plugin_state.event_count++;
    
    /* TODO: Process event
     * Example:
     *   if (event->message_type == RTM_NEWLINK) {
     *       plugin_state.ctx->log(NLMON_LOG_DEBUG, 
     *                            "Link event: %s", event->interface);
     *   }
     * 
     * For expensive operations, consider queuing for async processing:
     *   queue_event_for_processing(event);
     */
    
    return 0;  /* 0=success, -1=error, 1=filter event */
}

/**
 * Handle custom command
 * 
 * Called when a plugin-registered command is invoked from the CLI.
 * 
 * @param cmd Command name
 * @param args Command arguments (may be NULL)
 * @param response Buffer for response message
 * @param resp_len Size of response buffer
 * @return 0 on success, -1 on error
 */
static int template_on_command(const char *cmd, const char *args,
                               char *response, size_t resp_len) {
    /* Validate input */
    if (!cmd || !response) {
        return -1;
    }
    
    /* TODO: Handle commands
     * Example:
     *   if (strcmp(cmd, "template_stats") == 0) {
     *       snprintf(response, resp_len,
     *                "Template Plugin Statistics:\n"
     *                "  Events processed: %lu\n",
     *                plugin_state.event_count);
     *       return 0;
     *   }
     *   
     *   if (strcmp(cmd, "template_reset") == 0) {
     *       plugin_state.event_count = 0;
     *       snprintf(response, resp_len, "Statistics reset");
     *       return 0;
     *   }
     */
    
    snprintf(response, resp_len, "Unknown command: %s", cmd);
    return -1;
}

/**
 * Handle configuration reload
 * 
 * Called when nlmon configuration is reloaded. Use this to:
 * - Re-read configuration values
 * - Update plugin behavior
 * - Reconnect to services if needed
 * 
 * @param ctx Plugin context (may have updated config)
 * @return 0 on success, -1 on error
 */
static int template_on_config_reload(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "Template plugin: configuration reloaded");
    
    /* TODO: Reload configuration
     * Example:
     *   const char *new_setting = ctx->get_config("template.setting");
     *   if (new_setting && strcmp(new_setting, old_setting) != 0) {
     *       ctx->log(NLMON_LOG_INFO, "Setting changed: %s", new_setting);
     *       // Update behavior
     *   }
     */
    
    return 0;
}

/* ============================================================================
 * Event Filter (Optional)
 * ============================================================================
 * If you only want to process specific events, implement a filter function.
 * Return 1 to process the event, 0 to skip it.
 * If you want to process all events, set event_filter to NULL.
 */

/**
 * Filter events
 * 
 * @param event Event to filter
 * @return 1 to process event, 0 to skip
 */
static int template_event_filter(struct nlmon_event *event) {
    if (!event) {
        return 0;
    }
    
    /* TODO: Implement filter logic
     * Examples:
     *   // Only process link events
     *   return event->message_type == RTM_NEWLINK;
     *   
     *   // Only process events for specific interface
     *   return strncmp(event->interface, "eth", 3) == 0;
     *   
     *   // Process all events
     *   return 1;
     */
    
    return 1;  /* Process all events */
}

/* ============================================================================
 * Plugin Descriptor
 * ============================================================================
 * This structure defines your plugin's metadata and callbacks.
 */

static nlmon_plugin_t template_plugin = {
    /* Plugin metadata */
    .name = "template",                    /* TODO: Change to your plugin name */
    .version = "1.0.0",                    /* TODO: Update version */
    .description = "Plugin template",      /* TODO: Update description */
    .api_version = NLMON_PLUGIN_API_VERSION,
    
    /* Plugin callbacks */
    .callbacks = {
        .init = template_init,
        .cleanup = template_cleanup,
        .on_event = template_on_event,
        .on_command = template_on_command,
        .on_config_reload = template_on_config_reload,
    },
    
    /* Event filter (optional) */
    .event_filter = template_event_filter,  /* Set to NULL to process all events */
    
    /* Plugin flags */
    .flags = NLMON_PLUGIN_FLAG_NONE,
    /* Available flags:
     *   NLMON_PLUGIN_FLAG_NONE        - Default behavior
     *   NLMON_PLUGIN_FLAG_PROCESS_ALL - Receive all events (ignore filter)
     *   NLMON_PLUGIN_FLAG_ASYNC       - Async event processing
     *   NLMON_PLUGIN_FLAG_CRITICAL    - Plugin failure stops nlmon
     */
    
    /* Dependencies (optional) */
    .dependencies = NULL,
    /* Example with dependencies:
     *   static const char *deps[] = {"base_plugin", "helper_plugin", NULL};
     *   .dependencies = deps,
     */
};

/* ============================================================================
 * Plugin Registration
 * ============================================================================
 * This macro exports the plugin registration function.
 * DO NOT MODIFY THIS LINE.
 */
NLMON_PLUGIN_DEFINE(template_plugin);

/* ============================================================================
 * Configuration Example
 * ============================================================================
 * Add this to /etc/nlmon/nlmon.yaml to configure your plugin:
 * 
 * plugins:
 *   enabled:
 *     - template
 * 
 * template:
 *   setting1: value1
 *   setting2: value2
 *   server:
 *     host: localhost
 *     port: 8080
 * 
 * ============================================================================
 * Build Instructions
 * ============================================================================
 * 
 * Basic build:
 *   gcc -shared -fPIC -o template.so plugin_template.c -I../../../include
 * 
 * With debug symbols:
 *   gcc -g -shared -fPIC -o template.so plugin_template.c -I../../../include
 * 
 * With optimization:
 *   gcc -O2 -shared -fPIC -o template.so plugin_template.c -I../../../include
 * 
 * With external library (example: libcurl):
 *   gcc -shared -fPIC -o template.so plugin_template.c -I../../../include -lcurl
 * 
 * Install:
 *   sudo cp template.so /usr/lib/nlmon/plugins/
 * 
 * ============================================================================
 * Testing
 * ============================================================================
 * 
 * 1. Check plugin loads:
 *    sudo nlmon
 *    # Look for "Loaded plugin: template" in logs
 * 
 * 2. Test commands (in CLI mode):
 *    :template_stats
 * 
 * 3. Check for errors:
 *    tail -f /var/log/nlmon/nlmon.log
 * 
 * 4. Debug with valgrind:
 *    sudo valgrind --leak-check=full nlmon
 * 
 * ============================================================================
 */
