#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>

// 定义系统调用号，与内核中注册的相对应
#define __NR_eventopen   548
#define __NR_eventclose  549
#define __NR_eventwait   550
#define __NR_eventsig    551

int main(int argc, char **argv) {
    int n = 5; // 默认子进程数量
    if (argc > 1) {
        // 如果命令行参数给了一个数字，就用这个数量
        n = atoi(argv[1]);
    }

    // 创建一个事件，返回一个事件 ID
    int id = syscall(__NR_eventopen);
    if (id < 0) {
        // 如果创建失败，则打印错误并退出
        perror("eventopen");
        return 1;
    }
    printf("[1893] [Parent pid=%d] Created event id=%d; spawning %d children...\n", getpid(), id, n);

    // 启动 n 个子进程，每个子进程都会等待这个事件
    for (int i = 0; i < n; ++i) {
        pid_t p = fork();
        if (p < 0) {
            // fork 失败则退出
            perror("fork");
            return 1;
        }
        if (p == 0) { // 子进程
            printf("[1893] [Child#%d pid=%d] Waiting on event %d\n", i, getpid(), id);
            // 子进程调用 eventwait 阻塞等待事件被触发
            syscall(__NR_eventwait, id);
            // 被唤醒后执行下面这条
            printf("[1893] [Child#%d pid=%d] Woken up from event %d!\n", i, getpid(), id);
            return 0; // 子进程退出
        }
    }

    // 父进程等待一段时间，让所有子进程进入等待状态
    int wait_sec = 2;
    printf("[1893] [Parent pid=%d] Waiting %d seconds before signaling event id=%d\n", getpid(), wait_sec, id);
    sleep(wait_sec);

    // 父进程发出 signal，触发事件，唤醒所有等待者
    printf("[1893] [Parent pid=%d] Signaling event id=%d (wake all)\n", getpid(), id);
    syscall(__NR_eventsig, id);

    // 父进程等待所有子进程退出
    for (int i = 0; i < n; ++i) {
        wait(NULL);
    }

    // 父进程关闭事件
    printf("[1893] [Parent pid=%d] Closing event id=%d\n", getpid(), id);
    syscall(__NR_eventclose, id);

    // 最终打印并退出
    printf("[1893] [Parent pid=%d] All children awakened; exiting.\n", getpid());
    return 0;
}
