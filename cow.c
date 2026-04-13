#include "cow.h"
#include "path_utils.h"
#include "state.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <stdio.h>

int cow_copy(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    char lower_path[PATH_MAX], upper_path[PATH_MAX];
    make_path(state->lower_dir, path, lower_path);
    make_path(state->upper_dir, path, upper_path);

    char upper_dir_copy[PATH_MAX];
    strncpy(upper_dir_copy, upper_path, PATH_MAX);
    char *parent = dirname(upper_dir_copy);

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", parent);

    char *p = tmp + 1;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(tmp, 0755);

    int src = open(lower_path, O_RDONLY);
    if (src < 0) return -errno;

    struct stat st;
    if (fstat(src, &st) < 0) {
        close(src);
        return -errno;
    }

    int dst = open(upper_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (dst < 0) {
        close(src);
        return -errno;
    }

    char buf[65536];
    ssize_t n;

    while ((n = read(src, buf, sizeof(buf))) > 0) {
        if (write(dst, buf, n) != n) {
            close(src); close(dst);
            return -EIO;
        }
    }

    close(src);
    close(dst);
    return 0;
}
