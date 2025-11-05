/* filter_eval.c - Filter bytecode evaluation engine implementation
 *
 * Stack-based bytecode interpreter with regex matching and profiling.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <linux/netlink.h>
#include "filter_eval.h"
#include "filter_compiler.h"
#include "filter_parser.h"
#include "event_processor.h"
#include "nlmon_nl_route.h"
#include "nlmon_nl_genl.h"
#include "nlmon_nl_diag.h"
#include "nlmon_nl_netfilter.h"

#define INITIAL_STACK_CAPACITY 32
#define INITIAL_REGEX_CACHE_CAPACITY 8

/* Helper functions for stack operations */
static bool stack_push(struct filter_eval_context *ctx, struct filter_value value)
{
	if (ctx->stack_size >= ctx->stack_capacity) {
		size_t new_capacity = ctx->stack_capacity * 2;
		struct filter_value *new_stack = realloc(ctx->stack,
		                                         new_capacity * sizeof(struct filter_value));
		if (!new_stack)
			return false;
		ctx->stack = new_stack;
		ctx->stack_capacity = new_capacity;
	}
	
	ctx->stack[ctx->stack_size++] = value;
	return true;
}

static bool stack_pop(struct filter_eval_context *ctx, struct filter_value *value)
{
	if (ctx->stack_size == 0)
		return false;
	
	*value = ctx->stack[--ctx->stack_size];
	return true;
}

static bool stack_peek(struct filter_eval_context *ctx, struct filter_value *value)
{
	if (ctx->stack_size == 0)
		return false;
	
	*value = ctx->stack[ctx->stack_size - 1];
	return true;
}

static void value_free(struct filter_value *value)
{
	if (value->type == FILTER_VALUE_STRING && value->data.string_val) {
		free(value->data.string_val);
		value->data.string_val = NULL;
	}
}

/* Regex cache functions */
static regex_t *get_compiled_regex(struct filter_eval_context *ctx, const char *pattern)
{
	/* Check cache */
	for (size_t i = 0; i < ctx->regex_cache_size; i++) {
		if (ctx->regex_cache[i].valid &&
		    strcmp(ctx->regex_cache[i].pattern, pattern) == 0) {
			return &ctx->regex_cache[i].compiled;
		}
	}
	
	/* Not in cache, compile and add */
	if (ctx->regex_cache_size >= ctx->regex_cache_capacity) {
		size_t new_capacity = ctx->regex_cache_capacity * 2;
		void *new_cache = realloc(ctx->regex_cache,
		                          new_capacity * sizeof(*ctx->regex_cache));
		if (!new_cache)
			return NULL;
		ctx->regex_cache = new_cache;
		ctx->regex_cache_capacity = new_capacity;
	}
	
	size_t idx = ctx->regex_cache_size++;
	ctx->regex_cache[idx].pattern = strdup(pattern);
	if (!ctx->regex_cache[idx].pattern)
		return NULL;
	
	int ret = regcomp(&ctx->regex_cache[idx].compiled, pattern,
	                  REG_EXTENDED | REG_NOSUB);
	if (ret != 0) {
		free(ctx->regex_cache[idx].pattern);
		ctx->regex_cache_size--;
		return NULL;
	}
	
	ctx->regex_cache[idx].valid = true;
	return &ctx->regex_cache[idx].compiled;
}

/* Field extraction from event */
static bool extract_field(struct nlmon_event *event, uint8_t field_type,
                          struct filter_value *value)
{
	switch (field_type) {
	case FILTER_FIELD_INTERFACE:
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->interface);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_MESSAGE_TYPE:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->message_type;
		return true;
		
	case FILTER_FIELD_EVENT_TYPE:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->event_type;
		return true;
		
	case FILTER_FIELD_TIMESTAMP:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->timestamp;
		return true;
		
	case FILTER_FIELD_SEQUENCE:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->sequence;
		return true;
		
	case FILTER_FIELD_NAMESPACE:
		/* Namespace not in basic event structure, return empty string */
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup("");
		return value->data.string_val != NULL;
	
	/* Netlink common fields */
	case FILTER_FIELD_NL_PROTOCOL:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.protocol;
		return true;
		
	case FILTER_FIELD_NL_MSG_TYPE:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.msg_type;
		return true;
		
	case FILTER_FIELD_NL_MSG_FLAGS:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.msg_flags;
		return true;
		
	case FILTER_FIELD_NL_SEQ:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.seq;
		return true;
		
	case FILTER_FIELD_NL_PID:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.pid;
		return true;
		
	case FILTER_FIELD_NL_GENL_CMD:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.genl_cmd;
		return true;
		
	case FILTER_FIELD_NL_GENL_VERSION:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.genl_version;
		return true;
		
	case FILTER_FIELD_NL_GENL_FAMILY_ID:
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.genl_family_id;
		return true;
		
	case FILTER_FIELD_NL_GENL_FAMILY_NAME:
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.genl_family_name);
		return value->data.string_val != NULL;
	
	/* NETLINK_ROUTE link fields */
	case FILTER_FIELD_NL_LINK_IFNAME:
		if (!event->netlink.data.link)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.link->ifname);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_LINK_IFINDEX:
		if (!event->netlink.data.link)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.link->ifindex;
		return true;
		
	case FILTER_FIELD_NL_LINK_FLAGS:
		if (!event->netlink.data.link)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.link->flags;
		return true;
		
	case FILTER_FIELD_NL_LINK_MTU:
		if (!event->netlink.data.link)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.link->mtu;
		return true;
		
	case FILTER_FIELD_NL_LINK_OPERSTATE:
		if (!event->netlink.data.link)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.link->operstate;
		return true;
		
	case FILTER_FIELD_NL_LINK_QDISC:
		if (!event->netlink.data.link)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.link->qdisc);
		return value->data.string_val != NULL;
	
	/* NETLINK_ROUTE address fields */
	case FILTER_FIELD_NL_ADDR_FAMILY:
		if (!event->netlink.data.addr)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.addr->family;
		return true;
		
	case FILTER_FIELD_NL_ADDR_IFINDEX:
		if (!event->netlink.data.addr)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.addr->ifindex;
		return true;
		
	case FILTER_FIELD_NL_ADDR_PREFIXLEN:
		if (!event->netlink.data.addr)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.addr->prefixlen;
		return true;
		
	case FILTER_FIELD_NL_ADDR_SCOPE:
		if (!event->netlink.data.addr)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.addr->scope;
		return true;
		
	case FILTER_FIELD_NL_ADDR_ADDR:
		if (!event->netlink.data.addr)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.addr->addr);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_ADDR_LABEL:
		if (!event->netlink.data.addr)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.addr->label);
		return value->data.string_val != NULL;
	
	/* NETLINK_ROUTE route fields */
	case FILTER_FIELD_NL_ROUTE_FAMILY:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.route->family;
		return true;
		
	case FILTER_FIELD_NL_ROUTE_DST:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.route->dst);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_ROUTE_SRC:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.route->src);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_ROUTE_GATEWAY:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.route->gateway);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_ROUTE_OIF:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.route->oif;
		return true;
		
	case FILTER_FIELD_NL_ROUTE_PROTOCOL:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.route->protocol;
		return true;
		
	case FILTER_FIELD_NL_ROUTE_SCOPE:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.route->scope;
		return true;
		
	case FILTER_FIELD_NL_ROUTE_TYPE:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.route->type;
		return true;
		
	case FILTER_FIELD_NL_ROUTE_PRIORITY:
		if (!event->netlink.data.route)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.route->priority;
		return true;
	
	/* NETLINK_ROUTE neighbor fields */
	case FILTER_FIELD_NL_NEIGH_FAMILY:
		if (!event->netlink.data.neigh)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.neigh->family;
		return true;
		
	case FILTER_FIELD_NL_NEIGH_IFINDEX:
		if (!event->netlink.data.neigh)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.neigh->ifindex;
		return true;
		
	case FILTER_FIELD_NL_NEIGH_STATE:
		if (!event->netlink.data.neigh)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.neigh->state;
		return true;
		
	case FILTER_FIELD_NL_NEIGH_DST:
		if (!event->netlink.data.neigh)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.neigh->dst);
		return value->data.string_val != NULL;
	
	/* NETLINK_SOCK_DIAG fields */
	case FILTER_FIELD_NL_DIAG_FAMILY:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.diag->family;
		return true;
		
	case FILTER_FIELD_NL_DIAG_STATE:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.diag->state;
		return true;
		
	case FILTER_FIELD_NL_DIAG_PROTOCOL:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.diag->protocol;
		return true;
		
	case FILTER_FIELD_NL_DIAG_SRC_PORT:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.diag->src_port;
		return true;
		
	case FILTER_FIELD_NL_DIAG_DST_PORT:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.diag->dst_port;
		return true;
		
	case FILTER_FIELD_NL_DIAG_SRC_ADDR:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.diag->src_addr);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_DIAG_DST_ADDR:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.diag->dst_addr);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_DIAG_UID:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.diag->uid;
		return true;
		
	case FILTER_FIELD_NL_DIAG_INODE:
		if (!event->netlink.data.diag)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.diag->inode;
		return true;
	
	/* NETLINK_NETFILTER conntrack fields */
	case FILTER_FIELD_NL_CT_PROTOCOL:
		if (!event->netlink.data.conntrack)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.conntrack->protocol;
		return true;
		
	case FILTER_FIELD_NL_CT_TCP_STATE:
		if (!event->netlink.data.conntrack)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.conntrack->tcp_state;
		return true;
		
	case FILTER_FIELD_NL_CT_SRC_ADDR:
		if (!event->netlink.data.conntrack)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.conntrack->src_addr);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_CT_DST_ADDR:
		if (!event->netlink.data.conntrack)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.conntrack->dst_addr);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_CT_SRC_PORT:
		if (!event->netlink.data.conntrack)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.conntrack->src_port;
		return true;
		
	case FILTER_FIELD_NL_CT_DST_PORT:
		if (!event->netlink.data.conntrack)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.conntrack->dst_port;
		return true;
		
	case FILTER_FIELD_NL_CT_MARK:
		if (!event->netlink.data.conntrack)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.conntrack->mark;
		return true;
	
	/* NETLINK_GENERIC nl80211 fields */
	case FILTER_FIELD_NL_NL80211_CMD:
		if (!event->netlink.data.nl80211)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.nl80211->cmd;
		return true;
		
	case FILTER_FIELD_NL_NL80211_WIPHY:
		if (!event->netlink.data.nl80211)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.nl80211->wiphy;
		return true;
		
	case FILTER_FIELD_NL_NL80211_IFINDEX:
		if (!event->netlink.data.nl80211)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.nl80211->ifindex;
		return true;
		
	case FILTER_FIELD_NL_NL80211_IFNAME:
		if (!event->netlink.data.nl80211)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.nl80211->ifname);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_NL80211_IFTYPE:
		if (!event->netlink.data.nl80211)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.nl80211->iftype;
		return true;
		
	case FILTER_FIELD_NL_NL80211_FREQ:
		if (!event->netlink.data.nl80211)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.nl80211->freq;
		return true;
	
	/* QCA vendor fields */
	case FILTER_FIELD_NL_QCA_SUBCMD:
		if (!event->netlink.data.qca_vendor)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.qca_vendor->subcmd;
		return true;
		
	case FILTER_FIELD_NL_QCA_SUBCMD_NAME:
		if (!event->netlink.data.qca_vendor)
			return false;
		value->type = FILTER_VALUE_STRING;
		value->data.string_val = strdup(event->netlink.data.qca_vendor->subcmd_name);
		return value->data.string_val != NULL;
		
	case FILTER_FIELD_NL_QCA_VENDOR_ID:
		if (!event->netlink.data.qca_vendor)
			return false;
		value->type = FILTER_VALUE_NUMBER;
		value->data.number_val = event->netlink.data.qca_vendor->vendor_id;
		return true;
		
	default:
		return false;
	}
}

/* Comparison operations */
static bool compare_values(struct filter_value *left, struct filter_value *right,
                           enum filter_opcode op)
{
	/* Type coercion if needed */
	if (left->type == FILTER_VALUE_STRING && right->type == FILTER_VALUE_STRING) {
		int cmp = strcmp(left->data.string_val, right->data.string_val);
		switch (op) {
		case OP_EQ: return cmp == 0;
		case OP_NE: return cmp != 0;
		case OP_LT: return cmp < 0;
		case OP_GT: return cmp > 0;
		case OP_LE: return cmp <= 0;
		case OP_GE: return cmp >= 0;
		default: return false;
		}
	}
	
	if (left->type == FILTER_VALUE_NUMBER && right->type == FILTER_VALUE_NUMBER) {
		int64_t l = left->data.number_val;
		int64_t r = right->data.number_val;
		switch (op) {
		case OP_EQ: return l == r;
		case OP_NE: return l != r;
		case OP_LT: return l < r;
		case OP_GT: return l > r;
		case OP_LE: return l <= r;
		case OP_GE: return l >= r;
		default: return false;
		}
	}
	
	/* Type mismatch */
	return false;
}

static bool match_regex(struct filter_eval_context *ctx,
                        const char *text, const char *pattern)
{
	regex_t *regex = get_compiled_regex(ctx, pattern);
	if (!regex)
		return false;
	
	return regexec(regex, text, 0, NULL, 0) == 0;
}

/* Bytecode evaluation */
static bool eval_bytecode(struct filter_bytecode *bytecode,
                          struct nlmon_event *event,
                          struct filter_eval_context *ctx)
{
	size_t pc = 0; /* Program counter */
	struct filter_value result;
	
	while (pc < bytecode->instruction_count) {
		struct filter_instruction *instr = &bytecode->instructions[pc];
		struct filter_value val, left, right;
		
		switch (instr->opcode) {
		case OP_PUSH_FIELD:
			if (!extract_field(event, instr->operand.field.field_type, &val))
				return false;
			if (!stack_push(ctx, val)) {
				value_free(&val);
				return false;
			}
			break;
			
		case OP_PUSH_STRING:
			val.type = FILTER_VALUE_STRING;
			val.data.string_val = strdup(bytecode->strings[instr->operand.string.string_index]);
			if (!val.data.string_val)
				return false;
			if (!stack_push(ctx, val)) {
				value_free(&val);
				return false;
			}
			break;
			
		case OP_PUSH_NUMBER:
			val.type = FILTER_VALUE_NUMBER;
			val.data.number_val = instr->operand.number.value;
			if (!stack_push(ctx, val))
				return false;
			break;
			
		case OP_POP:
			if (!stack_pop(ctx, &val))
				return false;
			value_free(&val);
			break;
			
		case OP_EQ:
		case OP_NE:
		case OP_LT:
		case OP_GT:
		case OP_LE:
		case OP_GE:
			if (!stack_pop(ctx, &right) || !stack_pop(ctx, &left))
				return false;
			
			result.type = FILTER_VALUE_BOOL;
			result.data.bool_val = compare_values(&left, &right, instr->opcode);
			
			value_free(&left);
			value_free(&right);
			
			if (!stack_push(ctx, result))
				return false;
			break;
			
		case OP_MATCH:
		case OP_NMATCH:
			if (!stack_pop(ctx, &right) || !stack_pop(ctx, &left))
				return false;
			
			result.type = FILTER_VALUE_BOOL;
			if (left.type == FILTER_VALUE_STRING && right.type == FILTER_VALUE_STRING) {
				bool matches = match_regex(ctx, left.data.string_val, right.data.string_val);
				result.data.bool_val = (instr->opcode == OP_MATCH) ? matches : !matches;
			} else {
				result.data.bool_val = false;
			}
			
			value_free(&left);
			value_free(&right);
			
			if (!stack_push(ctx, result))
				return false;
			break;
			
		case OP_IN: {
			/* Pop all list items first (they're on top of stack) */
			struct filter_value *list_items = malloc(instr->operand.in.count * sizeof(struct filter_value));
			if (!list_items)
				return false;
			
			for (uint32_t i = 0; i < instr->operand.in.count; i++) {
				if (!stack_pop(ctx, &list_items[instr->operand.in.count - 1 - i])) {
					for (uint32_t j = 0; j < i; j++)
						value_free(&list_items[instr->operand.in.count - 1 - j]);
					free(list_items);
					return false;
				}
			}
			
			/* Now pop the value to check */
			if (!stack_pop(ctx, &left)) {
				for (uint32_t i = 0; i < instr->operand.in.count; i++)
					value_free(&list_items[i]);
				free(list_items);
				return false;
			}
			
			/* Check for match */
			bool found = false;
			for (uint32_t i = 0; i < instr->operand.in.count; i++) {
				if (compare_values(&left, &list_items[i], OP_EQ)) {
					found = true;
					break;
				}
			}
			
			/* Cleanup */
			value_free(&left);
			for (uint32_t i = 0; i < instr->operand.in.count; i++)
				value_free(&list_items[i]);
			free(list_items);
			
			result.type = FILTER_VALUE_BOOL;
			result.data.bool_val = found;
			if (!stack_push(ctx, result))
				return false;
			break;
		}
			
		case OP_AND:
			if (!stack_pop(ctx, &right) || !stack_pop(ctx, &left))
				return false;
			
			result.type = FILTER_VALUE_BOOL;
			result.data.bool_val = (left.type == FILTER_VALUE_BOOL && left.data.bool_val) &&
			                       (right.type == FILTER_VALUE_BOOL && right.data.bool_val);
			
			if (!stack_push(ctx, result))
				return false;
			break;
			
		case OP_OR:
			if (!stack_pop(ctx, &right) || !stack_pop(ctx, &left))
				return false;
			
			result.type = FILTER_VALUE_BOOL;
			result.data.bool_val = (left.type == FILTER_VALUE_BOOL && left.data.bool_val) ||
			                       (right.type == FILTER_VALUE_BOOL && right.data.bool_val);
			
			if (!stack_push(ctx, result))
				return false;
			break;
			
		case OP_NOT:
			if (!stack_pop(ctx, &left))
				return false;
			
			result.type = FILTER_VALUE_BOOL;
			result.data.bool_val = !(left.type == FILTER_VALUE_BOOL && left.data.bool_val);
			
			if (!stack_push(ctx, result))
				return false;
			break;
			
		case OP_JUMP:
			pc += instr->operand.jump.offset;
			continue;
			
		case OP_JUMP_IF_FALSE:
			if (!stack_peek(ctx, &val))
				return false;
			if (val.type == FILTER_VALUE_BOOL && !val.data.bool_val)
				pc += instr->operand.jump.offset;
			break;
			
		case OP_JUMP_IF_TRUE:
			if (!stack_peek(ctx, &val))
				return false;
			if (val.type == FILTER_VALUE_BOOL && val.data.bool_val)
				pc += instr->operand.jump.offset;
			break;
			
		case OP_RETURN:
			if (!stack_pop(ctx, &result))
				return false;
			
			bool ret = (result.type == FILTER_VALUE_BOOL && result.data.bool_val);
			value_free(&result);
			return ret;
			
		case OP_NOP:
			/* No operation */
			break;
		}
		
		pc++;
	}
	
	/* No explicit return, check top of stack */
	if (stack_pop(ctx, &result)) {
		bool ret = (result.type == FILTER_VALUE_BOOL && result.data.bool_val);
		value_free(&result);
		return ret;
	}
	
	return false;
}

/* Public API */
struct filter_eval_context *filter_eval_context_create(void)
{
	struct filter_eval_context *ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;
	
	ctx->stack_capacity = INITIAL_STACK_CAPACITY;
	ctx->stack = malloc(ctx->stack_capacity * sizeof(struct filter_value));
	if (!ctx->stack) {
		free(ctx);
		return NULL;
	}
	
	ctx->regex_cache_capacity = INITIAL_REGEX_CACHE_CAPACITY;
	ctx->regex_cache = calloc(ctx->regex_cache_capacity, sizeof(*ctx->regex_cache));
	if (!ctx->regex_cache) {
		free(ctx->stack);
		free(ctx);
		return NULL;
	}
	
	/* Allocate opcode statistics arrays (assuming max 32 opcodes) */
	ctx->opcode_counts = calloc(32, sizeof(uint64_t));
	ctx->opcode_times = calloc(32, sizeof(uint64_t));
	if (!ctx->opcode_counts || !ctx->opcode_times) {
		free(ctx->opcode_times);
		free(ctx->opcode_counts);
		free(ctx->regex_cache);
		free(ctx->stack);
		free(ctx);
		return NULL;
	}
	
	ctx->min_time_ns = UINT64_MAX;
	
	return ctx;
}

void filter_eval_context_destroy(struct filter_eval_context *ctx)
{
	if (!ctx)
		return;
	
	/* Free stack values */
	for (size_t i = 0; i < ctx->stack_size; i++)
		value_free(&ctx->stack[i]);
	free(ctx->stack);
	
	/* Free regex cache */
	for (size_t i = 0; i < ctx->regex_cache_size; i++) {
		if (ctx->regex_cache[i].valid) {
			regfree(&ctx->regex_cache[i].compiled);
			free(ctx->regex_cache[i].pattern);
		}
	}
	free(ctx->regex_cache);
	
	free(ctx->opcode_counts);
	free(ctx->opcode_times);
	free(ctx);
}

bool filter_eval(struct filter_bytecode *bytecode,
                 struct nlmon_event *event,
                 struct filter_eval_context *ctx)
{
	struct filter_eval_context *local_ctx = ctx;
	bool result;
	
	if (!bytecode || !event)
		return false;
	
	/* Create temporary context if none provided */
	if (!local_ctx) {
		local_ctx = filter_eval_context_create();
		if (!local_ctx)
			return false;
	}
	
	/* Reset stack */
	for (size_t i = 0; i < local_ctx->stack_size; i++)
		value_free(&local_ctx->stack[i]);
	local_ctx->stack_size = 0;
	
	/* Evaluate */
	result = eval_bytecode(bytecode, event, local_ctx);
	
	/* Cleanup temporary context */
	if (!ctx)
		filter_eval_context_destroy(local_ctx);
	
	return result;
}

bool filter_eval_with_profiling(struct filter_bytecode *bytecode,
                                 struct nlmon_event *event,
                                 struct filter_eval_context *ctx,
                                 uint64_t *elapsed_ns)
{
	struct timespec start, end;
	bool result;
	uint64_t elapsed;
	
	if (!bytecode || !event || !ctx)
		return false;
	
	clock_gettime(CLOCK_MONOTONIC, &start);
	result = filter_eval(bytecode, event, ctx);
	clock_gettime(CLOCK_MONOTONIC, &end);
	
	elapsed = (end.tv_sec - start.tv_sec) * 1000000000ULL +
	          (end.tv_nsec - start.tv_nsec);
	
	if (elapsed_ns)
		*elapsed_ns = elapsed;
	
	/* Update statistics */
	ctx->eval_count++;
	ctx->total_time_ns += elapsed;
	if (elapsed < ctx->min_time_ns)
		ctx->min_time_ns = elapsed;
	if (elapsed > ctx->max_time_ns)
		ctx->max_time_ns = elapsed;
	
	return result;
}

void filter_eval_stats(struct filter_eval_context *ctx,
                       uint64_t *eval_count,
                       uint64_t *avg_time_ns,
                       uint64_t *min_time_ns,
                       uint64_t *max_time_ns)
{
	if (!ctx)
		return;
	
	if (eval_count)
		*eval_count = ctx->eval_count;
	if (avg_time_ns)
		*avg_time_ns = ctx->eval_count > 0 ? ctx->total_time_ns / ctx->eval_count : 0;
	if (min_time_ns)
		*min_time_ns = ctx->min_time_ns;
	if (max_time_ns)
		*max_time_ns = ctx->max_time_ns;
}

void filter_eval_reset_stats(struct filter_eval_context *ctx)
{
	if (!ctx)
		return;
	
	ctx->eval_count = 0;
	ctx->total_time_ns = 0;
	ctx->min_time_ns = UINT64_MAX;
	ctx->max_time_ns = 0;
	
	memset(ctx->opcode_counts, 0, 32 * sizeof(uint64_t));
	memset(ctx->opcode_times, 0, 32 * sizeof(uint64_t));
}

void filter_eval_opcode_stats(struct filter_eval_context *ctx,
                              int opcode,
                              uint64_t *count,
                              uint64_t *total_time_ns)
{
	if (!ctx || opcode < 0 || opcode >= 32)
		return;
	
	if (count)
		*count = ctx->opcode_counts[opcode];
	if (total_time_ns)
		*total_time_ns = ctx->opcode_times[opcode];
}
