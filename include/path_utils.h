#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <limits.h>

void make_path(const char *dir, const char *path, char *out);
void make_whiteout_path(const char *upper_dir, const char *path, char *out);
int resolve_path(const char *path, char *resolved);
int is_in_upper(const char *path);

#endif
