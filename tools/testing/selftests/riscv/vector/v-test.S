/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 ARM Limited.
 * Copyright (C) 2025 RISC-V Foundation.
 */

#include <linux/syscall.h>
#include <asm/unistd.h>
#include <asm/hwcap.h>

#ifndef __riscv_v
#error "This test requires RISC-V vector support"
#endif

.text
.globl _start
.type _start,@function
_start:
	/* Set up stack frame */
	addi	sp, sp, -16
	sw	ra, 12(sp)

	/* Check if we have vector support */
	li	a0, AT_HWCAP
	li	a7, __NR_getauxval
	ecall
	li	t0, HWCAP_RISCV_V
	and	t0, a0, t0
	beqz	t0, no_vector_support

	/* Initialize vector registers with a pattern */
	li	t0, 0x12345678
	vsetvli	zero, zero, e32, m1
	vmv.v.x	v0, t0
	vmv.v.x	v1, t0
	vmv.v.x	v2, t0
	vmv.v.x	v3, t0
	vmv.v.x	v4, t0
	vmv.v.x	v5, t0
	vmv.v.x	v6, t0
	vmv.v.x	v7, t0

	/* Perform some vector operations */
	vadd.vv	v8, v0, v1
	vsub.vv	v9, v2, v3
	vand.vv	v10, v4, v5
	vxor.vv	v11, v6, v7

	/* More operations */
	vadd.vv	v12, v8, v9
	vxor.vv	v13, v10, v11
	vadd.vv	v14, v12, v13

	/* Signal that we're working */
	li	a0, 1
	li	a1, signal_str
	li	a2, signal_str_len
	li	a7, __NR_write
	ecall

	/* Sleep for a bit */
	li	a0, 1
	li	a7, __NR_sleep
	ecall

	/* Do it again to show we're still running */
	vadd.vv	v15, v14, v0
	vsub.vv	v16, v15, v1
	vand.vv	v17, v16, v2
	vxor.vv	v18, v17, v3

	/* Signal again */
	li	a0, 1
	li	a1, signal_str
	li	a2, signal_str_len
	li	a7, __NR_write
	ecall

	/* Exit with success */
	li	a0, 0
	li	a7, __NR_exit_group
	ecall

no_vector_support:
	/* Print error message */
	li	a0, 1
	li	a1, no_vector_str
	li	a2, no_vector_str_len
	li	a7, __NR_write
	ecall

	/* Exit with error */
	li	a0, 1
	li	a7, __NR_exit_group
	ecall

signal_str:
	.ascii "V"
signal_str_len = . - signal_str

no_vector_str:
	.ascii "NO VECTOR SUPPORT\n"
no_vector_str_len = . - no_vector_str