#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include "web_server.h"
#include "websocket_server.h"
#include "web_auth.h"
#include "storage_layer.h"
#include "filter_manager.h"
#include "nlmon_config.h"

/* Web dashboard configuration */
struct web_dashboard_config {
    uint16_t http_port;
    uint16_t ws_port;
    int enable_tls;
    char *cert_file;
    char *key_file;
    char *static_dir;
    char *auth_secret;
    int enable_auth;
};

/* Web dashboard context */
struct web_dashboard {
    struct web_server *http_server;
    struct websocket_server *ws_server;
    struct web_auth_context *auth;
    struct storage_layer *storage;
    struct filter_manager *filter_mgr;
    struct nlmon_config *config;
    struct web_dashboard_config dashboard_config;
};

/* Initialize web dashboard */
struct web_dashboard *web_dashboard_init(struct web_dashboard_config *config,
                                         struct storage_layer *storage,
                                         struct filter_manager *filter_mgr,
                                         struct nlmon_config *nlmon_config);

/* Start web dashboard */
int web_dashboard_start(struct web_dashboard *dashboard);

/* Stop web dashboard */
void web_dashboard_stop(struct web_dashboard *dashboard);

/* Cleanup web dashboard */
void web_dashboard_cleanup(struct web_dashboard *dashboard);

/* Broadcast event to all WebSocket clients */
int web_dashboard_broadcast_event(struct web_dashboard *dashboard, const char *event_json);

#endif /* WEB_DASHBOARD_H */
