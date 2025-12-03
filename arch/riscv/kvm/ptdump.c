// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debug helper used to dump the G-stage pagetables of the system and their
 * associated permissions.
 *
 */
#include <linux/debugfs.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>

#include <asm/kvm_gstage.h>
#include <asm/kvm_mmu.h>
#include <asm/pgtable.h>
#include <linux/ptdump.h>

#define pt_dump_seq_printf(m, fmt, args...)	\
({						\
	if (m)					\
		seq_printf(m, fmt, ##args);	\
})

#define pt_dump_seq_puts(m, fmt)	\
({					\
	if (m)				\
		seq_puts(m, fmt);	\
})

/*
 * The page dumper groups page table entries of the same type into a single
 * description. It uses pg_state to track the range information while
 * iterating over the pte entries. When the continuity is broken it then
 * dumps out a description of the range.
 */
struct pg_state {
	struct ptdump_state ptdump;
	struct seq_file *seq;
	const struct addr_marker *marker;
	unsigned long start_address;
	int level;
	u64 current_prot;
};

/* Address marker */
struct addr_marker {
	unsigned long start_address;
	const char *name;
};

#define MARKERS_LEN		2

static const struct ptdump_prot_bits gstage_pte_bits[] = {
	{
		.mask	= _PAGE_PRESENT,
		.val	= _PAGE_PRESENT,
		.set	= "V",
		.clear	= ".",
	}, {
		.mask	= _PAGE_READ,
		.val	= _PAGE_READ,
		.set	= "R",
		.clear	= ".",
	}, {
		.mask	= _PAGE_WRITE,
		.val	= _PAGE_WRITE,
		.set	= "W",
		.clear	= ".",
	}, {
		.mask	= _PAGE_EXEC,
		.val	= _PAGE_EXEC,
		.set	= "X",
		.clear	= ".",
	}, {
		.mask	= _PAGE_USER,
		.val	= _PAGE_USER,
		.set	= "U",
		.clear	= ".",
	}, {
		.mask	= _PAGE_GLOBAL,
		.val	= _PAGE_GLOBAL,
		.set	= "G",
		.clear	= ".",
	}, {
		.mask	= _PAGE_ACCESSED,
		.val	= _PAGE_ACCESSED,
		.set	= "A",
		.clear	= ".",
	}, {
		.mask	= _PAGE_DIRTY,
		.val	= _PAGE_DIRTY,
		.set	= "D",
		.clear	= ".",
	}, {
		.mask	= _PAGE_SOFT,
		.val	= _PAGE_SOFT,
		.set	= "RSW(%d)",
		.clear	= "  ..  ",
	},
};

static void dump_prot(struct pg_state *st)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(gstage_pte_bits); i++) {
		char s[7];
		unsigned long val;

		val = st->current_prot & gstage_pte_bits[i].mask;
		if (val) {
			if (gstage_pte_bits[i].mask == _PAGE_SOFT)
				sprintf(s, gstage_pte_bits[i].set, val >> 8);
			else
				sprintf(s, "%s", gstage_pte_bits[i].set);
		} else {
				sprintf(s, "%s", gstage_pte_bits[i].clear);
		}

		pt_dump_seq_printf(st->seq, " %s", s);
	}
}

#ifdef CONFIG_64BIT
#define ADDR_FORMAT	"0x%016lx"
#else
#define ADDR_FORMAT	"0x%08lx"
#endif
static void dump_addr(struct pg_state *st, unsigned long addr)
{
	static const char units[] = "KMGTPE";
	const char *unit = units;
	unsigned long delta;

	pt_dump_seq_printf(st->seq, ADDR_FORMAT "-" ADDR_FORMAT "   ",
			  st->start_address, addr);

	delta = (addr - st->start_address) >> 10;

	while (!(delta & 1023) && unit[1]) {
		delta >>= 10;
		unit++;
	}

	pt_dump_seq_printf(st->seq, "%9lu%c", delta, *unit);
}

static void note_page(struct ptdump_state *pt_st, unsigned long addr,
		     int level, u64 val)
{
	struct pg_state *st = container_of(pt_st, struct pg_state, ptdump);
	u64 prot = 0;

	if (level >= 0)
		prot = val;

	if (st->level == -1) {
		st->level = level;
		st->current_prot = prot;
		st->start_address = addr;
		pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	} else if (prot != st->current_prot ||
		  level != st->level || addr >= st->marker[1].start_address) {
		if (st->current_prot) {
			dump_addr(st, addr);
			dump_prot(st);
			pt_dump_seq_puts(st->seq, "\n");
		}

		while (addr >= st->marker[1].start_address) {
			st->marker++;
			pt_dump_seq_printf(st->seq, "---[ %s ]---\n",
					  st->marker->name);
		}

		st->start_address = addr;
		st->current_prot = prot;
		st->level = level;
	}
}

static void note_page_pte(struct ptdump_state *pt_st, unsigned long addr, pte_t pte)
{
	note_page(pt_st, addr, 4, pte_val(pte));
}

static void note_page_pmd(struct ptdump_state *pt_st, unsigned long addr, pmd_t pmd)
{
	note_page(pt_st, addr, 3, pmd_val(pmd));
}

static void note_page_pud(struct ptdump_state *pt_st, unsigned long addr, pud_t pud)
{
	note_page(pt_st, addr, 2, pud_val(pud));
}

static void note_page_p4d(struct ptdump_state *pt_st, unsigned long addr, p4d_t p4d)
{
	note_page(pt_st, addr, 1, p4d_val(p4d));
}

static void note_page_pgd(struct ptdump_state *pt_st, unsigned long addr, pgd_t pgd)
{
	note_page(pt_st, addr, 0, pgd_val(pgd));
}

struct kvm_ptdump_guest_state {
	struct kvm		*kvm;
	struct addr_marker	ipa_marker[MARKERS_LEN];
	struct ptdump_range	range[MARKERS_LEN];
};

static struct kvm_ptdump_guest_state *kvm_ptdump_parser_create(struct kvm *kvm)
{
	struct kvm_ptdump_guest_state *st;

	st = kzalloc(sizeof(struct kvm_ptdump_guest_state), GFP_KERNEL_ACCOUNT);
	if (!st)
		return ERR_PTR(-ENOMEM);

	st->ipa_marker[0].name		= "Guest IPA";
	st->ipa_marker[1].start_address = kvm_riscv_gstage_gpa_size;
	st->range[0].end		= kvm_riscv_gstage_gpa_size;

	st->kvm = kvm;

	return st;
}

static int kvm_ptdump_guest_show(struct seq_file *m, void *unused)
{
	struct kvm_ptdump_guest_state *st = m->private;
	struct kvm *kvm = st->kvm;
	struct pg_state pg_st;
	struct kvm_gstage gstage;

	gstage.kvm = kvm;
	gstage.flags = KVM_GSTAGE_FLAGS_LOCAL;
	gstage.vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage.pgd = kvm->arch.pgd;

	pg_st = (struct pg_state) {
		.seq = m,
		.marker = &st->ipa_marker[0],
		.level = -1,
		.ptdump = {
			.note_page_pte = note_page_pte,
			.note_page_pmd = note_page_pmd,
			.note_page_pud = note_page_pud,
			.note_page_p4d = note_page_p4d,
			.note_page_pgd = note_page_pgd,
			.note_page_flush = NULL,
			.range = &st->range[0],
		}
	};

	spin_lock(&kvm->mmu_lock);
	ptdump_walk_pgd(&pg_st.ptdump, NULL, gstage.pgd);
	spin_unlock(&kvm->mmu_lock);

	return 0;
}

static int kvm_ptdump_guest_open(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;
	struct kvm_ptdump_guest_state *st;
	int ret;

	if (!kvm_get_kvm_safe(kvm))
		return -ENOENT;

	st = kvm_ptdump_parser_create(kvm);
	if (IS_ERR(st)) {
		ret = PTR_ERR(st);
		goto err_with_kvm_ref;
	}

	ret = single_open(file, kvm_ptdump_guest_show, st);
	if (!ret)
		return 0;

	kfree(st);
err_with_kvm_ref:
	kvm_put_kvm(kvm);
	return ret;
}

static int kvm_ptdump_guest_close(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;
	void *st = ((struct seq_file *)file->private_data)->private;

	kfree(st);
	kvm_put_kvm(kvm);

	return single_release(inode, file);
}

static const struct file_operations kvm_ptdump_guest_fops = {
	.open		= kvm_ptdump_guest_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_ptdump_guest_close,
};

static int kvm_pgtable_range_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "%2lu\n", kvm_riscv_gstage_gpa_bits);
	return 0;
}

static int kvm_pgtable_levels_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "%1lu\n", kvm_riscv_gstage_pgd_levels);
	return 0;
}

static int kvm_pgtable_debugfs_open(struct inode *inode, struct file *file,
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

static int kvm_pgtable_range_open(struct inode *inode, struct file *file)
{
	return kvm_pgtable_debugfs_open(inode, file, kvm_pgtable_range_show);
}

static int kvm_pgtable_levels_open(struct inode *inode, struct file *file)
{
	return kvm_pgtable_debugfs_open(inode, file, kvm_pgtable_levels_show);
}

static int kvm_pgtable_debugfs_close(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;

	kvm_put_kvm(kvm);
	return single_release(inode, file);
}

static const struct file_operations kvm_pgtable_range_fops = {
	.open		= kvm_pgtable_range_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_pgtable_debugfs_close,
};

static const struct file_operations kvm_pgtable_levels_fops = {
	.open		= kvm_pgtable_levels_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_pgtable_debugfs_close,
};

void kvm_gstage_ptdump_create_debugfs(struct kvm *kvm)
{
	debugfs_create_file("gstage_page_tables", 0400, kvm->debugfs_dentry,
			    kvm, &kvm_ptdump_guest_fops);
	debugfs_create_file("gpa_range", 0400, kvm->debugfs_dentry, kvm,
			    &kvm_pgtable_range_fops);
	debugfs_create_file("gstage_levels", 0400, kvm->debugfs_dentry,
			    kvm, &kvm_pgtable_levels_fops);
}