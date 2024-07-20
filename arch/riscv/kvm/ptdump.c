// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 ISCAS
 * Author: Quan Zhou <zhouquan@iscas.ac.cn>
 */

#include <linux/debugfs.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>
#include <asm/pgtable.h>

#include "kvm_ptdump.h"

static const struct file_operations kvm_riscv_ptdump_fops = {
	.open		= kvm_riscv_ptdump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_riscv_ptdump_close,
};

void kvm_riscv_ptdump_register(struct kvm *kvm)
{
    debugfs_create_file("gstage_page_tables", 0400, kvm->debugfs_dentry,
                    kvm, &kvm_riscv_ptdump_fops);
    //TODO    
}
