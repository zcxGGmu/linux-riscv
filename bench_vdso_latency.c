// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V vDSO Latency Micro-Benchmark
 *
 * This program measures the latency of vDSO clock_gettime() calls
 * with high precision using cycle counters.
 *
 * Compile: gcc -O2 -o bench_vdso_latency bench_vdso_latency.c -lrt
 * Run: ./bench_vdso_latency
 */

#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>

// 定义测试参数
#define SAMPLES 1000000
#define WARMUP_ITER 10000

// RISC-V cycle CSR 读取封装
static inline uint64_t read_cycles(void)
{
    uint64_t cycles;
    asm volatile("rdtime %0" : "=r"(cycles));
    return cycles;
}

// 计算百分位数
static int compare_uint64(const void *a, const void *b)
{
    uint64_t arg1 = *(const uint64_t *)a;
    uint64_t arg2 = *(const uint64_t *)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

static uint64_t percentile(uint64_t *data, size_t size, int p)
{
    size_t idx = (size * p) / 100;
    if (idx >= size) idx = size - 1;
    return data[idx];
}

// 延迟测试
struct latency_stats {
    uint64_t min;
    uint64_t max;
    uint64_t avg;
    uint64_t p50;
    uint64_t p90;
    uint64_t p99;
    uint64_t p99_9;
};

static void measure_latency(clockid_t clk_id, const char *clk_name,
                            struct latency_stats *stats)
{
    struct timespec ts;
    uint64_t *latencies = malloc(SAMPLES * sizeof(uint64_t));
    uint64_t start, end;
    uint64_t prev_ns, cur_ns;
    uint64_t sum = 0;
    int count = 0;

    if (!latencies) {
        perror("malloc");
        exit(1);
    }

    printf("Measuring %s...\n", clk_name);

    // Warmup
    for (int i = 0; i < WARMUP_ITER; i++) {
        clock_gettime(clk_id, &ts);
    }

    // 测量连续调用之间的间隔
    clock_gettime(clk_id, &ts);
    prev_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    for (int i = 0; i < SAMPLES; i++) {
        clock_gettime(clk_id, &ts);
        cur_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        latencies[i] = cur_ns - prev_ns;
        prev_ns = cur_ns;
        sum += latencies[i];
    }

    // 排序计算百分位数
    qsort(latencies, SAMPLES, sizeof(uint64_t), compare_uint64);

    stats->min = latencies[0];
    stats->max = latencies[SAMPLES - 1];
    stats->avg = sum / SAMPLES;
    stats->p50 = percentile(latencies, SAMPLES, 50);
    stats->p90 = percentile(latencies, SAMPLES, 90);
    stats->p99 = percentile(latencies, SAMPLES, 99);
    stats->p99_9 = percentile(latencies, SAMPLES, 99.9);

    free(latencies);
}

// 周期级延迟测量
static void measure_cycle_latency(clockid_t clk_id, const char *clk_name)
{
    struct timespec ts;
    uint64_t start, end, min_cycles = UINT64_MAX;
    uint64_t total_cycles = 0;

    printf("\nCycle-level latency for %s:\n", clk_name);

    // Warmup
    for (int i = 0; i < WARMUP_ITER; i++) {
        clock_gettime(clk_id, &ts);
    }

    // 测量时钟调用本身的周期数
    for (int i = 0; i < 100000; i++) {
        start = read_cycles();
        clock_gettime(clk_id, &ts);
        end = read_cycles();

        uint64_t cycles = end - start;
        if (cycles < min_cycles) {
            min_cycles = cycles;
        }
        total_cycles += cycles;
    }

    printf("  Min cycles: %lu\n", min_cycles);
    printf("  Avg cycles: %lu\n", total_cycles / 100000);

    // 估算频率
    if (min_cycles > 0) {
        double freq_ghz = 1.0 / (min_cycles * 1e-9); // 粗略估计
        printf("  Estimated call latency: ~%.2f ns (assuming 1 cycle = %.2f ns)\n",
               min_cycles * 0.2, 0.2); // 假设 5GHz
    }
}

// 打印统计信息
static void print_stats(const char *name, const struct latency_stats *stats)
{
    printf("\n=== %s ===\n", name);
    printf("  Min: %lu ns\n", stats->min);
    printf("  Max: %lu ns\n", stats->max);
    printf("  Avg: %lu ns\n", stats->avg);
    printf("  P50: %lu ns\n", stats->p50);
    printf("  P90: %lu ns\n", stats->p90);
    printf("  P99: %lu ns\n", stats->p99);
    printf("  P99.9: %lu ns\n", stats->p99_9);
}

// 吞吐量测试
static void measure_throughput(clockid_t clk_id, const char *clk_name)
{
    struct timespec ts;
    const int ITER = 10000000;
    uint64_t start_cycles, end_cycles;

    printf("\nThroughput test for %s (%d iterations)...\n", clk_name, ITER);

    // Warmup
    for (int i = 0; i < WARMUP_ITER; i++) {
        clock_gettime(clk_id, &ts);
    }

    start_cycles = read_cycles();
    for (int i = 0; i < ITER; i++) {
        clock_gettime(clk_id, &ts);
    }
    end_cycles = read_cycles();

    uint64_t total_cycles = end_cycles - start_cycles;
    double cycles_per_call = (double)total_cycles / ITER;
    double ns_per_call = cycles_per_call * 0.2; // 假设 5GHz
    double calls_per_sec = 1e9 / ns_per_call;

    printf("  Cycles per call: %.2f\n", cycles_per_call);
    printf("  Latency per call: %.2f ns\n", ns_per_call);
    printf("  Throughput: %.0f calls/sec\n", calls_per_sec);
}

int main(void)
{
    struct latency_stats stats;

    printf("RISC-V vDSO Performance Micro-Benchmark\n");
    printf("=========================================\n\n");

    printf("Samples: %d\n", SAMPLES);
    printf("Warmup iterations: %d\n", WARMUP_ITER);

    // 测试不同时钟源
    clockid_t clocks[] = {
        CLOCK_REALTIME,
        CLOCK_MONOTONIC,
        CLOCK_MONOTONIC_RAW,
        CLOCK_BOOTTIME,
        CLOCK_REALTIME_COARSE,
        CLOCK_MONOTONIC_COARSE
    };

    const char *clock_names[] = {
        "CLOCK_REALTIME",
        "CLOCK_MONOTONIC",
        "CLOCK_MONOTONIC_RAW",
        "CLOCK_BOOTTIME",
        "CLOCK_REALTIME_COARSE",
        "CLOCK_MONOTONIC_COARSE"
    };

    for (int i = 0; i < 6; i++) {
        memset(&stats, 0, sizeof(stats));
        measure_latency(clocks[i], clock_names[i], &stats);
        print_stats(clock_names[i], &stats);
        measure_cycle_latency(clocks[i], clock_names[i]);
        measure_throughput(clocks[i], clock_names[i]);

        if (i < 5) {
            printf("\n---\n");
        }
    }

    // gettimeofday 测试
    printf("\n=== gettimeofday() ===\n");
    struct timeval tv;
    uint64_t start_cycles, end_cycles;
    const int ITER = 1000000;

    // Warmup
    for (int i = 0; i < WARMUP_ITER; i++) {
        gettimeofday(&tv, NULL);
    }

    start_cycles = read_cycles();
    for (int i = 0; i < ITER; i++) {
        gettimeofday(&tv, NULL);
    }
    end_cycles = read_cycles();

    double cycles_per_call = (double)(end_cycles - start_cycles) / ITER;
    printf("  Cycles per call: %.2f\n", cycles_per_call);
    printf("  Latency per call: %.2f ns\n", cycles_per_call * 0.2);

    printf("\n=== Benchmark Complete ===\n");
    printf("\nNote: All cycle measurements use rdtime instruction.\n");
    printf("Cycle-to-time conversion assumes 5GHz CPU frequency.\n");

    return 0;
}
