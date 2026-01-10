/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RISCV_ASM_VDSO_ARCH_DATA_H
#define __RISCV_ASM_VDSO_ARCH_DATA_H

#include <linux/types.h>
#include <vdso/datapage.h>
#include <asm/hwprobe.h>

struct vdso_arch_data {
	/* Stash static answers to the hwprobe queries when all CPUs are selected. */
	__u64 all_cpu_hwprobe_values[RISCV_HWPROBE_MAX_KEY + 1];

	/* Boolean indicating all CPUs have the same static hwprobe values. */
	__u8 homogeneous_cpus;

	/*
	 * A gate to check and see if the hwprobe data is actually ready, as
	 * probing is deferred to avoid boot slowdowns.
	 */
	__u8 ready;

#ifdef CONFIG_RISCV_VDSO_TIME_CACHE
	/*
	 * VDSO time caching infrastructure
	 *
	 * Cached CSR_TIME value to reduce expensive M-mode traps.
	 * The cache is validated against the vdso_time_data sequence
	 * counter to ensure freshness.
	 */
	struct {
		__u64 cached_cycles;		/* Cached CSR_TIME value */
		__u64 cache_timestamp;		/* Cache creation timestamp (cycles) */
		__u32 cache_generation;		/* Generation for invalidation */
		__u32 cache_valid_ns;		/* Cache validity period (ns) */
	} time_cache;
#endif
};

#endif /* __RISCV_ASM_VDSO_ARCH_DATA_H */
