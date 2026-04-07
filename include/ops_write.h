#ifndef OPS_WRITE_H
#define OPS_WRITE_H

#include <fuse.h>

int unionfs_write(const char *, const char *, size_t, off_t, struct fuse_file_info *);
int unionfs_create(const char *, mode_t, struct fuse_file_info *);
int unionfs_unlink(const char *);
int unionfs_mkdir(const char *, mode_t);
int unionfs_rmdir(const char *);
int unionfs_truncate(const char *, off_t, struct fuse_file_info *);

#endif
