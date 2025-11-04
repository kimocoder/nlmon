/* event_hooks.h - Event hook system for executing scripts on events
 *
 * Copyright (C) 2025  nlmon contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#ifndef EVENT_HOOKS_H
#define EVENT_HOOKS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "event_processor.h"

/* Maximum lengths */
#define HOOK_MAX_NAME 64
#define HOOK_MAX_SCRIPT 256
#define HOOK_MAX_CONDITION 512
#define HOOK_MAX_OUTPUT 4096

/* Hook execution result */
enum hook_result {
	HOOK_RESULT_SUCCESS = 0,
	HOOK_RESULT_TIMEOUT,
	HOOK_RESULT_ERROR,
	HOOK_RESULT_DISABLED
};

/* Hook statistics */
struct hook_stats {
	unsigned long executions;
	unsigned long successes;
	unsigned long failures;
	unsigned long timeouts;
	unsigned long total_duration_ms;
	unsigned long max_duration_ms;
	unsigned long min_duration_ms;
};

/* Hook configuration */
struct hook_config {
	char name[HOOK_MAX_NAME];
	char script[HOOK_MAX_SCRIPT];
	char condition[HOOK_MAX_CONDITION];  /* Filter expression */
	uint32_t timeout_ms;                 /* Execution timeout in milliseconds */
	bool enabled;
	bool capture_output;                 /* Capture stdout/stderr */
	bool async;                          /* Execute asynchronously */
};

/* Hook manager structure (opaque) */
struct hook_manager;

/**
 * hook_manager_create() - Create hook manager
 * @max_hooks: Maximum number of hooks
 * @max_concurrent: Maximum concurrent hook executions
 *
 * Returns: Pointer to hook manager or NULL on error
 */
struct hook_manager *hook_manager_create(size_t max_hooks, size_t max_concurrent);

/**
 * hook_manager_destroy() - Destroy hook manager
 * @hm: Hook manager
 * @wait: If true, wait for pending executions to complete
 */
void hook_manager_destroy(struct hook_manager *hm, bool wait);

/**
 * hook_manager_register() - Register a hook
 * @hm: Hook manager
 * @config: Hook configuration
 *
 * Returns: Hook ID or -1 on error
 */
int hook_manager_register(struct hook_manager *hm, const struct hook_config *config);

/**
 * hook_manager_unregister() - Unregister a hook
 * @hm: Hook manager
 * @hook_id: Hook ID returned by register
 *
 * Returns: true on success
 */
bool hook_manager_unregister(struct hook_manager *hm, int hook_id);

/**
 * hook_manager_enable() - Enable a hook
 * @hm: Hook manager
 * @hook_id: Hook ID
 *
 * Returns: true on success
 */
bool hook_manager_enable(struct hook_manager *hm, int hook_id);

/**
 * hook_manager_disable() - Disable a hook
 * @hm: Hook manager
 * @hook_id: Hook ID
 *
 * Returns: true on success
 */
bool hook_manager_disable(struct hook_manager *hm, int hook_id);

/**
 * hook_manager_execute() - Execute hooks for an event
 * @hm: Hook manager
 * @event: Event to process
 *
 * This function evaluates all registered hooks and executes those
 * whose conditions match the event.
 */
void hook_manager_execute(struct hook_manager *hm, struct nlmon_event *event);

/**
 * hook_manager_get_stats() - Get hook statistics
 * @hm: Hook manager
 * @hook_id: Hook ID
 * @stats: Output buffer for statistics
 *
 * Returns: true on success
 */
bool hook_manager_get_stats(struct hook_manager *hm, int hook_id,
                            struct hook_stats *stats);

/**
 * hook_manager_reset_stats() - Reset hook statistics
 * @hm: Hook manager
 * @hook_id: Hook ID (or -1 for all hooks)
 */
void hook_manager_reset_stats(struct hook_manager *hm, int hook_id);

/**
 * hook_manager_list() - List all registered hooks
 * @hm: Hook manager
 * @ids: Output buffer for hook IDs
 * @max_ids: Maximum number of IDs to return
 *
 * Returns: Number of hooks returned
 */
size_t hook_manager_list(struct hook_manager *hm, int *ids, size_t max_ids);

/**
 * hook_manager_get_config() - Get hook configuration
 * @hm: Hook manager
 * @hook_id: Hook ID
 * @config: Output buffer for configuration
 *
 * Returns: true on success
 */
bool hook_manager_get_config(struct hook_manager *hm, int hook_id,
                             struct hook_config *config);

/**
 * hook_manager_wait() - Wait for all pending hook executions
 * @hm: Hook manager
 */
void hook_manager_wait(struct hook_manager *hm);

#endif /* EVENT_HOOKS_H */
