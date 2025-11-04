#include "websocket_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define MAX_CONNECTIONS 100
#define BUFFER_SIZE 8192
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* WebSocket opcodes */
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT 0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE 0x8
#define WS_OPCODE_PING 0x9
#define WS_OPCODE_PONG 0xA

/* WebSocket connection */
struct ws_connection {
    int fd;
    int active;
    pthread_t thread;
    struct websocket_server *server;
    void *user_data;
    char recv_buffer[BUFFER_SIZE];
    size_t recv_len;
};

/* WebSocket server */
struct websocket_server {
    int listen_fd;
    int running;
    pthread_t accept_thread;
    struct websocket_config config;
    struct ws_connection connections[MAX_CONNECTIONS];
    pthread_mutex_t lock;
};

/* Base64 encode */
static char *base64_encode(const unsigned char *input, int length) {
    BIO *bmem, *b64;
    BUF_MEM *bptr;
    
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);
    
    char *buff = malloc(bptr->length + 1);
    memcpy(buff, bptr->data, bptr->length);
    buff[bptr->length] = 0;
    
    BIO_free_all(b64);
    
    return buff;
}

/* Parse WebSocket handshake */
static int parse_handshake(const char *request, char *key, size_t key_len) {
    const char *key_header = "Sec-WebSocket-Key: ";
    const char *key_start = strstr(request, key_header);
    
    if (!key_start) return -1;
    
    key_start += strlen(key_header);
    const char *key_end = strstr(key_start, "\r\n");
    
    if (!key_end) return -1;
    
    size_t len = key_end - key_start;
    if (len >= key_len) return -1;
    
    strncpy(key, key_start, len);
    key[len] = '\0';
    
    return 0;
}

/* Send WebSocket handshake response */
static int send_handshake_response(int fd, const char *key) {
    char accept_key[256];
    unsigned char hash[SHA_DIGEST_LENGTH];
    
    /* Concatenate key with GUID */
    snprintf(accept_key, sizeof(accept_key), "%s%s", key, WS_GUID);
    
    /* Calculate SHA-1 hash */
    SHA1((unsigned char *)accept_key, strlen(accept_key), hash);
    
    /* Base64 encode */
    char *encoded = base64_encode(hash, SHA_DIGEST_LENGTH);
    
    /* Send response */
    char response[512];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", encoded);
    
    free(encoded);
    
    int ret = send(fd, response, len, 0);
    
    return ret == len ? 0 : -1;
}

/* Decode WebSocket frame */
static int decode_frame(const unsigned char *data, size_t len,
                        unsigned char *payload, size_t *payload_len,
                        int *opcode) {
    if (len < 2) return -1;
    
    int fin = (data[0] & 0x80) != 0;
    (void)fin; /* Reserved for future fragmentation support */
    *opcode = data[0] & 0x0F;
    int masked = (data[1] & 0x80) != 0;
    uint64_t payload_length = data[1] & 0x7F;
    
    size_t offset = 2;
    
    /* Extended payload length */
    if (payload_length == 126) {
        if (len < 4) return -1;
        payload_length = (data[2] << 8) | data[3];
        offset = 4;
    } else if (payload_length == 127) {
        if (len < 10) return -1;
        payload_length = 0;
        for (int i = 0; i < 8; i++) {
            payload_length = (payload_length << 8) | data[2 + i];
        }
        offset = 10;
    }
    
    /* Masking key */
    unsigned char mask[4];
    if (masked) {
        if (len < offset + 4) return -1;
        memcpy(mask, data + offset, 4);
        offset += 4;
    }
    
    /* Check if we have complete payload */
    if (len < offset + payload_length) return -1;
    
    /* Decode payload */
    if (payload_length > *payload_len) {
        payload_length = *payload_len;
    }
    
    for (size_t i = 0; i < payload_length; i++) {
        payload[i] = masked ? (data[offset + i] ^ mask[i % 4]) : data[offset + i];
    }
    
    *payload_len = payload_length;
    
    return offset + payload_length;
}

/* Encode WebSocket frame */
static int encode_frame(const unsigned char *payload, size_t payload_len,
                        unsigned char *frame, size_t frame_size, int opcode) {
    if (frame_size < 2 + payload_len) return -1;
    
    frame[0] = 0x80 | (opcode & 0x0F);  /* FIN + opcode */
    
    size_t offset = 2;
    
    if (payload_len < 126) {
        frame[1] = payload_len;
    } else if (payload_len < 65536) {
        frame[1] = 126;
        frame[2] = (payload_len >> 8) & 0xFF;
        frame[3] = payload_len & 0xFF;
        offset = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (payload_len >> (56 - i * 8)) & 0xFF;
        }
        offset = 10;
    }
    
    memcpy(frame + offset, payload, payload_len);
    
    return offset + payload_len;
}

/* Connection handler thread */
static void *connection_handler(void *arg) {
    struct ws_connection *conn = arg;
    struct websocket_server *server = conn->server;
    char buffer[BUFFER_SIZE];
    int handshake_done = 0;
    
    while (conn->active) {
        int n = recv(conn->fd, buffer, sizeof(buffer) - 1, 0);
        
        if (n <= 0) {
            break;
        }
        
        buffer[n] = '\0';
        
        if (!handshake_done) {
            /* Handle WebSocket handshake */
            char key[256];
            if (parse_handshake(buffer, key, sizeof(key)) == 0) {
                if (send_handshake_response(conn->fd, key) == 0) {
                    handshake_done = 1;
                    if (server->config.on_connect) {
                        server->config.on_connect(conn, server->config.user_data);
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        } else {
            /* Handle WebSocket frame */
            unsigned char payload[BUFFER_SIZE];
            size_t payload_len = sizeof(payload);
            int opcode;
            
            int frame_len = decode_frame((unsigned char *)buffer, n,
                                         payload, &payload_len, &opcode);
            
            if (frame_len > 0) {
                switch (opcode) {
                    case WS_OPCODE_TEXT:
                    case WS_OPCODE_BINARY:
                        if (server->config.on_message) {
                            payload[payload_len] = '\0';
                            server->config.on_message(conn, (char *)payload,
                                                     payload_len,
                                                     server->config.user_data);
                        }
                        break;
                    
                    case WS_OPCODE_CLOSE:
                        conn->active = 0;
                        break;
                    
                    case WS_OPCODE_PING:
                        /* Send pong */
                        websocket_send(conn, (char *)payload, payload_len);
                        break;
                    
                    default:
                        break;
                }
            }
        }
    }
    
    if (server->config.on_disconnect) {
        server->config.on_disconnect(conn, server->config.user_data);
    }
    
    close(conn->fd);
    conn->active = 0;
    
    return NULL;
}

/* Accept thread */
static void *accept_thread(void *arg) {
    struct websocket_server *server = arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (server->running) {
        int client_fd = accept(server->listen_fd,
                               (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (server->running) {
                perror("accept");
            }
            continue;
        }
        
        /* Find free connection slot */
        pthread_mutex_lock(&server->lock);
        
        struct ws_connection *conn = NULL;
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (!server->connections[i].active) {
                conn = &server->connections[i];
                conn->fd = client_fd;
                conn->active = 1;
                conn->server = server;
                conn->user_data = NULL;
                break;
            }
        }
        
        pthread_mutex_unlock(&server->lock);
        
        if (conn) {
            pthread_create(&conn->thread, NULL, connection_handler, conn);
            pthread_detach(conn->thread);
        } else {
            close(client_fd);
        }
    }
    
    return NULL;
}

/* Initialize WebSocket server */
struct websocket_server *websocket_server_init(struct websocket_config *config) {
    if (!config) return NULL;
    
    struct websocket_server *server = calloc(1, sizeof(struct websocket_server));
    if (!server) return NULL;
    
    server->config = *config;
    server->listen_fd = -1;
    server->running = 0;
    pthread_mutex_init(&server->lock, NULL);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        server->connections[i].active = 0;
    }
    
    return server;
}

/* Start WebSocket server */
int websocket_server_start(struct websocket_server *server) {
    if (!server) return -1;
    
    /* Create socket */
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Set socket options */
    int opt = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(server->config.port);
    
    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server->listen_fd);
        return -1;
    }
    
    /* Listen */
    if (listen(server->listen_fd, 10) < 0) {
        perror("listen");
        close(server->listen_fd);
        return -1;
    }
    
    /* Start accept thread */
    server->running = 1;
    pthread_create(&server->accept_thread, NULL, accept_thread, server);
    
    printf("WebSocket server started on port %d\n", server->config.port);
    
    return 0;
}

/* Stop WebSocket server */
void websocket_server_stop(struct websocket_server *server) {
    if (!server) return;
    
    server->running = 0;
    
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    
    /* Close all connections */
    pthread_mutex_lock(&server->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (server->connections[i].active) {
            server->connections[i].active = 0;
            close(server->connections[i].fd);
        }
    }
    pthread_mutex_unlock(&server->lock);
}

/* Cleanup WebSocket server */
void websocket_server_cleanup(struct websocket_server *server) {
    if (!server) return;
    
    websocket_server_stop(server);
    pthread_mutex_destroy(&server->lock);
    free(server);
}

/* Send message to connection */
int websocket_send(struct ws_connection *conn, const char *message, size_t len) {
    if (!conn || !conn->active) return -1;
    
    unsigned char frame[BUFFER_SIZE];
    int frame_len = encode_frame((unsigned char *)message, len, frame,
                                 sizeof(frame), WS_OPCODE_TEXT);
    
    if (frame_len < 0) return -1;
    
    int ret = send(conn->fd, frame, frame_len, 0);
    
    return ret == frame_len ? 0 : -1;
}

/* Broadcast message to all connections */
int websocket_broadcast(struct websocket_server *server, const char *message, size_t len) {
    if (!server) return -1;
    
    pthread_mutex_lock(&server->lock);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (server->connections[i].active) {
            websocket_send(&server->connections[i], message, len);
        }
    }
    
    pthread_mutex_unlock(&server->lock);
    
    return 0;
}

/* Get connection count */
int websocket_get_connection_count(struct websocket_server *server) {
    if (!server) return 0;
    
    int count = 0;
    
    pthread_mutex_lock(&server->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (server->connections[i].active) {
            count++;
        }
    }
    pthread_mutex_unlock(&server->lock);
    
    return count;
}

/* Set connection user data */
void websocket_set_user_data(struct ws_connection *conn, void *user_data) {
    if (conn) {
        conn->user_data = user_data;
    }
}

/* Get connection user data */
void *websocket_get_user_data(struct ws_connection *conn) {
    return conn ? conn->user_data : NULL;
}
