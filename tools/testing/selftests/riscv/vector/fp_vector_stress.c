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
    struct sigaction sa;

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
    

}

