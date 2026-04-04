#ifndef STATE_H
#define STATE_H

#include <fuse.h>

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA \
((struct mini_unionfs_state *) fuse_get_context()->private_data)

#endif
