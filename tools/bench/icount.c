/* icount [-q] command [args...]: user-space instructions of a command, its threads and children, to stderr (-q: the
 * number only). Exits as the command did, 1 if killed; SIGTERM/INT/HUP SIGKILL it. Build: cc -O2 -o icount icount.c */
#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile pid_t child = 0;

static void StopChild(int sig)
{
    (void)sig;
    if (child > 0)
        kill(child, SIGKILL);
}

int main(int argc, char** argv)
{
    int quiet = 0, first = 1;
    if ((argc > 1) && !strcmp(argv[1], "-q"))
    {
        quiet = 1;
        first = 2;
    }
    if (first >= argc)
    {
        fprintf(stderr, "usage: icount [-q] command [args...]\n");
        return 2;
    }
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof pe);
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof pe;
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    pe.disabled = 1;
    pe.inherit = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.enable_on_exec = 1;
    int fd = syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
    if (fd < 0)
    {
        perror("perf_event_open");
        return 3;
    }
    pid_t pid = fork();
    if (pid == 0)
    {
        execvp(argv[first], argv + first);
        perror("exec");
        _exit(127);
    }
    child = pid;
    signal(SIGTERM, StopChild);
    signal(SIGINT, StopChild);
    signal(SIGHUP, StopChild);
    int status;
    while ((waitpid(pid, &status, 0) < 0) && (errno == EINTR))
        ;
    uint64_t n = 0;
    if (read(fd, &n, sizeof n) != sizeof n)
    {
        perror("read");
        return 4;
    }
    if (quiet)
        fprintf(stderr, "%llu\n", (unsigned long long)n);
    else
        fprintf(stderr, "instructions: %llu\n", (unsigned long long)n);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
