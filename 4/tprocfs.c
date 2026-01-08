#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("tprocfs: A pseudo filesystem mirroring process tree");

// === 宏定义 ===
// 魔数定义：标识文件系统类型的唯一 ID
// 0x54504301 对应 ASCII 码 "TPC" + 0x01，符合十六进制规范
#define TPROCFS_MAGIC 0x54504301 
#define TM_STATUS_FILE "status"

/**
 * get_task_state_name - 将内核状态位掩码转换为可读字符串
 * @state: 进程的状态位 (task->__state)
 * 
 * Linux 内核使用位图表示进程状态，这里需要逐一判断。
 */
static const char *get_task_state_name(long state)
{
    if (state == TASK_RUNNING) return "RUNNING";
    if (state == TASK_INTERRUPTIBLE) return "SLEEPING"; // 可中断睡眠
    if (state == TASK_UNINTERRUPTIBLE) return "WAITING"; // 不可中断睡眠
    if (state == __TASK_STOPPED) return "STOPPED";
    if (state == __TASK_TRACED) return "TRACED";
    if (state & EXIT_ZOMBIE) return "ZOMBIE";
    if (state & EXIT_DEAD) return "DEAD";
    return "UNKNOWN";
}

/**
 * tproc_show - seq_file 的输出回调函数
 * @m: seq_file 句柄，用于输出内容
 * @v: 迭代器指针（本例未使用）
 * 
 * 当用户执行 cat /tproc/<PID>/status 时，内核会调用此函数生成文件内容。
 */
static int tproc_show(struct seq_file *m, void *v)
{
    struct task_struct *task;
    struct task_struct *child;
    struct list_head *list;
    struct pid *pid_struct;
    unsigned long rss = 0;
    unsigned long long cpu_time_ms = 0;
    long state;
    
    // 获取传递进来的 PID。注意：我们在 open 时将 PID 存入了 m->private
    int pid_num = (int)(unsigned long)m->private;

    // === 关键点：RCU 读锁 ===
    // 保护进程列表，防止在查找过程中进程被销毁导致内存访问错误
    rcu_read_lock();
    
    // 1. 根据 PID 号查找内核 PID 结构
    pid_struct = find_get_pid(pid_num);
    if (!pid_struct) {
        rcu_read_unlock();
        seq_printf(m, "Process not found (PID %d)\n", pid_num);
        return 0;
    }
    
    // 2. 根据 PID 结构获取 task_struct (进程描述符)
    task = pid_task(pid_struct, PIDTYPE_PID);
    if (!task) {
        put_pid(pid_struct); // 释放 PID 引用
        rcu_read_unlock();
        seq_printf(m, "Process task struct missing (PID %d)\n", pid_num);
        return 0;
    }

    // 3. 获取内存信息 (RSS: Resident Set Size)
    if (task->mm) {
        // get_mm_rss 返回页数，左移 PAGE_SHIFT 转为字节，再除以 1024 转 KB
        // 这里简化：页数 * 4KB (假设) -> KB
        rss = get_mm_rss(task->mm) << (PAGE_SHIFT - 10);
    }

    // 4. 获取 CPU 时间
    cpu_time_ms = task->se.sum_exec_runtime; // 单位：纳秒
    do_div(cpu_time_ms, 1000000); // 转换为毫秒

    // 5. 获取进程状态 (使用 READ_ONCE 保证原子读取)
    state = READ_ONCE(task->__state);

    // === 格式化输出 ===
    seq_puts(m, "================= Process Status =================\n");
    seq_printf(m, "PID:         %d\n", task->pid);
    seq_printf(m, "State:       %s\n", get_task_state_name(state));
    seq_printf(m, "Memory RSS:  %lu KB\n", rss);
    seq_printf(m, "CPU Time:    %llu ms\n\n", cpu_time_ms);

    seq_puts(m, "================ Process Tree Info ===============\n");
    
    // 输出父进程信息
    if (task->real_parent) {
        seq_printf(m, "Root Process: %d (%s)\n", 
                   task->real_parent->pid, task->real_parent->comm);
    } else {
        seq_puts(m, "Root Process: None\n");
    }

    // 遍历子进程链表
    int child_count = 0;
    list_for_each(list, &task->children) {
        child = list_entry(list, struct task_struct, sibling);
        // 限制输出数量，防止子进程过多刷屏
        if (child_count < 29) {
            seq_printf(m, "Child:       %d (%s)\n", child->pid, child->comm);
        }
        child_count++;
    }
    
    if (child_count > 29) {
        seq_printf(m, "... and %d more children\n", child_count - 29);
    }

    // 清理工作
    put_pid(pid_struct);
    rcu_read_unlock();
    return 0;
}

/**
 * tproc_status_open - 打开 status 文件时的回调
 * 
 * 将 inode 中存储的 PID (i_private) 传递给 seq_file 的 private 字段，
 * 这样 tproc_show 就能知道要显示哪个进程的信息。
 */
static int tproc_status_open(struct inode *inode, struct file *file)
{
    return single_open(file, tproc_show, inode->i_private);
}

// status 文件的操作函数集
static const struct file_operations tproc_status_fops = {
    .owner   = THIS_MODULE,
    .open    = tproc_status_open,
    .read    = seq_read,      // 使用内核提供的序列读函数
    .llseek  = seq_lseek,
    .release = single_release,
};

/**
 * tproc_lookup - 目录查找回调 (核心逻辑)
 * @dir: 父目录的 inode
 * @dentry: 目标目录项
 * 
 * 当系统访问 /tproc/NAME 时，内核调用此函数。我们在这里实现“惰性求值”。
 */
static struct dentry *tproc_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    struct inode *inode = NULL;
    const char *name = dentry->d_name.name;
    int pid_num = 0;
    
    // 情况 1: 查找 "status" 文件
    // 前提：父目录必须是 PID 目录 (即 dir->i_private 存储了 PID)
    if (strcmp(name, TM_STATUS_FILE) == 0) {
        if (dir->i_private) {
            inode = new_inode(dir->i_sb);
            if (inode) {
                inode->i_ino = get_next_ino();
                inode->i_mode = S_IFREG | 0444; // 只读文件
                
                // [Linux 6.14 适配] 使用专用函数初始化时间戳 (i_atime/mtime/ctime)
                simple_inode_init_ts(inode);
                
                inode->i_fop = &tproc_status_fops; // 挂载操作集
                inode->i_private = dir->i_private; // 继承父目录的 PID
                d_add(dentry, inode); // 将 inode 关联到 dentry
                return NULL;
            }
        }
    }

    // 情况 2: 查找 PID 目录 (例如 /tproc/1234)
    // 前提：父目录是根目录 (i_private 为空)，且名字全是数字
    if (!dir->i_private && kstrtoint(name, 10, &pid_num) == 0) {
        // 验证 PID 是否真实存在
        struct pid *pid_struct = find_get_pid(pid_num);
        if (pid_struct) {
            put_pid(pid_struct); // 仅检查存在性，立即释放
            
            inode = new_inode(dir->i_sb);
            if (inode) {
                inode->i_ino = get_next_ino();
                inode->i_mode = S_IFDIR | 0555; // 目录权限
                
                // [Linux 6.14 适配] 时间戳初始化
                simple_inode_init_ts(inode);
                
                inode->i_op = dir->i_op;     // 子目录继续使用相同的 lookup (支持递归)
                inode->i_fop = &simple_dir_operations;
                inode->i_private = (void *)(unsigned long)pid_num; // 将 PID 存入 inode
                d_add(dentry, inode);
                return NULL;
            }
        }
    }

    return ERR_PTR(-ENOENT);
}

static const struct inode_operations tproc_dir_inode_operations = {
    .lookup = tproc_lookup,
    .getattr = simple_getattr,
};

// [Linux 6.14 适配] 
// simple_super_operations 在新内核中不再导出，必须手动定义
static const struct super_operations tproc_s_ops = {
    .statfs     = simple_statfs,
    .drop_inode = generic_delete_inode,
};

/**
 * tproc_fill_super - 初始化超级块
 * 创建文件系统的根目录
 */
static int tproc_fill_super(struct super_block *sb, void *data, int silent)
{
    struct inode *inode;

    sb->s_blocksize = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_magic = TPROCFS_MAGIC;
    // [Linux 6.14 适配] 挂载自定义的超级块操作
    sb->s_op = &tproc_s_ops;
    sb->s_time_gran = 1;

    // 创建根 inode
    inode = new_inode(sb);
    if (!inode) return -ENOMEM;

    inode->i_ino = 1;
    inode->i_mode = S_IFDIR | 0755;
    
    // [Linux 6.14 适配]
    simple_inode_init_ts(inode);
    
    inode->i_op = &tproc_dir_inode_operations; // 挂载核心 lookup 函数
    inode->i_fop = &simple_dir_operations;
    inode->i_private = NULL; // 根目录没有 PID

    sb->s_root = d_make_root(inode);
    if (!sb->s_root) return -ENOMEM;

    return 0;
}

static struct dentry *tproc_mount(struct file_system_type *fs_type,
                                  int flags, const char *dev_name,
                                  void *data)
{
    return mount_nodev(fs_type, flags, data, tproc_fill_super);
}

static struct file_system_type tproc_fs_type = {
    .name = "tprocfs",
    .mount = tproc_mount,
    .kill_sb = kill_litter_super,
    .owner = THIS_MODULE,
};

// === 模块加载与卸载 ===
static int __init tproc_init(void)
{
    return register_filesystem(&tproc_fs_type);
}

static void __exit tproc_exit(void)
{
    unregister_filesystem(&tproc_fs_type);
}

module_init(tproc_init);
module_exit(tproc_exit);