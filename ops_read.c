#include "ops_read.h"
#include "path_utils.h"
#include "cow.h"
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

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    (void) offset; (void) fi; (void) flags;

    struct mini_unionfs_state *state = UNIONFS_DATA;
    DIR *dp;
    struct dirent *de;

    /* We'll track which names have been added to avoid duplicates */
    /* Simple linked list of seen names */
    struct seen_entry {
        char name[NAME_MAX + 1];
        struct seen_entry *next;
    };
    struct seen_entry *seen_head = NULL;

    /* Helper: check if name already seen */
    #define NAME_SEEN(nm) ({ \
    int found = 0; \
    struct seen_entry *cur = seen_head; \
    while (cur) { if (strcmp(cur->name, (nm)) == 0) { found=1; break; } cur=cur->next; } \
        found; \
    })

    #define MARK_SEEN(nm) do { \
    struct seen_entry *e = malloc(sizeof(*e)); \
    strncpy(e->name, (nm), NAME_MAX); \
    e->next = seen_head; seen_head = e; \
    } while(0)

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    /* --- Scan upper_dir first --- */
    char upper_dir_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_dir_path);

    dp = opendir(upper_dir_path);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            /* Skip whiteout files from the user view */
            if (strncmp(de->d_name, ".wh.", 4) == 0)
                continue;

            filler(buf, de->d_name, NULL, 0, 0);
            MARK_SEEN(de->d_name);
        }
        closedir(dp);
    }

    /* --- Scan lower_dir --- */
    char lower_dir_path[PATH_MAX];
    make_path(state->lower_dir, path, lower_dir_path);

    dp = opendir(lower_dir_path);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            /* Skip if already shown from upper */
            if (NAME_SEEN(de->d_name))
                continue;

            /* Check for whiteout in upper */
            char wh_buf[PATH_MAX];
            char vpath[PATH_MAX];
            snprintf(vpath, sizeof(vpath), "%s/%s", path, de->d_name);
            make_whiteout_path(state->upper_dir, vpath, wh_buf);

            struct stat wh_st;
            if (lstat(wh_buf, &wh_st) == 0)
                continue; /* whiteout exists – hide this file */

                filler(buf, de->d_name, NULL, 0, 0);
            MARK_SEEN(de->d_name);
        }
        closedir(dp);
    }

    /* Free seen list */
    while (seen_head) {
        struct seen_entry *tmp = seen_head;
        seen_head = seen_head->next;
        free(tmp);
    }

    return 0;
}
