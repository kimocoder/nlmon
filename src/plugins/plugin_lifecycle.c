#include "plugin_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initialize a single plugin */
static int init_plugin(plugin_handle_t *handle, nlmon_plugin_context_t *ctx) {
    if (!handle || !ctx) return -1;
    
    if (handle->state == PLUGIN_STATE_INITIALIZED) {
        return 0;  /* Already initialized */
    }
    
    if (handle->state == PLUGIN_STATE_DISABLED) {
        fprintf(stderr, "Plugin %s is disabled\n", handle->name);
        return -1;
    }
    
    if (handle->state != PLUGIN_STATE_LOADED) {
        fprintf(stderr, "Plugin %s not in loaded state\n", handle->name);
        return -1;
    }
    
    /* Store context */
    handle->context = ctx;
    
    /* Call init callback if provided */
    if (handle->plugin->callbacks.init) {
        int ret = handle->plugin->callbacks.init(ctx);
        if (ret != 0) {
            fprintf(stderr, "Plugin %s initialization failed: %d\n", handle->name, ret);
            handle->state = PLUGIN_STATE_ERROR;
            handle->error_count++;
            
            /* If critical plugin fails, return error */
            if (handle->plugin->flags & NLMON_PLUGIN_FLAG_CRITICAL) {
                return -1;
            }
            
            return -1;
        }
    }
    
    handle->state = PLUGIN_STATE_INITIALIZED;
    printf("Initialized plugin: %s\n", handle->name);
    
    return 0;
}

/* Initialize all loaded plugins (respecting dependencies) */
int plugin_manager_init_all(plugin_manager_t *mgr, nlmon_plugin_context_t *ctx) {
    if (!mgr || !ctx) return -1;
    
    if (mgr->initialized) {
        return 0;  /* Already initialized */
    }
    
    /* Multiple passes to handle dependencies */
    int initialized_count = 0;
    int max_passes = 10;  /* Prevent infinite loop */
    
    for (int pass = 0; pass < max_passes; pass++) {
        int initialized_this_pass = 0;
        
        for (size_t i = 0; i < mgr->plugin_count; i++) {
            plugin_handle_t *handle = mgr->plugins[i];
            
            /* Skip if already initialized or in error state */
            if (handle->state == PLUGIN_STATE_INITIALIZED ||
                handle->state == PLUGIN_STATE_ERROR ||
                handle->state == PLUGIN_STATE_DISABLED) {
                continue;
            }
            
            /* Check dependencies */
            if (!check_dependencies(mgr, handle)) {
                continue;  /* Dependencies not satisfied yet */
            }
            
            /* Initialize plugin */
            if (init_plugin(handle, ctx) == 0) {
                initialized_count++;
                initialized_this_pass++;
            } else {
                /* Check if critical plugin failed */
                if (handle->plugin->flags & NLMON_PLUGIN_FLAG_CRITICAL) {
                    fprintf(stderr, "Critical plugin %s failed to initialize\n", handle->name);
                    return -1;
                }
            }
        }
        
        /* If no plugins initialized this pass, we're done */
        if (initialized_this_pass == 0) {
            break;
        }
    }
    
    /* Check for plugins that couldn't be initialized */
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        plugin_handle_t *handle = mgr->plugins[i];
        if (handle->state == PLUGIN_STATE_LOADED) {
            fprintf(stderr, "Warning: Plugin %s could not be initialized (dependency cycle?)\n",
                    handle->name);
            handle->state = PLUGIN_STATE_ERROR;
        }
    }
    
    mgr->initialized = 1;
    printf("Initialized %d plugin(s)\n", initialized_count);
    
    return initialized_count;
}

/* Cleanup all plugins */
void plugin_manager_cleanup_all(plugin_manager_t *mgr) {
    if (!mgr) return;
    
    /* Cleanup in reverse order (respects dependencies) */
    for (int i = (int)mgr->plugin_count - 1; i >= 0; i--) {
        plugin_handle_t *handle = mgr->plugins[i];
        
        if (handle->state == PLUGIN_STATE_INITIALIZED &&
            handle->plugin->callbacks.cleanup) {
            printf("Cleaning up plugin: %s\n", handle->name);
            handle->plugin->callbacks.cleanup();
        }
        
        handle->state = PLUGIN_STATE_UNLOADED;
    }
    
    mgr->initialized = 0;
}

/* Enable a plugin */
int plugin_manager_enable(plugin_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    plugin_handle_t *handle = find_plugin(mgr, name);
    if (!handle) {
        fprintf(stderr, "Plugin not found: %s\n", name);
        return -1;
    }
    
    if (handle->state == PLUGIN_STATE_DISABLED) {
        handle->state = PLUGIN_STATE_LOADED;
        printf("Enabled plugin: %s\n", name);
        return 0;
    }
    
    return 0;  /* Already enabled */
}

/* Disable a plugin */
int plugin_manager_disable(plugin_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    plugin_handle_t *handle = find_plugin(mgr, name);
    if (!handle) {
        fprintf(stderr, "Plugin not found: %s\n", name);
        return -1;
    }
    
    /* Cleanup if initialized */
    if (handle->state == PLUGIN_STATE_INITIALIZED &&
        handle->plugin->callbacks.cleanup) {
        handle->plugin->callbacks.cleanup();
    }
    
    handle->state = PLUGIN_STATE_DISABLED;
    printf("Disabled plugin: %s\n", name);
    
    return 0;
}

/* Reload a plugin */
int plugin_manager_reload(plugin_manager_t *mgr, const char *name, nlmon_plugin_context_t *ctx) {
    if (!mgr || !name || !ctx) return -1;
    
    plugin_handle_t *handle = find_plugin(mgr, name);
    if (!handle) {
        fprintf(stderr, "Plugin not found: %s\n", name);
        return -1;
    }
    
    /* Cleanup if initialized */
    if (handle->state == PLUGIN_STATE_INITIALIZED &&
        handle->plugin->callbacks.cleanup) {
        handle->plugin->callbacks.cleanup();
    }
    
    /* Re-initialize */
    handle->state = PLUGIN_STATE_LOADED;
    int ret = init_plugin(handle, ctx);
    
    if (ret == 0) {
        printf("Reloaded plugin: %s\n", name);
    } else {
        fprintf(stderr, "Failed to reload plugin: %s\n", name);
    }
    
    return ret;
}

/* Notify plugins of config reload */
int plugin_manager_notify_config_reload(plugin_manager_t *mgr, nlmon_plugin_context_t *ctx) {
    if (!mgr || !ctx) return -1;
    
    int notified = 0;
    
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        plugin_handle_t *handle = mgr->plugins[i];
        
        if (handle->state != PLUGIN_STATE_INITIALIZED) {
            continue;
        }
        
        if (handle->plugin->callbacks.on_config_reload) {
            int ret = handle->plugin->callbacks.on_config_reload(ctx);
            if (ret != 0) {
                fprintf(stderr, "Plugin %s config reload failed: %d\n", handle->name, ret);
                handle->error_count++;
            } else {
                notified++;
            }
        }
    }
    
    printf("Notified %d plugin(s) of config reload\n", notified);
    return notified;
}
