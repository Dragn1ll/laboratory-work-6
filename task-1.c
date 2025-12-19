#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

static void print_time_pid(const char *who) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        return;
    }

    time_t sec = tv.tv_sec;
    struct tm tmv;
    localtime_r(&sec, &tmv);

    long ms = tv.tv_usec / 1000;

    printf("%s: pid=%ld ppid=%ld time=%02d:%02d:%02d:%03ld\n",
           who,
           (long)getpid(),
           (long)getppid(),
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
    fflush(stdout);
}

int main(void) {
    print_time_pid("Parent start");

    pid_t c1 = fork();
    if (c1 < 0) {
        perror("fork #1");
        return 1;
    }

    if (c1 == 0) {
        print_time_pid("Child 1");
        sleep(2);
        _exit(0);
    }

    pid_t c2 = fork();
    if (c2 < 0) {
        perror("fork #2");
        (void)waitpid(c1, NULL, 0);
        return 1;
    }

    if (c2 == 0) {
        print_time_pid("Child 2");
        sleep(2);
        _exit(0);
    }

    print_time_pid("Parent");
    usleep(200 * 1000);

    puts("\n--- ps -x (from parent) ---");
    fflush(stdout);
    system("ps -x");

    (void)waitpid(c1, NULL, 0);
    (void)waitpid(c2, NULL, 0);

    print_time_pid("Parent done");
    return 0;
}