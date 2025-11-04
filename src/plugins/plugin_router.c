#include "plugin_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#define PLUGIN_TIMEOUT_SEC 1
#define MAX_CONSECUTIVE_ERRORS 10

/* Timeout handling */
static jmp_buf timeout_env;
static volatile sig_atomic_t timeout_occurred = 0;

static void timeout_handler(int sig) {
    (void)sig;
    timeout_occurred = 1;
    longjmp(timeout_env, 1);
}

/* Check if plugin should process event based on filter */
static int should_process_event(plugin_handle_t *handle, struct nlmon_event *event) {
    if (!handle || !event) return 0;

    /* If plugin processes all events, always return true */
    if (handle->plugin->flags & NLMON_PLUGIN_FLAG_PROCESS_ALL) {
        return 1;
    }

    /* If plugin has no filter, process all events */
    if (!handle->plugin->event_filter) {
        return 1;
    }

    /* Apply filter */
    int result = handle->plugin->event_filter(event);
    if (!result) {
        handle->events_filtered++;
    }

    return result;
}

/* Process event with timeout protection */
static int process_event_with_timeout(plugin_handle_t *handle, struct nlmon_event *event) {
    struct sigaction sa, old_sa;
    int ret = 0;
    
    /* Set up timeout handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGALRM, &sa, &old_sa) < 0) {
        fprintf(stderr, "Failed to set up timeout handler: %s\n", strerror(errno));
        /* Continue without timeout protection */
        return handle->plugin->callbacks.on_event(event);
    }
    
    timeout_occurred = 0;
    
    /* Set up timeout jump point */
    if (setjmp(timeout_env) != 0) {
        /* Timeout occurred */
        alarm(0);  /* Cancel alarm */
        sigaction(SIGALRM, &old_sa, NULL);  /* Restore old handler */
        
        fprintf(stderr, "Plugin %s timed out processing event\n", handle->name);
        handle->error_count++;
        
        /* Disable plugin if too many errors */
        if (handle->error_count >= MAX_CONSECUTIVE_ERRORS) {
            fprintf(stderr, "Plugin %s disabled due to excessive errors\n", handle->name);
            handle->state = PLUGIN_STATE_DISABLED;
        }

        return -1;
    }

    /* Set alarm */
    alarm(PLUGIN_TIMEOUT_SEC);

    /* Call plugin event handler */
    ret = handle->plugin->callbacks.on_event(event);

    /* Cancel alarm */
    alarm(0);
    
    /* Restore old signal handler */
    sigaction(SIGALRM, &old_sa, NULL);
    
    return ret;
}

/* Route event to all plugins */
int plugin_manager_route_event(plugin_manager_t *mgr, struct nlmon_event *event) {
    if (!mgr || !event) return -1;
    
    int processed = 0;
    int filtered = 0;
    
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        plugin_handle_t *handle = mgr->plugins[i];
        
        /* Skip if not initialized or disabled */
        if (handle->state != PLUGIN_STATE_INITIALIZED) {
            continue;
        }
        
        /* Skip if no event handler */
        if (!handle->plugin->callbacks.on_event) {
            continue;
        }
        
        /* Check if plugin should process this event */
        if (!should_process_event(handle, event)) {
            continue;
        }
        
        /* Process event with error handling */
        int ret;
        
        if (handle->plugin->flags & NLMON_PLUGIN_FLAG_ASYNC) {
            /* TODO: Queue event for async processing */
            ret = handle->plugin->callbacks.on_event(event);
        } else {
            /* Synchronous processing with timeout */
            ret = process_event_with_timeout(handle, event);
        }
        
        if (ret < 0) {
            /* Error occurred */
            handle->error_count++;
            
            /* Disable plugin if too many consecutive errors */
            if (handle->error_count >= MAX_CONSECUTIVE_ERRORS) {
                fprintf(stderr, "Plugin %s disabled due to excessive errors\n", handle->name);
                handle->state = PLUGIN_STATE_DISABLED;
            }
        } else if (ret > 0) {
            /* Plugin filtered the event - stop processing */
            filtered = 1;
            handle->events_processed++;
            break;
        } else {
            /* Success */
            handle->events_processed++;
            processed++;
            
            /* Reset error count on success */
            if (handle->error_count > 0) {
                handle->error_count = 0;
            }
        }
    }
    
    return filtered ? -1 : processed;
}

/* Invoke plugin command */
int plugin_manager_invoke_command(plugin_manager_t *mgr, const char *plugin_name,
                                  const char *cmd, const char *args,
                                  char *response, size_t resp_len) {
    if (!mgr || !plugin_name || !cmd) return -1;
    
    /* Find plugin */
    plugin_handle_t *handle = NULL;
    for (size_t i = 0; i < mgr->plugin_count; i++) {
        if (strcmp(mgr->plugins[i]->name, plugin_name) == 0) {
            handle = mgr->plugins[i];
            break;
        }
    }
    
    if (!handle) {
        if (response && resp_len > 0) {
            snprintf(response, resp_len, "Plugin not found: %s", plugin_name);
        }
        return -1;
    }
    
    /* Check if plugin is initialized */
    if (handle->state != PLUGIN_STATE_INITIALIZED) {
        if (response && resp_len > 0) {
            snprintf(response, resp_len, "Plugin not initialized: %s", plugin_name);
        }
        return -1;
    }
    
    /* Check if plugin has command handler */
    if (!handle->plugin->callbacks.on_command) {
        if (response && resp_len > 0) {
            snprintf(response, resp_len, "Plugin does not support commands: %s", plugin_name);
        }
        return -1;
    }
    
    /* Invoke command */
    int ret = handle->plugin->callbacks.on_command(cmd, args, response, resp_len);
    
    if (ret != 0) {
        handle->error_count++;
    }
    
    return ret;
}
