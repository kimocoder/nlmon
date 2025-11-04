/* filter_manager.h - Filter management system
 *
 * Provides filter save/load, naming, organization, and validation.
 * Manages a collection of named filters with persistence.
 */

#ifndef FILTER_MANAGER_H
#define FILTER_MANAGER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Forward declarations */
struct filter_expr;
struct filter_bytecode;
struct filter_eval_context;
struct nlmon_event;

/* Filter entry in the manager */
struct filter_entry {
	char *name;                      /* Filter name */
	char *description;               /* Optional description */
	char *expression;                /* Filter expression string */
	struct filter_expr *parsed;      /* Parsed AST */
	struct filter_bytecode *compiled; /* Compiled bytecode */
	
	/* Statistics */
	uint64_t eval_count;             /* Number of evaluations */
	uint64_t match_count;            /* Number of matches */
	uint64_t total_time_ns;          /* Total evaluation time */
	
	/* Metadata */
	time_t created;                  /* Creation timestamp */
	time_t modified;                 /* Last modification timestamp */
	bool enabled;                    /* Whether filter is active */
	
	struct filter_entry *next;       /* Linked list */
};

/* Filter manager */
struct filter_manager {
	struct filter_entry *filters;    /* Linked list of filters */
	size_t filter_count;
	
	struct filter_eval_context *eval_ctx; /* Shared evaluation context */
	
	char *storage_path;              /* Path to filter storage file */
	bool auto_save;                  /* Auto-save on changes */
};

/**
 * filter_manager_create() - Create filter manager
 * @storage_path: Path to filter storage file (can be NULL)
 *
 * Returns: Pointer to filter manager or NULL on error
 */
struct filter_manager *filter_manager_create(const char *storage_path);

/**
 * filter_manager_destroy() - Destroy filter manager
 * @mgr: Filter manager
 */
void filter_manager_destroy(struct filter_manager *mgr);

/**
 * filter_manager_add() - Add filter to manager
 * @mgr: Filter manager
 * @name: Filter name (must be unique)
 * @expression: Filter expression
 * @description: Optional description (can be NULL)
 *
 * Returns: true on success, false on error
 */
bool filter_manager_add(struct filter_manager *mgr,
                        const char *name,
                        const char *expression,
                        const char *description);

/**
 * filter_manager_remove() - Remove filter from manager
 * @mgr: Filter manager
 * @name: Filter name
 *
 * Returns: true if filter was removed, false if not found
 */
bool filter_manager_remove(struct filter_manager *mgr, const char *name);

/**
 * filter_manager_get() - Get filter by name
 * @mgr: Filter manager
 * @name: Filter name
 *
 * Returns: Pointer to filter entry or NULL if not found
 */
struct filter_entry *filter_manager_get(struct filter_manager *mgr,
                                        const char *name);

/**
 * filter_manager_update() - Update filter expression
 * @mgr: Filter manager
 * @name: Filter name
 * @expression: New filter expression
 *
 * Returns: true on success, false on error
 */
bool filter_manager_update(struct filter_manager *mgr,
                           const char *name,
                           const char *expression);

/**
 * filter_manager_enable() - Enable filter
 * @mgr: Filter manager
 * @name: Filter name
 *
 * Returns: true on success, false if not found
 */
bool filter_manager_enable(struct filter_manager *mgr, const char *name);

/**
 * filter_manager_disable() - Disable filter
 * @mgr: Filter manager
 * @name: Filter name
 *
 * Returns: true on success, false if not found
 */
bool filter_manager_disable(struct filter_manager *mgr, const char *name);

/**
 * filter_manager_eval() - Evaluate event against filter
 * @mgr: Filter manager
 * @name: Filter name
 * @event: Event to evaluate
 *
 * Returns: true if event matches filter, false otherwise
 */
bool filter_manager_eval(struct filter_manager *mgr,
                         const char *name,
                         struct nlmon_event *event);

/**
 * filter_manager_eval_all() - Evaluate event against all enabled filters
 * @mgr: Filter manager
 * @event: Event to evaluate
 * @matches: Output array of matching filter names (can be NULL)
 * @max_matches: Maximum number of matches to return
 *
 * Returns: Number of matching filters
 */
size_t filter_manager_eval_all(struct filter_manager *mgr,
                               struct nlmon_event *event,
                               const char **matches,
                               size_t max_matches);

/**
 * filter_manager_list() - List all filters
 * @mgr: Filter manager
 * @names: Output array of filter names
 * @max_count: Maximum number of names to return
 *
 * Returns: Number of filters
 */
size_t filter_manager_list(struct filter_manager *mgr,
                           const char **names,
                           size_t max_count);

/**
 * filter_manager_save() - Save filters to storage file
 * @mgr: Filter manager
 *
 * Returns: true on success, false on error
 */
bool filter_manager_save(struct filter_manager *mgr);

/**
 * filter_manager_load() - Load filters from storage file
 * @mgr: Filter manager
 *
 * Returns: Number of filters loaded, or -1 on error
 */
int filter_manager_load(struct filter_manager *mgr);

/**
 * filter_manager_validate() - Validate filter expression
 * @mgr: Filter manager
 * @expression: Filter expression to validate
 * @error_msg: Output buffer for error message (can be NULL)
 * @error_size: Size of error message buffer
 *
 * Returns: true if valid, false otherwise
 */
bool filter_manager_validate(struct filter_manager *mgr,
                             const char *expression,
                             char *error_msg,
                             size_t error_size);

/**
 * filter_manager_stats() - Get filter statistics
 * @mgr: Filter manager
 * @name: Filter name
 * @eval_count: Output for evaluation count
 * @match_count: Output for match count
 * @avg_time_ns: Output for average evaluation time
 *
 * Returns: true if filter found, false otherwise
 */
bool filter_manager_stats(struct filter_manager *mgr,
                         const char *name,
                         uint64_t *eval_count,
                         uint64_t *match_count,
                         uint64_t *avg_time_ns);

/**
 * filter_manager_reset_stats() - Reset statistics for filter
 * @mgr: Filter manager
 * @name: Filter name (NULL for all filters)
 */
void filter_manager_reset_stats(struct filter_manager *mgr, const char *name);

#endif /* FILTER_MANAGER_H */
