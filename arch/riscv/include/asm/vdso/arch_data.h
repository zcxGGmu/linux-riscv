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
	 * Kernel-managed VDSO time cache
	 *
	 * These fields are WRITTEN by the kernel (timekeeping code) and
	 * READ by VDSO code in userspace. This is safe because:
	 * - Kernel has write access to VVAR page
	 * - Userspace only has read access (VM_READ)
	 *
	 * The cache is updated by the kernel periodically during
	 * timekeeping updates, avoiding the need for userspace writes
	 * or dynamic relocations.
	 */
	struct {
		__u64 cached_cycles;		/* Cached CSR_TIME value */
		__u64 cache_timestamp;		/* When cache was updated (cycles) */
		__u32 cache_generation;		/* Generation counter for validation */
		__u32 cache_valid;		/* Is cache valid (non-zero if valid) */
	} time_cache;
#endif
};

#endif /* __RISCV_ASM_VDSO_ARCH_DATA_H */
