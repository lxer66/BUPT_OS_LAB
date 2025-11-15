#ifndef _LINUX_EVENT_SYNC_H
#define _LINUX_EVENT_SYNC_H

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

#define MAX_EVENTS 256  /* 支持的最大事件数 */

/* 每个事件对应一个 event_entry 结构体 */
struct event_entry {
    int id;                  /* 事件 ID，表中索引 */
    int active;              /* 是否有效 (1 = 活跃，0 = 已关闭) */
    wait_queue_head_t wq;    /* 等待队列头，用于阻塞等待该事件的进程 */
    int signaled;            /* 触发标志 (0 = 未触发, 1 = 已触发) */
    spinlock_t lock;         /* 自旋锁，用于保护本结构体并发访问 */
};

#endif /* _LINUX_EVENT_SYNC_H */
