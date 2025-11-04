#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>
#include <stddef.h>

/* Web server configuration */
struct web_server_config {
    uint16_t port;
    int enable_tls;
    char *cert_file;
    char *key_file;
    char *static_dir;
    int max_connections;
};

/* Web server context */
struct web_server;

/* Request handler callback */
typedef int (*request_handler_t)(void *cls, const char *url, const char *method,
                                  const char *version, const char *upload_data,
                                  size_t *upload_data_size, void **con_cls,
                                  char **response, size_t *response_len,
                                  const char **content_type);

/* Initialize web server */
struct web_server *web_server_init(struct web_server_config *config);

/* Register route handler */
int web_server_register_route(struct web_server *server, const char *path,
                               const char *method, request_handler_t handler,
                               void *user_data);

/* Start web server */
int web_server_start(struct web_server *server);

/* Stop web server */
void web_server_stop(struct web_server *server);

/* Cleanup web server */
void web_server_cleanup(struct web_server *server);

/* Get server statistics */
struct web_server_stats {
    uint64_t total_requests;
    uint64_t active_connections;
    uint64_t bytes_sent;
    uint64_t bytes_received;
};

int web_server_get_stats(struct web_server *server, struct web_server_stats *stats);

#endif /* WEB_SERVER_H */
