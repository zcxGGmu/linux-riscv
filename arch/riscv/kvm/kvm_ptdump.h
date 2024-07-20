// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 ISCAS
 * Author: Quan Zhou <zhouquan@iscas.ac.cn>
 */

#ifndef __KVM_PTDUMP_H
#define __KVM_PTDUMP_H

#include <linux/kvm_host.h>
#include <asm/ptdump.h>

#ifdef CONFIG_PTDUMP_GSTAGE_DEBUGFS
void kvm_riscv_ptdump_register(struct kvm *kvm);
#else
static inline void kvm_riscv_ptdump_register(struct kvm *kvm) {}
#endif /* CONFIG_PTDUMP_GSTAGE_DEBUGFS */

#endif /* __KVM_PTDUMP_H */
