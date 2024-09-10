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
#include <asm/csr.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

#define kvm_riscv_trap_type \
	{1, "IRQ"}, 			\
	{0, "EXC"}

#define IRQ(x) { IRQ_##x, #x }
#define EXC(x) { EXC_##x, #x }

#define kvm_riscv_trap_class \
	IRQ(S_SOFT), IRQ(S_TIMER), IRQ(S_EXT), \
	IRQ(S_GEXT), IRQ(PMU_OVF), \
	EXC(INST_ACCESS), EXC(INST_ILLEGAL), \
	EXC(BREAKPOINT), EXC(LOAD_MISALIGNED), \
	EXC(LOAD_ACCESS), EXC(STORE_MISALIGNED), \
	EXC(STORE_ACCESS), EXC(SUPERVISOR_SYSCALL), \
	EXC(INST_GUEST_PAGE_FAULT), EXC(LOAD_GUEST_PAGE_FAULT), \
	EXC(VIRTUAL_INST_FAULT), EXC(STORE_GUEST_PAGE_FAULT)

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

	TP_printk("%s: SEPC:0x%lx, SCAUSE:0x%lx (%s), STVAL:0x%lx, HTVAL:0x%lx, HTINST:0x%lx",
		__print_symbolic((__entry->scause & CAUSE_IRQ_FLAG), kvm_riscv_trap_type),
		__entry->sepc,
		__entry->scause,
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

TRACE_EVENT(kvm_timer_save_state,
	TP_PROTO(unsigned long vcpu_id, u64 compare, u64 time),
	TP_ARGS(vcpu_id, compare, time),

	TP_STRUCT__entry(
		__field(	unsigned long,	vcpu_id	)
		__field(	u64,		compare	)
		__field(	u64,		time	)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->compare	= compare;
		__entry->time		= time;
	),

	TP_printk("vcpu: %ld, compare: %lld, time: %lld",
		  __entry->vcpu_id, __entry->compare, __entry->time)
);

TRACE_EVENT(kvm_timer_restore_state,
	TP_PROTO(unsigned long vcpu_id, u64 compare, u64 time),
	TP_ARGS(vcpu_id, compare, time),

	TP_STRUCT__entry(
		__field(	unsigned long,	vcpu_id	)
		__field(	u64,		compare	)
		__field(	u64,		time	)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->compare	= compare;
		__entry->time		= time;
	),

	TP_printk("vcpu: %ld, compare: %lld, time: %lld",
		  __entry->vcpu_id, __entry->compare, __entry->time)
);

#endif /* _TRACE_RSICV_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
