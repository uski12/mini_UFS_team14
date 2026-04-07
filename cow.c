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
