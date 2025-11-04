#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <stdint.h>
#include <stddef.h>

/* WebSocket connection */
struct ws_connection;

/* WebSocket server */
struct websocket_server;

/* WebSocket message callback */
typedef void (*ws_message_callback_t)(struct ws_connection *conn,
                                       const char *message, size_t len,
                                       void *user_data);

/* WebSocket connection callback */
typedef void (*ws_connect_callback_t)(struct ws_connection *conn, void *user_data);
typedef void (*ws_disconnect_callback_t)(struct ws_connection *conn, void *user_data);

/* WebSocket server configuration */
struct websocket_config {
    uint16_t port;
    ws_message_callback_t on_message;
    ws_connect_callback_t on_connect;
    ws_disconnect_callback_t on_disconnect;
    void *user_data;
};

/* Initialize WebSocket server */
struct websocket_server *websocket_server_init(struct websocket_config *config);

/* Start WebSocket server */
int websocket_server_start(struct websocket_server *server);

/* Stop WebSocket server */
void websocket_server_stop(struct websocket_server *server);

/* Cleanup WebSocket server */
void websocket_server_cleanup(struct websocket_server *server);

/* Send message to connection */
int websocket_send(struct ws_connection *conn, const char *message, size_t len);

/* Broadcast message to all connections */
int websocket_broadcast(struct websocket_server *server, const char *message, size_t len);

/* Get connection count */
int websocket_get_connection_count(struct websocket_server *server);

/* Set connection user data */
void websocket_set_user_data(struct ws_connection *conn, void *user_data);

/* Get connection user data */
void *websocket_get_user_data(struct ws_connection *conn);

#endif /* WEBSOCKET_SERVER_H */
