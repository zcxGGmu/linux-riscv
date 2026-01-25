#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# RISC-V vDSO Performance Analysis Script
#
# This script provides comprehensive analysis tools for vDSO performance

set -e

KERNEL_PATH=${1:-"/home/zcxggmu/workspace/patch-work/linux"}
VDSO_PATH="${KERNEL_PATH}/build/arch/riscv/kernel/vdso/vdso.so.dbg"
OBJDUMP="riscv64-linux-gnu-objdump"
READELF="riscv64-linux-gnu-readelf"
OBJCOPY="riscv64-linux-gnu-objcopy"

echo "=========================================="
echo "RISC-V vDSO Performance Analysis Toolkit"
echo "=========================================="
echo ""

# 检查工具是否存在
check_tools() {
    local missing=0

    for tool in $OBJDUMP $READELF $OBJCOPY; do
        if ! command -v $tool &> /dev/null; then
            echo "Error: $tool not found"
            missing=1
        fi
    done

    if [ $missing -eq 1 ]; then
        echo "Please install RISC-V toolchain:"
        echo "  apt-get install binutils-riscv64-linux-gnu"
        exit 1
    fi

    if [ ! -f "$VDSO_PATH" ]; then
        echo "Error: vDSO binary not found at $VDSO_PATH"
        echo "Please build the kernel first:"
        echo "  cd $KERNEL_PATH"
        echo "  make ARCH=riscv defconfig"
        echo "  make ARCH=riscv -j\$(nproc)"
        exit 1
    fi
}

# 函数分析
analyze_function() {
    local func=$1
    echo "=========================================="
    echo "Function: $func"
    echo "=========================================="

    # 提取函数地址
    local addr=$($OBJDUMP -t "$VDSO_PATH" | grep " $func$" | awk '{print $1}')
    if [ -z "$addr" ]; then
        echo "Function $func not found"
        return
    fi

    echo "Address: 0x$addr"
    echo ""

    # 反汇编函数
    echo "Disassembly:"
    $OBJDUMP -d "$VDSO_PATH" | grep -A 200 "^$addr " | head -50
    echo ""

    # 指令统计
    echo "Instruction Statistics:"
    $OBJDUMP -d "$VDSO_PATH" | awk "/^$addr /,/^[0-9a-f]+ <.*>/" | \
        grep -v "^$" | grep -v "^File" | wc -l | \
        xargs echo "  Total instructions:"

    $OBJDUMP -d "$VDSO_PATH" | awk "/^$addr /,/^[0-9a-f]+ <.*>/" | \
        grep -E "\t(ld|sd|lw|sw)\t" | wc -l | \
        xargs echo "  Memory operations:"

    $OBJDUMP -d "$VDSO_PATH" | awk "/^$addr /,/^[0-9a-f]+ <.*>/" | \
        grep -E "\t(mul|div|rem)\t" | wc -l | \
        xargs echo "  Multiply/divide:"

    $OBJDUMP -d "$VDSO_PATH" | awk "/^$addr /,/^[0-9a-f]+ <.*>/" | \
        grep -E "\t(beq|bne|blt|bge|bltu|bgeu|j|jal|ret)\t" | wc -l | \
        xargs echo "  Branch/jump:"

    $OBJDUMP -d "$VDSO_PATH" | awk "/^$addr /,/^[0-9a-f]+ <.*>/" | \
        grep -E "\tfence\t" | wc -l | \
        xargs echo "  Memory barriers:"

    echo ""
}

# 符号表分析
analyze_symbols() {
    echo "=========================================="
    echo "vDSO Symbol Table"
    echo "=========================================="
    echo ""

    echo "Exported Functions:"
    echo "-------------------"
    $OBJDUMP -t "$VDSO_PATH" | grep -E "F \.text.*__vdso_" | \
        awk '{printf "  %-30s 0x%s  %5d bytes\n", $NF, $1, strtonum($2)}' | \
        sort -k2
    echo ""

    echo "Data Symbols:"
    echo "-------------"
    $OBJDUMP -t "$VDSO_PATH" | grep -E "O .*vdso_" | \
        awk '{printf "  %-30s 0x%s\n", $NF, $1}'
    echo ""
}

# 段分析
analyze_sections() {
    echo "=========================================="
    echo "Section Analysis"
    echo "=========================================="
    echo ""

    $READELF -S "$VDSO_PATH" | grep -E "Name|\.text|\.rodata|\.data|\.bss|\.dyn"
    echo ""

    echo "Memory Layout:"
    echo "--------------"
    $READELF -l "$VDSO_PATH" | grep -A 10 "LOAD"
    echo ""
}

# 关键路径分析
analyze_critical_path() {
    echo "=========================================="
    echo "Critical Path Analysis: __vdso_clock_gettime"
    echo "=========================================="
    echo ""

    # 提取关键指令序列
    local func="__vdso_clock_gettime"
    local addr=$($OBJDUMP -t "$VDSO_PATH" | grep " $func$" | awk '{print $1}')

    if [ -z "$addr" ]; then
        echo "Function not found"
        return
    fi

    echo "Fast Path (Typical Execution):"
    echo "-------------------------------"

    # 识别快速路径的关键指令
    $OBJDUMP -d "$VDSO_PATH" | awk "/^$addr /,/^[0-9a-f]+ <.*>/" | \
        grep -E "rdtime|fence|ld.*\(s1\)|mul|srl" | head -20

    echo ""
    echo "Latency Breakdown (estimated):"
    echo "------------------------------"
    echo "  Seqlock read:        ~5 cycles"
    echo "  Memory barriers:     ~8 cycles (2 x fence r,r)"
    echo "  Data loads (6x):     ~24 cycles (4 cycles each, L1 hit)"
    echo "  rdtime instruction:  ~20-50 cycles (implementation dependent)"
    echo "  Multiply (64-bit):   ~3-5 cycles"
    echo "  Shift right:         ~1 cycle"
    echo "  Seqlock validation:  ~5 cycles"
    echo "  ----------------------------"
    echo "  Total fast path:     ~70-100 cycles"
    echo ""
}

# rdtime 指令使用分析
analyze_rdtime() {
    echo "=========================================="
    echo "rdtime Instruction Usage"
    echo "=========================================="
    echo ""

    echo "rdtime instruction locations:"
    echo "-----------------------------"
    $OBJDUMP -d "$VDSO_PATH" | grep -n "rdtime" | while read line; do
        local line_num=$(echo $line | cut -d: -f1)
        local instr=$(echo $line | cut -d: -f2-)
        local func=$(echo "$instr" | sed 's/.*<\([^>]*\)>.*/\1/')

        # 获取函数上下文
        local func_addr=$($OBJDUMP -t "$VDSO_PATH" | grep " <$func>$" | awk '{print $1}')
        if [ -n "$func_addr" ]; then
            echo "  Function: $func"
            echo "  Line $line_num: $instr"
            echo ""
        fi
    done

    echo "rdtime Performance Notes:"
    echo "-------------------------"
    echo "  - Instruction: rdtime rd (encodes to 0xC01027F3)"
    echo "  - Accesses CSR_TIME (0xC01)"
    echo "  - Traps to M-mode if not directly supported"
    echo "  - Latency: ~20-50 cycles (typical)"
    echo "  - Atomic 64-bit read guaranteed"
    echo ""
}

# 内存屏障分析
analyze_barriers() {
    echo "=========================================="
    echo "Memory Barrier Analysis"
    echo "=========================================="
    echo ""

    echo "fence instruction usage:"
    echo "------------------------"

    $OBJDUMP -d "$VDSO_PATH" | grep -n "fence" | while read line; do
        echo "$line"
    done

    echo ""
    echo "Barrier Types:"
    echo "-------------"
    $OBJDUMP -d "$VDSO_PATH" | grep "fence" | \
        awk '{print $NF}' | sort | uniq -c | \
        awk '{print "  " $2 ": " $1 " occurrences"}'

    echo ""
    echo "Performance Impact:"
    echo "------------------"
    echo "  fence r,r: 2-5 cycles (read-read barrier)"
    echo "  fence w,w: 2-5 cycles (write-write barrier)"
    echo "  fence rw,rw: 5-10 cycles (full barrier)"
    echo ""
}

# 分支预测分析
analyze_branches() {
    echo "=========================================="
    echo "Branch Prediction Analysis"
    echo "=========================================="
    echo ""

    local func="__vdso_clock_gettime"
    local addr=$($OBJDUMP -t "$VDSO_PATH" | grep " $func$" | awk '{print $1}')

    if [ -z "$addr" ]; then
        echo "Function not found"
        return
    fi

    echo "Branch Instructions in $func:"
    echo "------------------------------"

    $OBJDUMP -d "$VDSO_PATH" | awk "/^$addr /,/^[0-9a-f]+ <.*>/" | \
        grep -E "\t(beqz|bnez|blt|bge|bltu|bgeu|jal|ret)\t" | \
        head -20 | while read line; do
            echo "  $line"
        done

    echo ""
    echo "Branch Prediction Notes:"
    echo "-----------------------"
    echo "  - Fall-through branches are predicted as 'not taken'"
    echo "  - Misprediction penalty: ~10-20 cycles"
    echo "  - Seqlock retry loop is hot path for branch predictor"
    echo ""
}

# 生成性能报告
generate_report() {
    local output_file="vdso_performance_report.txt"

    echo "Generating performance report to $output_file..."

    {
        echo "RISC-V vDSO Performance Analysis Report"
        echo "========================================"
        echo "Date: $(date)"
        echo "Kernel: $(uname -r)"
        echo "Architecture: $(uname -m)"
        echo ""

        analyze_symbols
        echo ""

        analyze_sections
        echo ""

        analyze_function "__vdso_clock_gettime"
        echo ""

        analyze_function "__vdso_gettimeofday"
        echo ""

        analyze_critical_path
        echo ""

        analyze_rdtime
        echo ""

        analyze_barriers
        echo ""

        analyze_branches
        echo ""

    } > "$output_file"

    echo "Report saved to $output_file"
}

# 性能测试建议
print_perf_recommendations() {
    echo "=========================================="
    echo "Performance Testing Recommendations"
    echo "=========================================="
    echo ""

    echo "1. Compile the benchmark:"
    echo "   gcc -O2 -o bench_vdso_latency bench_vdso_latency.c -lrt"
    echo ""

    echo "2. Run latency tests:"
    echo "   sudo perf stat -e cycles,instructions,cache-references,cache-misses \\"
    echo "                  -e branches,branch-misses ./bench_vdso_latency"
    echo ""

    echo "3. Profile with perf:"
    echo "   sudo perf record -g -e cycles ./bench_vdso_latency"
    echo "   sudo perf report"
    echo ""

    echo "4. Annotate assembly with perf:"
    echo "   sudo perf annotate --symbol=__vdso_clock_gettime --stdio"
    echo ""

    echo "5. Measure cache behavior:"
    echo "   sudo perf stat -e L1-dcache-loads,L1-dcache-load-misses \\"
    echo "                  -e LLC-loads,LLC-load-misses ./bench_vdso_latency"
    echo ""

    echo "6. Test with different workloads:"
    echo "   - Single-threaded latency"
    echo "   - Multi-threaded contention"
    echo "   - Real-time priority tasks"
    echo ""

    echo "7. Compare with syscalls:"
    echo "   # Use vDSO bypass for comparison"
    echo "   export GLIBC_TUNABLES=glibc.pthread.rtlock=0"
    echo ""
}

# 主菜单
main() {
    check_tools

    if [ "$1" == "--report" ]; then
        generate_report
        exit 0
    fi

    # 默认执行所有分析
    analyze_symbols
    analyze_sections
    analyze_function "__vdso_clock_gettime"
    analyze_function "__vdso_gettimeofday"
    analyze_critical_path
    analyze_rdtime
    analyze_barriers
    analyze_branches
    print_perf_recommendations
}

# 参数处理
case "${1:-}" in
    --symbols)
        analyze_symbols
        ;;
    --sections)
        analyze_sections
        ;;
    --function)
        analyze_function "$2"
        ;;
    --critical-path)
        analyze_critical_path
        ;;
    --rdtime)
        analyze_rdtime
        ;;
    --barriers)
        analyze_barriers
        ;;
    --branches)
        analyze_branches
        ;;
    --report)
        generate_report
        ;;
    --help|*)
        echo "Usage: $0 [OPTION]"
        echo ""
        echo "Options:"
        echo "  --symbols         Show vDSO symbol table"
        echo "  --sections        Show section information"
        echo "  --function NAME   Analyze specific function"
        echo "  --critical-path   Analyze critical path"
        echo "  --rdtime          Analyze rdtime usage"
        echo "  --barriers        Analyze memory barriers"
        echo "  --branches        Analyze branch patterns"
        echo "  --report          Generate full report"
        echo "  --help            Show this help"
        echo ""
        echo "If no option specified, runs all analyses."
        ;;
esac
