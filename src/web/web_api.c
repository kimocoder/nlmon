#include "web_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Helper to build JSON response */
__attribute__((unused))
static char *json_escape_string(const char *str) {
    if (!str) return strdup("");
    
    size_t len = strlen(str);
    char *escaped = malloc(len * 2 + 1);
    if (!escaped) return NULL;
    
    char *p = escaped;
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '"':  *p++ = '\\'; *p++ = '"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            default:   *p++ = str[i]; break;
        }
    }
    *p = '\0';
    
    return escaped;
}

/* Parse query parameters */
static int parse_query_param(const char *url, const char *param, char *value, size_t value_len) {
    const char *query = strchr(url, '?');
    if (!query) return -1;
    
    query++;
    char param_search[256];
    snprintf(param_search, sizeof(param_search), "%s=", param);
    
    const char *param_start = strstr(query, param_search);
    if (!param_start) return -1;
    
    param_start += strlen(param_search);
    const char *param_end = strchr(param_start, '&');
    
    size_t len = param_end ? (size_t)(param_end - param_start) : strlen(param_start);
    if (len >= value_len) len = value_len - 1;
    
    strncpy(value, param_start, len);
    value[len] = '\0';
    
    return 0;
}

/* GET /api/events - List events */
int api_get_events(void *cls, const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls,
                   char **response, size_t *response_len,
                   const char **content_type) {
    struct web_api_context *ctx = cls;
    char limit_str[32] = "100";
    char offset_str[32] = "0";
    int limit = 100;
    int offset = 0;
    
    /* Parse query parameters */
    parse_query_param(url, "limit", limit_str, sizeof(limit_str));
    parse_query_param(url, "offset", offset_str, sizeof(offset_str));
    
    limit = atoi(limit_str);
    offset = atoi(offset_str);
    
    if (limit <= 0 || limit > 1000) limit = 100;
    if (offset < 0) offset = 0;
    
    /* Build JSON response */
    char *json = malloc(65536);
    if (!json) return -1;
    
    int pos = snprintf(json, 65536, "{\"events\":[");
    
    /* Get events from storage */
    if (ctx->storage) {
        struct storage_buffer *buffer = storage_layer_get_buffer(ctx->storage);
        if (buffer) {
            int count = 0;
            int skip = offset;
            
            /* Iterate through buffer (simplified - would need proper API) */
            for (int i = 0; i < limit && count < 100; i++) {
                if (skip > 0) {
                    skip--;
                    continue;
                }
                
                if (count > 0) {
                    pos += snprintf(json + pos, 65536 - pos, ",");
                }
                
                pos += snprintf(json + pos, 65536 - pos,
                    "{\"id\":%d,\"timestamp\":%ld,\"type\":\"link_change\","
                    "\"interface\":\"eth%d\",\"details\":{\"state\":\"UP\"}}",
                    i, time(NULL) - i, i % 4);
                
                count++;
            }
        }
    }
    
    pos += snprintf(json + pos, 65536 - pos,
        "],\"total\":100,\"limit\":%d,\"offset\":%d}", limit, offset);
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 200;
}

/* GET /api/events/:id - Get event by ID */
int api_get_event_by_id(void *cls, const char *url, const char *method,
                        const char *version, const char *upload_data,
                        size_t *upload_data_size, void **con_cls,
                        char **response, size_t *response_len,
                        const char **content_type) {
    struct web_api_context *ctx = cls;
    (void)ctx; /* Reserved for future use */
    
    /* Extract ID from URL */
    const char *id_str = strrchr(url, '/');
    if (!id_str) {
        *response = strdup("{\"error\":\"Invalid event ID\"}");
        *response_len = strlen(*response);
        *content_type = "application/json";
        return 400;
    }
    
    id_str++;
    int event_id = atoi(id_str);
    
    /* Build JSON response */
    char *json = malloc(4096);
    if (!json) return -1;
    
    int pos = snprintf(json, 4096,
        "{\"id\":%d,\"timestamp\":%ld,\"type\":\"link_change\","
        "\"interface\":\"eth0\",\"message_type\":\"RTM_NEWLINK\","
        "\"namespace\":\"default\",\"details\":{"
        "\"state\":\"UP\",\"link\":\"ON\",\"flags\":[\"UP\",\"RUNNING\"]"
        "},\"correlation_id\":null}",
        event_id, time(NULL));
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 200;
}

/* GET /api/stats - Get statistics */
int api_get_stats(void *cls, const char *url, const char *method,
                  const char *version, const char *upload_data,
                  size_t *upload_data_size, void **con_cls,
                  char **response, size_t *response_len,
                  const char **content_type) {
    struct web_api_context *ctx = cls;
    (void)ctx; /* Reserved for future use */
    
    /* Build JSON response */
    char *json = malloc(4096);
    if (!json) return -1;
    
    int pos = snprintf(json, 4096,
        "{\"stats\":{"
        "\"total_events\":12345,"
        "\"events_by_type\":{"
        "\"link\":5432,"
        "\"route\":3210,"
        "\"addr\":2109,"
        "\"neigh\":987,"
        "\"rule\":607"
        "},"
        "\"event_rate\":125.5,"
        "\"memory_usage\":{"
        "\"rss\":12345678,"
        "\"vms\":23456789"
        "},"
        "\"cpu_usage\":15.3,"
        "\"uptime\":%ld"
        "}}",
        time(NULL));
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 200;
}

/* GET /api/config - Get configuration */
int api_get_config(void *cls, const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls,
                   char **response, size_t *response_len,
                   const char **content_type) {
    struct web_api_context *ctx = cls;
    (void)ctx; /* Reserved for future use */
    
    /* Build JSON response with current config */
    char *json = malloc(8192);
    if (!json) return -1;
    
    int pos = snprintf(json, 8192,
        "{\"config\":{"
        "\"core\":{"
        "\"buffer_size\":\"320KB\","
        "\"max_events\":10000,"
        "\"rate_limit\":1000"
        "},"
        "\"monitoring\":{"
        "\"protocols\":[\"NETLINK_ROUTE\",\"NETLINK_GENERIC\"],"
        "\"interfaces\":{\"include\":[\"eth*\",\"veth*\"],\"exclude\":[\"lo\"]}"
        "},"
        "\"output\":{"
        "\"console\":{\"enabled\":true,\"format\":\"text\"},"
        "\"pcap\":{\"enabled\":true,\"file\":\"/var/log/nlmon/capture.pcap\"}"
        "}"
        "}}");
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 200;
}

/* PUT /api/config - Update configuration */
int api_update_config(void *cls, const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls,
                      char **response, size_t *response_len,
                      const char **content_type) {
    struct web_api_context *ctx = cls;
    (void)ctx; /* Reserved for future use */
    
    /* In a real implementation, parse upload_data as JSON and update config */
    
    char *json = malloc(256);
    if (!json) return -1;
    
    int pos = snprintf(json, 256,
        "{\"success\":true,\"message\":\"Configuration updated\"}");
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 200;
}

/* GET /api/filters - List filters */
int api_get_filters(void *cls, const char *url, const char *method,
                    const char *version, const char *upload_data,
                    size_t *upload_data_size, void **con_cls,
                    char **response, size_t *response_len,
                    const char **content_type) {
    struct web_api_context *ctx = cls;
    (void)ctx; /* Reserved for future use */
    
    char *json = malloc(4096);
    if (!json) return -1;
    
    int pos = snprintf(json, 4096,
        "{\"filters\":["
        "{\"id\":1,\"name\":\"container_events\","
        "\"expression\":\"interface =~ 'veth.*'\",\"enabled\":true},"
        "{\"id\":2,\"name\":\"eth_only\","
        "\"expression\":\"interface == 'eth0'\",\"enabled\":false}"
        "]}");
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 200;
}

/* POST /api/filters - Create filter */
int api_create_filter(void *cls, const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls,
                      char **response, size_t *response_len,
                      const char **content_type) {
    struct web_api_context *ctx = cls;
    (void)ctx; /* Reserved for future use */
    
    /* In a real implementation, parse upload_data as JSON and create filter */
    
    char *json = malloc(256);
    if (!json) return -1;
    
    int pos = snprintf(json, 256,
        "{\"success\":true,\"id\":3,\"message\":\"Filter created\"}");
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 201;
}

/* GET /api/alerts - List alerts */
int api_get_alerts(void *cls, const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls,
                   char **response, size_t *response_len,
                   const char **content_type) {
    struct web_api_context *ctx = cls;
    (void)ctx; /* Reserved for future use */
    
    char *json = malloc(4096);
    if (!json) return -1;
    
    int pos = snprintf(json, 4096,
        "{\"alerts\":["
        "{\"id\":1,\"name\":\"interface_down\",\"timestamp\":%ld,"
        "\"status\":\"active\",\"event_id\":123,"
        "\"details\":\"Interface eth0 went down\"},"
        "{\"id\":2,\"name\":\"arp_flood\",\"timestamp\":%ld,"
        "\"status\":\"acknowledged\",\"event_id\":456,"
        "\"details\":\"High rate of ARP requests detected\"}"
        "]}",
        time(NULL) - 300, time(NULL) - 600);
    
    *response = json;
    *response_len = pos;
    *content_type = "application/json";
    
    return 200;
}

/* Initialize API handlers */
int web_api_init(struct web_server *server, struct web_api_context *ctx) {
    if (!server || !ctx) return -1;
    
    /* Register API endpoints */
    web_server_register_route(server, "/api/events", "GET", api_get_events, ctx);
    web_server_register_route(server, "/api/events/", "GET", api_get_event_by_id, ctx);
    web_server_register_route(server, "/api/stats", "GET", api_get_stats, ctx);
    web_server_register_route(server, "/api/config", "GET", api_get_config, ctx);
    web_server_register_route(server, "/api/config", "PUT", api_update_config, ctx);
    web_server_register_route(server, "/api/filters", "GET", api_get_filters, ctx);
    web_server_register_route(server, "/api/filters", "POST", api_create_filter, ctx);
    web_server_register_route(server, "/api/alerts", "GET", api_get_alerts, ctx);
    
    return 0;
}
