#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#define __NR_proc_dfs 548  /* 与 syscall_64.tbl 中编号一致 */

struct proc_dfs_info {
    pid_t pid;
    long state;
    char state_name[16];
    unsigned long utime;
    unsigned long stime;
    pid_t parent_pid;
    pid_t first_child_pid;
    pid_t next_sibling_pid;
};

int main(int argc, char **argv) {
    struct proc_dfs_info *buf;
    size_t entries = 512;
    pid_t top = getpid();

    if (argc > 1)
        top = (pid_t) atoi(argv[1]);

    buf = malloc(entries * sizeof(struct proc_dfs_info));
    if (!buf) {
        perror("malloc");
        return 1;
    }

    long ret = syscall(__NR_proc_dfs, top, buf, entries * sizeof(struct proc_dfs_info));
    if (ret < 0) {
        fprintf(stderr, "sys_proc_dfs failed: %s\n", strerror(-ret));
        free(buf);
        return 1;
    }

    printf("DFS traversal (%ld entries) for pid %d:\n", ret, (int)top);
    printf(" PID\tPPID\tCHILD\tSIBLING\tSTATE\t\tUTIME(s)\tSTIME(s)\n");
    for (int i = 0; i < ret; ++i) {
        double ut = buf[i].utime / 1e9;
        double st = buf[i].stime / 1e9;
        printf("%-5d\t%-5d\t%-5d\t%-7d\t%-10s\t%8.6f\t%8.6f\n",
            buf[i].pid,
            buf[i].parent_pid,
            buf[i].first_child_pid,
            buf[i].next_sibling_pid,
            buf[i].state_name,
            ut, st);
    }

    free(buf);
    return 0;
}