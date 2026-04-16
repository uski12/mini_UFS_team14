#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "include/state.h"
#include "include/ops_read.h"
#include "include/ops_write.h"

static struct fuse_operations unionfs_oper = {
    .getattr  = unionfs_getattr,
    .readdir  = unionfs_readdir,
    .open     = unionfs_open,
    .read     = unionfs_read,
    .write    = unionfs_write,
    .create   = unionfs_create,
    .unlink   = unionfs_unlink,
    .mkdir    = unionfs_mkdir,
    .rmdir    = unionfs_rmdir,
    .truncate = unionfs_truncate,
};

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_point>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = calloc(1, sizeof(*state));

    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    char *fuse_argv[4];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[3];
    fuse_argv[2] = "-f";
    fuse_argv[3] = NULL;

    return fuse_main(3, fuse_argv, &unionfs_oper, state);
}
