#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/wait.h>

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strlist;

static void sl_init(strlist *sl) {
    sl->items = NULL;
    sl->count = 0;
    sl->cap = 0;
}

static void sl_free(strlist *sl) {
    for (size_t i = 0; i < sl->count; i++) free(sl->items[i]);
    free(sl->items);
    sl->items = NULL;
    sl->count = sl->cap = 0;
}

static int sl_push(strlist *sl, const char *s) {
    if (sl->count == sl->cap) {
        size_t ncap = sl->cap ? sl->cap * 2 : 16;
        char **nitems = (char**)realloc(sl->items, ncap * sizeof(char*));
        if (!nitems) return -1;
        sl->items = nitems;
        sl->cap = ncap;
    }
    sl->items[sl->count] = strdup(s);
    if (!sl->items[sl->count]) return -1;
    sl->count++;
    return 0;
}

static int list_regular_files(const char *dirpath, strlist *out) {
    DIR *dp = opendir(dirpath);
    if (!dp) {
        fprintf(stderr, "opendir('%s') failed: %s\n", dirpath, strerror(errno));
        return -1;
    }

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char full[PATH_MAX];
        if (snprintf(full, sizeof(full), "%s/%s", dirpath, de->d_name) >= (int)sizeof(full)) {
            fprintf(stderr, "Path too long: %s/%s\n", dirpath, de->d_name);
            continue;
        }

        struct stat st;
        if (stat(full, &st) != 0) {
            fprintf(stderr, "stat('%s') failed: %s\n", full, strerror(errno));
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        if (sl_push(out, full) != 0) {
            fprintf(stderr, "Out of memory\n");
            closedir(dp);
            return -1;
        }
    }

    closedir(dp);
    return 0;
}

static int compare_files(const char *p1, const char *p2, long long *bytes_viewed) {
    *bytes_viewed = 0;

    struct stat s1, s2;
    if (stat(p1, &s1) != 0 || stat(p2, &s2) != 0) return -2;
    if (s1.st_size != s2.st_size) return 1;

    int fd1 = open(p1, O_RDONLY);
    if (fd1 < 0) return -2;
    int fd2 = open(p2, O_RDONLY);
    if (fd2 < 0) { close(fd1); return -2; }

    unsigned char b1[65536], b2[65536];
    for (;;) {
        ssize_t r1 = read(fd1, b1, sizeof(b1));
        if (r1 < 0) { close(fd1); close(fd2); return -2; }
        ssize_t r2 = read(fd2, b2, sizeof(b2));
        if (r2 < 0) { close(fd1); close(fd2); return -2; }

        if (r1 == 0 && r2 == 0) break;
        if (r1 != r2) {
            close(fd1); close(fd2);
            return 1;
        }

        *bytes_viewed += r1;
        if (memcmp(b1, b2, (size_t)r1) != 0) {
            close(fd1); close(fd2);
            return 1;
        }
    }

    close(fd1);
    close(fd2);
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <Dir1> <Dir2> <N>\n", prog);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        usage(argv[0]);
        return 2;
    }

    const char *dir1 = argv[1];
    const char *dir2 = argv[2];
    long N = strtol(argv[3], NULL, 10);
    if (N < 1 || N > 1000000) {
        fprintf(stderr, "Bad N: %s\n", argv[3]);
        return 2;
    }

    strlist a, b;
    sl_init(&a);
    sl_init(&b);

    if (list_regular_files(dir1, &a) != 0 || list_regular_files(dir2, &b) != 0) {
        sl_free(&a);
        sl_free(&b);
        return 1;
    }

    printf("Dir1 files: %zu, Dir2 files: %zu, max parallel N=%ld\n", a.count, b.count, N);
    fflush(stdout);

    long active = 0;
    for (size_t i = 0; i < a.count; i++) {
        for (size_t j = 0; j < b.count; j++) {

            while (active >= N) {
                if (waitpid(-1, NULL, 0) > 0) active--;
                else if (errno != EINTR) break;
            }

            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "fork failed: %s\n", strerror(errno));
                if (waitpid(-1, NULL, 0) > 0) active--;
                continue;
            }

            if (pid == 0) {
                long long bytes = 0;
                int rc = compare_files(a.items[i], b.items[j], &bytes);

                if (rc == 0) {
                    printf("pid=%ld EQUAL bytes=%lld '%s' <-> '%s'\n",
                           (long)getpid(), bytes, a.items[i], b.items[j]);
                    fflush(stdout);
                    _exit(0);
                } else if (rc == 1) {
                    printf("pid=%ld DIFF  bytes=%lld '%s' <-> '%s'\n",
                           (long)getpid(), bytes, a.items[i], b.items[j]);
                    fflush(stdout);
                    _exit(1);
                } else {
                    printf("pid=%ld ERROR bytes=%lld '%s' <-> '%s' (%s)\n",
                           (long)getpid(), bytes, a.items[i], b.items[j], strerror(errno));
                    fflush(stdout);
                    _exit(3);
                }
            }

            active++;
        }
    }

    while (active > 0) {
        if (waitpid(-1, NULL, 0) > 0) active--;
        else if (errno != EINTR) break;
    }

    sl_free(&a);
    sl_free(&b);
    return 0;
}
