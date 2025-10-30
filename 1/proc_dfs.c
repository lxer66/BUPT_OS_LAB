#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/proc_dfs.h>
#include <linux/sched/cputime.h>
#include <linux/string.h>
#include <linux/rcupdate.h>
#include <linux/printk.h>

/* 将内核 state 转为可读字符串 */
static const char *get_task_state_name(long state)
{
    /* 优先判断常见单一状态 */
    if (state == TASK_RUNNING) return "RUNNING";
    if (state == TASK_INTERRUPTIBLE) return "SLEEPING";
    if (state == TASK_UNINTERRUPTIBLE) return "WAITING";
    if (state == __TASK_STOPPED) return "STOPPED";
    if (state == __TASK_TRACED) return "TRACED";
    /* 按位判断一些组合或特殊状态 */
    if (state & EXIT_ZOMBIE) return "ZOMBIE";
    if (state & EXIT_DEAD) return "DEAD";
    if (state & TASK_DEAD) return "DEAD";
    if (state & TASK_WAKEKILL) return "WAKEKILL";
    if (state & TASK_WAKING) return "WAKING";
    if (state & TASK_PARKED) return "PARKED";
    return "UNKNOWN";
}

/* DFS traversal: fill kbuf with info, stop when count >= max_count */
static void dfs_task(struct task_struct *task,
                     struct proc_dfs_info *kbuf,
                     int *count,
                     int max_count)
{
    struct task_struct *child;
    struct list_head *list;

    if (*count >= max_count)
        return;

    /* 填充当前进程记录 */
    struct proc_dfs_info *info = &kbuf[*count];
    info->pid = task->pid;

    /* 读取状态（使用 __state 或 state，根据内核版本） */
    info->state = READ_ONCE(task->__state);

    /* 复制状态名，确保以 '\0' 结尾 */
    strncpy(info->state_name, get_task_state_name(info->state), sizeof(info->state_name));
    info->state_name[sizeof(info->state_name) - 1] = '\0';

    /* 获取精确的用户态和内核态时间（Linux 6.8+ 接口） */
    {
        u64 ut, st;
        /* task_cputime 返回 true/false，但我们只关心输出值 */
        task_cputime(task, &ut, &st);
        info->utime = (unsigned long)ut;
        info->stime = (unsigned long)st;
    }

    info->parent_pid = task->real_parent ? task->real_parent->pid : 0;

    if (list_empty(&task->children))
        info->first_child_pid = 0;
    else
        info->first_child_pid = list_first_entry(&task->children, struct task_struct, sibling)->pid;

    if (task->real_parent && !list_is_last(&task->sibling, &task->real_parent->children))
        info->next_sibling_pid = list_next_entry(task, sibling)->pid;
    else
        info->next_sibling_pid = 0;

    (*count)++;

    /* 递归遍历子进程 */
    list_for_each(list, &task->children) {
        child = list_entry(list, struct task_struct, sibling);
        dfs_task(child, kbuf, count, max_count);
    }
}

/* 系统调用实现 */
SYSCALL_DEFINE3(proc_dfs, pid_t, top_pid, struct proc_dfs_info __user *, buf, size_t, buf_size)
{
    struct task_struct *task;
    struct proc_dfs_info *kbuf;
    int max_count;
    int count = 0;
    int ret = 0;

    if (!buf || buf_size == 0)
        return -EINVAL;

    max_count = buf_size / sizeof(struct proc_dfs_info);
    if (max_count <= 0)
        return -EINVAL;

    /* 查找目标进程（在 RCU 读取上下文中） */
    rcu_read_lock();
    task = find_task_by_vpid(top_pid);
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }
    /* 增加引用，避免 task 在我们访问时被释放 */
    get_task_struct(task);
    rcu_read_unlock();

    kbuf = kmalloc(buf_size, GFP_KERNEL);
    if (!kbuf) {
        put_task_struct(task);
        return -ENOMEM;
    }

    dfs_task(task, kbuf, &count, max_count);

    /* 拷贝回用户空间 */
    if (copy_to_user(buf, kbuf, (size_t)count * sizeof(struct proc_dfs_info)))
        ret = -EFAULT;
    else
        ret = count;

    kfree(kbuf);
    put_task_struct(task);
    return ret;
}
