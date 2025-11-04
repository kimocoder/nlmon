#ifndef WEB_API_H
#define WEB_API_H

#include "web_server.h"
#include "storage_layer.h"
#include "filter_manager.h"
#include "nlmon_config.h"

/* API context */
struct web_api_context {
    struct storage_layer *storage;
    struct filter_manager *filter_mgr;
    struct nlmon_config *config;
    void *alert_mgr;  /* Forward declaration */
};

/* Initialize API handlers */
int web_api_init(struct web_server *server, struct web_api_context *ctx);

/* API endpoint handlers */
int api_get_events(void *cls, const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls,
                   char **response, size_t *response_len,
                   const char **content_type);

int api_get_event_by_id(void *cls, const char *url, const char *method,
                        const char *version, const char *upload_data,
                        size_t *upload_data_size, void **con_cls,
                        char **response, size_t *response_len,
                        const char **content_type);

int api_get_stats(void *cls, const char *url, const char *method,
                  const char *version, const char *upload_data,
                  size_t *upload_data_size, void **con_cls,
                  char **response, size_t *response_len,
                  const char **content_type);

int api_get_config(void *cls, const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls,
                   char **response, size_t *response_len,
                   const char **content_type);

int api_update_config(void *cls, const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls,
                      char **response, size_t *response_len,
                      const char **content_type);

int api_get_filters(void *cls, const char *url, const char *method,
                    const char *version, const char *upload_data,
                    size_t *upload_data_size, void **con_cls,
                    char **response, size_t *response_len,
                    const char **content_type);

int api_create_filter(void *cls, const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls,
                      char **response, size_t *response_len,
                      const char **content_type);

int api_get_alerts(void *cls, const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls,
                   char **response, size_t *response_len,
                   const char **content_type);

#endif /* WEB_API_H */
