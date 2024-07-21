// SPDX-License-Identifier: GPL-2.0
/*
 * Tracepoints for RISC-V KVM
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *
 */
#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

#define kvm_riscv_trap_type \
	{1, "IRQ"},	\
	{0, "EXC"}

#define TCC_IRQ(x) { IRQ_##x, #x }
#define TCC_EXC(x) { EXC_##x, #x }

#define kvm_riscv_trap_class \
	TCC_IRQ(S_SOFT), TCC_IRQ(S_TIMER), TCC_IRQ(S_EXT), \
	TCC_IRQ(S_GEXT), TCC_IRQ(S_GEXT), TCC_IRQ(PMU_OVF) \
	TCC_EXC(INST_ILLEGAL), TCC_EXC(LOAD_MISALIGNED), TCC_EXC(STORE_MISALIGNED), \
	TCC_EXC(VIRTUAL_INST_FAULT), TCC_EXC(INST_GUEST_PAGE_FAULT), \
	TCC_EXC(LOAD_GUEST_PAGE_FAULT), TCC_EXC(STORE_GUEST_PAGE_FAULT), \
	TCC_EXC(SUPERVISOR_SYSCALL), TCC_EXC(BREAKPOINT)

TRACE_EVENT(kvm_entry,
	TP_PROTO(struct kvm_vcpu *vcpu),
	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(unsigned long, pc)
	),

	TP_fast_assign(
		__entry->pc	= vcpu->arch.guest_context.sepc;
	),

	TP_printk("PC: 0x016%lx", __entry->pc)
);

TRACE_EVENT(kvm_exit,
	TP_PROTO(struct kvm_cpu_trap *trap),
	TP_ARGS(trap),

	TP_STRUCT__entry(
		__field(unsigned long, sepc)
		__field(unsigned long, scause)
		__field(unsigned long, stval)
		__field(unsigned long, htval)
		__field(unsigned long, htinst)
	),

	TP_fast_assign(
		__entry->sepc		= trap->sepc;
		__entry->scause		= trap->scause;
		__entry->stval		= trap->stval;
		__entry->htval		= trap->htval;
		__entry->htinst		= trap->htinst;
	),

	TP_printk("SEPC:0x%lx, SCAUSE:0x%lx(%s/%s), STVAL:0x%lx, HTVAL:0x%lx, HTINST:0x%lx",
		__entry->sepc,
		__entry->scause,
		__print_symbolic((__entry->scause & CAUSE_IRQ_FLAG), kvm_riscv_trap_type),
		__print_symbolic(__entry->scause, kvm_riscv_trap_class),
		__entry->stval,
		__entry->htval,
		__entry->htinst)
);

TRACE_EVENT(kvm_guest_page_fault,
	TP_PROTO(struct kvm_cpu_trap *trap, unsigned long fault_addr),
	TP_ARGS(trap, fault_addr),

	TP_STRUCT__entry(
		__field(unsigned long, sepc)
		__field(unsigned long, scause)
		__field(unsigned long, fault_addr)
	),

	TP_fast_assign(
		__entry->sepc			= trap->sepc;
		__entry->scause			= trap->scause;
		__entry->fault_addr		= fault_addr;
	),

	TP_printk("fault_addr %#lx, sepc %#08lx, scause %#08lx(%s)",
		  __entry->fault_addr, __entry->sepc, __entry->scause
		  __print_symbolic(__entry->scause, kvm_riscv_trap_class))
);

TRACE_EVENT(kvm_irq_inject,
	TP_PROTO(struct aplic_irq *irqd, u32 source, bool level, bool inject),
	TP_ARGS(irqd, source, level, inject),

	TP_STRUCT__entry(
		__field(u32,	source)
		__field(u32,	target)
		__field(u32,	mode)
		__field(int,	level)
        __field(bool,	state)
	),

	TP_fast_assign(
		__entry->source	= source;
		__entry->target	= irqd->target;
		__entry->mode	= irqd->sourcecfg &	APLIC_SOURCECFG_SM_MASK;
		__entry->level	= level;
        __entry->state  = inject;
	),

	TP_printk("Inject interrupt, source: %d, target: %d, mode: %s, level: %d, state: %s",
			__entry->source, __entry->target,
			(__entry->mode == APLIC_SOURCECFG_SM_EDGE_RISE) ? "edge-rise" :
			(__entry->mode == APLIC_SOURCECFG_SM_EDGE_FALL) ? "edge-fall" :
			(__entry->mode == APLIC_SOURCECFG_SM_LEVEL_HIGH) ? "level-high" :
			(__entry->mode == APLIC_SOURCECFG_SM_LEVEL_LOW) ? "level-low" : "unknown",
			__entry->level, __entry->state ? "injected" : "non-injected")
);

#endif /* _TRACE_RSICV_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
