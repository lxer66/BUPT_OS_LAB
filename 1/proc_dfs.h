#ifndef _LINUX_PROC_DFS_H
#define _LINUX_PROC_DFS_H

#include <linux/types.h>

struct proc_dfs_info {
    pid_t pid;
    long state;                 /* 内核状态位掩码 */
    char state_name[16];        /* 可读状态名（NUL 结尾） */
    unsigned long utime;        /* 用户态 CPU 时间（纳秒）或总运行时间 */
    unsigned long stime;        /* 内核态 CPU 时间（纳秒） */
    pid_t parent_pid;
    pid_t first_child_pid;
    pid_t next_sibling_pid;
};

#endif /* _LINUX_PROC_DFS_H */
