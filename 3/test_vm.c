#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

int main() {
    size_t size = 4096 * 10; // 40KB
    char *p = mmap(NULL, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* 产生某些页面 */
    p[4096 * 1] = 1; // 访问第2页，使其被分配
    p[4096 * 2] = 2; // 访问第3页，使其被分配
    p[4096 * 3] = 3; // 访问第4页，使其被分配

    /* 产生高 refcount */
    char *q = p + 4096 * 4; // 第5页
    for (int i = 0; i < 20; i++)
        *(volatile char*)q; // 多次访问，增加引用计数

    printf("PID = %d\n", getpid());
    printf("Check /proc/%d/maps\n", getpid());
    getchar(); // 暂停，方便查看 maps
    return 0;
}