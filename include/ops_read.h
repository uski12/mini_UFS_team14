#ifndef OPS_READ_H
#define OPS_READ_H

#include <fuse.h>

int unionfs_getattr(const char *, struct stat *, struct fuse_file_info *);
int unionfs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
                    struct fuse_file_info *, enum fuse_readdir_flags);
int unionfs_open(const char *, struct fuse_file_info *);
int unionfs_read(const char *, char *, size_t, off_t, struct fuse_file_info *);

#endif
