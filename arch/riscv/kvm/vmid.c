// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_tlb.h>
#include <asm/kvm_vmid.h>

static unsigned long vmid_version = 1;
static unsigned long vmid_bits __ro_after_init;
static unsigned long num_vmids;
static unsigned long *vmid_map;
static DEFINE_RAW_SPINLOCK(vmid_lock);
static cpumask_t vmid_tlb_flush_pending;

static DEFINE_PER_CPU(atomic_long_t, active_vmid);
static DEFINE_PER_CPU(unsigned long, reserved_vmid);

static bool check_update_reserved_vmid(unsigned long vmid,
				       unsigned long newvmid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved VMID looking for a match.
	 * If we find one, then we can update our mm to use new VMID
	 * (i.e. the same VMID in the current_version) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old VMID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved VMID in a future
	 * version.
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_vmid, cpu) == vmid) {
			hit = true;
			per_cpu(reserved_vmid, cpu) = newvmid;
		}
	}

	return hit;
}

static void __flush_vmid(void)
{
	int i;
	unsigned long vmid;

	/* Must be called with vmid_lock held */
	lockdep_assert_held(&vmid_lock);

	/* Update the list of reserved VMIDs and the VMID bitmap. */
	bitmap_zero(vmid_map, num_vmids);

	/* Mark already active VMIDs as used */
	for_each_possible_cpu(i) {
		vmid = atomic_long_xchg_relaxed(&per_cpu(active_vmid, i), 0);
		/*
		 * If this CPU has already been through a rollover, but
		 * hasn't run another task in the meantime, we must preserve
		 * its reserved VMID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (vmid == 0)
			vmid = per_cpu(reserved_vmid, i);

		__set_bit(vmid, vmid_map);
		per_cpu(reserved_vmid, i) = vmid;
	}

	/* Mark VMID #0 as used because it is used at boot-time */
	__set_bit(0, vmid_map);

	/* Queue a TLB invalidation for each CPU on next vmid-switch */
	cpumask_setall(&vmid_tlb_flush_pending);
}

static unsigned long __new_vmid(struct kvm *kvm)
{
	static u32 cur_idx = 1;
	unsigned long vmid = kvm->arch.vmid.vmid;
	unsigned long asid, ver = READ_ONCE(vmid_version);

	/* Must be called with vmid_lock held */
	lockdep_assert_held(&vmid_lock);

	if (vmid != 0) {
		unsigned long newvmid = ver | vmid;

		/*
		 * If our current VMID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_vmid(vmid, newvmid))
			return newvmid;

		/*
		 * We had a valid VMID in a previous life, so try to
		 * re-use it if possible.
		 */
		if (!__test_and_set_bit(vmid, vmid_map))
			return newvmid;
	}

	/*
	 * Allocate a free VMID. If we can't find one then increment
	 * vmid_version and flush all VMIDs.
	 */
	asid = find_next_zero_bit(vmid_map, num_vmids, cur_idx);
	if (asid != num_vmids)
		goto set_vmid;

	/* We're out of VMIDs, so increment vmid_version */
	ver = vmid_version + 1;
	WRITE_ONCE(vmid_version, ver);

	/* Flush everything  */
	__flush_vmid();

	/* We have more VMIDs than CPUs, so this will always succeed */
	asid = find_next_zero_bit(vmid_map, num_vmids, 1);

set_vmid:
	__set_bit(asid, vmid_map);
	cur_idx = asid;
	return asid | ver;
}

void __init kvm_riscv_gstage_vmid_detect(void)
{
	/* Figure-out number of VMID bits in HW */
	csr_write(CSR_HGATP, (kvm_riscv_gstage_mode << HGATP_MODE_SHIFT) | HGATP_VMID);
	vmid_bits = csr_read(CSR_HGATP);
	vmid_bits = (vmid_bits & HGATP_VMID) >> HGATP_VMID_SHIFT;
	vmid_bits = fls_long(vmid_bits);
	csr_write(CSR_HGATP, 0);

	/* We polluted local TLB so flush all guest TLB */
	kvm_riscv_local_hfence_gvma_all();

	/* We don't use VMID bits if they are not sufficient */
	if ((1UL << vmid_bits) < num_possible_cpus())
		vmid_bits = 0;

	/* Pre-compute VMID details */
	if (vmid_bits) {
		num_vmids = 1 << vmid_bits;
	}

	/*
	 * Use VMID allocator only if number of HW VMIDs are
	 * at-least twice more than CPUs
	 */
	if (num_vmids > (2 * num_possible_cpus())) {
		vmid_map = bitmap_zalloc(num_vmids, GFP_KERNEL);
		if (!vmid_map)
			panic("Failed to allocate bitmap for %lu VMIDs\n",
			      num_vmids);

		__set_bit(0, vmid_map);

		pr_info("VMID allocator using %lu bits (%lu entries)\n",
			vmid_bits, num_vmids);
	} else {
		pr_info("VMID allocator disabled (%lu bits)\n", vmid_bits);
	}
}

unsigned long kvm_riscv_gstage_vmid_bits(void)
{
	return vmid_bits;
}

int kvm_riscv_gstage_vmid_init(struct kvm *kvm)
{
	/* Mark the initial VMID and VMID version invalid */
	kvm->arch.vmid.vmid_version = 0;
	kvm->arch.vmid.vmid = 0;

	return 0;
}

bool kvm_riscv_gstage_vmid_ver_changed(struct kvm_vmid *vmid)
{
	if (!vmid_bits)
		return false;

	return unlikely(READ_ONCE(vmid->vmid_version) !=
			READ_ONCE(vmid_version));
}

static void __local_hfence_gvma_all(void *info)
{
	kvm_riscv_local_hfence_gvma_all();
}

void kvm_riscv_gstage_vmid_update(struct kvm_vcpu *vcpu)
{
	unsigned long i;
	struct kvm_vcpu *v;
	struct kvm_vmid *vmid = &vcpu->kvm->arch.vmid;
	unsigned long flags;
	bool need_flush_tlb = false;
	unsigned long vmid_val, old_active_vmid;

	vmid_val = vmid->vmid;

	/*
	 * If our active_vmid is non-zero and the vmid matches the
	 * vmid_version, then we update the active_vmid entry with a
	 * relaxed cmpxchg.
	 *
	 * Following is how we handle racing with a concurrent rollover:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on the
	 *   lock. Taking the lock synchronises with the rollover and so
	 *   we are forced to see the updated version.
	 *
	 * - We get a valid vmid back from the cmpxchg then we continue
	 *   using old VMID because __flush_vmid() would have marked VMID
	 *   of active_vmid as used and next vmid switch we will
	 *   allocate new vmid.
	 */
	old_active_vmid = atomic_long_read(&per_cpu(active_vmid, vcpu->cpu));
	if (old_active_vmid &&
	    (vmid_val && vmid->vmid_version == READ_ONCE(vmid_version)) &&
	    atomic_long_cmpxchg_relaxed(&per_cpu(active_vmid, vcpu->cpu),
					old_active_vmid, vmid_val))
		goto update_vcpus;

	raw_spin_lock_irqsave(&vmid_lock, flags);

	/* Check that our VMID belongs to the current_version. */
	if (kvm_riscv_gstage_vmid_ver_changed(vmid)) {
		vmid_val = __new_vmid(vcpu->kvm);
		vmid->vmid = vmid_val;
		vmid->vmid_version = READ_ONCE(vmid_version);
	}

	if (cpumask_test_and_clear_cpu(vcpu->cpu, &vmid_tlb_flush_pending))
		need_flush_tlb = true;

	atomic_long_set(&per_cpu(active_vmid, vcpu->cpu), vmid_val);

	raw_spin_unlock_irqrestore(&vmid_lock, flags);

	if (need_flush_tlb)
		kvm_riscv_local_hfence_gvma_all();

update_vcpus:
	/* Request G-stage page table update for all VCPUs */
	kvm_for_each_vcpu(i, v, vcpu->kvm)
		kvm_make_request(KVM_REQ_UPDATE_HGATP, v);
}

void kvm_riscv_gstage_vmid_sanitize(struct kvm_vcpu *vcpu)
{
	unsigned long vmid;

	if (!kvm_riscv_gstage_vmid_bits() ||
	    vcpu->arch.last_exit_cpu == vcpu->cpu)
		return;

	/*
	 * On RISC-V platforms with hardware VMID support, we share same
	 * VMID for all VCPUs of a particular Guest/VM. This means we might
	 * have stale G-stage TLB entries on the current Host CPU due to
	 * some other VCPU of the same Guest which ran previously on the
	 * current Host CPU.
	 *
	 * To cleanup stale TLB entries, we simply flush all G-stage TLB
	 * entries by VMID whenever underlying Host CPU changes for a VCPU.
	 */

	vmid = READ_ONCE(vcpu->kvm->arch.vmid.vmid);
	kvm_riscv_local_hfence_gvma_vmid_all(vmid);
}