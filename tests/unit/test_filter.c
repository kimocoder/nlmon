/* test_filter.c - Unit tests for filter system */

#include "test_framework.h"
#include "filter_parser.h"
#include "filter_compiler.h"
#include "filter_eval.h"
#include "event_processor.h"
#include <string.h>

TEST(filter_parser_simple)
{
	struct filter_ast *ast;
	
	/* Test simple equality */
	ast = filter_parse("interface == \"eth0\"");
	ASSERT_NOT_NULL(ast);
	filter_ast_free(ast);
	
	/* Test pattern match */
	ast = filter_parse("interface =~ \"eth.*\"");
	ASSERT_NOT_NULL(ast);
	filter_ast_free(ast);
	
	/* Test message type */
	ast = filter_parse("message_type == 16");
	ASSERT_NOT_NULL(ast);
	filter_ast_free(ast);
}

TEST(filter_parser_logical_ops)
{
	struct filter_ast *ast;
	
	/* Test AND */
	ast = filter_parse("interface == \"eth0\" AND message_type == 16");
	ASSERT_NOT_NULL(ast);
	filter_ast_free(ast);
	
	/* Test OR */
	ast = filter_parse("interface == \"eth0\" OR interface == \"eth1\"");
	ASSERT_NOT_NULL(ast);
	filter_ast_free(ast);
	
	/* Test NOT */
	ast = filter_parse("NOT (interface == \"lo\")");
	ASSERT_NOT_NULL(ast);
	filter_ast_free(ast);
}

TEST(filter_parser_complex)
{
	struct filter_ast *ast;
	
	/* Test complex expression */
	ast = filter_parse("(interface =~ \"veth.*\" AND message_type IN [16, 17]) OR priority == \"high\"");
	ASSERT_NOT_NULL(ast);
	filter_ast_free(ast);
}

TEST(filter_parser_invalid)
{
	struct filter_ast *ast;
	
	/* Test invalid syntax */
	ast = filter_parse("interface ==");
	ASSERT_NULL(ast);
	
	ast = filter_parse("AND interface == \"eth0\"");
	ASSERT_NULL(ast);
	
	ast = filter_parse("interface == \"eth0\" AND");
	ASSERT_NULL(ast);
}

TEST(filter_compiler_basic)
{
	struct filter_ast *ast;
	struct filter_bytecode *bc;
	
	ast = filter_parse("interface == \"eth0\"");
	ASSERT_NOT_NULL(ast);
	
	bc = filter_compile(ast);
	ASSERT_NOT_NULL(bc);
	ASSERT_TRUE(bc->instruction_count > 0);
	
	filter_bytecode_free(bc);
	filter_ast_free(ast);
}

TEST(filter_eval_equality)
{
	struct filter_ast *ast;
	struct filter_bytecode *bc;
	struct nlmon_event event;
	
	ast = filter_parse("interface == \"eth0\"");
	ASSERT_NOT_NULL(ast);
	
	bc = filter_compile(ast);
	ASSERT_NOT_NULL(bc);
	
	/* Test matching event */
	memset(&event, 0, sizeof(event));
	strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
	ASSERT_TRUE(filter_eval(bc, &event));
	
	/* Test non-matching event */
	memset(&event, 0, sizeof(event));
	strncpy(event.interface, "eth1", sizeof(event.interface) - 1);
	ASSERT_FALSE(filter_eval(bc, &event));
	
	filter_bytecode_free(bc);
	filter_ast_free(ast);
}

TEST(filter_eval_pattern)
{
	struct filter_ast *ast;
	struct filter_bytecode *bc;
	struct nlmon_event event;
	
	ast = filter_parse("interface =~ \"eth.*\"");
	ASSERT_NOT_NULL(ast);
	
	bc = filter_compile(ast);
	ASSERT_NOT_NULL(bc);
	
	/* Test matching patterns */
	memset(&event, 0, sizeof(event));
	strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
	ASSERT_TRUE(filter_eval(bc, &event));
	
	strncpy(event.interface, "eth1", sizeof(event.interface) - 1);
	ASSERT_TRUE(filter_eval(bc, &event));
	
	strncpy(event.interface, "ethX", sizeof(event.interface) - 1);
	ASSERT_TRUE(filter_eval(bc, &event));
	
	/* Test non-matching pattern */
	strncpy(event.interface, "wlan0", sizeof(event.interface) - 1);
	ASSERT_FALSE(filter_eval(bc, &event));
	
	filter_bytecode_free(bc);
	filter_ast_free(ast);
}

TEST(filter_eval_logical_and)
{
	struct filter_ast *ast;
	struct filter_bytecode *bc;
	struct nlmon_event event;
	
	ast = filter_parse("interface == \"eth0\" AND message_type == 16");
	ASSERT_NOT_NULL(ast);
	
	bc = filter_compile(ast);
	ASSERT_NOT_NULL(bc);
	
	/* Both conditions true */
	memset(&event, 0, sizeof(event));
	strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
	event.message_type = 16;
	ASSERT_TRUE(filter_eval(bc, &event));
	
	/* First true, second false */
	event.message_type = 17;
	ASSERT_FALSE(filter_eval(bc, &event));
	
	/* First false, second true */
	strncpy(event.interface, "eth1", sizeof(event.interface) - 1);
	event.message_type = 16;
	ASSERT_FALSE(filter_eval(bc, &event));
	
	filter_bytecode_free(bc);
	filter_ast_free(ast);
}

TEST(filter_eval_logical_or)
{
	struct filter_ast *ast;
	struct filter_bytecode *bc;
	struct nlmon_event event;
	
	ast = filter_parse("interface == \"eth0\" OR interface == \"eth1\"");
	ASSERT_NOT_NULL(ast);
	
	bc = filter_compile(ast);
	ASSERT_NOT_NULL(bc);
	
	/* First condition true */
	memset(&event, 0, sizeof(event));
	strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
	ASSERT_TRUE(filter_eval(bc, &event));
	
	/* Second condition true */
	strncpy(event.interface, "eth1", sizeof(event.interface) - 1);
	ASSERT_TRUE(filter_eval(bc, &event));
	
	/* Both false */
	strncpy(event.interface, "wlan0", sizeof(event.interface) - 1);
	ASSERT_FALSE(filter_eval(bc, &event));
	
	filter_bytecode_free(bc);
	filter_ast_free(ast);
}

TEST(filter_eval_logical_not)
{
	struct filter_ast *ast;
	struct filter_bytecode *bc;
	struct nlmon_event event;
	
	ast = filter_parse("NOT (interface == \"lo\")");
	ASSERT_NOT_NULL(ast);
	
	bc = filter_compile(ast);
	ASSERT_NOT_NULL(bc);
	
	/* Should match everything except "lo" */
	memset(&event, 0, sizeof(event));
	strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
	ASSERT_TRUE(filter_eval(bc, &event));
	
	strncpy(event.interface, "lo", sizeof(event.interface) - 1);
	ASSERT_FALSE(filter_eval(bc, &event));
	
	filter_bytecode_free(bc);
	filter_ast_free(ast);
}

TEST_SUITE_BEGIN("Filter System")
	RUN_TEST(filter_parser_simple);
	RUN_TEST(filter_parser_logical_ops);
	RUN_TEST(filter_parser_complex);
	RUN_TEST(filter_parser_invalid);
	RUN_TEST(filter_compiler_basic);
	RUN_TEST(filter_eval_equality);
	RUN_TEST(filter_eval_pattern);
	RUN_TEST(filter_eval_logical_and);
	RUN_TEST(filter_eval_logical_or);
	RUN_TEST(filter_eval_logical_not);
TEST_SUITE_END()
