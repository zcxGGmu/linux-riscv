#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <asm/hwcap.h>

#include "../hwprobe/hwprobe.h"
#include "../../kselftest.h"

struct child_data {
    char *name, *output;
    pid_t pid;
    int stdout;
    bool output_seen;
    bool exited;
    int exit_status;
};

static int epoll_fd;
static struct child_data *children;
static struct epoll_event *evs;
static int tests;
static int num_children;
static bool terminate;

static int startup_pipe[2];

static int num_processors(void)
{
    long nproc = sysconf(_SC_NPROCESSORS_CONF);
    if (nproc < 0) {
        perror("Unable to read number of processors\n");
        exit(EXIT_FAILURE);
    }
    
    return nproc;
}

static const struct option options[] = {
    { "timeout", required_argument, NULL, 't' },
    { }
};

int main(int argc, char **argv)
{
    int ret;
    int timeout = 10;
    int cpus, i, j, c;
    bool all_children_started = false;
    int seen_children;
    bool have_fp, have_vector;
    int flag;
    struct sigaction sa;
    struct riscv_hwprobe pair;

    while ((c = getopt_long(argc, argv, "t:", options, NULL)) != -1) {
        switch (c) {
            case 't':
                ret = sscanf(optarg, "%d", &timeout);
                if (ret != 1)
                    ksft_exit_fail_msg("Failed to parse timeout %s\n",
                                        optarg);
                break;
            default:
                ksft_exit_fail_msg("unknown argument\n");
        }
    }

    cpus = num_processors();
    tests = 0;

    /** riscv hwprobe: EXT_{V/FD} */
    pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
    ret = riscv_hwprobe(&pair, 1, 0, NULL, 0);
    if (ret < 0) {
        ksft_test_result_fail("hwprobe() failed with %ld\n", ret);
        return -1;
    }

    if (pair.key != RISCV_HWPROBE_KEY_IMA_EXT_0) {
        ksft_test_result_fail("hwprobe cannot probe RISCV_HWPROBE_KEY_IMA_EXT_0\n");
        return -1;
    }
    
    have_fp = pair.value & RISCV_HWPROBE_IMA_FD ? true : false;
    have_vector = pair.value & RISCV_HWPROBE_IMA_V ? true : false;
    if (!have_fp && !have_vector) {
        ksft_test_result_skip("FD/Vcetor not supported\n");
        return 0;
    }
    
    // have_fp => fp_test.S
    if (have_fp) {
        // TODO
    }

    // have_vector => fp_vector.S
    if (have_vector) {
        /* Turn on next's vector explicitly and inherit */
        flag = PR_RISCV_V_VSTATE_CTRL_ON << PR_RISC_V_VSTATE_CTRL_NEXT_SHIFT;
        flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
        ret = prctl(PR_RISCV_V_SET_CONTROL, flag);
        if (ret != 0) {
            ksft_test_result_fail("prctl with flags arg %lx failed with code %d\n",
                flag, ret);
            return -1;
        }
        // enable next's vector explicitly and test inherit ???
        // TODO
    }
    
    return 0;
}

