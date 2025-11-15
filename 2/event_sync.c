#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/event_sync.h>

static struct event_entry *events[MAX_EVENTS];
static DEFINE_SPINLOCK(events_lock);

/* 创建一个新的事件，返回其 ID */
SYSCALL_DEFINE0(eventopen)
{
    int id = -1;
    struct event_entry *ev;

    /* 为新的 event_entry 分配内存 */
    ev = kmalloc(sizeof(*ev), GFP_KERNEL);
    if (!ev)
        return -ENOMEM;

    /* 在全局事件表中寻找空槽，保护操作使用 events_lock */
    spin_lock(&events_lock);
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (!events[i]) {
            id = i;
            events[i] = ev;
            break;
        }
    }
    spin_unlock(&events_lock);

    /* 如果没有空位，释放内存并返回错误 */
    if (id < 0) {
        kfree(ev);
        return -ENOSPC;
    }

    /* 初始化事件结构 */
    ev->id = id;
    ev->active = 1;          /* 标记为有效 */
    ev->signaled = 0;        /* 尚未触发 */
    init_waitqueue_head(&ev->wq);  /* 初始化等待队列头 */
    spin_lock_init(&ev->lock);     /* 初始化该事件自身的锁 */

    return id;
}

/* 关闭指定 ID 的事件，并唤醒所有等待者 */
SYSCALL_DEFINE1(eventclose, int, id)
{
    struct event_entry *ev;

    /* 检查 id 是否有效 */
    if (id < 0 || id >= MAX_EVENTS)
        return -EINVAL;

    /* 从全局表中取出事件，并清空槽位 */
    spin_lock(&events_lock);
    ev = events[id];
    events[id] = NULL;
    spin_unlock(&events_lock);

    /* 若该 ID 没有对应事件，返回错误 */
    if (!ev)
        return -ENOENT;

    /* 锁住该事件结构，安全修改其状态 */
    spin_lock(&ev->lock);
    ev->active = 0;       /* 标记为无效 (已关闭) */
    ev->signaled = 1;     /* 强制标记为已触发，表示唤醒条件满足 */
    wake_up_all(&ev->wq); /* 唤醒所有在该等待队列中的进程 */
    spin_unlock(&ev->lock);

    /* 释放内存，销毁事件 */
    kfree(ev);
    return 0;
}

/* 等待事件被触发 (或关闭) */
SYSCALL_DEFINE1(eventwait, int, id)
{
    struct event_entry *ev;

    /* 检查事件 id 范围是否合法 */
    if (id < 0 || id >= MAX_EVENTS)
        return -EINVAL;

    /* 读取事件指针 (不锁住事件本身) */
    spin_lock(&events_lock);
    ev = events[id];
    spin_unlock(&events_lock);

    /* 检查事件是否存在且处于活动状态 */
    if (!ev || !ev->active)
        return -ENOENT;

    /* 等待直到 signaled 为真 或者事件被关闭 (active 被置0) */
    wait_event_interruptible(ev->wq, ev->signaled || !ev->active);

    return 0;
}

/* 触发事件，将其标记为已触发，并唤醒所有等待者 */
SYSCALL_DEFINE1(eventsig, int, id)
{
    struct event_entry *ev;

    /* 检查 id 是否合理 */
    if (id < 0 || id >= MAX_EVENTS)
        return -EINVAL;

    /* 获取事件指针 */
    spin_lock(&events_lock);
    ev = events[id];
    spin_unlock(&events_lock);

    if (!ev)
        return -ENOENT;

    /* 加锁事件自身状态，进行触发操作 */
    spin_lock(&ev->lock);
    ev->signaled = 1;            /* 标记为已触发 */
    wake_up_all(&ev->wq);        /* 唤醒所有在等待队列上的进程 */
    spin_unlock(&ev->lock);

    return 0;
}
