#include "ops_write.h"
#include "path_utils.h"
#include "state.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;

    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);

    int fd = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd < 0) return -errno;
    close(fd);

    char wh_path[PATH_MAX];
    make_whiteout_path(state->upper_dir, path, wh_path);
    unlink(wh_path);

    return 0;
}

