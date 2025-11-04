/* filter_compiler.h - Filter bytecode compiler
 *
 * Compiles filter AST to bytecode for efficient evaluation.
 * Includes optimization passes to improve runtime performance.
 */

#ifndef FILTER_COMPILER_H
#define FILTER_COMPILER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declaration */
struct filter_expr;
struct filter_node;

/* Bytecode instruction opcodes */
enum filter_opcode {
	/* Stack operations */
	OP_PUSH_FIELD,      /* Push field value onto stack */
	OP_PUSH_STRING,     /* Push string constant onto stack */
	OP_PUSH_NUMBER,     /* Push number constant onto stack */
	OP_POP,             /* Pop value from stack */
	
	/* Comparison operations */
	OP_EQ,              /* == */
	OP_NE,              /* != */
	OP_LT,              /* < */
	OP_GT,              /* > */
	OP_LE,              /* <= */
	OP_GE,              /* >= */
	OP_MATCH,           /* =~ (regex match) */
	OP_NMATCH,          /* !~ (regex not match) */
	OP_IN,              /* IN (set membership) */
	
	/* Logical operations */
	OP_AND,             /* Logical AND */
	OP_OR,              /* Logical OR */
	OP_NOT,             /* Logical NOT */
	
	/* Control flow */
	OP_JUMP,            /* Unconditional jump */
	OP_JUMP_IF_FALSE,   /* Jump if top of stack is false */
	OP_JUMP_IF_TRUE,    /* Jump if top of stack is true */
	
	/* Special */
	OP_RETURN,          /* Return result */
	OP_NOP,             /* No operation */
};

/* Bytecode instruction */
struct filter_instruction {
	enum filter_opcode opcode;
	
	union {
		/* For PUSH_FIELD */
		struct {
			uint8_t field_type;
		} field;
		
		/* For PUSH_STRING */
		struct {
			uint32_t string_index;  /* Index into string constant table */
		} string;
		
		/* For PUSH_NUMBER */
		struct {
			int64_t value;
		} number;
		
		/* For JUMP instructions */
		struct {
			int32_t offset;  /* Relative offset */
		} jump;
		
		/* For IN operation */
		struct {
			uint32_t count;  /* Number of items in set */
		} in;
	} operand;
};

/* Compiled bytecode */
struct filter_bytecode {
	struct filter_instruction *instructions;
	size_t instruction_count;
	size_t instruction_capacity;
	
	/* String constant table */
	char **strings;
	size_t string_count;
	size_t string_capacity;
	
	/* Optimization statistics */
	size_t original_instruction_count;
	size_t optimizations_applied;
};

/**
 * filter_compile() - Compile filter expression to bytecode
 * @expr: Parsed filter expression
 *
 * Returns: Pointer to compiled bytecode or NULL on error
 */
struct filter_bytecode *filter_compile(struct filter_expr *expr);

/**
 * filter_compile_node() - Compile AST node to bytecode
 * @node: AST node to compile
 * @bytecode: Bytecode structure to append to
 *
 * Returns: true on success, false on error
 */
bool filter_compile_node(struct filter_node *node, struct filter_bytecode *bytecode);

/**
 * filter_bytecode_free() - Free compiled bytecode
 * @bytecode: Bytecode to free
 */
void filter_bytecode_free(struct filter_bytecode *bytecode);

/**
 * filter_bytecode_optimize() - Apply optimization passes to bytecode
 * @bytecode: Bytecode to optimize
 *
 * Optimizations include:
 * - Constant folding
 * - Dead code elimination
 * - Jump optimization
 * - Peephole optimization
 *
 * Returns: Number of optimizations applied
 */
size_t filter_bytecode_optimize(struct filter_bytecode *bytecode);

/**
 * filter_bytecode_disassemble() - Disassemble bytecode to human-readable form
 * @bytecode: Bytecode to disassemble
 * @buf: Output buffer
 * @size: Buffer size
 *
 * Returns: Number of characters written
 */
size_t filter_bytecode_disassemble(struct filter_bytecode *bytecode,
                                   char *buf, size_t size);

/**
 * filter_bytecode_stats() - Get bytecode statistics
 * @bytecode: Bytecode
 * @instruction_count: Output for instruction count
 * @string_count: Output for string constant count
 * @optimizations: Output for optimizations applied
 */
void filter_bytecode_stats(struct filter_bytecode *bytecode,
                           size_t *instruction_count,
                           size_t *string_count,
                           size_t *optimizations);

#endif /* FILTER_COMPILER_H */
