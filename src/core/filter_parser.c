/* filter_parser.c - Filter expression parser implementation
 *
 * Implements recursive descent parser for filter expressions.
 * Grammar:
 *   expr     -> or_expr
 *   or_expr  -> and_expr ( OR and_expr )*
 *   and_expr -> not_expr ( AND not_expr )*
 *   not_expr -> NOT not_expr | cmp_expr
 *   cmp_expr -> primary ( (==|!=|<|>|<=|>=|=~|!~|IN) primary )?
 *   primary  -> FIELD | STRING | NUMBER | '(' expr ')' | '[' list ']'
 *   list     -> primary ( ',' primary )*
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "filter_parser.h"

/* Token types */
enum token_type {
	TOKEN_EOF,
	TOKEN_ERROR,
	
	/* Operators */
	TOKEN_EQ,          /* == */
	TOKEN_NE,          /* != */
	TOKEN_LT,          /* < */
	TOKEN_GT,          /* > */
	TOKEN_LE,          /* <= */
	TOKEN_GE,          /* >= */
	TOKEN_MATCH,       /* =~ */
	TOKEN_NMATCH,      /* !~ */
	
	/* Keywords */
	TOKEN_AND,
	TOKEN_OR,
	TOKEN_NOT,
	TOKEN_IN,
	
	/* Literals */
	TOKEN_FIELD,
	TOKEN_STRING,
	TOKEN_NUMBER,
	
	/* Punctuation */
	TOKEN_LPAREN,      /* ( */
	TOKEN_RPAREN,      /* ) */
	TOKEN_LBRACKET,    /* [ */
	TOKEN_RBRACKET,    /* ] */
	TOKEN_COMMA,       /* , */
};

/* Token structure */
struct token {
	enum token_type type;
	char *value;
	size_t position;
	size_t line;
	size_t column;
};

/* Lexer state */
struct lexer {
	const char *input;
	size_t position;
	size_t line;
	size_t column;
	struct token current;
};

/* Parser state */
struct parser {
	struct lexer lexer;
	struct filter_parse_error error;
	bool has_error;
};

/* Forward declarations */
static struct filter_node *parse_expr(struct parser *p);
static struct filter_node *parse_or_expr(struct parser *p);
static struct filter_node *parse_and_expr(struct parser *p);
static struct filter_node *parse_not_expr(struct parser *p);
static struct filter_node *parse_cmp_expr(struct parser *p);
static struct filter_node *parse_primary(struct parser *p);

/* Helper functions */
static void set_error(struct parser *p, const char *message)
{
	if (p->has_error)
		return;
	
	snprintf(p->error.message, sizeof(p->error.message), "%s", message);
	p->error.position = p->lexer.current.position;
	p->error.line = p->lexer.current.line;
	p->error.column = p->lexer.current.column;
	p->has_error = true;
}

static bool is_keyword(const char *str, const char *keyword)
{
	return strcasecmp(str, keyword) == 0;
}

static enum filter_field_type parse_field_name(const char *name)
{
	if (strcasecmp(name, "interface") == 0)
		return FILTER_FIELD_INTERFACE;
	if (strcasecmp(name, "message_type") == 0)
		return FILTER_FIELD_MESSAGE_TYPE;
	if (strcasecmp(name, "event_type") == 0)
		return FILTER_FIELD_EVENT_TYPE;
	if (strcasecmp(name, "namespace") == 0)
		return FILTER_FIELD_NAMESPACE;
	if (strcasecmp(name, "timestamp") == 0)
		return FILTER_FIELD_TIMESTAMP;
	if (strcasecmp(name, "sequence") == 0)
		return FILTER_FIELD_SEQUENCE;
	
	return FILTER_FIELD_INTERFACE; /* Default */
}

/* Lexer functions */
static void lexer_init(struct lexer *l, const char *input)
{
	memset(l, 0, sizeof(*l));
	l->input = input;
	l->line = 1;
	l->column = 1;
}

static void lexer_free_token(struct token *t)
{
	if (t->value) {
		free(t->value);
		t->value = NULL;
	}
}

static char lexer_peek(struct lexer *l)
{
	return l->input[l->position];
}

static char lexer_advance(struct lexer *l)
{
	char c = l->input[l->position];
	if (c == '\0')
		return c;
	
	l->position++;
	l->column++;
	
	if (c == '\n') {
		l->line++;
		l->column = 1;
	}
	
	return c;
}

static void lexer_skip_whitespace(struct lexer *l)
{
	while (isspace(lexer_peek(l)))
		lexer_advance(l);
}

static char *lexer_read_identifier(struct lexer *l)
{
	size_t start = l->position;
	size_t len = 0;
	char *result;
	
	while (isalnum(lexer_peek(l)) || lexer_peek(l) == '_') {
		lexer_advance(l);
		len++;
	}
	
	result = malloc(len + 1);
	if (result) {
		memcpy(result, l->input + start, len);
		result[len] = '\0';
	}
	
	return result;
}

static char *lexer_read_string(struct lexer *l)
{
	char quote = lexer_advance(l); /* Skip opening quote */
	size_t start = l->position;
	size_t len = 0;
	char *result;
	
	while (lexer_peek(l) != quote && lexer_peek(l) != '\0') {
		if (lexer_peek(l) == '\\')
			lexer_advance(l); /* Skip escape char */
		lexer_advance(l);
		len++;
	}
	
	if (lexer_peek(l) == quote)
		lexer_advance(l); /* Skip closing quote */
	
	result = malloc(len + 1);
	if (result) {
		memcpy(result, l->input + start, len);
		result[len] = '\0';
	}
	
	return result;
}

static char *lexer_read_number(struct lexer *l)
{
	size_t start = l->position;
	size_t len = 0;
	char *result;
	
	while (isdigit(lexer_peek(l))) {
		lexer_advance(l);
		len++;
	}
	
	result = malloc(len + 1);
	if (result) {
		memcpy(result, l->input + start, len);
		result[len] = '\0';
	}
	
	return result;
}

static void lexer_next_token(struct lexer *l)
{
	char c;
	
	lexer_free_token(&l->current);
	lexer_skip_whitespace(l);
	
	l->current.position = l->position;
	l->current.line = l->line;
	l->current.column = l->column;
	
	c = lexer_peek(l);
	
	if (c == '\0') {
		l->current.type = TOKEN_EOF;
		return;
	}
	
	/* Two-character operators */
	if (c == '=' && l->input[l->position + 1] == '=') {
		l->current.type = TOKEN_EQ;
		lexer_advance(l);
		lexer_advance(l);
		return;
	}
	
	if (c == '!' && l->input[l->position + 1] == '=') {
		l->current.type = TOKEN_NE;
		lexer_advance(l);
		lexer_advance(l);
		return;
	}
	
	if (c == '<' && l->input[l->position + 1] == '=') {
		l->current.type = TOKEN_LE;
		lexer_advance(l);
		lexer_advance(l);
		return;
	}
	
	if (c == '>' && l->input[l->position + 1] == '=') {
		l->current.type = TOKEN_GE;
		lexer_advance(l);
		lexer_advance(l);
		return;
	}
	
	if (c == '=' && l->input[l->position + 1] == '~') {
		l->current.type = TOKEN_MATCH;
		lexer_advance(l);
		lexer_advance(l);
		return;
	}
	
	if (c == '!' && l->input[l->position + 1] == '~') {
		l->current.type = TOKEN_NMATCH;
		lexer_advance(l);
		lexer_advance(l);
		return;
	}
	
	/* Single-character operators */
	if (c == '<') {
		l->current.type = TOKEN_LT;
		lexer_advance(l);
		return;
	}
	
	if (c == '>') {
		l->current.type = TOKEN_GT;
		lexer_advance(l);
		return;
	}
	
	/* Punctuation */
	if (c == '(') {
		l->current.type = TOKEN_LPAREN;
		lexer_advance(l);
		return;
	}
	
	if (c == ')') {
		l->current.type = TOKEN_RPAREN;
		lexer_advance(l);
		return;
	}
	
	if (c == '[') {
		l->current.type = TOKEN_LBRACKET;
		lexer_advance(l);
		return;
	}
	
	if (c == ']') {
		l->current.type = TOKEN_RBRACKET;
		lexer_advance(l);
		return;
	}
	
	if (c == ',') {
		l->current.type = TOKEN_COMMA;
		lexer_advance(l);
		return;
	}
	
	/* String literals */
	if (c == '"' || c == '\'') {
		l->current.type = TOKEN_STRING;
		l->current.value = lexer_read_string(l);
		return;
	}
	
	/* Numbers */
	if (isdigit(c)) {
		l->current.type = TOKEN_NUMBER;
		l->current.value = lexer_read_number(l);
		return;
	}
	
	/* Identifiers and keywords */
	if (isalpha(c) || c == '_') {
		l->current.value = lexer_read_identifier(l);
		
		if (is_keyword(l->current.value, "AND")) {
			l->current.type = TOKEN_AND;
		} else if (is_keyword(l->current.value, "OR")) {
			l->current.type = TOKEN_OR;
		} else if (is_keyword(l->current.value, "NOT")) {
			l->current.type = TOKEN_NOT;
		} else if (is_keyword(l->current.value, "IN")) {
			l->current.type = TOKEN_IN;
		} else {
			l->current.type = TOKEN_FIELD;
		}
		return;
	}
	
	/* Unknown character */
	l->current.type = TOKEN_ERROR;
}

/* AST node creation functions */
static struct filter_node *create_node(enum filter_node_type type)
{
	struct filter_node *node = calloc(1, sizeof(*node));
	if (node)
		node->type = type;
	return node;
}

static struct filter_node *create_binary_node(enum filter_node_type type,
                                              struct filter_node *left,
                                              struct filter_node *right)
{
	struct filter_node *node = create_node(type);
	if (node) {
		node->data.binary.left = left;
		node->data.binary.right = right;
	}
	return node;
}

static struct filter_node *create_unary_node(enum filter_node_type type,
                                             struct filter_node *operand)
{
	struct filter_node *node = create_node(type);
	if (node)
		node->data.unary.operand = operand;
	return node;
}

static struct filter_node *create_field_node(enum filter_field_type field)
{
	struct filter_node *node = create_node(FILTER_NODE_FIELD);
	if (node)
		node->data.field.field = field;
	return node;
}

static struct filter_node *create_string_node(const char *value)
{
	struct filter_node *node = create_node(FILTER_NODE_STRING);
	if (node) {
		node->data.string.value = strdup(value);
		if (!node->data.string.value) {
			free(node);
			return NULL;
		}
	}
	return node;
}

static struct filter_node *create_number_node(int64_t value)
{
	struct filter_node *node = create_node(FILTER_NODE_NUMBER);
	if (node)
		node->data.number.value = value;
	return node;
}

/* Parser functions */
static bool expect_token(struct parser *p, enum token_type type)
{
	if (p->lexer.current.type != type) {
		set_error(p, "Unexpected token");
		return false;
	}
	return true;
}

static struct filter_node *parse_primary(struct parser *p)
{
	struct filter_node *node = NULL;
	
	switch (p->lexer.current.type) {
	case TOKEN_FIELD:
		node = create_field_node(parse_field_name(p->lexer.current.value));
		lexer_next_token(&p->lexer);
		break;
		
	case TOKEN_STRING:
		node = create_string_node(p->lexer.current.value);
		lexer_next_token(&p->lexer);
		break;
		
	case TOKEN_NUMBER:
		node = create_number_node(atoll(p->lexer.current.value));
		lexer_next_token(&p->lexer);
		break;
		
	case TOKEN_LPAREN:
		lexer_next_token(&p->lexer);
		node = parse_expr(p);
		if (!expect_token(p, TOKEN_RPAREN)) {
			filter_node_free(node);
			return NULL;
		}
		lexer_next_token(&p->lexer);
		break;
		
	case TOKEN_LBRACKET: {
		/* Parse list for IN operator */
		struct filter_node **items = NULL;
		size_t count = 0;
		size_t capacity = 4;
		
		items = malloc(capacity * sizeof(struct filter_node *));
		if (!items) {
			set_error(p, "Out of memory");
			return NULL;
		}
		
		lexer_next_token(&p->lexer);
		
		while (p->lexer.current.type != TOKEN_RBRACKET &&
		       p->lexer.current.type != TOKEN_EOF) {
			if (count >= capacity) {
				capacity *= 2;
				struct filter_node **new_items = realloc(items,
				                                         capacity * sizeof(struct filter_node *));
				if (!new_items) {
					for (size_t i = 0; i < count; i++)
						filter_node_free(items[i]);
					free(items);
					set_error(p, "Out of memory");
					return NULL;
				}
				items = new_items;
			}
			
			items[count] = parse_primary(p);
			if (!items[count]) {
				for (size_t i = 0; i < count; i++)
					filter_node_free(items[i]);
				free(items);
				return NULL;
			}
			count++;
			
			if (p->lexer.current.type == TOKEN_COMMA)
				lexer_next_token(&p->lexer);
		}
		
		if (!expect_token(p, TOKEN_RBRACKET)) {
			for (size_t i = 0; i < count; i++)
				filter_node_free(items[i]);
			free(items);
			return NULL;
		}
		lexer_next_token(&p->lexer);
		
		node = create_node(FILTER_NODE_LIST);
		if (node) {
			node->data.list.items = items;
			node->data.list.count = count;
		} else {
			for (size_t i = 0; i < count; i++)
				filter_node_free(items[i]);
			free(items);
		}
		break;
	}
		
	default:
		set_error(p, "Expected field, string, number, or '('");
		break;
	}
	
	return node;
}

static struct filter_node *parse_cmp_expr(struct parser *p)
{
	struct filter_node *left, *right;
	enum filter_node_type op_type;
	
	left = parse_primary(p);
	if (!left)
		return NULL;
	
	/* Check for comparison operator */
	switch (p->lexer.current.type) {
	case TOKEN_EQ:
		op_type = FILTER_NODE_EQ;
		break;
	case TOKEN_NE:
		op_type = FILTER_NODE_NE;
		break;
	case TOKEN_LT:
		op_type = FILTER_NODE_LT;
		break;
	case TOKEN_GT:
		op_type = FILTER_NODE_GT;
		break;
	case TOKEN_LE:
		op_type = FILTER_NODE_LE;
		break;
	case TOKEN_GE:
		op_type = FILTER_NODE_GE;
		break;
	case TOKEN_MATCH:
		op_type = FILTER_NODE_MATCH;
		break;
	case TOKEN_NMATCH:
		op_type = FILTER_NODE_NMATCH;
		break;
	case TOKEN_IN:
		op_type = FILTER_NODE_IN;
		break;
	default:
		/* No comparison operator, just return primary */
		return left;
	}
	
	lexer_next_token(&p->lexer);
	right = parse_primary(p);
	if (!right) {
		filter_node_free(left);
		return NULL;
	}
	
	return create_binary_node(op_type, left, right);
}

static struct filter_node *parse_not_expr(struct parser *p)
{
	if (p->lexer.current.type == TOKEN_NOT) {
		lexer_next_token(&p->lexer);
		struct filter_node *operand = parse_not_expr(p);
		if (!operand)
			return NULL;
		return create_unary_node(FILTER_NODE_NOT, operand);
	}
	
	return parse_cmp_expr(p);
}

static struct filter_node *parse_and_expr(struct parser *p)
{
	struct filter_node *left, *right;
	
	left = parse_not_expr(p);
	if (!left)
		return NULL;
	
	while (p->lexer.current.type == TOKEN_AND) {
		lexer_next_token(&p->lexer);
		right = parse_not_expr(p);
		if (!right) {
			filter_node_free(left);
			return NULL;
		}
		left = create_binary_node(FILTER_NODE_AND, left, right);
		if (!left) {
			filter_node_free(right);
			return NULL;
		}
	}
	
	return left;
}

static struct filter_node *parse_or_expr(struct parser *p)
{
	struct filter_node *left, *right;
	
	left = parse_and_expr(p);
	if (!left)
		return NULL;
	
	while (p->lexer.current.type == TOKEN_OR) {
		lexer_next_token(&p->lexer);
		right = parse_and_expr(p);
		if (!right) {
			filter_node_free(left);
			return NULL;
		}
		left = create_binary_node(FILTER_NODE_OR, left, right);
		if (!left) {
			filter_node_free(right);
			return NULL;
		}
	}
	
	return left;
}

static struct filter_node *parse_expr(struct parser *p)
{
	return parse_or_expr(p);
}

/* Public API */
struct filter_expr *filter_parse(const char *expression)
{
	struct filter_expr *expr;
	struct parser parser;
	
	if (!expression)
		return NULL;
	
	expr = calloc(1, sizeof(*expr));
	if (!expr)
		return NULL;
	
	expr->expression = strdup(expression);
	if (!expr->expression) {
		free(expr);
		return NULL;
	}
	
	/* Initialize parser */
	memset(&parser, 0, sizeof(parser));
	lexer_init(&parser.lexer, expression);
	lexer_next_token(&parser.lexer);
	
	/* Parse expression */
	expr->ast = parse_expr(&parser);
	
	if (parser.has_error) {
		expr->error = parser.error;
		expr->valid = false;
	} else if (parser.lexer.current.type != TOKEN_EOF) {
		snprintf(expr->error.message, sizeof(expr->error.message),
		         "Unexpected token at end of expression");
		expr->error.position = parser.lexer.current.position;
		expr->error.line = parser.lexer.current.line;
		expr->error.column = parser.lexer.current.column;
		expr->valid = false;
	} else {
		expr->valid = true;
	}
	
	/* Cleanup lexer */
	lexer_free_token(&parser.lexer.current);
	
	return expr;
}

void filter_node_free(struct filter_node *node)
{
	if (!node)
		return;
	
	switch (node->type) {
	case FILTER_NODE_AND:
	case FILTER_NODE_OR:
	case FILTER_NODE_EQ:
	case FILTER_NODE_NE:
	case FILTER_NODE_LT:
	case FILTER_NODE_GT:
	case FILTER_NODE_LE:
	case FILTER_NODE_GE:
	case FILTER_NODE_MATCH:
	case FILTER_NODE_NMATCH:
	case FILTER_NODE_IN:
		filter_node_free(node->data.binary.left);
		filter_node_free(node->data.binary.right);
		break;
		
	case FILTER_NODE_NOT:
		filter_node_free(node->data.unary.operand);
		break;
		
	case FILTER_NODE_STRING:
		free(node->data.string.value);
		break;
		
	case FILTER_NODE_LIST:
		for (size_t i = 0; i < node->data.list.count; i++)
			filter_node_free(node->data.list.items[i]);
		free(node->data.list.items);
		break;
		
	case FILTER_NODE_FIELD:
	case FILTER_NODE_NUMBER:
		/* No dynamic memory to free */
		break;
	}
	
	free(node);
}

void filter_expr_free(struct filter_expr *expr)
{
	if (!expr)
		return;
	
	free(expr->expression);
	filter_node_free(expr->ast);
	free(expr);
}

bool filter_validate(const char *expression, struct filter_parse_error *error)
{
	struct filter_expr *expr = filter_parse(expression);
	bool valid;
	
	if (!expr)
		return false;
	
	valid = expr->valid;
	
	if (!valid && error)
		*error = expr->error;
	
	filter_expr_free(expr);
	return valid;
}

size_t filter_expr_to_string(struct filter_expr *expr, char *buf, size_t size)
{
	if (!expr || !buf || size == 0)
		return 0;
	
	/* For now, just return the original expression */
	return snprintf(buf, size, "%s", expr->expression ? expr->expression : "");
}
