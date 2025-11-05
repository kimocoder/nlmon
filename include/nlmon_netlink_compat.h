#ifndef NLMON_NETLINK_COMPAT_H
#define NLMON_NETLINK_COMPAT_H

/**
 * Netlink Compatibility Layer
 * 
 * This compatibility layer provides wrapper functions that match the old
 * netlink_multi_protocol API while internally using the new libnl-based
 * implementation. This allows existing code to continue working without
 * modifications during the migration period.
 * 
 * The compatibility layer can be enabled/disabled via configuration.
 */

#include <stdint.h>
#include <linux/netlink.h>
#include "netlink_multi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nlmon_nl_manager;
struct nlmon_event;

/**
 * Compatibility context structure
 * 
 * This wraps the new nlmon_nl_manager and provides the old API interface.
 * It maintains state needed to translate between old and new APIs.
 */
struct nlmon_multi_protocol_ctx_compat {
	/* Pointer to new implementation manager */
	struct nlmon_nl_manager *nl_mgr;
	
	/* Old-style callback and user data */
	void (*event_callback)(nlmon_event_type_t type, void *data, void *user_data);
	void *user_data;
	
	/* Protocol enable flags (for compatibility) */
	int enable_route;
	int enable_generic;
	int enable_sock_diag;
	
	/* File descriptors (cached for quick access) */
	int route_fd;
	int generic_fd;
	int sock_diag_fd;
};

/**
 * Compatibility API Functions
 * 
 * These functions match the old netlink_multi_protocol API exactly,
 * but internally use the new libnl-based implementation.
 */

/**
 * Initialize multi-protocol support (compatibility wrapper)
 * 
 * Creates a compatibility context that wraps the new nlmon_nl_manager.
 * 
 * @return Compatibility context pointer, or NULL on error
 */
struct nlmon_multi_protocol_ctx *nlmon_multi_protocol_init_compat(void);

/**
 * Enable specific protocol (compatibility wrapper)
 * 
 * @param ctx Compatibility context (cast from old ctx type)
 * @param proto Protocol to enable
 * @return 0 on success, negative error code on failure
 */
int nlmon_multi_protocol_enable_compat(struct nlmon_multi_protocol_ctx *ctx,
                                       nlmon_protocol_t proto);

/**
 * Disable specific protocol (compatibility wrapper)
 * 
 * @param ctx Compatibility context
 * @param proto Protocol to disable
 * @return 0 on success, negative error code on failure
 */
int nlmon_multi_protocol_disable_compat(struct nlmon_multi_protocol_ctx *ctx,
                                        nlmon_protocol_t proto);

/**
 * Set event callback (compatibility wrapper)
 * 
 * @param ctx Compatibility context
 * @param callback Event callback function
 * @param user_data User data passed to callback
 */
void nlmon_multi_protocol_set_callback_compat(struct nlmon_multi_protocol_ctx *ctx,
                                              void (*callback)(nlmon_event_type_t, void *, void *),
                                              void *user_data);

/**
 * Get file descriptor for event loop (compatibility wrapper)
 * 
 * @param ctx Compatibility context
 * @param proto Protocol to get FD for
 * @return File descriptor, or -1 on error
 */
int nlmon_multi_protocol_get_fd_compat(struct nlmon_multi_protocol_ctx *ctx,
                                       nlmon_protocol_t proto);

/**
 * Process messages from protocol (compatibility wrapper)
 * 
 * @param ctx Compatibility context
 * @param proto Protocol to process
 * @return 0 on success, negative error code on failure
 */
int nlmon_multi_protocol_process_compat(struct nlmon_multi_protocol_ctx *ctx,
                                        nlmon_protocol_t proto);

/**
 * Cleanup and destroy context (compatibility wrapper)
 * 
 * @param ctx Compatibility context to destroy
 */
void nlmon_multi_protocol_destroy_compat(struct nlmon_multi_protocol_ctx *ctx);

/**
 * Helper function to convert new event format to old format
 * 
 * This is called internally by the compatibility layer to translate
 * events from the new libnl-based format to the old format expected
 * by existing code.
 * 
 * @param evt New-style nlmon_event
 * @param user_data Compatibility context
 */
void nlmon_compat_event_translator(struct nlmon_event *evt, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NETLINK_COMPAT_H */
