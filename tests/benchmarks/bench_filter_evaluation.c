/* bench_filter_evaluation.c - Filter evaluation benchmark */

#include "benchmark_framework.h"
#include "filter_parser.h"
#include "filter_compiler.h"
#include "filter_eval.h"
#include "event_processor.h"
#include <string.h>

static struct filter_bytecode *g_simple_filter = NULL;
static struct filter_bytecode *g_complex_filter = NULL;
static struct nlmon_event g_test_event;

BENCHMARK(filter_parse, 10000)
{
	struct filter_ast *ast = filter_parse("interface == \"eth0\"");
	if (ast) {
		filter_ast_free(ast);
	}
}

BENCHMARK(filter_compile, 10000)
{
	struct filter_ast *ast = filter_parse("interface == \"eth0\"");
	if (ast) {
		struct filter_bytecode *bc = filter_compile(ast);
		if (bc) {
			filter_bytecode_free(bc);
		}
		filter_ast_free(ast);
	}
}

BENCHMARK(filter_eval_simple, 1000000)
{
	if (g_simple_filter) {
		filter_eval(g_simple_filter, &g_test_event);
	}
}

BENCHMARK(filter_eval_pattern, 1000000)
{
	static struct filter_bytecode *pattern_filter = NULL;
	
	if (!pattern_filter) {
		struct filter_ast *ast = filter_parse("interface =~ \"eth.*\"");
		if (ast) {
			pattern_filter = filter_compile(ast);
			filter_ast_free(ast);
		}
	}
	
	if (pattern_filter) {
		filter_eval(pattern_filter, &g_test_event);
	}
}

BENCHMARK(filter_eval_complex, 100000)
{
	if (g_complex_filter) {
		filter_eval(g_complex_filter, &g_test_event);
	}
}

THROUGHPUT_BENCHMARK(filter_evaluation_throughput, 5.0)
{
	if (g_simple_filter) {
		filter_eval(g_simple_filter, &g_test_event);
		return 1;
	}
	return 0;
}

MEMORY_BENCHMARK(filter_memory)
{
	struct filter_ast *ast;
	struct filter_bytecode *bc;
	size_t memory = 0;
	
	/* Parse and compile a complex filter */
	ast = filter_parse("(interface =~ \"veth.*\" AND message_type IN [16, 17]) OR priority == \"high\"");
	if (ast) {
		bc = filter_compile(ast);
		if (bc) {
			memory = sizeof(struct filter_bytecode);
			memory += bc->instruction_count * sizeof(uint32_t);
			filter_bytecode_free(bc);
		}
		filter_ast_free(ast);
	}
	
	return memory;
}

BENCHMARK_SUITE_BEGIN("Filter Evaluation")
	/* Setup test event */
	memset(&g_test_event, 0, sizeof(g_test_event));
	g_test_event.message_type = 16;
	strncpy(g_test_event.interface, "eth0", sizeof(g_test_event.interface) - 1);
	
	/* Compile filters for benchmarks */
	struct filter_ast *ast;
	
	ast = filter_parse("interface == \"eth0\"");
	if (ast) {
		g_simple_filter = filter_compile(ast);
		filter_ast_free(ast);
	}
	
	ast = filter_parse("(interface == \"eth0\" AND message_type == 16) OR (interface =~ \"veth.*\" AND message_type IN [16, 17])");
	if (ast) {
		g_complex_filter = filter_compile(ast);
		filter_ast_free(ast);
	}
	
	/* Run benchmarks */
	RUN_BENCHMARK(filter_parse);
	RUN_BENCHMARK(filter_compile);
	RUN_BENCHMARK(filter_eval_simple);
	RUN_BENCHMARK(filter_eval_pattern);
	RUN_BENCHMARK(filter_eval_complex);
	RUN_THROUGHPUT_BENCHMARK(filter_evaluation_throughput);
	RUN_MEMORY_BENCHMARK(filter_memory);
	
	/* Cleanup */
	if (g_simple_filter) {
		filter_bytecode_free(g_simple_filter);
	}
	if (g_complex_filter) {
		filter_bytecode_free(g_complex_filter);
	}
BENCHMARK_SUITE_END()
