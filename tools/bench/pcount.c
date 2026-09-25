// pcount -- cmd args...: user-space hardware counters for cmd and every thread it starts.
// SPDX-License-Identifier: AGPL-3.0-or-later
#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const struct { const char *name; uint64_t config; } EVENTS[] = {
    {"instructions", PERF_COUNT_HW_INSTRUCTIONS},
    {"cycles", PERF_COUNT_HW_CPU_CYCLES},
    {"branch-misses", PERF_COUNT_HW_BRANCH_MISSES},
};
#define NEVENTS (sizeof EVENTS / sizeof EVENTS[0])

int main(int argc, char **argv) {
    int a = 1;
    if (a < argc && strcmp(argv[a], "--") == 0) a++;
    if (a >= argc) { fprintf(stderr, "usage: pcount -- cmd args...\n"); return 2; }
    int go[2];
    if (pipe(go)) { perror("pipe"); return 2; }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 2; }
    if (pid == 0) {
        char c;
        close(go[1]);
        if (read(go[0], &c, 1) < 0) _exit(127);
        execvp(argv[a], argv + a);
        perror("execvp");
        _exit(127);
    }
    close(go[0]);
    int fd[NEVENTS];
    for (size_t i = 0; i < NEVENTS; i++) {
        struct perf_event_attr at = {0};
        at.size = sizeof at;
        at.type = PERF_TYPE_HARDWARE;
        at.config = EVENTS[i].config;
        at.disabled = 1;
        at.enable_on_exec = 1;
        at.inherit = 1;
        at.exclude_kernel = 1;
        at.exclude_hv = 1;
        at.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        fd[i] = (int)syscall(SYS_perf_event_open, &at, pid, -1, -1, 0);
        if (fd[i] < 0) { perror(EVENTS[i].name); kill(pid, SIGKILL); return 2; }
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    close(go[1]);
    int status;
    struct rusage ru;
    if (wait4(pid, &status, 0, &ru) < 0) { perror("wait4"); return 2; }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    FILE *out = stderr;
    for (size_t i = 0; i < NEVENTS; i++) {
        uint64_t v[3] = {0};
        if (read(fd[i], v, sizeof v) != sizeof v) { perror("read"); return 2; }
        fprintf(out, "pcount %s %llu%s\n", EVENTS[i].name, (unsigned long long)v[0],
                v[2] < v[1] ? " multiplexed" : "");
    }
    fprintf(out, "pcount user-s %.3f\npcount sys-s %.3f\npcount wall-s %.3f\npcount maxrss-mb %.0f\n",
            ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6, ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6,
            (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9, ru.ru_maxrss / 1024.0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
