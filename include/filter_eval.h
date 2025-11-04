/* filter_eval.h - Filter bytecode evaluation engine
 *
 * Implements stack-based bytecode interpreter with regex matching
 * and performance profiling support.
 */

#ifndef FILTER_EVAL_H
#define FILTER_EVAL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <regex.h>

/* Forward declarations */
struct filter_bytecode;
struct nlmon_event;

/* Stack value types */
enum filter_value_type {
	FILTER_VALUE_BOOL,
	FILTER_VALUE_NUMBER,
	FILTER_VALUE_STRING,
};

/* Stack value */
struct filter_value {
	enum filter_value_type type;
	union {
		bool bool_val;
		int64_t number_val;
		char *string_val;
	} data;
};

/* Evaluation context */
struct filter_eval_context {
	struct filter_value *stack;
	size_t stack_size;
	size_t stack_capacity;
	
	/* Regex cache for pattern matching */
	struct {
		char *pattern;
		regex_t compiled;
		bool valid;
	} *regex_cache;
	size_t regex_cache_size;
	size_t regex_cache_capacity;
	
	/* Performance profiling */
	uint64_t eval_count;
	uint64_t total_time_ns;
	uint64_t min_time_ns;
	uint64_t max_time_ns;
	
	/* Statistics per opcode */
	uint64_t *opcode_counts;
	uint64_t *opcode_times;
};

/**
 * filter_eval_context_create() - Create evaluation context
 *
 * Returns: Pointer to context or NULL on error
 */
struct filter_eval_context *filter_eval_context_create(void);

/**
 * filter_eval_context_destroy() - Destroy evaluation context
 * @ctx: Context to destroy
 */
void filter_eval_context_destroy(struct filter_eval_context *ctx);

/**
 * filter_eval() - Evaluate filter bytecode against event
 * @bytecode: Compiled filter bytecode
 * @event: Event to evaluate
 * @ctx: Evaluation context (can be NULL for one-time evaluation)
 *
 * Returns: true if event matches filter, false otherwise
 */
bool filter_eval(struct filter_bytecode *bytecode,
                 struct nlmon_event *event,
                 struct filter_eval_context *ctx);

/**
 * filter_eval_with_profiling() - Evaluate filter with profiling enabled
 * @bytecode: Compiled filter bytecode
 * @event: Event to evaluate
 * @ctx: Evaluation context (required)
 * @elapsed_ns: Output for evaluation time in nanoseconds
 *
 * Returns: true if event matches filter, false otherwise
 */
bool filter_eval_with_profiling(struct filter_bytecode *bytecode,
                                 struct nlmon_event *event,
                                 struct filter_eval_context *ctx,
                                 uint64_t *elapsed_ns);

/**
 * filter_eval_stats() - Get evaluation statistics
 * @ctx: Evaluation context
 * @eval_count: Output for total evaluations
 * @avg_time_ns: Output for average evaluation time
 * @min_time_ns: Output for minimum evaluation time
 * @max_time_ns: Output for maximum evaluation time
 */
void filter_eval_stats(struct filter_eval_context *ctx,
                       uint64_t *eval_count,
                       uint64_t *avg_time_ns,
                       uint64_t *min_time_ns,
                       uint64_t *max_time_ns);

/**
 * filter_eval_reset_stats() - Reset evaluation statistics
 * @ctx: Evaluation context
 */
void filter_eval_reset_stats(struct filter_eval_context *ctx);

/**
 * filter_eval_opcode_stats() - Get per-opcode statistics
 * @ctx: Evaluation context
 * @opcode: Opcode to query
 * @count: Output for execution count
 * @total_time_ns: Output for total time spent
 */
void filter_eval_opcode_stats(struct filter_eval_context *ctx,
                              int opcode,
                              uint64_t *count,
                              uint64_t *total_time_ns);

#endif /* FILTER_EVAL_H */
