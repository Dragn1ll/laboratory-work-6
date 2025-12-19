#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

static void print_time_pid(const char *tag) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        return;
    }

    struct tm tmv;
    localtime_r(&tv.tv_sec, &tmv);
    long ms = tv.tv_usec / 1000;

    printf("%s pid=%ld ppid=%ld time=%02d:%02d:%02d:%03ld\n",
           tag,
           (long)getpid(),
           (long)getppid(),
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
    fflush(stdout);
}

int main(void) {
    print_time_pid("PARENT(start)");

    pid_t c1 = fork();
    if (c1 < 0) {
        perror("fork (child1)");
        return 1;
    }
    if (c1 == 0) {
        print_time_pid("CHILD1");
        sleep(5);
        _exit(0);
    }

    pid_t c2 = fork();
    if (c2 < 0) {
        perror("fork (child2)");
        (void)waitpid(c1, NULL, 0);
        return 1;
    }
    if (c2 == 0) {
        print_time_pid("CHILD2");
        sleep(5);
        _exit(0);
    }

    sleep(1);
    printf("\n--- ps -x (ищите PID: %ld, %ld, %ld) ---\n",
           (long)getpid(), (long)c1, (long)c2);
    fflush(stdout);

    int rc = system("ps -x");
    if (rc == -1) {
        perror("system(ps -x)");
    }

    (void)waitpid(c1, NULL, 0);
    (void)waitpid(c2, NULL, 0);

    print_time_pid("PARENT(done)");
    return 0;
}
