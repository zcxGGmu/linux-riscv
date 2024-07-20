// SPDX-License-Identifier: GPL-2.0
#ifndef ARCH_PERF_RISCV_EXCEPTION_TYPES_H
#define ARCH_PERF_RISCV_EXCEPTION_TYPES_H

/* Exception cause high bit - is an interrupt if set */
#define CAUSE_IRQ_FLAG		(_AC(1, UL) << (__riscv_xlen - 1))

/* Interrupt causes (minus the high bit) */
#define IRQ_S_SOFT		1
#define IRQ_VS_SOFT		2
#define IRQ_M_SOFT		3
#define IRQ_S_TIMER		5
#define IRQ_VS_TIMER	6
#define IRQ_M_TIMER		7
#define IRQ_S_EXT		9
#define IRQ_VS_EXT		10
#define IRQ_M_EXT		11
#define IRQ_S_GEXT		12
#define IRQ_PMU_OVF		13

/* Exception causes */
#define EXC_INST_MISALIGNED 0
#define EXC_INST_ACCESS 1
#define EXC_INST_ILLEGAL 2
#define EXC_BREAKPOINT 3
#define EXC_LOAD_MISALIGNED 4
#define EXC_LOAD_ACCESS 5
#define EXC_STORE_MISALIGNED 6
#define EXC_STORE_ACCESS 7
#define EXC_SYSCALL 8
#define EXC_HYPERVISOR_SYSCALL 9
#define EXC_SUPERVISOR_SYSCALL 10
#define EXC_INST_PAGE_FAULT 12
#define EXC_LOAD_PAGE_FAULT 13
#define EXC_STORE_PAGE_FAULT 15
#define EXC_INST_GUEST_PAGE_FAULT 20
#define EXC_LOAD_GUEST_PAGE_FAULT 21
#define EXC_VIRTUAL_INST_FAULT 22
#define EXC_STORE_GUEST_PAGE_FAULT 23

#define TCC_IRQ(x) { IRQ_##x, #x }
#define TCC_EXC(x) { EXC_##x, #x }

#define kvm_riscv_irq_class \
	TCC_IRQ(S_SOFT), TCC_IRQ(S_TIMER), TCC_IRQ(S_EXT), \
	TCC_IRQ(S_GEXT), TCC_IRQ(S_GEXT), TCC_IRQ(PMU_OVF)	

#define kvm_riscv_exc_class \
	TCC_EXC(INST_ILLEGAL), TCC_EXC(LOAD_MISALIGNED), TCC_EXC(STORE_MISALIGNED), \
	TCC_EXC(VIRTUAL_INST_FAULT), TCC_EXC(INST_GUEST_PAGE_FAULT), \
	TCC_EXC(LOAD_GUEST_PAGE_FAULT), TCC_EXC(STORE_GUEST_PAGE_FAULT), \
	TCC_EXC(SUPERVISOR_SYSCALL), TCC_EXC(BREAKPOINT)

#endif /* ARCH_PERF_RISCV_EXCEPTION_TYPES_H */
