/* filter_parser.h - Filter expression parser with AST representation
 *
 * Implements recursive descent parser for filter language with support for:
 * - Field comparisons (==, !=, <, >, <=, >=)
 * - Pattern matching (=~, !~)
 * - Logical operators (AND, OR, NOT)
 * - Parentheses for grouping
 * - IN operator for set membership
 */

#ifndef FILTER_PARSER_H
#define FILTER_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* AST node types */
enum filter_node_type {
	/* Comparison operators */
	FILTER_NODE_EQ,        /* == */
	FILTER_NODE_NE,        /* != */
	FILTER_NODE_LT,        /* < */
	FILTER_NODE_GT,        /* > */
	FILTER_NODE_LE,        /* <= */
	FILTER_NODE_GE,        /* >= */
	FILTER_NODE_MATCH,     /* =~ (regex match) */
	FILTER_NODE_NMATCH,    /* !~ (regex not match) */
	FILTER_NODE_IN,        /* IN (set membership) */
	
	/* Logical operators */
	FILTER_NODE_AND,       /* AND */
	FILTER_NODE_OR,        /* OR */
	FILTER_NODE_NOT,       /* NOT */
	
	/* Operands */
	FILTER_NODE_FIELD,     /* Field reference (e.g., interface, message_type) */
	FILTER_NODE_STRING,    /* String literal */
	FILTER_NODE_NUMBER,    /* Numeric literal */
	FILTER_NODE_LIST,      /* List of values for IN operator */
};

/* Field types that can be filtered */
enum filter_field_type {
	FILTER_FIELD_INTERFACE,
	FILTER_FIELD_MESSAGE_TYPE,
	FILTER_FIELD_EVENT_TYPE,
	FILTER_FIELD_NAMESPACE,
	FILTER_FIELD_TIMESTAMP,
	FILTER_FIELD_SEQUENCE,
};

/* AST node structure */
struct filter_node {
	enum filter_node_type type;
	
	union {
		/* For binary operators (comparison, logical) */
		struct {
			struct filter_node *left;
			struct filter_node *right;
		} binary;
		
		/* For unary operators (NOT) */
		struct {
			struct filter_node *operand;
		} unary;
		
		/* For field references */
		struct {
			enum filter_field_type field;
		} field;
		
		/* For string literals */
		struct {
			char *value;
		} string;
		
		/* For numeric literals */
		struct {
			int64_t value;
		} number;
		
		/* For list of values (IN operator) */
		struct {
			struct filter_node **items;
			size_t count;
		} list;
	} data;
};

/* Parse error information */
struct filter_parse_error {
	char message[256];
	size_t position;
	size_t line;
	size_t column;
};

/* Filter expression structure */
struct filter_expr {
	char *expression;              /* Original expression string */
	struct filter_node *ast;       /* Abstract syntax tree */
	struct filter_parse_error error; /* Parse error if any */
	bool valid;                    /* Whether parse was successful */
};

/**
 * filter_parse() - Parse filter expression into AST
 * @expression: Filter expression string
 *
 * Returns: Pointer to filter_expr structure or NULL on allocation failure
 *          Check expr->valid to see if parsing succeeded
 */
struct filter_expr *filter_parse(const char *expression);

/**
 * filter_expr_free() - Free filter expression and AST
 * @expr: Filter expression to free
 */
void filter_expr_free(struct filter_expr *expr);

/**
 * filter_node_free() - Free AST node and its children
 * @node: AST node to free
 */
void filter_node_free(struct filter_node *node);

/**
 * filter_expr_to_string() - Convert AST back to string representation
 * @expr: Filter expression
 * @buf: Output buffer
 * @size: Buffer size
 *
 * Returns: Number of characters written (excluding null terminator)
 */
size_t filter_expr_to_string(struct filter_expr *expr, char *buf, size_t size);

/**
 * filter_validate() - Validate filter expression syntax
 * @expression: Filter expression string
 * @error: Output for error information (can be NULL)
 *
 * Returns: true if valid, false otherwise
 */
bool filter_validate(const char *expression, struct filter_parse_error *error);

#endif /* FILTER_PARSER_H */
