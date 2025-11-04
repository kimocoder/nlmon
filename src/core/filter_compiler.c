/* filter_compiler.c - Filter bytecode compiler implementation
 *
 * Compiles filter AST to stack-based bytecode with optimization passes.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "filter_compiler.h"
#include "filter_parser.h"

/* Helper functions for bytecode generation */
static struct filter_bytecode *bytecode_create(void)
{
	struct filter_bytecode *bc = calloc(1, sizeof(*bc));
	if (!bc)
		return NULL;
	
	bc->instruction_capacity = 64;
	bc->instructions = malloc(bc->instruction_capacity * sizeof(struct filter_instruction));
	if (!bc->instructions) {
		free(bc);
		return NULL;
	}
	
	bc->string_capacity = 16;
	bc->strings = malloc(bc->string_capacity * sizeof(char *));
	if (!bc->strings) {
		free(bc->instructions);
		free(bc);
		return NULL;
	}
	
	return bc;
}

static bool bytecode_emit(struct filter_bytecode *bc, struct filter_instruction instr)
{
	if (bc->instruction_count >= bc->instruction_capacity) {
		size_t new_capacity = bc->instruction_capacity * 2;
		struct filter_instruction *new_instructions = realloc(bc->instructions,
		                                                      new_capacity * sizeof(struct filter_instruction));
		if (!new_instructions)
			return false;
		bc->instructions = new_instructions;
		bc->instruction_capacity = new_capacity;
	}
	
	bc->instructions[bc->instruction_count++] = instr;
	return true;
}

static uint32_t bytecode_add_string(struct filter_bytecode *bc, const char *str)
{
	/* Check if string already exists */
	for (size_t i = 0; i < bc->string_count; i++) {
		if (strcmp(bc->strings[i], str) == 0)
			return i;
	}
	
	/* Add new string */
	if (bc->string_count >= bc->string_capacity) {
		size_t new_capacity = bc->string_capacity * 2;
		char **new_strings = realloc(bc->strings, new_capacity * sizeof(char *));
		if (!new_strings)
			return 0;
		bc->strings = new_strings;
		bc->string_capacity = new_capacity;
	}
	
	bc->strings[bc->string_count] = strdup(str);
	if (!bc->strings[bc->string_count])
		return 0;
	
	return bc->string_count++;
}

static bool compile_node_recursive(struct filter_node *node, struct filter_bytecode *bc);

static bool compile_binary_op(struct filter_node *node, struct filter_bytecode *bc,
                               enum filter_opcode opcode)
{
	/* Compile left operand */
	if (!compile_node_recursive(node->data.binary.left, bc))
		return false;
	
	/* Compile right operand */
	if (!compile_node_recursive(node->data.binary.right, bc))
		return false;
	
	/* Emit operation */
	struct filter_instruction instr = { .opcode = opcode };
	return bytecode_emit(bc, instr);
}

static bool compile_logical_and(struct filter_node *node, struct filter_bytecode *bc)
{
	/* Compile left operand */
	if (!compile_node_recursive(node->data.binary.left, bc))
		return false;
	
	/* Jump to end if false (short-circuit) */
	size_t jump_pos = bc->instruction_count;
	struct filter_instruction jump_instr = { .opcode = OP_JUMP_IF_FALSE };
	if (!bytecode_emit(bc, jump_instr))
		return false;
	
	/* Pop the left result */
	struct filter_instruction pop_instr = { .opcode = OP_POP };
	if (!bytecode_emit(bc, pop_instr))
		return false;
	
	/* Compile right operand */
	if (!compile_node_recursive(node->data.binary.right, bc))
		return false;
	
	/* Patch jump offset */
	bc->instructions[jump_pos].operand.jump.offset = bc->instruction_count - jump_pos - 1;
	
	return true;
}

static bool compile_logical_or(struct filter_node *node, struct filter_bytecode *bc)
{
	/* Compile left operand */
	if (!compile_node_recursive(node->data.binary.left, bc))
		return false;
	
	/* Jump to end if true (short-circuit) */
	size_t jump_pos = bc->instruction_count;
	struct filter_instruction jump_instr = { .opcode = OP_JUMP_IF_TRUE };
	if (!bytecode_emit(bc, jump_instr))
		return false;
	
	/* Pop the left result */
	struct filter_instruction pop_instr = { .opcode = OP_POP };
	if (!bytecode_emit(bc, pop_instr))
		return false;
	
	/* Compile right operand */
	if (!compile_node_recursive(node->data.binary.right, bc))
		return false;
	
	/* Patch jump offset */
	bc->instructions[jump_pos].operand.jump.offset = bc->instruction_count - jump_pos - 1;
	
	return true;
}

static bool compile_in_op(struct filter_node *node, struct filter_bytecode *bc)
{
	/* Compile left operand (value to check) */
	if (!compile_node_recursive(node->data.binary.left, bc))
		return false;
	
	/* Right operand should be a list */
	struct filter_node *list = node->data.binary.right;
	if (list->type != FILTER_NODE_LIST)
		return false;
	
	/* Compile each list item */
	for (size_t i = 0; i < list->data.list.count; i++) {
		if (!compile_node_recursive(list->data.list.items[i], bc))
			return false;
	}
	
	/* Emit IN operation with count */
	struct filter_instruction instr = {
		.opcode = OP_IN,
		.operand.in.count = list->data.list.count
	};
	return bytecode_emit(bc, instr);
}

static bool compile_node_recursive(struct filter_node *node, struct filter_bytecode *bc)
{
	struct filter_instruction instr;
	
	if (!node)
		return false;
	
	switch (node->type) {
	case FILTER_NODE_FIELD:
		instr.opcode = OP_PUSH_FIELD;
		instr.operand.field.field_type = node->data.field.field;
		return bytecode_emit(bc, instr);
		
	case FILTER_NODE_STRING:
		instr.opcode = OP_PUSH_STRING;
		instr.operand.string.string_index = bytecode_add_string(bc, node->data.string.value);
		return bytecode_emit(bc, instr);
		
	case FILTER_NODE_NUMBER:
		instr.opcode = OP_PUSH_NUMBER;
		instr.operand.number.value = node->data.number.value;
		return bytecode_emit(bc, instr);
		
	case FILTER_NODE_EQ:
		return compile_binary_op(node, bc, OP_EQ);
		
	case FILTER_NODE_NE:
		return compile_binary_op(node, bc, OP_NE);
		
	case FILTER_NODE_LT:
		return compile_binary_op(node, bc, OP_LT);
		
	case FILTER_NODE_GT:
		return compile_binary_op(node, bc, OP_GT);
		
	case FILTER_NODE_LE:
		return compile_binary_op(node, bc, OP_LE);
		
	case FILTER_NODE_GE:
		return compile_binary_op(node, bc, OP_GE);
		
	case FILTER_NODE_MATCH:
		return compile_binary_op(node, bc, OP_MATCH);
		
	case FILTER_NODE_NMATCH:
		return compile_binary_op(node, bc, OP_NMATCH);
		
	case FILTER_NODE_IN:
		return compile_in_op(node, bc);
		
	case FILTER_NODE_AND:
		return compile_logical_and(node, bc);
		
	case FILTER_NODE_OR:
		return compile_logical_or(node, bc);
		
	case FILTER_NODE_NOT:
		/* Compile operand */
		if (!compile_node_recursive(node->data.unary.operand, bc))
			return false;
		/* Emit NOT operation */
		instr.opcode = OP_NOT;
		return bytecode_emit(bc, instr);
		
	case FILTER_NODE_LIST:
		/* Lists are handled by IN operator */
		return false;
	}
	
	return false;
}

/* Optimization passes */
static size_t optimize_peephole(struct filter_bytecode *bc)
{
	size_t optimizations = 0;
	
	for (size_t i = 0; i < bc->instruction_count - 1; i++) {
		/* Remove POP followed by PUSH of same value (not implemented yet) */
		/* Remove redundant jumps */
		if (bc->instructions[i].opcode == OP_JUMP &&
		    bc->instructions[i].operand.jump.offset == 0) {
			bc->instructions[i].opcode = OP_NOP;
			optimizations++;
		}
	}
	
	return optimizations;
}

static size_t optimize_dead_code(struct filter_bytecode *bc)
{
	size_t optimizations = 0;
	
	/* Remove instructions after unconditional jumps or returns */
	for (size_t i = 0; i < bc->instruction_count - 1; i++) {
		if (bc->instructions[i].opcode == OP_RETURN ||
		    bc->instructions[i].opcode == OP_JUMP) {
			/* Mark next instruction as NOP if it's not a jump target */
			/* (Simplified - full implementation would track jump targets) */
		}
	}
	
	return optimizations;
}

static size_t optimize_constant_folding(struct filter_bytecode *bc)
{
	size_t optimizations = 0;
	
	/* Look for patterns like PUSH_NUMBER, PUSH_NUMBER, OP_EQ */
	/* and replace with PUSH_NUMBER (result) */
	/* This is a simplified version - full implementation would be more complex */
	
	return optimizations;
}

/* Public API */
struct filter_bytecode *filter_compile(struct filter_expr *expr)
{
	struct filter_bytecode *bc;
	struct filter_instruction return_instr;
	
	if (!expr || !expr->valid || !expr->ast)
		return NULL;
	
	bc = bytecode_create();
	if (!bc)
		return NULL;
	
	/* Compile AST */
	if (!compile_node_recursive(expr->ast, bc)) {
		filter_bytecode_free(bc);
		return NULL;
	}
	
	/* Emit return instruction */
	return_instr.opcode = OP_RETURN;
	if (!bytecode_emit(bc, return_instr)) {
		filter_bytecode_free(bc);
		return NULL;
	}
	
	/* Store original instruction count before optimization */
	bc->original_instruction_count = bc->instruction_count;
	
	return bc;
}

bool filter_compile_node(struct filter_node *node, struct filter_bytecode *bytecode)
{
	if (!node || !bytecode)
		return false;
	
	return compile_node_recursive(node, bytecode);
}

void filter_bytecode_free(struct filter_bytecode *bytecode)
{
	if (!bytecode)
		return;
	
	free(bytecode->instructions);
	
	for (size_t i = 0; i < bytecode->string_count; i++)
		free(bytecode->strings[i]);
	free(bytecode->strings);
	
	free(bytecode);
}

size_t filter_bytecode_optimize(struct filter_bytecode *bytecode)
{
	size_t total_optimizations = 0;
	
	if (!bytecode)
		return 0;
	
	/* Apply optimization passes */
	total_optimizations += optimize_peephole(bytecode);
	total_optimizations += optimize_dead_code(bytecode);
	total_optimizations += optimize_constant_folding(bytecode);
	
	bytecode->optimizations_applied = total_optimizations;
	
	return total_optimizations;
}

size_t filter_bytecode_disassemble(struct filter_bytecode *bytecode,
                                   char *buf, size_t size)
{
	size_t offset = 0;
	
	if (!bytecode || !buf || size == 0)
		return 0;
	
	offset += snprintf(buf + offset, size - offset,
	                   "Bytecode (%zu instructions, %zu strings):\n",
	                   bytecode->instruction_count, bytecode->string_count);
	
	for (size_t i = 0; i < bytecode->instruction_count && offset < size; i++) {
		struct filter_instruction *instr = &bytecode->instructions[i];
		
		offset += snprintf(buf + offset, size - offset, "%04zu: ", i);
		
		switch (instr->opcode) {
		case OP_PUSH_FIELD:
			offset += snprintf(buf + offset, size - offset,
			                   "PUSH_FIELD %u\n", instr->operand.field.field_type);
			break;
		case OP_PUSH_STRING:
			offset += snprintf(buf + offset, size - offset,
			                   "PUSH_STRING \"%s\"\n",
			                   bytecode->strings[instr->operand.string.string_index]);
			break;
		case OP_PUSH_NUMBER:
			offset += snprintf(buf + offset, size - offset,
			                   "PUSH_NUMBER %ld\n", instr->operand.number.value);
			break;
		case OP_POP:
			offset += snprintf(buf + offset, size - offset, "POP\n");
			break;
		case OP_EQ:
			offset += snprintf(buf + offset, size - offset, "EQ\n");
			break;
		case OP_NE:
			offset += snprintf(buf + offset, size - offset, "NE\n");
			break;
		case OP_LT:
			offset += snprintf(buf + offset, size - offset, "LT\n");
			break;
		case OP_GT:
			offset += snprintf(buf + offset, size - offset, "GT\n");
			break;
		case OP_LE:
			offset += snprintf(buf + offset, size - offset, "LE\n");
			break;
		case OP_GE:
			offset += snprintf(buf + offset, size - offset, "GE\n");
			break;
		case OP_MATCH:
			offset += snprintf(buf + offset, size - offset, "MATCH\n");
			break;
		case OP_NMATCH:
			offset += snprintf(buf + offset, size - offset, "NMATCH\n");
			break;
		case OP_IN:
			offset += snprintf(buf + offset, size - offset,
			                   "IN %u\n", instr->operand.in.count);
			break;
		case OP_AND:
			offset += snprintf(buf + offset, size - offset, "AND\n");
			break;
		case OP_OR:
			offset += snprintf(buf + offset, size - offset, "OR\n");
			break;
		case OP_NOT:
			offset += snprintf(buf + offset, size - offset, "NOT\n");
			break;
		case OP_JUMP:
			offset += snprintf(buf + offset, size - offset,
			                   "JUMP %d\n", instr->operand.jump.offset);
			break;
		case OP_JUMP_IF_FALSE:
			offset += snprintf(buf + offset, size - offset,
			                   "JUMP_IF_FALSE %d\n", instr->operand.jump.offset);
			break;
		case OP_JUMP_IF_TRUE:
			offset += snprintf(buf + offset, size - offset,
			                   "JUMP_IF_TRUE %d\n", instr->operand.jump.offset);
			break;
		case OP_RETURN:
			offset += snprintf(buf + offset, size - offset, "RETURN\n");
			break;
		case OP_NOP:
			offset += snprintf(buf + offset, size - offset, "NOP\n");
			break;
		}
	}
	
	return offset;
}

void filter_bytecode_stats(struct filter_bytecode *bytecode,
                           size_t *instruction_count,
                           size_t *string_count,
                           size_t *optimizations)
{
	if (!bytecode)
		return;
	
	if (instruction_count)
		*instruction_count = bytecode->instruction_count;
	if (string_count)
		*string_count = bytecode->string_count;
	if (optimizations)
		*optimizations = bytecode->optimizations_applied;
}
