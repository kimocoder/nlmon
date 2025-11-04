/* benchmark_framework.h - Simple benchmarking framework */

#ifndef BENCHMARK_FRAMEWORK_H
#define BENCHMARK_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>

/* Benchmark statistics */
struct benchmark_stats {
	uint64_t iterations;
	double total_time_sec;
	double avg_time_ns;
	double min_time_ns;
	double max_time_ns;
	double ops_per_sec;
};

/* Get current time in nanoseconds */
static inline uint64_t benchmark_get_time_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Benchmark macros */
#define BENCHMARK(name, iterations) \
	static void benchmark_##name(struct benchmark_stats *stats); \
	static void run_benchmark_##name(void) { \
		struct benchmark_stats stats = {0}; \
		uint64_t start, end, duration; \
		uint64_t min_time = UINT64_MAX; \
		uint64_t max_time = 0; \
		uint64_t total_time = 0; \
		\
		printf("\n=== Benchmark: %s ===\n", #name); \
		printf("Iterations: %d\n", iterations); \
		\
		for (int i = 0; i < iterations; i++) { \
			start = benchmark_get_time_ns(); \
			benchmark_##name(&stats); \
			end = benchmark_get_time_ns(); \
			\
			duration = end - start; \
			total_time += duration; \
			\
			if (duration < min_time) min_time = duration; \
			if (duration > max_time) max_time = duration; \
		} \
		\
		stats.iterations = iterations; \
		stats.total_time_sec = (double)total_time / 1000000000.0; \
		stats.avg_time_ns = (double)total_time / iterations; \
		stats.min_time_ns = (double)min_time; \
		stats.max_time_ns = (double)max_time; \
		stats.ops_per_sec = 1000000000.0 / stats.avg_time_ns; \
		\
		printf("Total time:    %.3f sec\n", stats.total_time_sec); \
		printf("Average time:  %.2f ns\n", stats.avg_time_ns); \
		printf("Min time:      %.2f ns\n", stats.min_time_ns); \
		printf("Max time:      %.2f ns\n", stats.max_time_ns); \
		printf("Throughput:    %.0f ops/sec\n", stats.ops_per_sec); \
	} \
	static void benchmark_##name(struct benchmark_stats *stats __attribute__((unused)))

#define RUN_BENCHMARK(name) run_benchmark_##name()

/* Throughput benchmark - measures ops/sec over duration */
#define THROUGHPUT_BENCHMARK(name, duration_sec) \
	static uint64_t benchmark_throughput_##name(void); \
	static void run_throughput_benchmark_##name(void) { \
		uint64_t start, end, elapsed; \
		uint64_t operations = 0; \
		double duration = duration_sec; \
		\
		printf("\n=== Throughput Benchmark: %s ===\n", #name); \
		printf("Duration: %.1f sec\n", duration); \
		\
		start = benchmark_get_time_ns(); \
		end = start + (uint64_t)(duration * 1000000000.0); \
		\
		while (benchmark_get_time_ns() < end) { \
			operations += benchmark_throughput_##name(); \
		} \
		\
		elapsed = benchmark_get_time_ns() - start; \
		double elapsed_sec = (double)elapsed / 1000000000.0; \
		double ops_per_sec = (double)operations / elapsed_sec; \
		\
		printf("Operations:    %lu\n", operations); \
		printf("Elapsed time:  %.3f sec\n", elapsed_sec); \
		printf("Throughput:    %.0f ops/sec\n", ops_per_sec); \
	} \
	static uint64_t benchmark_throughput_##name(void)

#define RUN_THROUGHPUT_BENCHMARK(name) run_throughput_benchmark_##name()

/* Memory benchmark - measures memory usage */
#define MEMORY_BENCHMARK(name) \
	static size_t benchmark_memory_##name(void); \
	static void run_memory_benchmark_##name(void) { \
		size_t memory_used; \
		\
		printf("\n=== Memory Benchmark: %s ===\n", #name); \
		\
		memory_used = benchmark_memory_##name(); \
		\
		printf("Memory used:   %zu bytes\n", memory_used); \
		printf("Memory used:   %.2f KB\n", (double)memory_used / 1024.0); \
		printf("Memory used:   %.2f MB\n", (double)memory_used / (1024.0 * 1024.0)); \
	} \
	static size_t benchmark_memory_##name(void)

#define RUN_MEMORY_BENCHMARK(name) run_memory_benchmark_##name()

/* Benchmark suite */
#define BENCHMARK_SUITE_BEGIN(name) \
	int main(void) { \
		printf("\n"); \
		printf("===============================================\n"); \
		printf("  Benchmark Suite: %s\n", name); \
		printf("===============================================\n");

#define BENCHMARK_SUITE_END() \
		printf("\n"); \
		printf("===============================================\n"); \
		printf("  Benchmarks Complete\n"); \
		printf("===============================================\n"); \
		printf("\n"); \
		return 0; \
	}

#endif /* BENCHMARK_FRAMEWORK_H */
