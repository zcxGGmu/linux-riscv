// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 ISCAS
 * Author: Quan Zhou <zhouquan@iscas.ac.cn>
 */

#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>
#include <asm/kvm_aia.h>
#include <asm/kvm_aia_aplic.h>
#include <asm/kvm_aia_imsic.h>

/*
 * Structure to control looping through the entire aia state.  We start at
 * zero for each field and move upwards.  So, if aplic_id is 0 we print the
 * aplic info.  When aplic_id is 1, we have already printed it and move
 * on.
 *
 * When vcpu_id < nr_cpus we print the vcpu info until vcpu_id == nr_cpus and
 * so on.
 */
struct aia_state_iter {
    int nr_harts;
    int nr_ids; //number of msis
    int vcpu_id;
    int guest_id;  
};

static void *kvm_aia_debug_start(struct seq_file *s, loff_t *pos)
{
    //TODO
}

static void *kvm_aia_debug_next(struct seq_file *s, void *v, loff_t *pos)
{
    //TODO
}

static void kvm_aia_debug_stop(struct seq_file *s, void *v)
{
    //TODO
}

static int kvm_aia_debug_show(struct seq_file *s, void *v)
{
    //TODO
}

static const struct seq_operations kvm_aia_debug_sops = {
    .start = kvm_aia_debug_start,
    .next  = kvm_aia_debug_next,
    .stop  = kvm_aia_debug_stop,
    .show  = kvm_aia_debug_show,
}

DEFINE_SEQ_ATTRIBUTE(kvm_aia_debug);

void kvm_aia_debug_init(struct kvm *kvm)
{
    debugfs_create_file("aia-state", 0444, kvm->debugfs_dentry, kvm,
                        &kvm_aia_debug_sops);    
}

void kvm_aia_debug_destroy(struct kvm *kvm)
{
}
