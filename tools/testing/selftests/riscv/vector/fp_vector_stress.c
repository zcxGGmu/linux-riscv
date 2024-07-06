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

/** Child Operations... */
static void child_start(struct child_data *child, const char *program)
{
    int ret, pipefd[2], i;
    struct epoll_event ev;

    ret = pipe(pipefd);
    if (ret != 0)
        ksft_exit_fail_msg("Failed to create stdout pipe: %s (%d)\n",
                    strerror(errno), errno);

    child->pid = fork();
    if (child->pid == -1)
        ksft_exit_fail_msg("Failed to create stdout pipe: %s (%d)\n",
                    strerror(errno), errno);

    if (!child->pid) {
        /**
         *  In child, replace stdout with the pipe, errors to
         *  stderr from here as kselftest prints to stdout.
         */
        ret = dup2(pipefd[1], 1);
        if (ret == -1) {
            fprintf(stderr, "dup2() %d\n", errno);
            exit(EXIT_FAILURE);
        }

        /**
         *  Duplicate the read side of the startup pipe to
         *  FD 3 so we can close everything else.
         */
        ret = dup2(startup_pipe[0], 3);
        if (ret == -1) {
            fprintf(stderr, "dup2() %d\n", errno);
            exit(EXIT_FAILURE);
        }

        /**
         *  Very dumb mechanism to clean open FDs other than
         *  stdio. We don't want O_CLOEXEC for the pipes...
         */
        for (i = 4; i < 8192; i++)
            close(i);

        /**
         *  Read from the startup pipe, there should be no data
         *  and we should block until it is closed. We just
         *  carry on on error since this isn't super critical.
         */
        ret = read(3, &i, sizeof(i));
        if (ret < 0)
            fprintf(stderr, "read(startup_pipe) failed: %s (%d)\n",
                strerror(errno), errno);
        if (ret > 0)
            fprintf(stderr, "%d bytes of data on startup pipe\n",
                ret);
        close(3);
        
        ret = execl(program, program, NULL);
        fprintf(stderr, "execl(%s) failed: %d (%s)\n",
            program, errno, strerror(errno));
        
        exit(EXIT_FAILURE);
    } else {
        /**
         *  In parent, remember the child and close our copy of the
         *  write side of stdout.
         */
        close(pipefd[1]);
        child->stdout = pipefd[0];
        child->output = NULL;
        child->exited = false;
        child->output_seen = false;
        
        ev.events = EPOLLIN | EPOLLHUP;
        ev.data.ptr = child;
        
        ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, child->stdout, &ev);
        if (ret < 0) {
            ksft_exit_fail_msg("%s EPOLL_CTL_ADD failed: %s (%d)\n",
                        child->name, strerror(errno), errno);
        }
    }
}

static void child_stop(struct child_data *child)
{
    if (!child->exited)
        kill(child->pid, SIGTERM);
}

static bool child_output_read(struct child_data *child)
{
    
}

static void child_output(struct child_data *child, uint32_t events,
                bool flush)
{
    
}

static void child_tickle(struct child_data *child)
{
    if (child->output_seen && !child->exited)
        kill(child->pid, SIGUSR2);
}

static void start_fp(struct child_data *child, int cpu)
{
    int ret;
    
    ret = asprintf(&child->name, "FP-%d", cpu);
    if (ret = -1)
        ksft_exit_fail_msg("asprintf() failed\n");

    child_start(child, "./fp-test");

    ksft_print_msg("Started %s\n", child->name);    
}

static void start_vector(struct child_data *child, int cpu)
{
    int ret;

    /* Turn on next's vector explicitly and inherit */
    flag = PR_RISCV_V_VSTATE_CTRL_ON << PR_RISC_V_VSTATE_CTRL_NEXT_SHIFT;
    flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;

    ret = prctl(PR_RISCV_V_SET_CONTROL, flag);
    if (ret != 0) {
        ksft_test_result_fail("prctl with flags arg %lx failed with code %d\n",
                    flag, ret);
        exit(-1);
    }

    ret = asprintf(&child->name, "Vector-%d", cpu);
    if (ret = -1)
        ksft_exit_fail_msg("asprintf() failed\n");

    child_start(child, "./vector-test");

    ksft_print_msg("Started %s\n", child->name);
}

/** Signal Handler... */
static void handle_exit_signal(int sig, siginfo_t *info, void *context)
{
    int i;

    /** If we're already exiting then don't signal again */
    if (terminate)
        return;

    ksft_print_msg("Got signal, exiting...\n");

    terminate = true;

    /**
     *  This should be redundant, the main loop should clean up
     *  after us, but for safety stop everything we can here.
     */
    for (i = 0; i < num_children; i++)
        child_stop(&children[i]);
}

static void handle_child_signal(int sig, siginfo_t *info, void *context)
{
    int i;
    bool found = false;
    
    for (i = 0; i < num_children; i++) {
        if (children[i].pid == info->si_pid) {
            children[i].exited = true;
            children[i].exit_status = info->si_status;
            found = true;
            break;
        }
    }

    if (!found)
        ksft_print_msg("SIGCHLD for unknown PID %d with status %d\n",
                    info->si_pid, info->si_status);
}

/* Handle any pending output without blocking */
static void drain_output(bool flush)
{

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
    tests = cpus;

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
    
    ksft_print_header();
    kset_set_plans(tests);
    
    ksft_print_msg("CPUS %d, FP %s, Vector %s\n", cpus,
        have_fp ? "present" : "absent",
        have_vector ? "present" : "absent");

    if (timeout > 0)
        ksft_print_msg("Will run for %ds\n", timeout);
    else
        ksft_print_msg("Will run until terminated\n");
    
    children = calloc(sizeof(*children), tests);
    if (!children)
        ksft_exit_fail_msg("Unable to allocate child data\n");

    ret = epoll_create1(EPOLL_CLOEXEC);
    if (ret < 0)
        ksft_exit_fail_msg("epoll_create1() failed: %s (%d)\n",
                strerror(errno), ret);
    epoll_fd = ret;

    /** Create a pipe which children will block on before execing */
    ret = pipe(startup_pipe);
    if (ret != 0)
        ksft_exit_fail_msg("Failed to create startup pipe: %s (%d)\n",
                strerror(errno), errno);

    /** Get signal handlers ready before we start any children */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handle_exit_signal;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    
    ret = sigaction(SIGINT, &sa, NULL);
    if (ret < 0)
        ksft_print_msg("Failed to install SIGINT handler: %s (%d)",
                strerror(errno), errno);

    ret = sigaction(SIGTERM, &sa, NULL);
    if (ret < 0)
        ksft_print_msg("Failed to install SIGTERM handler: %s (%d)",
                strerror(errno), errno);

    sa.sa_sigaction = handle_child_signal;    
    ret = sigaction(SIGCHLD, &sa, NULL);
    if (ret < 0)
        ksft_print_msg("Failed to install SIGCHLD handler: %s (%d)",
                strerror(errno), errno);
    
    evs = calloc(tests, sizeof(*evs));
    if (!evs)
        ksft_exit_fail_msg("Failed to allocated %d epoll events\n",
                        tests);

    for (i = 0; i < cpus; i++) {
        // have_fp => fp_test.S
        if (have_fp)
            start_fp(&children[num_children++], i);
        // have_vector => fp_vector.S
        if (have_vector)
            start_vector(&children[num_children++], i);
    }

    /**
     *  All chilren started, close the startup pipe and let
     *  them run.
     */
    close(startup_pipe[0]);
    close(startup_pipe[1]);

    for (;;) {
        /* Did We get a signal asking us to exit? */
        if (terminate)
            break;

		/*
		 * Timeout is counted in seconds with no output, the
		 * tests print during startup then are silent when
		 * running so this should ensure they all ran enough
		 * to install the signal handler, this is especially
		 * useful in emulation where we will both be slow and
		 * likely to have a large fp/vector test.
		 */
        ret = epoll_wait(epoll_fd, evs, tests, 1000);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            ksft_exit_fail_msg("epoll_wait() failed: %s (%d)\n",
                        strerror(errno), errno);
        }

        /* Output? */
        if (ret > 0) {
            for (i = 0; i < ret; i++) {
                child_output(evs[i].data.ptr, evs[i].events,
                        false);
            }
            continue;
        }

        /* Otherwise epoll_wait() timed out */

		/*
		 * If the child processes have not produced output they
		 * aren't actually running the tests yet .
		 */
        if (!all_children_started) {
            seen_children = 0;
            
            for (i = 0; i < num_children; i++)
                if (children[i].output_seen ||
                    children[i].exited)
                    seen_children++;

            if (seen_children != num_children) {
                ksft_print_msg("Waiting for %d children\n",
                            num_children - seen_children);
                continue;
            }

            all_children_started = true;
        }

        ksft_print_msg("Sending signals, timeout remaining: %d\n",
                    timeout);

        for (i = 0; i < num_children; i++)
            child_tickle(&children[i]);

        /* Negative timeout means run indefinitely */
        if (timeout < 0)
            continue;
        if (--timeout == 0)
            break;
    }

    ksft_print_msg("Finshing up...\n");    
    terminate = true;
    
    for (i = 0; i < tests; i++)
        child_stop(&children[i]);
    drain_output(false);

    for (i = 0; i < tests; i++)
        child_cleanup(&children[i]);
    drain_output(true);

    ksft_print_cnts();

    return 0;
}

