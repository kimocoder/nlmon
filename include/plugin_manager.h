#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include "plugin_api.h"
#include <stddef.h>

/* Plugin manager handle */
typedef struct plugin_manager plugin_manager_t;

/* Plugin handle */
typedef struct plugin_handle plugin_handle_t;

/* Plugin state */
typedef enum {
    PLUGIN_STATE_UNLOADED = 0,
    PLUGIN_STATE_LOADED,
    PLUGIN_STATE_INITIALIZED,
    PLUGIN_STATE_ERROR,
    PLUGIN_STATE_DISABLED
} plugin_state_t;

/* Plugin info structure */
typedef struct plugin_info {
    char name[NLMON_PLUGIN_NAME_MAX];
    char version[NLMON_PLUGIN_VERSION_MAX];
    char description[NLMON_PLUGIN_DESC_MAX];
    char path[256];
    plugin_state_t state;
    int error_count;
    uint64_t events_processed;
    uint64_t events_filtered;
} plugin_info_t;

/* Create plugin manager */
plugin_manager_t *plugin_manager_create(const char *plugin_dir);

/* Destroy plugin manager */
void plugin_manager_destroy(plugin_manager_t *mgr);

/* Discover plugins in directory */
int plugin_manager_discover(plugin_manager_t *mgr);

/* Load a specific plugin */
plugin_handle_t *plugin_manager_load(plugin_manager_t *mgr, const char *name);

/* Unload a specific plugin */
int plugin_manager_unload(plugin_manager_t *mgr, const char *name);

/* Initialize all loaded plugins */
int plugin_manager_init_all(plugin_manager_t *mgr, nlmon_plugin_context_t *ctx);

/* Cleanup all plugins */
void plugin_manager_cleanup_all(plugin_manager_t *mgr);

/* Enable a plugin */
int plugin_manager_enable(plugin_manager_t *mgr, const char *name);

/* Disable a plugin */
int plugin_manager_disable(plugin_manager_t *mgr, const char *name);

/* Reload a plugin */
int plugin_manager_reload(plugin_manager_t *mgr, const char *name, nlmon_plugin_context_t *ctx);

/* Route event to all plugins */
int plugin_manager_route_event(plugin_manager_t *mgr, struct nlmon_event *event);

/* Invoke plugin command */
int plugin_manager_invoke_command(plugin_manager_t *mgr, const char *plugin_name,
                                  const char *cmd, const char *args,
                                  char *response, size_t resp_len);

/* Get plugin info */
int plugin_manager_get_info(plugin_manager_t *mgr, const char *name, plugin_info_t *info);

/* List all plugins */
int plugin_manager_list(plugin_manager_t *mgr, plugin_info_t **list, size_t *count);

/* Notify plugins of config reload */
int plugin_manager_notify_config_reload(plugin_manager_t *mgr, nlmon_plugin_context_t *ctx);

#endif /* PLUGIN_MANAGER_H */
