#ifndef PLUGIN_INTERNAL_H
#define PLUGIN_INTERNAL_H

#include "plugin_api.h"
#include "plugin_manager.h"

#define MAX_PLUGINS 64

/* Internal plugin handle structure */
typedef struct plugin_handle {
    char name[NLMON_PLUGIN_NAME_MAX];
    char path[256];
    void *dl_handle;
    nlmon_plugin_t *plugin;
    plugin_state_t state;
    int error_count;
    uint64_t events_processed;
    uint64_t events_filtered;
    nlmon_plugin_context_t *context;
} plugin_handle_t;

/* Plugin manager structure */
typedef struct plugin_manager {
    char plugin_dir[256];
    plugin_handle_t *plugins[MAX_PLUGINS];
    size_t plugin_count;
    int initialized;
} plugin_manager_t;

/* Internal functions shared between plugin modules */
plugin_handle_t *find_plugin(plugin_manager_t *mgr, const char *name);
int check_dependencies(plugin_manager_t *mgr, plugin_handle_t *handle);

#endif /* PLUGIN_INTERNAL_H */
