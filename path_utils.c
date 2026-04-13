#include "path_utils.h"
#include "state.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>

void make_path(const char *dir, const char *path, char *out) {
    snprintf(out, PATH_MAX, "%s%s", dir, path);
}

void make_whiteout_path(const char *upper_dir, const char *path, char *out) {
    char tmp[PATH_MAX], tmp2[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    snprintf(tmp2, sizeof(tmp2), "%s", path);

    char *dir_part = dirname(tmp);
    char *base_part = basename(tmp2);

    if (strcmp(dir_part, "/") == 0 || strcmp(dir_part, ".") == 0)
        snprintf(out, PATH_MAX, "%s/.wh.%s", upper_dir, base_part);
    else
        snprintf(out, PATH_MAX, "%s%s/.wh.%s", upper_dir, dir_part, base_part);
}

int resolve_path(const char *path, char *resolved) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    struct stat st;

    char wh_path[PATH_MAX];
    make_whiteout_path(state->upper_dir, path, wh_path);
    if (lstat(wh_path, &st) == 0)
        return -ENOENT;

    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);
    if (lstat(upper_path, &st) == 0) {
        strncpy(resolved, upper_path, PATH_MAX);
        return 0;
    }

    char lower_path[PATH_MAX];
    make_path(state->lower_dir, path, lower_path);
    if (lstat(lower_path, &st) == 0) {
        strncpy(resolved, lower_path, PATH_MAX);
        return 0;
    }

    return -ENOENT;
}

int is_in_upper(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    struct stat st;

    make_path(state->upper_dir, path, upper_path);
    return (lstat(upper_path, &st) == 0);
}
