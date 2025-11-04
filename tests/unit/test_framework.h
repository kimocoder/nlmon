/* test_framework.h - Minimal unit testing framework for nlmon
 *
 * Provides simple macros for unit testing without external dependencies.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Test statistics */
struct test_stats {
	int total;
	int passed;
	int failed;
	int skipped;
};

static struct test_stats g_test_stats = {0, 0, 0, 0};
static const char *g_current_test = NULL;
static bool g_test_failed = false;

/* Color codes for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_BOLD    "\033[1m"

/* Test macros */
#define TEST(name) \
	static void test_##name(void); \
	static void run_test_##name(void) { \
		g_current_test = #name; \
		g_test_failed = false; \
		g_test_stats.total++; \
		printf(COLOR_BLUE "[ RUN      ]" COLOR_RESET " %s\n", #name); \
		test_##name(); \
		if (!g_test_failed) { \
			g_test_stats.passed++; \
			printf(COLOR_GREEN "[       OK ]" COLOR_RESET " %s\n", #name); \
		} else { \
			g_test_stats.failed++; \
			printf(COLOR_RED "[  FAILED  ]" COLOR_RESET " %s\n", #name); \
		} \
	} \
	static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

/* Assertion macros */
#define ASSERT_TRUE(expr) \
	do { \
		if (!(expr)) { \
			printf(COLOR_RED "  ASSERT_TRUE failed:" COLOR_RESET " %s:%d: %s\n", \
			       __FILE__, __LINE__, #expr); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

#define ASSERT_FALSE(expr) \
	do { \
		if (expr) { \
			printf(COLOR_RED "  ASSERT_FALSE failed:" COLOR_RESET " %s:%d: %s\n", \
			       __FILE__, __LINE__, #expr); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

#define ASSERT_EQ(a, b) \
	do { \
		if ((a) != (b)) { \
			printf(COLOR_RED "  ASSERT_EQ failed:" COLOR_RESET " %s:%d: %s != %s\n", \
			       __FILE__, __LINE__, #a, #b); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

#define ASSERT_NE(a, b) \
	do { \
		if ((a) == (b)) { \
			printf(COLOR_RED "  ASSERT_NE failed:" COLOR_RESET " %s:%d: %s == %s\n", \
			       __FILE__, __LINE__, #a, #b); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

#define ASSERT_NULL(ptr) \
	do { \
		if ((ptr) != NULL) { \
			printf(COLOR_RED "  ASSERT_NULL failed:" COLOR_RESET " %s:%d: %s is not NULL\n", \
			       __FILE__, __LINE__, #ptr); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

#define ASSERT_NOT_NULL(ptr) \
	do { \
		if ((ptr) == NULL) { \
			printf(COLOR_RED "  ASSERT_NOT_NULL failed:" COLOR_RESET " %s:%d: %s is NULL\n", \
			       __FILE__, __LINE__, #ptr); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

#define ASSERT_STR_EQ(a, b) \
	do { \
		if (strcmp((a), (b)) != 0) { \
			printf(COLOR_RED "  ASSERT_STR_EQ failed:" COLOR_RESET " %s:%d\n", \
			       __FILE__, __LINE__); \
			printf("    Expected: \"%s\"\n", (b)); \
			printf("    Got:      \"%s\"\n", (a)); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

#define ASSERT_FLOAT_EQ(a, b, epsilon) \
	do { \
		double _diff = (a) - (b); \
		if (_diff < 0) _diff = -_diff; \
		if (_diff > (epsilon)) { \
			printf(COLOR_RED "  ASSERT_FLOAT_EQ failed:" COLOR_RESET " %s:%d\n", \
			       __FILE__, __LINE__); \
			printf("    Expected: %f\n", (double)(b)); \
			printf("    Got:      %f\n", (double)(a)); \
			printf("    Diff:     %f (epsilon: %f)\n", _diff, (double)(epsilon)); \
			g_test_failed = true; \
			return; \
		} \
	} while (0)

/* Test suite management */
#define TEST_SUITE_BEGIN(name) \
	int main(void) { \
		printf("\n"); \
		printf(COLOR_BOLD "===============================================\n"); \
		printf("  Test Suite: %s\n", name); \
		printf("===============================================" COLOR_RESET "\n\n");

#define TEST_SUITE_END() \
		printf("\n"); \
		printf(COLOR_BOLD "===============================================\n"); \
		printf("  Test Results\n"); \
		printf("===============================================" COLOR_RESET "\n"); \
		printf("Total:   %d\n", g_test_stats.total); \
		printf(COLOR_GREEN "Passed:  %d" COLOR_RESET "\n", g_test_stats.passed); \
		if (g_test_stats.failed > 0) { \
			printf(COLOR_RED "Failed:  %d" COLOR_RESET "\n", g_test_stats.failed); \
		} else { \
			printf("Failed:  %d\n", g_test_stats.failed); \
		} \
		if (g_test_stats.skipped > 0) { \
			printf(COLOR_YELLOW "Skipped: %d" COLOR_RESET "\n", g_test_stats.skipped); \
		} \
		printf("\n"); \
		return (g_test_stats.failed > 0) ? 1 : 0; \
	}

/* Setup/teardown support */
typedef void (*test_setup_fn)(void);
typedef void (*test_teardown_fn)(void);

static test_setup_fn g_setup_fn = NULL;
static test_teardown_fn g_teardown_fn = NULL;

#define TEST_SETUP(fn) \
	static void setup_##fn(void); \
	__attribute__((constructor)) static void register_setup_##fn(void) { \
		g_setup_fn = setup_##fn; \
	} \
	static void setup_##fn(void)

#define TEST_TEARDOWN(fn) \
	static void teardown_##fn(void); \
	__attribute__((constructor)) static void register_teardown_##fn(void) { \
		g_teardown_fn = teardown_##fn; \
	} \
	static void teardown_##fn(void)

#endif /* TEST_FRAMEWORK_H */
