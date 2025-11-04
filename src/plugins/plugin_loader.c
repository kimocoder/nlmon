#include "plugin_internal.h"
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define PLUGIN_EXT ".so"

/* Create plugin manager */
plugin_manager_t *plugin_manager_create(const char *plugin_dir) {
    plugin_manager_t *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) {
        return NULL;
    }
    
    if (plugin_dir) {
        strncpy(mgr->plugin_dir, plugin_dir, sizeof(mgr->plugin_dir) - 1);
    } else {
        strncpy(mgr->plugin_dir, "/usr/lib/nlmon/plugins", sizeof(mgr->plugin_dir) - 1);
    }
    
    mgr->plugin_count = 0;
    mgr->initialized = 0;
    
    return mgr;
}

/* Destroy plugin manager */
void plugin_manager_destroy(plugin_manager_t *mgr) {
    if (!mgr) return;
    
    /* Cleanup and unload all plugins */
    plugin_manager_cleanup_all(mgr);
    
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        if (mgr->plugins[i]) {
            if (mgr->plugins[i]->dl_handle) {
                dlclose(mgr->plugins[i]->dl_handle);
            }
            free(mgr->plugins[i]);
        }
    }
    
    free(mgr);
}

/* Check if file is a plugin (ends with .so) */
static int is_plugin_file(const char *filename) {
    size_t len = strlen(filename);
    size_t ext_len = strlen(PLUGIN_EXT);
    
    if (len <= ext_len) {
        return 0;
    }
    
    return strcmp(filename + len - ext_len, PLUGIN_EXT) == 0;
}

/* Discover plugins in directory */
int plugin_manager_discover(plugin_manager_t *mgr) {
    if (!mgr) return -1;
    
    DIR *dir = opendir(mgr->plugin_dir);
    if (!dir) {
        fprintf(stderr, "Failed to open plugin directory: %s: %s\n",
                mgr->plugin_dir, strerror(errno));
        return -1;
    }
    
    struct dirent *entry;
    int discovered = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG && entry->d_type != DT_LNK) {
            continue;
        }
        
        if (!is_plugin_file(entry->d_name)) {
            continue;
        }
        
        /* Extract plugin name (remove .so extension) */
        char name[NLMON_PLUGIN_NAME_MAX];
        strncpy(name, entry->d_name, sizeof(name) - 1);
        char *ext = strstr(name, PLUGIN_EXT);
        if (ext) {
            *ext = '\0';
        }
        
        /* Check if already loaded */
        int already_loaded = 0;
        for (size_t i = 0; i < mgr->plugin_count; i++) {
            if (strcmp(mgr->plugins[i]->name, name) == 0) {
                already_loaded = 1;
                break;
            }
        }
        
        if (!already_loaded) {
            plugin_handle_t *handle = plugin_manager_load(mgr, name);
            if (handle) {
                discovered++;
            }
        }
    }
    
    closedir(dir);
    
    printf("Discovered %d plugin(s) in %s\n", discovered, mgr->plugin_dir);
    return discovered;
}

/* Load a specific plugin */
plugin_handle_t *plugin_manager_load(plugin_manager_t *mgr, const char *name) {
    if (!mgr || !name) return NULL;
    
    if (mgr->plugin_count >= MAX_PLUGINS) {
        fprintf(stderr, "Maximum number of plugins reached\n");
        return NULL;
    }
    
    /* Build plugin path */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s%s", mgr->plugin_dir, name, PLUGIN_EXT);
    
    /* Check if file exists */
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "Plugin file not found: %s\n", path);
        return NULL;
    }
    
    /* Create plugin handle */
    plugin_handle_t *handle = calloc(1, sizeof(*handle));
    if (!handle) {
        fprintf(stderr, "Failed to allocate plugin handle\n");
        return NULL;
    }
    
    strncpy(handle->name, name, sizeof(handle->name) - 1);
    strncpy(handle->path, path, sizeof(handle->path) - 1);
    handle->state = PLUGIN_STATE_UNLOADED;
    
    /* Load shared library */
    handle->dl_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle->dl_handle) {
        fprintf(stderr, "Failed to load plugin %s: %s\n", name, dlerror());
        free(handle);
        return NULL;
    }
    
    /* Clear any existing error */
    dlerror();
    
    /* Resolve plugin registration function */
    nlmon_plugin_register_fn register_fn = 
        (nlmon_plugin_register_fn)dlsym(handle->dl_handle, "nlmon_plugin_register");
    
    const char *dl_error = dlerror();
    if (dl_error) {
        fprintf(stderr, "Failed to find registration function in %s: %s\n", name, dl_error);
        dlclose(handle->dl_handle);
        free(handle);
        return NULL;
    }
    
    /* Call registration function */
    handle->plugin = register_fn();
    if (!handle->plugin) {
        fprintf(stderr, "Plugin registration failed for %s\n", name);
        dlclose(handle->dl_handle);
        free(handle);
        return NULL;
    }
    
    /* Verify API version */
    if (handle->plugin->api_version != NLMON_PLUGIN_API_VERSION) {
        fprintf(stderr, "Plugin %s API version mismatch: expected %d, got %d\n",
                name, NLMON_PLUGIN_API_VERSION, handle->plugin->api_version);
        dlclose(handle->dl_handle);
        free(handle);
        return NULL;
    }
    
    /* Verify plugin name matches */
    if (strcmp(handle->plugin->name, name) != 0) {
        fprintf(stderr, "Warning: Plugin filename '%s' doesn't match plugin name '%s'\n",
                name, handle->plugin->name);
    }
    
    handle->state = PLUGIN_STATE_LOADED;
    
    /* Add to plugin list */
    mgr->plugins[mgr->plugin_count++] = handle;
    
    printf("Loaded plugin: %s v%s - %s\n",
           handle->plugin->name,
           handle->plugin->version,
           handle->plugin->description);
    
    return handle;
}

/* Unload a specific plugin */
int plugin_manager_unload(plugin_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    /* Find plugin */
    plugin_handle_t *handle = NULL;
    size_t index = 0;
    
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        if (strcmp(mgr->plugins[i]->name, name) == 0) {
            handle = mgr->plugins[i];
            index = i;
            break;
        }
    }
    
    if (!handle) {
        fprintf(stderr, "Plugin not found: %s\n", name);
        return -1;
    }
    
    /* Call cleanup if initialized */
    if (handle->state == PLUGIN_STATE_INITIALIZED &&
        handle->plugin->callbacks.cleanup) {
        handle->plugin->callbacks.cleanup();
    }
    
    /* Unload shared library */
    if (handle->dl_handle) {
        dlclose(handle->dl_handle);
    }
    
    /* Remove from list */
    free(handle);
    
    for (size_t i = index; i < mgr->plugin_count - 1; i++) {
        mgr->plugins[i] = mgr->plugins[i + 1];
    }
    mgr->plugin_count--;
    
    printf("Unloaded plugin: %s\n", name);
    return 0;
}

/* Find plugin by name */
plugin_handle_t *find_plugin(plugin_manager_t *mgr, const char *name) {
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        if (strcmp(mgr->plugins[i]->name, name) == 0) {
            return mgr->plugins[i];
        }
    }
    return NULL;
}

/* Check if all dependencies are satisfied */
int check_dependencies(plugin_manager_t *mgr, plugin_handle_t *handle) {
    if (!handle->plugin->dependencies) {
        return 1;  /* No dependencies */
    }
    
    for (const char **dep = handle->plugin->dependencies; *dep; dep++) {
        plugin_handle_t *dep_handle = find_plugin(mgr, *dep);
        if (!dep_handle) {
            fprintf(stderr, "Plugin %s: missing dependency %s\n",
                    handle->name, *dep);
            return 0;
        }
        
        if (dep_handle->state != PLUGIN_STATE_INITIALIZED) {
            fprintf(stderr, "Plugin %s: dependency %s not initialized\n",
                    handle->name, *dep);
            return 0;
        }
    }
    
    return 1;
}

/* Get plugin info */
int plugin_manager_get_info(plugin_manager_t *mgr, const char *name, plugin_info_t *info) {
    if (!mgr || !name || !info) return -1;
    
    plugin_handle_t *handle = find_plugin(mgr, name);
    if (!handle) return -1;
    
    strncpy(info->name, handle->name, sizeof(info->name) - 1);
    strncpy(info->path, handle->path, sizeof(info->path) - 1);
    info->state = handle->state;
    info->error_count = handle->error_count;
    info->events_processed = handle->events_processed;
    info->events_filtered = handle->events_filtered;
    
    if (handle->plugin) {
        strncpy(info->version, handle->plugin->version, sizeof(info->version) - 1);
        strncpy(info->description, handle->plugin->description, sizeof(info->description) - 1);
    }
    
    return 0;
}

/* List all plugins */
int plugin_manager_list(plugin_manager_t *mgr, plugin_info_t **list, size_t *count) {
    if (!mgr || !list || !count) return -1;
    
    *count = mgr->plugin_count;
    if (*count == 0) {
        *list = NULL;
        return 0;
    }
    
    *list = calloc(*count, sizeof(plugin_info_t));
    if (!*list) return -1;
    
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        plugin_manager_get_info(mgr, mgr->plugins[i]->name, &(*list)[i]);
    }
    
    return 0;
}
