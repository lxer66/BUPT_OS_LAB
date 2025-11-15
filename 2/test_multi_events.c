#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>

// 定义系统调用号，对应内核中注册的 event 原语接口
#define __NR_eventopen   548   // 创建事件
#define __NR_eventwait   550   // 等待事件
#define __NR_eventsig    551   // 触发（signal）事件
#define __NR_eventclose  549   // 关闭（销毁）事件

int main(int argc, char **argv) {
    int events = 3;      // 默认需要创建的事件数
    int per_event = 4;   // 每个事件上等待的子进程数
    if (argc > 1) events = atoi(argv[1]);       // 支持通过第一个参数设定事件数
    if (argc > 2) per_event = atoi(argv[2]);    // 支持通过第二个参数设定每事件的等待进程数

    // 打印程序启动信息，标明父进程 PID 和测试目标
    printf("[1893] [pid = %d] Starting multi-event multi-wait test...\n", getpid());

    // 为事件 ID 分配动态数组
    int *ids = malloc(sizeof(int) * events);
    if (!ids) {
        perror("malloc");  // 内存分配失败则报错退出
        return 1;
    }

    // 创建多个事件，每个事件通过系统调用 eventopen 获取唯一 ID
    for (int i = 0; i < events; ++i) {
        ids[i] = syscall(__NR_eventopen);
        if (ids[i] < 0) {  
            perror("eventopen");  // 创建失败时打印错误
            return 1;
        }
        printf("[1893] [pid = %d] Created Event #%d → ID=%d\n", getpid(), i, ids[i]);
    }

    // 对每个事件启动 per_event 个子进程，让它们等待该事件
    for (int e = 0; e < events; ++e) {
        for (int j = 0; j < per_event; ++j) {
            pid_t p = fork();
            if (p < 0) {
                perror("fork");  // fork 失败
                return 1;
            }
            if (p == 0) { // 子进程部分
                printf("[1893] [pid = %d] Child-Ev%d#%d waiting for event ID=%d\n",
                       getpid(), e, j, ids[e]);
                // 子进程调用 eventwait，阻塞直到事件被触发或关闭
                syscall(__NR_eventwait, ids[e]);
                // 唤醒后继续执行，打印唤醒信息
                printf("[1893] [pid = %d] Child-Ev%d#%d woke up from event ID=%d!\n",
                       getpid(), e, j, ids[e]);
                _exit(0);  // 退出子进程
            }
        }
    }

    // 父进程随机延迟触发每个事件，目的是模拟真实环境中事件不同时发生
    srand(time(NULL));
    for (int i = 0; i < events; ++i) {
        int wait_time = (rand() % 3) + 1;  // 等待 1–3 秒
        printf("[1893] [pid = %d] Parent: Waiting %d seconds before signaling event ID=%d\n",
               getpid(), wait_time, ids[i]);
        sleep(wait_time);

        // 发出信号 (触发) 事件
        printf("[1893] [pid = %d] Parent: Signaling event ID=%d\n", getpid(), ids[i]);
        syscall(__NR_eventsig, ids[i]);
    }

    // 等待所有子进程结束。总子进程数 = events * per_event
    int total = events * per_event;
    for (int i = 0; i < total; ++i) {
        wait(NULL);
    }

    // 子进程全部退出后，父进程关闭所有事件
    for (int i = 0; i < events; ++i) {
        printf("[1893] [pid = %d] Parent: Closing event ID=%d\n", getpid(), ids[i]);
        syscall(__NR_eventclose, ids[i]);
    }

    // 打印结束信息并释放分配的 ID 数组
    printf("[1893] [pid = %d] All events processed; exiting.\n", getpid());
    free(ids);
    return 0;
}
