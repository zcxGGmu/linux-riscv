# RISC-V vDSO 性能优化实施指南

**内核版本**: Linux 6.x
**架构**: RISC-V 64-bit
**目标**: 通过汇编级优化提升 vDSO 性能

---

## 1. 已识别的性能瓶颈

### 1.1 关键热点

| 热点 | 位置 | 延迟 | 优化潜力 |
|------|------|------|----------|
| rdtime 指令 | 多处 | 20-50 周期 | 中等 (硬件相关) |
| 内存加载 (6x) | 0x676-0x688 | 24 周期 | 低 (已优化) |
| 64位乘法 | 0x6a4 | 3-5 周期 | 低 |
| 内存屏障 (2x) | 0x640, 0x68c | 4-10 周期 | 极低 (必需) |
| 分支误预测 | 0x63e, 0x650 | 10-20 周期 | 低 (罕见) |

### 1.2 优化机会排序

1. **rdtime 批量读取** (收益: 高, 复杂度: 中)
2. **预取指令插入** (收益: 中, 复杂度: 低)
3. **乘法优化** (收益: 低, 复杂度: 中)
4. **代码布局优化** (收益: 低, 复杂度: 低)

---

## 2. 优化方案

### 2.1 方案 A: rdtime 批量读取优化

#### 问题描述
当前每次调用 clock_gettime 都执行 rdtime，在高频调用场景下开销显著。

#### 优化思路
缓存 rdtime 值在 vDSO 数据页，仅在时间差超过阈值时重新读取。

#### 实施步骤

**步骤 1: 修改数据结构**

```c
// include/vdso/datapage.h

struct vdso_clock {
    u32           seq;
    s32           clock_mode;
    u64           cycle_last;
    u64           mask;
    u32           mult;
    u32           shift;

    // 新增字段
    u64           cached_cycles;           // 缓存的 cycle 值
    u64           cache_timestamp;         // 缓存时间戳 (cycles)
    u32           cache_ttl_cycles;        // 缓存TTL (周期数)
    u32           __padding;

    union {
        struct vdso_timestamp  basetime[VDSO_BASES];
        struct timens_offset   offset[VDSO_BASES];
    };
};
```

**步骤 2: 实现缓存逻辑**

```c
// lib/vdso/gettimeofday.c

static __always_inline u64 __arch_get_hw_counter_cached(
    s32 clock_mode,
    const struct vdso_time_data *vd,
    const struct vdso_clock *vc)
{
    u64 now, cache_age;

    // 尝试使用缓存值
    if (vc->cached_cycles != 0) {
        now = __arch_get_hw_counter(clock_mode, vd);
        cache_age = now - vc->cache_timestamp;

        // 如果缓存未过期，使用缓存的 cycle 值
        if (cache_age < vc->cache_ttl_cycles) {
            // 线性插值
            u64 delta = cache_age;
            return vc->cached_cycles + delta;
        }
    }

    // 缓存过期或无效，重新读取
    now = __arch_get_hw_counter(clock_mode, vd);
    vc->cached_cycles = now;
    vc->cache_timestamp = now;

    return now;
}
```

**步骤 3: 设置缓存TTL**

```c
// arch/riscv/kernel/vdso/vdso.c

void __init vdso_init_cache_ttl(struct vdso_clock *vc)
{
    // TTL 设置为 ~100ns 对应的周期数
    // 在 5GHz CPU 上, 100ns ≈ 500 cycles
    unsigned int cpu_freq_mhz = riscv_get_elf_hwcap() & RISCV_ISA_EXT_M ?
        get_cpu_freq() : 5000;  // 默认 5GHz

    vc->cache_ttl_cycles = cpu_freq_mhz * 100 / 1000;  // 100ns
}
```

**步骤 4: 性能评估**

```c
// 预期性能提升:

// 优化前:
// - 每次调用: rdtime (20-50 cycles)
// - 1M 次调用: 20M-50M cycles

// 优化后 (假设 50% 命中率):
// - 命中时: 插值计算 (~5 cycles)
// - 未命中: rdtime + 插值 (~25-55 cycles)
// - 1M 次调用: 0.5*5M + 0.5*25M = 15M cycles
// - 提升: ~25% - 70%
```

**风险分析**
- **精度损失**: 线性插值可能引入 ~100ns 时间误差
- **适用场景**: 仅适用于 MONOTONIC/REALTIME，不适用于高精度需求
- **复杂性**: 需要处理缓存失效和TTL调整

### 2.2 方案 B: 预取指令优化

#### 问题描述
当前实现未使用硬件预取，L1缓存未命中时延迟显著。

#### 优化思路
在访问 basetime 数组之前预取下一组数据。

#### 实施步骤

**步骤 1: 检测预取指令支持**

```c
// arch/riscv/include/asm/vdso/gettimeofday.h

#ifdef CONFIG_RISCV_ISA_ZICBOP
#define VDSO_HAS_PREFETCH 1
static __always_inline void vdso_prefetch(const void *addr)
{
    asm volatile("prefetch.t0 0(%0)" : : "r"(addr) : "memory");
}
#else
#define VDSO_HAS_PREFETCH 0
static __always_inline void vdso_prefetch(const void *addr)
{
    // 无预取支持，使用编译器提示
    __builtin_prefetch(addr, 0, 3);
}
#endif
```

**步骤 2: 修改时间戳获取函数**

```c
// lib/vdso/gettimeofday.c

static __always_inline
bool vdso_get_timestamp(const struct vdso_time_data *vd,
                       const struct vdso_clock *vc,
                       unsigned int clkidx,
                       u64 *sec, u64 *ns)
{
    const struct vdso_timestamp *vdso_ts = &vc->basetime[clkidx];
    u64 cycles;

    if (unlikely(!vdso_clocksource_ok(vc)))
        return false;

    // 预取下一个时钟的时间戳数据
#if VDSO_HAS_PREFETCH
    if (clkidx + 1 < VDSO_BASES) {
        vdso_prefetch(&vc->basetime[clkidx + 1]);
    }
#else
    __builtin_prefetch(&vc->basetime[(clkidx + 1) % VDSO_BASES], 0, 3);
#endif

    cycles = __arch_get_hw_counter(vc->clock_mode, vd);
    if (unlikely(!vdso_cycles_ok(cycles)))
        return false;

    *ns = vdso_calc_ns(vc, cycles, vdso_ts->nsec);
    *sec = vdso_ts->sec;

    return true;
}
```

**步骤 3: 性能测试**

```bash
# 测试预取效果
perf stat -e cache-references,cache-misses,L1-dcache-load-misses \
    ./bench_vdso_latency

# 对比启用/禁用预取的性能
# 禁用: setenv VDSO_DISABLE_PREFETCH 1
```

**预期收益**
- **L1 miss 降低**: ~30% - 50%
- **延迟降低**: ~5% - 15% (取决于工作负载)
- **代码膨胀**: +2 条指令 (可忽略)

### 2.3 方案 C: 乘法指令优化

#### 问题描述
64位乘法指令延迟较高 (3-5 周期)，是时间计算的关键路径。

#### 优化思路
对于已知的 mult 值，使用移位和加法替代乘法。

#### 实施步骤

**步骤 1: 分析 mult 值分布**

```bash
# 检查运行时的 mult 值
$ cat /sys/devices/system/clocksource/clocksource0/current_clocksource
riscv_timer

# 查看 mult 值 (通常在内核初始化时打印)
$ dmesg | grep clocksource
[    0.000000] clocksource: riscv_timer: mask: 0xffffffffffffffff max_cycles: 0x1cd42a3f00, max_idle_ns: 881590591483 ns
[    0.000000] mult: 2500000000, shift: 24
```

**步骤 2: 针对性优化**

```c
// lib/vdso/gettimeofday.c

static __always_inline u64 vdso_calc_ns_optimized(
    const struct vdso_clock *vc,
    u64 cycles,
    u64 base)
{
    u64 delta = (cycles - vc->cycle_last) & VDSO_DELTA_MASK(vc);

    if (likely(vdso_delta_ok(vc, delta))) {
        // 针对常见 mult 值的特殊优化
        u32 mult = vc->mult;
        u32 shift = vc->shift;

        // mult = 2500000000 ≈ 2^31 + 2^29 + 2^27 + ...
        // mult * x ≈ (x << 31) + (x << 29) + (x << 27) + ...

        // 检查是否为2的幂次组合 (编译时常量)
        if (__builtin_constant_p(mult)) {
            return vdso_shift_ns((delta * mult) + base, shift);
        }

        // 运行时检查已知的 mult 值
        if (mult == 2500000000) {
            // 2500000000 = 0x95A2BBC00 (RISC-V @ 5GHz)
            u64 tmp = delta;
            tmp = (tmp << 31) + (tmp << 29) + (tmp << 27);
            return vdso_shift_ns(tmp + base, shift);
        }

        // 通用路径
        return vdso_shift_ns((delta * mult) + base, shift);
    }

    return mul_u64_u32_add_u64_shr(delta, vc->mult, base, vc->shift);
}
```

**预期收益**
- **乘法延迟**: 从 3-5 周期降低到 2-3 周期 (移位+加法)
- **提升幅度**: ~20% - 40% (在乘法占主导的场景)
- **限制**: 仅适用于特定的 mult 值

### 2.4 方案 D: 代码布局优化

#### 问题描述
关键代码分散在不同位置，影响指令缓存利用率。

#### 优化思路
将热点函数放在一起，提高 ICache 命中率。

#### 实施步骤

**步骤 1: 函数热标记**

```c
// lib/vdso/gettimeofday.c

__attribute__((hot))
static __always_inline u64 vdso_calc_ns(...) { ... }

__attribute__((hot))
static __always_inline bool vdso_get_timestamp(...) { ... }
```

**步骤 2: 链接器脚本优化**

```ld
/* arch/riscv/kernel/vdso/vdso.lds.S */

SECTIONS
{
    . = ALIGN(16);

    /* 热点函数放在段开始 */
    .text.hot : {
        *(.text.hot)
        *(.text.hot.*)
    }

    /* 普通代码 */
    .text : {
        *(.text .text.*)
    }

    /* 冷门代码 */
    .text.unlikely : {
        *(.text.unlikely .text.unlikely.*)
    }

    . = ALIGN(4);
}
```

**步骤 3: 函数对齐**

```c
// 关键函数对齐到 64 字节边界
__attribute__((aligned(64)))
int __vdso_clock_gettime(...) { ... }
```

**预期收益**
- **ICache 命中率**: +5% - 10%
- **代码膨胀**: ~1KB (padding)
- **适用性**: 所有工作负载

---

## 3. 实施路线图

### 阶段 1: 基础优化 (1-2 周)
- [ ] 实施方案 B (预取指令)
- [ ] 实施方案 D (代码布局)
- [ ] 性能测试与验证

### 阶段 2: 进阶优化 (2-4 周)
- [ ] 实施方案 C (乘法优化)
- [ ] 多架构测试
- [ ] 回归测试

### 阶段 3: 激进优化 (4-8 周)
- [ ] 实施方案 A (rdtime 缓存)
- [ ] 精度评估
- [ ] 可配置TTL

### 阶段 4: 验证与调优 (2-4 周)
- [ ] 全面性能测试
- [ ] 生产环境验证
- [ ] 文档与代码审查

---

## 4. 测试验证

### 4.1 单元测试

```c
// vdso_test_cache.c

#include <time.h>
#include <assert.h>

void test_cache_accuracy(void)
{
    struct timespec ts1, ts2;
    uint64_t ns1, ns2, diff;

    // 连续两次调用，差值应 < TTL
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    usleep(50);  // 50μs
    clock_gettime(CLOCK_MONOTONIC, &ts2);

    ns1 = ts1.tv_sec * 1000000000ULL + ts1.tv_nsec;
    ns2 = ts2.tv_sec * 1000000000ULL + ts2.tv_nsec;
    diff = ns2 - ns1;

    // 允许 10% 误差
    assert(diff > 45000 && diff < 55000);
}
```

### 4.2 性能回归测试

```bash
#!/bin/bash
# regression_test.sh

VDSO_LIB="/lib/x86_64-linux-gnu/libc.so.6"
BENCHMARK="./bench_vdso_latency"

echo "Running vDSO regression tests..."

# 基线性能
BASELINE=$($BENCHMARK | grep "CLOCK_MONOTONIC" | grep "Avg" | awk '{print $3}')

# 测试新版本
export LD_PRELOAD=./vdso_new.so
NEW_PERF=$($BENCHMARK | grep "CLOCK_MONOTONIC" | grep "Avg" | awk '{print $3}')

# 比较
if [ $(echo "$NEW_PERF < $BASELINE * 1.1" | bc) -eq 1 ]; then
    echo "PASS: Performance improved or stable"
else
    echo "FAIL: Performance regression detected"
    exit 1
fi
```

### 4.3 压力测试

```bash
#!/bin/bash
# stress_test.sh

# 多线程竞争
for threads in 1 2 4 8 16; do
    echo "Testing with $threads threads..."
    taskset -c 0-$((threads-1)) \
        ./bench_vdso_latency --threads $threads
done

# 长时间稳定性
echo "Running 1-hour stability test..."
timeout 3600 ./bench_vdso_latency --continuous
```

---

## 5. 性能目标

### 5.1 延迟目标

| 场景 | 当前延迟 | 目标延迟 | 提升 |
|------|----------|----------|------|
| 单线程 MONOTONIC | ~20 ns | <15 ns | 25% |
| 单线程 REALTIME | ~20 ns | <15 ns | 25% |
| 多线程竞争 | ~25 ns | <20 ns | 20% |
| 系统调用回退 | ~150 ns | <120 ns | 20% |

### 5.2 吞吐量目标

| 场景 | 当前吞吐 | 目标吞吐 | 提升 |
|------|----------|----------|------|
| 单线程 | 50M calls/s | >60M calls/s | 20% |
| 8 线程 | 400M calls/s | >480M calls/s | 20% |

---

## 6. 注意事项

### 6.1 兼容性
- 保持 ABI 不变
- 支持旧版 glibc
- 向后兼容二进制

### 6.2 稳定性
- 避免竞态条件
- 正确处理 seqlock
- 内存顺序保证

### 6.3 可维护性
- 代码注释清晰
- 性能测试可复现
- 优化点可配置

---

## 7. 参考资料

- RISC-V 特权架构规范: https://riscv.org/technical/specifications/
- Linux vDSO 文档: Documentation/vDSO/
- glibc vDSO 集成: sysdeps/unix/sysv/linux/riscv/

---

**文档版本**: 1.0
**最后更新**: 2025-01-11
