// SPDX-License-Identifier: GPL-2.0-only
#ifndef __ASM_PTDUMP_H
#define __ASM_PTDUMP_H

#ifdef CONFIG_PTDUMP_CORE

#include <linux/mm_types.h>
#include <linux/seq_file.h>
#include <linux/ptdump.h>

/* Address marker */
struct addr_marker {
	unsigned long start_address;
	const char *name;
};

/* Private information for debugfs */
struct ptd_mm_info {
	struct mm_struct		*mm;
	const struct addr_marker	*markers;
	unsigned long base_addr;
	unsigned long end;
};

/* Page Table Entry */
struct prot_bits {
	u64 mask;
	const char *set;
	const char *clear;
};

/* Page Level */
struct pg_level {
	const char *name;
	u64 mask;
};

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
	unsigned long start_pa;
	unsigned long last_pa;
	int level;
	u64 current_prot;
	bool check_wx;
	unsigned long wx_pages;
};

void ptdump_walk(struct seq_file *s, struct ptdump_info *info);
void note_page(struct ptdump_state *pt_st, unsigned long addr, int level,
	       u64 val);
#else
static inline void note_page(void *pt_st, unsigned long addr,
			     int level, u64 val) { }
#endif /* CONFIG_PTDUMP_CORE */

#endif /* __ASM_PTDUMP_H */
