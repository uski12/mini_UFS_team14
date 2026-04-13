#include "ops_read.h"
#include "path_utils.h"
#include "state.h"

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res < 0) return res;

    if (lstat(resolved, stbuf) < 0)
        return -errno;

    return 0;
}

int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res < 0) return res;

    if ((fi->flags & O_ACCMODE) != O_RDONLY && !is_in_upper(path)) {
        res = cow_copy(path);
        if (res < 0) return res;
    }

    return 0;
}

int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res < 0) return res;

    int fd = open(resolved, O_RDONLY);
    if (fd < 0) return -errno;

    res = pread(fd, buf, size, offset);
    if (res < 0) res = -errno;

    close(fd);
    return res;
}

int unionfs_unlink(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    if (is_in_upper(path)) {
        char upper_path[PATH_MAX];
        make_path(state->upper_dir, path, upper_path);
        if (unlink(upper_path) < 0) return -errno;
    } else {
        char wh_path[PATH_MAX];
        make_whiteout_path(state->upper_dir, path, wh_path);
        int fd = open(wh_path, O_CREAT | O_WRONLY, 0000);
        if (fd < 0) return -errno;
        close(fd);
    }

    return 0;
}
