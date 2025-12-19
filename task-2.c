#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} StrList;

static void list_init(StrList *l) {
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

static void list_free(StrList *l) {
    for (size_t i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
}

static void list_push(StrList *l, const char *s) {
    if (l->count == l->cap) {
        size_t ncap = (l->cap == 0) ? 32 : l->cap * 2;
        char **n = (char **)realloc(l->items, ncap * sizeof(char *));
        if (!n) {
            perror("realloc");
            exit(1);
        }
        l->items = n;
        l->cap = ncap;
    }
    l->items[l->count] = strdup(s);
    if (!l->items[l->count]) {
        perror("strdup");
        exit(1);
    }
    l->count++;
}

static int is_regular_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

static void collect_files(const char *dir, StrList *out) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "opendir(%s) failed: %s\n", dir, strerror(errno));
        exit(1);
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >= (int)sizeof(path)) {
            fprintf(stderr, "Path too long: %s/%s\n", dir, de->d_name);
            continue;
        }

        if (is_regular_file(path)) {
            list_push(out, path);
        }
    }

    closedir(d);
}

static int compare_files(const char *p1, const char *p2, unsigned long long *bytes_compared) {
    *bytes_compared = 0;

    int f1 = open(p1, O_RDONLY);
    if (f1 < 0) return -1;

    int f2 = open(p2, O_RDONLY);
    if (f2 < 0) { close(f1); return -1; }

    unsigned char b1[64 * 1024];
    unsigned char b2[64 * 1024];

    for (;;) {
        ssize_t r1 = read(f1, b1, sizeof(b1));
        if (r1 < 0) { close(f1); close(f2); return -1; }

        ssize_t r2 = read(f2, b2, (r1 > 0) ? (size_t)r1 : sizeof(b2));
        if (r2 < 0) { close(f1); close(f2); return -1; }

        if (r1 == 0 && r2 == 0) {
            close(f1); close(f2);
            return 1;
        }

        if (r1 == 0 || r2 == 0) {
            close(f1); close(f2);
            return 0;
        }

        if (memcmp(b1, b2, (size_t)r1) != 0) {
            close(f1); close(f2);
            *bytes_compared += (unsigned long long)r1;
            return 0;
        }

        *bytes_compared += (unsigned long long)r1;
    }
}

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? (s + 1) : path;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <Dir1> <Dir2> <N>\n", argv[0]);
        return 2;
    }

    const char *dir1 = argv[1];
    const char *dir2 = argv[2];
    int N = atoi(argv[3]);
    if (N <= 0) {
        fprintf(stderr, "N must be > 0\n");
        return 2;
    }

    StrList a, b;
    list_init(&a);
    list_init(&b);

    collect_files(dir1, &a);
    collect_files(dir2, &b);

    int active = 0;

    for (size_t i = 0; i < a.count; i++) {
        for (size_t j = 0; j < b.count; j++) {

            while (active >= N) {
                if (waitpid(-1, NULL, 0) > 0) active--;
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                while (active > 0) {
                    if (waitpid(-1, NULL, 0) > 0) active--;
                }
                list_free(&a);
                list_free(&b);
                return 1;
            }

            if (pid == 0) {
                unsigned long long bytes = 0;
                int res = compare_files(a.items[i], b.items[j], &bytes);

                if (res < 0) {
                    fprintf(stderr, "pid=%ld compare error: %s <-> %s (%s)\n",
                            (long)getpid(), base_name(a.items[i]), base_name(b.items[j]), strerror(errno));
                    _exit(3);
                }

                printf("pid=%ld %s <-> %s bytes=%llu result=%s\n",
                       (long)getpid(),
                       base_name(a.items[i]),
                       base_name(b.items[j]),
                       bytes,
                       (res == 1) ? "EQUAL" : "DIFFER");
                fflush(stdout);

                _exit((res == 1) ? 0 : 1);
            }

            active++;
        }
    }

    while (active > 0) {
        if (waitpid(-1, NULL, 0) > 0) active--;
    }

    list_free(&a);
    list_free(&b);
    return 0;
}