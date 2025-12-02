// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debug helper used to dump the G-stage pagetables of the system and their
 * associated permissions.
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 * Based on ARM64 implementation by Google, 2024
 * Author: RISC-V KVM Team
 */
#include <linux/debugfs.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>

#include <asm/kvm_host.h>
#include <asm/kvm_gstage.h>
#include <asm/pgtable.h>

/* Forward declarations for external variables and functions */
extern unsigned long kvm_riscv_gstage_mode;
extern unsigned long kvm_riscv_gstage_pgd_levels;

/*
 * Note page implementation adapted from ARM64
 * This function prints page table entry information
 */
void note_page(struct ptdump_state *pt_st, unsigned long addr, int level,
	       unsigned long val)
{
	struct ptdump_pg_state *st = container_of(pt_st, struct ptdump_pg_state, ptdump);
	struct ptdump_pg_level *pg_level = st->pg_level;
	static const char units[] = "KMGTPE";
	unsigned long prot = 0;
	int i;
	unsigned long delta;

	/* Only print leaf entries */
	if (level >= 0)
		prot = val & pg_level[level].mask;

	if (st->level == -1) {
		st->level = level;
		st->current_prot = prot;
		st->start_address = addr;
		seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	} else if (prot != st->current_prot || level != st->level) {
		note_page(pt_st, 0, -1, 0);
		st->level = level;
		st->current_prot = prot;
		st->start_address = addr;
	}

	if (st->level == -1)
		return;

	delta = addr - st->start_address;
	if (!delta)
		return;

	/* Calculate size and unit */
	for (i = 0; i < ARRAY_SIZE(units) - 1; i++) {
		if (delta & 0x3ff)
			break;
		delta >>= 10;
	}

	seq_printf(st->seq, "0x%09lx-0x%09lx   %9lu%c ",
		   st->start_address, addr - 1, delta, units[i]);

	/* Print protection bits */
	for (i = 0; i < pg_level[st->level].num; i++) {
		const char *s;

		if ((pg_level[st->level].bits[i].mask & prot) ==
		    pg_level[st->level].bits[i].val)
			s = pg_level[st->level].bits[i].set;
		else
			s = pg_level[st->level].bits[i].clear;

		seq_printf(st->seq, "%s", s);
	}

	seq_puts(st->seq, "\n");
}

#define MARKERS_LEN			2
#define KVM_GSTAGE_MAX_LEVELS	(kvm_riscv_gstage_pgd_levels)

struct kvm_gstage_ptdump_state {
	struct kvm			*kvm;
	struct ptdump_pg_state		parser_state;
	struct addr_marker		gpa_marker[MARKERS_LEN];
	struct ptdump_pg_level		level[KVM_GSTAGE_MAX_LEVELS];
	struct ptdump_range		range[MARKERS_LEN];
};

static const struct ptdump_prot_bits gstage_pte_bits[] = {
	{
		.mask	= _PAGE_PRESENT,
		.val	= _PAGE_PRESENT,
		.set	= "V",
		.clear	= "I",
	}, {
		.mask	= _PAGE_READ,
		.val	= _PAGE_READ,
		.set	= "R",
		.clear	= " ",
	}, {
		.mask	= _PAGE_WRITE,
		.val	= _PAGE_WRITE,
		.set	= "W",
		.clear	= " ",
	}, {
		.mask	= _PAGE_EXEC,
		.val	= _PAGE_EXEC,
		.set	= "X",
		.clear	= "NX",
	}, {
		.mask	= _PAGE_ACCESSED,
		.val	= _PAGE_ACCESSED,
		.set	= "A",
		.clear	= "  ",
	}, {
		.mask	= _PAGE_DIRTY,
		.val	= _PAGE_DIRTY,
		.set	= "D",
		.clear	= " ",
	}, {
		.mask	= _PAGE_GLOBAL,
		.val	= _PAGE_GLOBAL,
		.set	= "G",
		.clear	= " ",
	}, {
		.mask	= _PAGE_USER,
		.val	= _PAGE_USER,
		.set	= "U",
		.clear	= " ",
	}, {
		.mask	= _PAGE_LEAF,
		.val	= _PAGE_LEAF,
		.set	= "L",
		.clear	= " ",
	},
};

static int kvm_gstage_ptdump_visitor(const struct kvm_gstage_mapping *map,
				     void *arg)
{
	struct ptdump_pg_state *st = arg;
	struct ptdump_state *pt_st = &st->ptdump;
	unsigned long addr = map->addr;
	unsigned long pte = map->pte.val;
	u32 level = map->level;

	note_page(pt_st, addr, level, (pte_t){ pte });

	return 0;
}

static int kvm_gstage_build_levels(struct ptdump_pg_level *level, u32 start_lvl)
{
	u32 i;
	u64 mask;

	if (WARN_ON_ONCE(start_lvl >= KVM_GSTAGE_MAX_LEVELS))
		return -EINVAL;

	mask = 0;
	for (i = 0; i < ARRAY_SIZE(gstage_pte_bits); i++)
		mask |= gstage_pte_bits[i].mask;

	for (i = start_lvl; i < KVM_GSTAGE_MAX_LEVELS; i++) {
		snprintf(level[i].name, sizeof(level[i].name), "%u", i);

		level[i].num	= ARRAY_SIZE(gstage_pte_bits);
		level[i].bits	= gstage_pte_bits;
		level[i].mask	= mask;
	}

	return 0;
}

static struct kvm_gstage_ptdump_state *kvm_gstage_ptdump_parser_create(struct kvm *kvm)
{
	struct kvm_gstage_ptdump_state *st;
	struct kvm_gstage *gstage = &kvm->arch.gstage;
	int ret;

	st = kzalloc(sizeof(struct kvm_gstage_ptdump_state), GFP_KERNEL_ACCOUNT);
	if (!st)
		return ERR_PTR(-ENOMEM);

	ret = kvm_gstage_build_levels(&st->level[0], 0);
	if (ret) {
		kfree(st);
		return ERR_PTR(ret);
	}

	st->gpa_marker[0].name		= "Guest GPA";
	st->gpa_marker[1].start_address = BIT(kvm_riscv_gstage_gpa_bits);
	st->range[0].end			= BIT(kvm_riscv_gstage_gpa_bits);

	st->kvm = kvm;
	st->parser_state = (struct ptdump_pg_state) {
		.marker		= &st->gpa_marker[0],
		.level			= -1,
		.pg_level		= &st->level[0],
		.ptdump.range	= &st->range[0],
		.start_address	= 0,
	};

	return st;
}

/* Simple walker for G-stage page tables */
static int kvm_gstage_walk_ptes(struct kvm_gstage *gstage,
				 unsigned long addr,
				 unsigned long end,
				 int (*cb)(const struct kvm_gstage_mapping *, void *),
				 void *arg)
{
	unsigned long page_size = PAGE_SIZE;
	unsigned long current_addr = addr;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct kvm_gstage_mapping map;
	int ret = 0;

	while (current_addr < end) {
		pgd = gstage->pgd + pgd_index(current_addr);
		if (pgd_none(*pgd))
			goto next;

#ifdef __PAGETABLE_P4D_FOLDED
		p4d = (p4d_t *)pgd;
#else
		p4d = p4d_offset(pgd, current_addr);
		if (p4d_none(*p4d))
			goto next;
#endif

		pud = pud_offset(p4d, current_addr);
		if (pud_none(*pud))
			goto next;

		if (pud_leaf(*pud) && kvm_riscv_gstage_pgd_levels > 2) {
			map.addr = current_addr;
			map.pte = *pud_pte(*pud);
			map.level = KVM_GSTAGE_MAX_LEVELS - 3; /* PUD level */
			ret = cb(&map, arg);
			if (ret)
				break;
			current_addr += PUD_SIZE;
			continue;
		}

		pmd = pmd_offset(pud, current_addr);
		if (pmd_none(*pmd))
			goto next;

		if (pmd_leaf(*pmd) && kvm_riscv_gstage_pgd_levels > 2) {
			map.addr = current_addr;
			map.pte = *pmd_pte(*pmd);
			map.level = KVM_GSTAGE_MAX_LEVELS - 2; /* PMD level */
			ret = cb(&map, arg);
			if (ret)
				break;
			current_addr += PMD_SIZE;
			continue;
		}

		pte = pte_offset_kernel(pmd, current_addr);
		if (pte_none(*pte))
			goto next;

		map.addr = current_addr;
		map.pte = *pte;
		map.level = KVM_GSTAGE_MAX_LEVELS - 1; /* PTE level */
		ret = cb(&map, arg);
		if (ret)
			break;

next:
		current_addr += page_size;
	}

	return ret;
}

static int kvm_gstage_ptdump_guest_show(struct seq_file *m, void *unused)
{
	int ret;
	struct kvm_gstage_ptdump_state *st = m->private;
	struct kvm *kvm = st->kvm;
	struct ptdump_pg_state *parser_state = &st->parser_state;
	struct kvm_gstage gstage;

	/* Setup gstage structure for walk */
	gstage.kvm = kvm;
	gstage.pgd = kvm->arch.pgd;
	gstage.flags = 0;
	gstage.vmid = 0; /* VMID is not used for ptdump */

	parser_state->seq = m;

	write_lock(&kvm->mmu_lock);
	ret = kvm_gstage_walk_ptes(&gstage, 0, kvm_riscv_gstage_gpa_size,
				    kvm_gstage_ptdump_visitor, parser_state);
	write_unlock(&kvm->mmu_lock);

	return ret;
}

static int kvm_gstage_ptdump_guest_open(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;
	struct kvm_gstage_ptdump_state *st;
	int ret;

	if (!kvm_get_kvm_safe(kvm))
		return -ENOENT;

	st = kvm_gstage_ptdump_parser_create(kvm);
	if (IS_ERR(st)) {
		ret = PTR_ERR(st);
		goto err_with_kvm_ref;
	}

	ret = single_open(file, kvm_gstage_ptdump_guest_show, st);
	if (!ret)
		return 0;

	kfree(st);
err_with_kvm_ref:
	kvm_put_kvm(kvm);
	return ret;
}

static int kvm_gstage_ptdump_guest_close(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;
	void *st = ((struct seq_file *)file->private_data)->private;

	kfree(st);
	kvm_put_kvm(kvm);

	return single_release(inode, file);
}

static const struct file_operations kvm_gstage_ptdump_guest_fops = {
	.open		= kvm_gstage_ptdump_guest_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_gstage_ptdump_guest_close,
};

static int kvm_gstage_gpa_bits_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "%lu\n", kvm_riscv_gstage_gpa_bits);
	return 0;
}

static int kvm_gstage_levels_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "%d\n", KVM_GSTAGE_MAX_LEVELS);
	return 0;
}

static int kvm_gstage_debugfs_open(struct inode *inode, struct file *file,
				   int (*show)(struct seq_file *, void *))
{
	struct kvm *kvm = inode->i_private;
	int ret;

	if (!kvm_get_kvm_safe(kvm))
		return -ENOENT;

	ret = single_open(file, show, kvm);
	if (ret < 0)
		kvm_put_kvm(kvm);
	return ret;
}

static int kvm_gstage_gpa_bits_open(struct inode *inode, struct file *file)
{
	return kvm_gstage_debugfs_open(inode, file, kvm_gstage_gpa_bits_show);
}

static int kvm_gstage_levels_open(struct inode *inode, struct file *file)
{
	return kvm_gstage_debugfs_open(inode, file, kvm_gstage_levels_show);
}

static int kvm_gstage_debugfs_close(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;

	kvm_put_kvm(kvm);
	return single_release(inode, file);
}

static const struct file_operations kvm_gstage_gpa_bits_fops = {
	.open		= kvm_gstage_gpa_bits_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_gstage_debugfs_close,
};

static const struct file_operations kvm_gstage_levels_fops = {
	.open		= kvm_gstage_levels_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_gstage_debugfs_close,
};

void kvm_riscv_gstage_ptdump_create_debugfs(struct kvm *kvm)
{
	debugfs_create_file("gstage_page_tables", 0400, kvm->debugfs_dentry,
			    kvm, &kvm_gstage_ptdump_guest_fops);
	debugfs_create_file("gpa_range", 0400, kvm->debugfs_dentry, kvm,
			    &kvm_gstage_gpa_bits_fops);
	debugfs_create_file("gstage_levels", 0400, kvm->debugfs_dentry,
			    kvm, &kvm_gstage_levels_fops);
}