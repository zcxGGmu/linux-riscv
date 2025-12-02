/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 * Based on ARM64 implementation
 */

#ifndef __ASM_RISCV_PTDUMP_H
#define __ASM_RISCV_PTDUMP_H

#include <linux/ptdump.h>

#ifdef CONFIG_PTDUMP

#include <linux/mm_types.h>
#include <linux/seq_file.h>

struct addr_marker {
	unsigned long start_address;
	char *name;
};

struct ptdump_prot_bits {
	unsigned long mask;
	unsigned long val;
	const char *set;
	const char *clear;
};

struct ptdump_pg_level {
	const struct ptdump_prot_bits *bits;
	char name[4];
	int num;
	unsigned long mask;
};

/*
 * The page dumper groups page table entries of the same type into a single
 * description. It uses pg_state to track the range information while
 * iterating over the pte entries. When the continuity is broken it then
 * dumps out a description of the range.
 */
struct ptdump_pg_state {
	struct ptdump_state ptdump;
	struct ptdump_pg_level *pg_level;
	struct seq_file *seq;
	const struct addr_marker *marker;
	const struct mm_struct *mm;
	unsigned long start_address;
	int level;
	unsigned long current_prot;
	bool check_wx;
	unsigned long wx_pages;
	unsigned long x_pages;
};

void note_page(struct ptdump_state *pt_st, unsigned long addr, int level,
	       unsigned long val);

#else
static inline void note_page(struct ptdump_state *pt_st, unsigned long addr,
			     int level, unsigned long val) { }
#endif /* CONFIG_PTDUMP */

#endif /* __ASM_RISCV_PTDUMP_H */