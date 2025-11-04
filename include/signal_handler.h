/* signal_handler.h - Signal handling for graceful shutdown
 *
 * Provides proper signal handling with resource cleanup.
 */

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <stdbool.h>

/* Shutdown callback function */
typedef void (*shutdown_callback_fn)(void *ctx);

/* Signal handler context (opaque) */
struct signal_handler;

/**
 * signal_handler_init() - Initialize signal handler
 *
 * Returns: Pointer to signal handler or NULL on error
 */
struct signal_handler *signal_handler_init(void);

/**
 * signal_handler_cleanup() - Cleanup signal handler
 * @handler: Signal handler
 */
void signal_handler_cleanup(struct signal_handler *handler);

/**
 * signal_handler_register_callback() - Register shutdown callback
 * @handler: Signal handler
 * @callback: Callback function to call on shutdown
 * @ctx: Context to pass to callback
 *
 * Returns: Callback ID or -1 on error
 */
int signal_handler_register_callback(struct signal_handler *handler,
                                     shutdown_callback_fn callback,
                                     void *ctx);

/**
 * signal_handler_unregister_callback() - Unregister shutdown callback
 * @handler: Signal handler
 * @callback_id: Callback ID returned by register
 */
void signal_handler_unregister_callback(struct signal_handler *handler,
                                        int callback_id);

/**
 * signal_handler_should_exit() - Check if shutdown was requested
 * @handler: Signal handler
 *
 * Returns: true if shutdown signal received
 */
bool signal_handler_should_exit(struct signal_handler *handler);

/**
 * signal_handler_wait_for_signal() - Wait for shutdown signal
 * @handler: Signal handler
 *
 * Blocks until shutdown signal received
 */
void signal_handler_wait_for_signal(struct signal_handler *handler);

#endif /* SIGNAL_HANDLER_H */
