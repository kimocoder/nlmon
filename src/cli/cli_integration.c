#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "cli_interface.h"

/* Global CLI state */
static cli_state_t *g_cli_state = NULL;
static pthread_t g_cli_thread;
static bool g_cli_running = false;

/* CLI update thread */
static void *cli_update_thread(void *arg)
{
    cli_state_t *state = (cli_state_t *)arg;
    
    while (state && state->running) {
        /* Handle input */
        int ch = getch();
        if (ch != ERR) {
            cli_handle_input(state, ch);
        }
        
        /* Refresh display if needed */
        for (int i = 0; i < PANEL_MAX; i++) {
            if (state->panels[i].needs_refresh) {
                cli_refresh_panel(state, i);
            }
        }
        
        /* Sleep briefly to avoid busy-waiting */
        usleep(50000); /* 50ms */
    }
    
    return NULL;
}

/* Initialize enhanced CLI */
int cli_enhanced_init(void)
{
    if (g_cli_state) {
        return 0; /* Already initialized */
    }
    
    if (cli_init(&g_cli_state) < 0) {
        return -1;
    }
    
    /* Start CLI update thread */
    g_cli_running = true;
    if (pthread_create(&g_cli_thread, NULL, cli_update_thread, g_cli_state) != 0) {
        cli_cleanup(g_cli_state);
        g_cli_state = NULL;
        return -1;
    }
    
    return 0;
}

/* Cleanup enhanced CLI */
void cli_enhanced_cleanup(void)
{
    if (!g_cli_state) {
        return;
    }
    
    /* Stop CLI thread */
    g_cli_state->running = false;
    g_cli_running = false;
    
    if (g_cli_thread) {
        pthread_join(g_cli_thread, NULL);
        g_cli_thread = 0;
    }
    
    cli_cleanup(g_cli_state);
    g_cli_state = NULL;
}

/* Log event to enhanced CLI */
void cli_enhanced_log_event(const char *event_type, const char *interface, 
                            const char *message, const char *details_json)
{
    if (!g_cli_state) {
        return;
    }
    
    /* Generate timestamp */
    time_t now;
    struct tm *tm_info;
    char timestamp[32];
    
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
    
    /* Add event to CLI */
    cli_add_event(g_cli_state, timestamp, event_type, interface, message, details_json);
}

/* Check if CLI is running */
bool cli_enhanced_is_running(void)
{
    return g_cli_state && g_cli_state->running;
}

/* Check if CLI is paused */
bool cli_enhanced_is_paused(void)
{
    return g_cli_state && g_cli_state->paused;
}

/* Refresh CLI display */
void cli_enhanced_refresh(void)
{
    if (g_cli_state) {
        cli_refresh_all(g_cli_state);
    }
}
