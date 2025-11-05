#ifndef NLMON_NETLINK_API_H
#define NLMON_NETLINK_API_H

/**
 * Unified Netlink API
 * 
 * This header provides a unified API that can use either the old
 * netlink_multi_protocol implementation or the new libnl-based
 * implementation, based on configuration.
 * 
 * Applications should use these functions instead of calling the
 * implementation-specific functions directly.
 */

#include "netlink_multi_protocol.h"
#include "nlmon_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize netlink support with implementation selection
 * 
 * This function checks the configuration and initializes either the
 * old or new implementation accordingly.
 * 
 * @param config Netlink configuration (contains use_libnl flag)
 * @return Context pointer (opaque), or NULL on error
 */
struct nlmon_multi_protocol_ctx *nlmon_netlink_init(const struct nlmon_netlink_config *config);

/**
 * Enable specific protocol
 * 
 * @param ctx Context from nlmon_netlink_init()
 * @param proto Protocol to enable
 * @param use_libnl Whether using libnl implementation
 * @return 0 on success, negative error code on failure
 */
int nlmon_netlink_enable(struct nlmon_multi_protocol_ctx *ctx,
                         nlmon_protocol_t proto,
                         int use_libnl);

/**
 * Disable specific protocol
 * 
 * @param ctx Context from nlmon_netlink_init()
 * @param proto Protocol to disable
 * @param use_libnl Whether using libnl implementation
 * @return 0 on success, negative error code on failure
 */
int nlmon_netlink_disable(struct nlmon_multi_protocol_ctx *ctx,
                          nlmon_protocol_t proto,
                          int use_libnl);

/**
 * Set event callback
 * 
 * @param ctx Context from nlmon_netlink_init()
 * @param callback Event callback function
 * @param user_data User data passed to callback
 * @param use_libnl Whether using libnl implementation
 */
void nlmon_netlink_set_callback(struct nlmon_multi_protocol_ctx *ctx,
                                void (*callback)(nlmon_event_type_t, void *, void *),
                                void *user_data,
                                int use_libnl);

/**
 * Get file descriptor for event loop
 * 
 * @param ctx Context from nlmon_netlink_init()
 * @param proto Protocol to get FD for
 * @param use_libnl Whether using libnl implementation
 * @return File descriptor, or -1 on error
 */
int nlmon_netlink_get_fd(struct nlmon_multi_protocol_ctx *ctx,
                         nlmon_protocol_t proto,
                         int use_libnl);

/**
 * Process messages from protocol
 * 
 * @param ctx Context from nlmon_netlink_init()
 * @param proto Protocol to process
 * @param use_libnl Whether using libnl implementation
 * @return 0 on success, negative error code on failure
 */
int nlmon_netlink_process(struct nlmon_multi_protocol_ctx *ctx,
                          nlmon_protocol_t proto,
                          int use_libnl);

/**
 * Cleanup and destroy context
 * 
 * @param ctx Context to destroy
 * @param use_libnl Whether using libnl implementation
 */
void nlmon_netlink_destroy(struct nlmon_multi_protocol_ctx *ctx,
                           int use_libnl);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NETLINK_API_H */
