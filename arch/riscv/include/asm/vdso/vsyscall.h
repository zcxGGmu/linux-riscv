/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLER__

#include <vdso/datapage.h>
#include <asm/csr.h>

/*
 * RISC-V VDSO time cache update
 *
 * This function is called by the kernel during timekeeping updates to
 * refresh the cached CSR_TIME value in the VDSO data page.
 *
 * The cache allows userspace VDSO code to read a cached time value
 * instead of trapping to M-mode for every clock_gettime call.
 */
static __always_inline void __arch_update_vdso_clock(struct vdso_clock *vc)
{
#ifdef CONFIG_RISCV_VDSO_TIME_CACHE
	extern struct vdso_arch_data *vdso_k_arch_data;
	u64 cycles;

	/* Read current CSR_TIME value */
	cycles = csr_read(CSR_TIME);

	/*
	 * Update the cached time value in vdso_arch_data
	 *
	 * This is safe because:
	 * - We're in kernel context with write access to VVAR page
	 * - vdso_write_begin/end provides sequence locking
	 * - Userspace will see consistent snapshot via READ_ONCE
	 */
	vdso_k_arch_data->time_cache.cached_cycles = cycles;
	vdso_k_arch_data->time_cache.cache_timestamp = cycles;
	vdso_k_arch_data->time_cache.cache_generation = vc->seq;
	vdso_k_arch_data->time_cache.cache_valid = 1;
#endif
}

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
