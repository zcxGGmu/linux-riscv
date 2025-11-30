// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Limited.
 * Copyright (C) 2025 RISC-V Foundation.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>

static volatile int signal_count = 0;

static void signal_handler(int sig, siginfo_t *info, void *uc)
{
	signal_count++;
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	int count = 0;

	/* Install signal handler */
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = signal_handler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	/* Check if we have vector support */
	if (!(getauxval(AT_HWCAP) & HWCAP_RISCV_V)) {
		printf("No vector support\n");
		exit(1);
	}

	/* Print initial message */
	printf("KERNEL-TEST running\n");

	/* Main loop */
	while (1) {
		count++;
		
		/* Do some work */
		volatile int sum = 0;
		for (int i = 0; i < 1000; i++) {
			sum += i;
		}
		
		/* Print progress every 100 iterations */
		if (count % 100 == 0) {
			printf("KERNEL-TEST: %d iterations, %d signals\n", count, signal_count);
		}
		
		/* Sleep briefly */
		usleep(10000); /* 10ms */
	}

	return 0;
}