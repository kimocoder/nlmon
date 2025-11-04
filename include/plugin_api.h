#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#define NLMON_PLUGIN_API_VERSION 1
#define NLMON_PLUGIN_NAME_MAX 64
#define NLMON_PLUGIN_VERSION_MAX 32
#define NLMON_PLUGIN_DESC_MAX 256
#define NLMON_PLUGIN_CMD_MAX 64

/* Forward declarations */
struct nlmon_event;
struct nlmon_plugin_context;

/* Log levels */
typedef enum {
    NLMON_LOG_DEBUG = 0,
    NLMON_LOG_INFO,
    NLMON_LOG_WARN,
    NLMON_LOG_ERROR
} nlmon_log_level_t;

/* Plugin command handler function type */
typedef int (*nlmon_command_handler_t)(const char *args, char *response, size_t resp_len);

/* Plugin event filter function type */
typedef int (*nlmon_event_filter_t)(struct nlmon_event *event);

/* Plugin context - provides API for plugins to interact with nlmon */
typedef struct nlmon_plugin_context {
    /* Configuration access */
    const void *config;
    
    /* Logging function */
    void (*log)(nlmon_log_level_t level, const char *fmt, ...);
    
    /* Register a custom CLI command */
    int (*register_command)(const char *name, nlmon_command_handler_t handler, const char *help);
    
    /* Emit a new event (for plugins that generate events) */
    int (*emit_event)(struct nlmon_event *event);
    
    /* Get configuration value */
    const char *(*get_config)(const char *key);
    
    /* Store plugin-specific data */
    void *plugin_data;
} nlmon_plugin_context_t;

/* Plugin callbacks */
typedef struct nlmon_plugin_callbacks {
    /* Initialize plugin - called once at load time */
    int (*init)(nlmon_plugin_context_t *ctx);
    
    /* Cleanup plugin - called once at unload time */
    void (*cleanup)(void);
    
    /* Process event - called for each event if plugin subscribes to events */
    int (*on_event)(struct nlmon_event *event);
    
    /* Handle custom command - called when plugin command is invoked */
    int (*on_command)(const char *cmd, const char *args, char *response, size_t resp_len);
    
    /* Configuration reload - called when configuration changes */
    int (*on_config_reload)(nlmon_plugin_context_t *ctx);
} nlmon_plugin_callbacks_t;

/* Plugin descriptor - main plugin structure */
typedef struct nlmon_plugin {
    /* Plugin metadata */
    char name[NLMON_PLUGIN_NAME_MAX];
    char version[NLMON_PLUGIN_VERSION_MAX];
    char description[NLMON_PLUGIN_DESC_MAX];
    int api_version;
    
    /* Plugin callbacks */
    nlmon_plugin_callbacks_t callbacks;
    
    /* Event filter - if set, only matching events are passed to on_event */
    nlmon_event_filter_t event_filter;
    
    /* Plugin flags */
    uint32_t flags;
#define NLMON_PLUGIN_FLAG_NONE          0x00
#define NLMON_PLUGIN_FLAG_PROCESS_ALL   0x01  /* Process all events */
#define NLMON_PLUGIN_FLAG_ASYNC         0x02  /* Async event processing */
#define NLMON_PLUGIN_FLAG_CRITICAL      0x04  /* Critical plugin - failure stops nlmon */
    
    /* Dependencies - NULL-terminated array of plugin names */
    const char **dependencies;
} nlmon_plugin_t;

/* Plugin registration function - must be exported by plugin */
typedef nlmon_plugin_t *(*nlmon_plugin_register_fn)(void);

/* Plugin must export this symbol */
#define NLMON_PLUGIN_REGISTER nlmon_plugin_register

/* Helper macro for plugin registration */
#define NLMON_PLUGIN_DEFINE(plugin_var) \
    nlmon_plugin_t *nlmon_plugin_register(void) { \
        return &plugin_var; \
    }

#endif /* PLUGIN_API_H */
