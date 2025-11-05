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
	
	/* Netlink-specific fields */
	FILTER_FIELD_NL_PROTOCOL,        /* netlink.protocol */
	FILTER_FIELD_NL_MSG_TYPE,        /* netlink.msg_type */
	FILTER_FIELD_NL_MSG_FLAGS,       /* netlink.msg_flags */
	FILTER_FIELD_NL_SEQ,             /* netlink.seq */
	FILTER_FIELD_NL_PID,             /* netlink.pid */
	FILTER_FIELD_NL_GENL_CMD,        /* netlink.genl_cmd */
	FILTER_FIELD_NL_GENL_VERSION,    /* netlink.genl_version */
	FILTER_FIELD_NL_GENL_FAMILY_ID,  /* netlink.genl_family_id */
	FILTER_FIELD_NL_GENL_FAMILY_NAME,/* netlink.genl_family_name */
	
	/* NETLINK_ROUTE fields */
	FILTER_FIELD_NL_LINK_IFNAME,     /* netlink.link.ifname */
	FILTER_FIELD_NL_LINK_IFINDEX,    /* netlink.link.ifindex */
	FILTER_FIELD_NL_LINK_FLAGS,      /* netlink.link.flags */
	FILTER_FIELD_NL_LINK_MTU,        /* netlink.link.mtu */
	FILTER_FIELD_NL_LINK_OPERSTATE,  /* netlink.link.operstate */
	FILTER_FIELD_NL_LINK_QDISC,      /* netlink.link.qdisc */
	
	FILTER_FIELD_NL_ADDR_FAMILY,     /* netlink.addr.family */
	FILTER_FIELD_NL_ADDR_IFINDEX,    /* netlink.addr.ifindex */
	FILTER_FIELD_NL_ADDR_PREFIXLEN,  /* netlink.addr.prefixlen */
	FILTER_FIELD_NL_ADDR_SCOPE,      /* netlink.addr.scope */
	FILTER_FIELD_NL_ADDR_ADDR,       /* netlink.addr.addr */
	FILTER_FIELD_NL_ADDR_LABEL,      /* netlink.addr.label */
	
	FILTER_FIELD_NL_ROUTE_FAMILY,    /* netlink.route.family */
	FILTER_FIELD_NL_ROUTE_DST,       /* netlink.route.dst */
	FILTER_FIELD_NL_ROUTE_SRC,       /* netlink.route.src */
	FILTER_FIELD_NL_ROUTE_GATEWAY,   /* netlink.route.gateway */
	FILTER_FIELD_NL_ROUTE_OIF,       /* netlink.route.oif */
	FILTER_FIELD_NL_ROUTE_PROTOCOL,  /* netlink.route.protocol */
	FILTER_FIELD_NL_ROUTE_SCOPE,     /* netlink.route.scope */
	FILTER_FIELD_NL_ROUTE_TYPE,      /* netlink.route.type */
	FILTER_FIELD_NL_ROUTE_PRIORITY,  /* netlink.route.priority */
	
	FILTER_FIELD_NL_NEIGH_FAMILY,    /* netlink.neigh.family */
	FILTER_FIELD_NL_NEIGH_IFINDEX,   /* netlink.neigh.ifindex */
	FILTER_FIELD_NL_NEIGH_STATE,     /* netlink.neigh.state */
	FILTER_FIELD_NL_NEIGH_DST,       /* netlink.neigh.dst */
	
	/* NETLINK_SOCK_DIAG fields */
	FILTER_FIELD_NL_DIAG_FAMILY,     /* netlink.diag.family */
	FILTER_FIELD_NL_DIAG_STATE,      /* netlink.diag.state */
	FILTER_FIELD_NL_DIAG_PROTOCOL,   /* netlink.diag.protocol */
	FILTER_FIELD_NL_DIAG_SRC_PORT,   /* netlink.diag.src_port */
	FILTER_FIELD_NL_DIAG_DST_PORT,   /* netlink.diag.dst_port */
	FILTER_FIELD_NL_DIAG_SRC_ADDR,   /* netlink.diag.src_addr */
	FILTER_FIELD_NL_DIAG_DST_ADDR,   /* netlink.diag.dst_addr */
	FILTER_FIELD_NL_DIAG_UID,        /* netlink.diag.uid */
	FILTER_FIELD_NL_DIAG_INODE,      /* netlink.diag.inode */
	
	/* NETLINK_NETFILTER fields */
	FILTER_FIELD_NL_CT_PROTOCOL,     /* netlink.ct.protocol */
	FILTER_FIELD_NL_CT_TCP_STATE,    /* netlink.ct.tcp_state */
	FILTER_FIELD_NL_CT_SRC_ADDR,     /* netlink.ct.src_addr */
	FILTER_FIELD_NL_CT_DST_ADDR,     /* netlink.ct.dst_addr */
	FILTER_FIELD_NL_CT_SRC_PORT,     /* netlink.ct.src_port */
	FILTER_FIELD_NL_CT_DST_PORT,     /* netlink.ct.dst_port */
	FILTER_FIELD_NL_CT_MARK,         /* netlink.ct.mark */
	
	/* NETLINK_GENERIC nl80211 fields */
	FILTER_FIELD_NL_NL80211_CMD,     /* netlink.nl80211.cmd */
	FILTER_FIELD_NL_NL80211_WIPHY,   /* netlink.nl80211.wiphy */
	FILTER_FIELD_NL_NL80211_IFINDEX, /* netlink.nl80211.ifindex */
	FILTER_FIELD_NL_NL80211_IFNAME,  /* netlink.nl80211.ifname */
	FILTER_FIELD_NL_NL80211_IFTYPE,  /* netlink.nl80211.iftype */
	FILTER_FIELD_NL_NL80211_FREQ,    /* netlink.nl80211.freq */
	
	/* QCA vendor fields */
	FILTER_FIELD_NL_QCA_SUBCMD,      /* netlink.qca.subcmd */
	FILTER_FIELD_NL_QCA_SUBCMD_NAME, /* netlink.qca.subcmd_name */
	FILTER_FIELD_NL_QCA_VENDOR_ID,   /* netlink.qca.vendor_id */
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
