#include "web_dashboard.h"
#include "web_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WebSocket message handler */
static void ws_on_message(struct ws_connection *conn, const char *message,
                          size_t len, void *user_data) {
    struct web_dashboard *dashboard = user_data;
    (void)dashboard; /* Currently unused, reserved for future use */

    printf("WebSocket message received: %.*s\n", (int)len, message);

    /* Parse message and handle subscription requests */
    if (strstr(message, "\"type\":\"subscribe\"")) {
        /* Client subscribed to events */
        websocket_send(conn, "{\"type\":\"subscribed\"}", 23);
    }
}

/* WebSocket connection handler */
static void ws_on_connect(struct ws_connection *conn, void *user_data) {
    struct web_dashboard *dashboard = user_data;
    (void)dashboard; /* Currently unused, reserved for future use */

    printf("WebSocket client connected\n");

    /* Send welcome message */
    websocket_send(conn, "{\"type\":\"welcome\",\"message\":\"Connected to nlmon\"}", 50);
}

/* WebSocket disconnection handler */
static void ws_on_disconnect(struct ws_connection *conn, void *user_data) {
    struct web_dashboard *dashboard = user_data;
    (void)dashboard; /* Currently unused, reserved for future use */

    printf("WebSocket client disconnected\n");
}

/* Initialize web dashboard */
struct web_dashboard *web_dashboard_init(struct web_dashboard_config *config,
                                         struct storage_layer *storage,
                                         struct filter_manager *filter_mgr,
                                         struct nlmon_config *nlmon_config) {
    if (!config) return NULL;

    struct web_dashboard *dashboard = calloc(1, sizeof(struct web_dashboard));
    if (!dashboard) return NULL;

    dashboard->dashboard_config = *config;
    dashboard->storage = storage;
    dashboard->filter_mgr = filter_mgr;
    dashboard->config = nlmon_config;

    /* Initialize authentication if enabled */
    if (config->enable_auth) {
        dashboard->auth = web_auth_init(config->auth_secret);
        if (!dashboard->auth) {
            fprintf(stderr, "Failed to initialize authentication\n");
            free(dashboard);
            return NULL;
        }

        /* Add default admin user */
        web_auth_add_user(dashboard->auth, "admin", "admin", "admin");
    }

    /* Initialize HTTP server */
    struct web_server_config http_config = {
        .port = config->http_port,
        .enable_tls = config->enable_tls,
        .cert_file = config->cert_file,
        .key_file = config->key_file,
        .static_dir = config->static_dir,
        .max_connections = 100
    };

    dashboard->http_server = web_server_init(&http_config);
    if (!dashboard->http_server) {
        fprintf(stderr, "Failed to initialize HTTP server\n");
        if (dashboard->auth) web_auth_cleanup(dashboard->auth);
        free(dashboard);
        return NULL;
    }

    /* Initialize WebSocket server */
    struct websocket_config ws_config = {
        .port = config->ws_port,
        .on_message = ws_on_message,
        .on_connect = ws_on_connect,
        .on_disconnect = ws_on_disconnect,
        .user_data = dashboard
    };

    dashboard->ws_server = websocket_server_init(&ws_config);
    if (!dashboard->ws_server) {
        fprintf(stderr, "Failed to initialize WebSocket server\n");
        web_server_cleanup(dashboard->http_server);
        if (dashboard->auth) web_auth_cleanup(dashboard->auth);
        free(dashboard);
        return NULL;
    }

    /* Register API endpoints */
    struct web_api_context api_ctx = {
        .storage = storage,
        .filter_mgr = filter_mgr,
        .config = nlmon_config,
        .alert_mgr = NULL
    };

    web_api_init(dashboard->http_server, &api_ctx);

    return dashboard;
}

/* Start web dashboard */
int web_dashboard_start(struct web_dashboard *dashboard) {
    if (!dashboard) return -1;

    /* Start HTTP server */
    if (web_server_start(dashboard->http_server) != 0) {
        fprintf(stderr, "Failed to start HTTP server\n");
        return -1;
    }

    /* Start WebSocket server */
    if (websocket_server_start(dashboard->ws_server) != 0) {
        fprintf(stderr, "Failed to start WebSocket server\n");
        web_server_stop(dashboard->http_server);
        return -1;
    }

    printf("Web dashboard started successfully\n");
    printf("  HTTP server: http://localhost:%d\n", dashboard->dashboard_config.http_port);
    printf("  WebSocket server: ws://localhost:%d\n", dashboard->dashboard_config.ws_port);

    if (dashboard->dashboard_config.enable_tls) {
        printf("  TLS enabled\n");
    }

    if (dashboard->dashboard_config.enable_auth) {
        printf("  Authentication enabled (default user: admin/admin)\n");
    }

    return 0;
}

/* Stop web dashboard */
void web_dashboard_stop(struct web_dashboard *dashboard) {
    if (!dashboard) return;

    if (dashboard->http_server) {
        web_server_stop(dashboard->http_server);
    }

    if (dashboard->ws_server) {
        websocket_server_stop(dashboard->ws_server);
    }

    printf("Web dashboard stopped\n");
}

/* Cleanup web dashboard */
void web_dashboard_cleanup(struct web_dashboard *dashboard) {
    if (!dashboard) return;

    web_dashboard_stop(dashboard);

    if (dashboard->http_server) {
        web_server_cleanup(dashboard->http_server);
    }

    if (dashboard->ws_server) {
        websocket_server_cleanup(dashboard->ws_server);
    }

    if (dashboard->auth) {
        web_auth_cleanup(dashboard->auth);
    }

    free(dashboard);
}

/* Broadcast event to all WebSocket clients */
int web_dashboard_broadcast_event(struct web_dashboard *dashboard, const char *event_json) {
    if (!dashboard || !dashboard->ws_server || !event_json) return -1;

    char message[4096];
    snprintf(message, sizeof(message), "{\"type\":\"event\",\"data\":%s}", event_json);

    return websocket_broadcast(dashboard->ws_server, message, strlen(message));
}
