# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited
# Originally tools/testing/arm64/abi/Makefile

TEST_GEN_PROGS := \
	vstate_prctl \
	v_initval_nolibc \
	fp_vector_stress
TEST_GEN_PROGS_EXTENDED := \
	vstate_exec_nolibc \
	fp_test	\
	vector_test

include ../../lib.mk

$(OUTPUT)/vstate_prctl: vstate_prctl.c ../hwprobe/sys_hwprobe.S
	$(CC) -static -o$@ $(CFLAGS) $(LDFLAGS) $^

$(OUTPUT)/vstate_exec_nolibc: vstate_exec_nolibc.c
	$(CC) -nostdlib -static -include ../../../../include/nolibc/nolibc.h \
		-Wall $(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc

$(OUTPUT)/v_initval_nolibc: v_initval_nolibc.c
	$(CC) -nostdlib -static -include ../../../../include/nolibc/nolibc.h \
		-Wall $(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc

$(OUTPUT)/fp_vector_stress: fp_vector_stress.c
	$(CC) -static -o$@ $(CFLAGS) $(LDFLAGS) $^

$(OUTPUT)/fp-test: fp-test.S
	$(CC) -nostdlib -static -Wall \
		$(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc

$(OUTPUT)/vector-test: vector-test.S
	$(CC) -nostdlib -static -Wall \
		$(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc
