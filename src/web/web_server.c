#include "web_server.h"
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_ROUTES 64
#define MAX_PATH_LEN 256

/* Route entry */
struct route_entry {
    char path[MAX_PATH_LEN];
    char method[16];
    request_handler_t handler;
    void *user_data;
};

/* Web server context */
struct web_server {
    struct MHD_Daemon *daemon;
    struct web_server_config config;
    struct route_entry routes[MAX_ROUTES];
    int num_routes;
    struct web_server_stats stats;
};

/* Connection info */
struct connection_info {
    char *upload_data;
    size_t upload_size;
    int first_call;
};

/* MIME type mapping */
static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";

    return "application/octet-stream";
}

/* Serve static file */
static enum MHD_Result serve_static_file(struct web_server *server, const char *path,
                              struct MHD_Connection *connection) {
    char filepath[512];
    struct stat st;
    int fd;
    struct MHD_Response *response;
    int ret;

    /* Build full file path */
    snprintf(filepath, sizeof(filepath), "%s%s", 
             server->config.static_dir ? server->config.static_dir : ".", path);

    /* Security check - prevent directory traversal */
    if (strstr(filepath, "..") != NULL) {
        const char *error = "403 Forbidden";
        response = MHD_create_response_from_buffer(strlen(error),
                                                    (void *)error,
                                                    MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_FORBIDDEN, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Open file */
    fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        const char *error = "404 Not Found";
        response = MHD_create_response_from_buffer(strlen(error),
                                                    (void *)error,
                                                    MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Get file size */
    if (fstat(fd, &st) < 0) {
        close(fd);
        const char *error = "500 Internal Server Error";
        response = MHD_create_response_from_buffer(strlen(error),
                                                    (void *)error,
                                                    MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Create response from file */
    response = MHD_create_response_from_fd(st.st_size, fd);
    if (!response) {
        close(fd);
        const char *error = "500 Internal Server Error";
        response = MHD_create_response_from_buffer(strlen(error),
                                                    (void *)error,
                                                    MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Set content type */
    MHD_add_response_header(response, "Content-Type", get_mime_type(path));

    /* Queue response */
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);

    server->stats.bytes_sent += st.st_size;

    return ret;
}

/* Find matching route */
static struct route_entry *find_route(struct web_server *server,
                                      const char *url, const char *method) {
    for (int i = 0; i < server->num_routes; i++) {
        if (strcmp(server->routes[i].method, method) == 0 &&
            strcmp(server->routes[i].path, url) == 0) {
            return &server->routes[i];
        }
    }
    return NULL;
}

/* Request completed callback */
static void request_completed(void *cls, struct MHD_Connection *connection,
                               void **con_cls, enum MHD_RequestTerminationCode toe) {
    struct connection_info *con_info = *con_cls;

    if (con_info) {
        if (con_info->upload_data) {
            free(con_info->upload_data);
        }
        free(con_info);
        *con_cls = NULL;
    }
}

/* Main request handler */
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {
    struct web_server *server = cls;
    struct connection_info *con_info = *con_cls;
    struct route_entry *route;
    char *response_data = NULL;
    size_t response_len = 0;
    const char *content_type = "text/plain";
    struct MHD_Response *response;
    int ret;
    int status_code = MHD_HTTP_OK;

    /* First call - initialize connection info */
    if (!con_info) {
        con_info = calloc(1, sizeof(struct connection_info));
        if (!con_info) return MHD_NO;

        con_info->first_call = 1;
        *con_cls = con_info;

        server->stats.total_requests++;
        server->stats.active_connections++;

        return MHD_YES;
    }

    /* Handle upload data */
    if (*upload_data_size > 0) {
        char *new_data = realloc(con_info->upload_data,
                                 con_info->upload_size + *upload_data_size + 1);
        if (!new_data) {
            return MHD_NO;
        }

        con_info->upload_data = new_data;
        memcpy(con_info->upload_data + con_info->upload_size,
               upload_data, *upload_data_size);
        con_info->upload_size += *upload_data_size;
        con_info->upload_data[con_info->upload_size] = '\0';

        server->stats.bytes_received += *upload_data_size;

        *upload_data_size = 0;
        return MHD_YES;
    }

    /* Process request */
    if (con_info->first_call) {
        con_info->first_call = 0;
    }

    /* Try to find matching route */
    route = find_route(server, url, method);
    if (route) {
        /* Call route handler */
        ret = route->handler(route->user_data, url, method, version,
                             con_info->upload_data, &con_info->upload_size,
                             con_cls, &response_data, &response_len,
                             &content_type);

        if (ret < 0) {
            status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
            response_data = strdup("500 Internal Server Error");
            response_len = strlen(response_data);
        } else {
            status_code = ret > 0 ? ret : MHD_HTTP_OK;
        }
    } else if (strncmp(url, "/api/", 5) == 0) {
        /* API endpoint not found */
        status_code = MHD_HTTP_NOT_FOUND;
        response_data = strdup("{\"error\":\"Endpoint not found\"}");
        response_len = strlen(response_data);
        content_type = "application/json";
    } else {
        /* Try to serve static file */
        server->stats.active_connections--;
        return serve_static_file(server, url, connection);
    }

    /* Create response */
    if (!response_data) {
        response_data = strdup("");
        response_len = 0;
    }

    response = MHD_create_response_from_buffer(response_len, response_data,
                                                MHD_RESPMEM_MUST_FREE);
    if (!response) {
        free(response_data);
        server->stats.active_connections--;
        return MHD_NO;
    }

    /* Add headers */
    MHD_add_response_header(response, "Content-Type", content_type);
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods",
                            "GET, POST, PUT, DELETE, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers",
                            "Content-Type, Authorization");

    /* Queue response */
    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);

    server->stats.bytes_sent += response_len;
    server->stats.active_connections--;

    return ret;
}

/* Initialize web server */
struct web_server *web_server_init(struct web_server_config *config) {
    struct web_server *server;

    if (!config) return NULL;

    server = calloc(1, sizeof(struct web_server));
    if (!server) return NULL;

    server->config = *config;
    server->num_routes = 0;
    memset(&server->stats, 0, sizeof(server->stats));

    /* Duplicate string fields */
    if (config->cert_file) {
        server->config.cert_file = strdup(config->cert_file);
    }
    if (config->key_file) {
        server->config.key_file = strdup(config->key_file);
    }
    if (config->static_dir) {
        server->config.static_dir = strdup(config->static_dir);
    }

    return server;
}

/* Register route handler */
int web_server_register_route(struct web_server *server, const char *path,
                               const char *method, request_handler_t handler,
                               void *user_data) {
    if (!server || !path || !method || !handler) return -1;
    if (server->num_routes >= MAX_ROUTES) return -1;

    struct route_entry *route = &server->routes[server->num_routes];

    snprintf(route->path, sizeof(route->path), "%s", path);
    snprintf(route->method, sizeof(route->method), "%s", method);
    route->handler = handler;
    route->user_data = user_data;

    server->num_routes++;

    return 0;
}

/* Start web server */
int web_server_start(struct web_server *server) {
    unsigned int flags = MHD_USE_THREAD_PER_CONNECTION;

    if (!server) return -1;

    /* Add TLS support if enabled */
    if (server->config.enable_tls) {
        flags |= MHD_USE_TLS;
    }

    /* Start daemon */
    if (server->config.enable_tls && server->config.cert_file && server->config.key_file) {
        server->daemon = MHD_start_daemon(flags, server->config.port,
                                          NULL, NULL,
                                          &answer_to_connection, server,
                                          MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
                                          MHD_OPTION_CONNECTION_LIMIT,
                                          server->config.max_connections > 0 ?
                                          server->config.max_connections : 100,
                                          MHD_OPTION_HTTPS_MEM_CERT, server->config.cert_file,
                                          MHD_OPTION_HTTPS_MEM_KEY, server->config.key_file,
                                          MHD_OPTION_END);
    } else {
        server->daemon = MHD_start_daemon(flags, server->config.port,
                                          NULL, NULL,
                                          &answer_to_connection, server,
                                          MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
                                          MHD_OPTION_CONNECTION_LIMIT,
                                          server->config.max_connections > 0 ?
                                          server->config.max_connections : 100,
                                          MHD_OPTION_END);
    }

    if (!server->daemon) {
        fprintf(stderr, "Failed to start web server on port %d\n", server->config.port);
        return -1;
    }

    printf("Web server started on port %d\n", server->config.port);

    return 0;
}

/* Stop web server */
void web_server_stop(struct web_server *server) {
    if (!server) return;

    if (server->daemon) {
        MHD_stop_daemon(server->daemon);
        server->daemon = NULL;
    }
}

/* Cleanup web server */
void web_server_cleanup(struct web_server *server) {
    if (!server) return;

    web_server_stop(server);

    if (server->config.cert_file) free(server->config.cert_file);
    if (server->config.key_file) free(server->config.key_file);
    if (server->config.static_dir) free(server->config.static_dir);

    free(server);
}

/* Get server statistics */
int web_server_get_stats(struct web_server *server, struct web_server_stats *stats) {
    if (!server || !stats) return -1;

    *stats = server->stats;

    return 0;
}
