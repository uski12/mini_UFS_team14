# mini_unionfs — Complete Guide & Line-by-Line Code Explanation

> **Team 14 | Cloud Computing — Semester 6**
> Built with C + FUSE3. Runs on Linux (or Docker on macOS).

---

## Table of Contents

1. [What is a Filesystem?](#1-what-is-a-filesystem)
2. [What is a Union Filesystem (UnionFS)?](#2-what-is-a-union-filesystem-unionfs)
3. [Why Does UnionFS Exist?](#3-why-does-unionfs-exist)
4. [Where is UnionFS Used in the Real World?](#4-where-is-unionfs-used-in-the-real-world)
5. [How UnionFS is Different from a Regular Filesystem](#5-how-unionfs-is-different-from-a-regular-filesystem)
6. [Core Concepts: Upper, Lower, Mount, COW, Whiteouts](#6-core-concepts-upper-lower-mount-cow-whiteouts)
7. [What is FUSE?](#7-what-is-fuse)
8. [Project Architecture](#8-project-architecture)
9. [Line-by-Line Code Explanation](#9-line-by-line-code-explanation)
   - [include/state.h](#includestateh)
   - [include/path_utils.h](#includepath_utilsh)
   - [include/cow.h](#includecowh)
   - [include/ops_read.h](#includeops_readh)
   - [include/ops_write.h](#includeops_writeh)
   - [path_utils.c](#path_utilsc)
   - [cow.c](#cowc)
   - [ops_read.c](#ops_readc)
   - [ops_write.c](#ops_writec)
   - [main.c](#mainc)
   - [Makefile](#makefile)
   - [Dockerfile and run_in_docker.sh](#dockerfile-and-run_in_dockersh)
10. [Complete Data Flow: What Happens When You Do X](#10-complete-data-flow-what-happens-when-you-do-x)
11. [How to Build and Run](#11-how-to-build-and-run)

---

## 1. What is a Filesystem?

Before talking about UnionFS, you need to understand what a filesystem is.

A **filesystem** is the part of an operating system that decides:
- How files are stored on disk
- How files are named and organized into directories
- How you find, read, write, and delete files
- Who is allowed to do what (permissions)

When you type `ls`, `cat`, `cp`, or anything that touches a file, the OS talks to the filesystem. The filesystem translates your request into raw disk operations (reading/writing blocks of data). Examples: `ext4` (Linux), `APFS` (macOS), `NTFS` (Windows).

Think of the filesystem as a librarian. You ask "give me book X". The librarian knows exactly which shelf it's on, fetches it, and hands it to you. Without the librarian (filesystem), you'd have to know the exact physical location of every byte on the disk.

---

## 2. What is a Union Filesystem (UnionFS)?

A **Union Filesystem** is a special type of filesystem that takes **multiple separate directories** and **stacks them on top of each other**, presenting them to the user as a **single unified directory**.

Think of it like this: imagine you have two transparent sheets of paper with writing on them. When you lay one on top of the other and hold them up to the light, you see a single combined view. If both sheets have content at the same spot, the one on top wins (it hides the one below).

**That is exactly what UnionFS does with directories.**

```
User sees ONE merged view at mount/:
  mount/
    file_a.txt   <- came from upper/
    file_b.txt   <- came from lower/ (upper did not have it)
    file_c.txt   <- came from upper/ (upper's version overrides lower's copy)

Actual disk layout:
  upper/              lower/
    file_a.txt          file_b.txt
    file_c.txt          file_c.txt  <- hidden by upper's version
```

The key insight: **neither the lower nor upper directory is modified when you read**. The union is a logical view, not a physical merge.

---

## 3. Why Does UnionFS Exist?

### The Core Problem It Solves

Imagine you have a **read-only base image** — a complete Linux OS installation on disk. You want 100 users to each have their own session where they can modify files freely, but:
- You do not want to copy 10GB of OS files 100 times (that wastes 1TB of disk).
- You want changes by one user to NOT affect other users.
- You want to throw away changes when the user logs out.

**Without UnionFS:** You would copy the entire OS for each user. Slow, wasteful, impractical.

**With UnionFS:** Every user shares the same read-only lower layer. Each user gets a tiny personal upper layer that only stores their changes. The view they see merges both layers seamlessly.

This is exactly how **Docker containers** work.

---

## 4. Where is UnionFS Used in the Real World?

### Docker / Container Images

This is the single biggest real-world use case of UnionFS technology.

When you pull a Docker image (e.g., `ubuntu:22.04`), Docker does not copy the entire image for every container. Instead:
- The image layers are the **lower layers** (read-only).
- Each container gets a thin **upper layer** (read-write) for its own changes.
- The container sees a merged filesystem.

When the container is deleted, only the upper layer is discarded. The shared lower layers remain intact for the next container.

Docker uses `OverlayFS` (built into the Linux kernel), which is a production-grade implementation of the same concept as this project.

### Linux Live USB / Live CD

When you boot a Linux live USB, the OS runs entirely in RAM. Any files you create or modify go into an in-memory upper layer. The USB stick (read-only lower layer) is never written to. When you reboot, all changes vanish.

### Package Managers with Rollback (NixOS, ostree)

Some Linux systems keep multiple OS versions as read-only lower layers and use union mounting to let you switch between them instantly, with per-version writable upper layers.

### Software Build Systems

Build systems can present a clean source tree as the lower layer and capture all generated/modified files in the upper layer, making it easy to see exactly what changed during a build.

---

## 5. How UnionFS is Different from a Regular Filesystem

| Feature | Regular Filesystem (ext4, APFS) | Union Filesystem |
|---|---|---|
| Stores data | On a physical disk partition | Delegates to underlying real filesystems |
| Number of sources | One directory = one location | Multiple directories merged into one view |
| Write destination | Wherever the file currently is | Always the upper (writable) layer |
| Read-only layers | Not natively supported | Core feature — lower layer can be read-only |
| Copy-on-Write | Optional (some filesystems have it) | Built-in, fundamental mechanism |
| File deletion | Actually deletes the bytes | Creates a "whiteout" marker to hide the file |
| Purpose | Persist data reliably | Compose layers, isolate writes, share base data |

A regular filesystem is like a physical filing cabinet — files live in specific drawers.

A union filesystem is like a set of overlapping transparent sheets — it is a **view** composed from multiple sources, not a storage system itself.

---

## 6. Core Concepts: Upper, Lower, Mount, COW, Whiteouts

These five concepts are the entire intellectual heart of this project. Understand these and the code will make complete sense.

### Lower Directory (Read-Only Base)

The **lower directory** is the base layer. It contains the original files. In this project, it is treated as **read-only** — the code never writes directly to it.

```
lower/
  test.txt     <- "Hello from lower!"
  config.cfg
  scripts/
    build.sh
```

Think of it as the "original" or "golden" state of the filesystem.

### Upper Directory (Writable Layer)

The **upper directory** is where all writes go. When you create a new file, modify an existing file, or delete a file — all of those changes end up in the upper directory.

```
upper/
  test.txt         <- modified version (overrides lower's test.txt)
  new_file.txt     <- created by the user (does not exist in lower)
  .wh.old_file.txt <- a "whiteout" — tells the system old_file.txt was deleted
```

### Mount Point (The Merged View)

The **mount point** is the directory where you see the unified, merged view. You interact with this directory as if it were a regular directory — you do not think about upper or lower.

```
mount/
  test.txt         <- shows upper's version (it overrides lower's)
  config.cfg       <- shows lower's version (upper does not have it)
  new_file.txt     <- shows upper's version (lower does not have it)
  (old_file.txt is HIDDEN — the whiteout in upper suppresses it)
```

### Copy-on-Write (COW)

This is the mechanism that keeps the lower directory untouched.

**The rule:** You may NEVER write directly to a lower-layer file.

**The mechanism:** If you try to write to a file that only exists in the lower layer, the system first **copies** the entire file from lower to upper, and THEN applies your write to the copy in upper.

```
Before write to test.txt:
  lower/test.txt  <- exists ("Hello from lower!")
  upper/test.txt  <- does NOT exist

User writes "I appended this!" to mount/test.txt

COW kicks in:
  Step 1: Copy lower/test.txt --> upper/test.txt
  Step 2: Apply the write to upper/test.txt

After write:
  lower/test.txt  <- UNCHANGED ("Hello from lower!")
  upper/test.txt  <- "Hello from lower!\nI appended this!"
```

This is why the lower directory is always safe. No matter what the user does at the mount point, the original lower files are never touched.

### Whiteouts (The Deletion Trick)

Here is a tricky problem: how do you "delete" a file from the merged view when the file lives in the read-only lower layer?

You cannot actually delete it from lower. So instead, you create a special marker file in upper called a **whiteout**. The naming convention is `.wh.<filename>`.

```
User deletes mount/config.cfg

What actually happens:
  lower/config.cfg     <- still exists (we cannot delete it)
  upper/.wh.config.cfg <- whiteout created

What the user sees at mount/config.cfg: ENOENT (file not found)

Why? Because when resolving any path, the code checks for a whiteout
first. If a whiteout exists for this path, it immediately returns
"file not found" without even checking if the file exists in lower.
```

---

## 7. What is FUSE?

**FUSE** stands for **Filesystem in Userspace**.

Normally, filesystems are implemented inside the Linux **kernel** — deep inside the OS, running with full privileges. Writing kernel code is extremely dangerous (a bug can crash the entire system) and requires expert knowledge.

**FUSE flips this around.** It lets you write a filesystem in a normal user program (like this C program), and the kernel acts as a translator:

```
User program: open("/mount/test.txt")
       |
       v
Linux Kernel: "This path is on a FUSE filesystem. I'll call the FUSE program."
       |
       v
FUSE Library: Calls your C function unionfs_open()
       |
       v
Your Code: Does whatever logic you need (check upper, check lower, etc.)
       |
       v
Returns result back to kernel --> back to user program
```

The kernel provides a special device `/dev/fuse` that acts as a communication channel between the kernel and your FUSE program.

**FUSE3** is the modern version of this interface (version 3). This project uses FUSE3.

**Why is FUSE used here?**
- macOS does not have OverlayFS built in.
- Even on Linux, writing a real kernel filesystem is overkill for a project.
- FUSE lets you implement the full filesystem logic in safe, debuggable C code.

**The FUSE operation table** is the key: you register function pointers for every filesystem operation (`getattr`, `read`, `write`, `readdir`, etc.), and FUSE calls your functions whenever the OS needs to perform those operations on your mounted filesystem.

---

## 8. Project Architecture

```
mini_UFS_team14/
|
+-- main.c          <- Entry point. Sets up FUSE, passes state (lower/upper dirs).
|
+-- include/
|   +-- state.h     <- Defines the global state struct (lower_dir, upper_dir).
|   |                  Also defines the UNIONFS_DATA macro to access state anywhere.
|   +-- path_utils.h<- Declares path helper functions.
|   +-- cow.h       <- Declares the cow_copy() function.
|   +-- ops_read.h  <- Declares read-side FUSE operations.
|   +-- ops_write.h <- Declares write-side FUSE operations.
|
+-- path_utils.c    <- Path helpers: build real paths, check upper, resolve where a
|                      file lives, build whiteout paths.
|
+-- cow.c           <- Copy-on-Write: copies a file from lower to upper before writes.
|
+-- ops_read.c      <- FUSE handlers: getattr, open, read, readdir.
|
+-- ops_write.c     <- FUSE handlers: write, create, unlink, mkdir, rmdir, truncate.
|
+-- lower/          <- Read-only base layer (original files live here).
|   +-- test.txt
|
+-- upper/          <- Writable layer (all modifications go here).
|   +-- test.txt    <- COW copy of lower/test.txt after it was modified.
|
+-- Makefile        <- Build system.
+-- Dockerfile      <- Container environment for running on macOS.
+-- run_in_docker.sh<- Script to build Docker image and launch container.
```

**Data flows in one direction for writes:**
```
User writes to mount/  -->  always lands in upper/
User reads from mount/ -->  served from upper/ first, then lower/
User deletes from mount/ --> creates .wh. file in upper/ (if file is in lower)
```

---

## 9. Line-by-Line Code Explanation

---

### include/state.h

This header defines the **shared global state** of the entire program. Every source file includes this.

```c
#ifndef STATE_H      // Include guard: prevents this file from being
#define STATE_H      // included more than once (avoids duplicate definitions)

#include <fuse.h>    // Need fuse_get_context() used in the macro below
```

```c
struct mini_unionfs_state {
    char *lower_dir;   // Absolute path to the lower (read-only base) directory
    char *upper_dir;   // Absolute path to the upper (writable) directory
};
```

This struct is the "brain" of the program. It holds the two critical paths.
It gets allocated once in `main()` and stored inside FUSE's private data slot.

```c
#define UNIONFS_DATA \
((struct mini_unionfs_state *) fuse_get_context()->private_data)
```

This is a **macro** — a text substitution. Wherever you write `UNIONFS_DATA` in code, the compiler replaces it with `((struct mini_unionfs_state *) fuse_get_context()->private_data)`.

What does this do?
- `fuse_get_context()` is a FUSE library function that returns a pointer to a `struct fuse_context`. This context is unique to the current thread/call.
- `->private_data` is a `void *` field inside `fuse_context` where you can store anything. We stored our `mini_unionfs_state` pointer here (done in `main()`).
- The cast `(struct mini_unionfs_state *)` converts the raw `void *` back to our typed pointer.

Why a macro? Because every FUSE callback needs access to `lower_dir` and `upper_dir`. The macro gives you a one-liner to get state from anywhere, without passing it as a parameter everywhere.

```c
#endif  // End of include guard
```

---

### include/path_utils.h

Declares 4 utility functions. This is just the **declaration** (the "promise" that these functions exist somewhere).

```c
#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <limits.h>   // For PATH_MAX (maximum path length, typically 4096 on Linux)

void make_path(const char *dir, const char *path, char *out);
// Builds a real filesystem path: dir + virtual_path -> actual path on disk
// Example: make_path("/home/user/upper", "/test.txt", out) -> out = "/home/user/upper/test.txt"

void make_whiteout_path(const char *upper_dir, const char *path, char *out);
// Builds the whiteout path for a given virtual path
// Example: make_whiteout_path("/upper", "/foo.txt", out) -> out = "/upper/.wh.foo.txt"

int resolve_path(const char *path, char *resolved);
// Given a virtual path (like "/test.txt"), finds where the actual file is:
//   - If whiteout exists -> return -ENOENT (file was deleted)
//   - If file is in upper -> return upper's real path
//   - If file is in lower -> return lower's real path
//   - Otherwise -> return -ENOENT

int is_in_upper(const char *path);
// Returns 1 if the virtual path has a real file in upper/, 0 otherwise

#endif
```

---

### include/cow.h

Extremely simple — just declares one function:

```c
#ifndef COW_H
#define COW_H

int cow_copy(const char *path);
// Copies a file from lower/ to upper/ byte-for-byte.
// Called before any write to a file that only exists in lower.
// Returns 0 on success, negative errno on failure.

#endif
```

---

### include/ops_read.h

Declares the 4 FUSE read-side handlers:

```c
int unionfs_getattr(...);  // stat() equivalent - get file metadata (size, permissions, etc.)
int unionfs_readdir(...);  // ls equivalent - list directory contents
int unionfs_open(...);     // open() - called when a file is opened
int unionfs_read(...);     // read() - called when file contents are read
```

---

### include/ops_write.h

Declares the 6 FUSE write-side handlers:

```c
int unionfs_write(...);     // write() - write data to a file
int unionfs_create(...);    // creat() / open(O_CREAT) - create a new file
int unionfs_unlink(...);    // unlink() - delete a file
int unionfs_mkdir(...);     // mkdir() - create a directory
int unionfs_rmdir(...);     // rmdir() - remove a directory
int unionfs_truncate(...);  // truncate() - resize a file
```

---

### path_utils.c

This is the routing engine of the entire project. Every operation passes through here to find where a file actually lives.

```c
#include "path_utils.h"
#include "state.h"    // For UNIONFS_DATA macro

#include <stdio.h>    // snprintf
#include <string.h>   // strcmp, strncpy
#include <sys/stat.h> // struct stat, lstat, mkdir
#include <unistd.h>   // access
#include <libgen.h>   // dirname(), basename()
#include <errno.h>    // errno, ENOENT
```

#### `make_path()`

```c
void make_path(const char *dir, const char *path, char *out) {
    snprintf(out, PATH_MAX, "%s%s", dir, path);
}
```

Dead simple. Concatenates a real directory path with a virtual path.

Example:
- `dir`  = `/home/user/project/upper`
- `path` = `/subdir/file.txt`  (this is the virtual path inside the union mount)
- `out`  = `/home/user/project/upper/subdir/file.txt`

Note: FUSE always gives you `path` with a leading `/`, so no separator is needed between `dir` and `path`.

---

#### `make_whiteout_path()`

```c
void make_whiteout_path(const char *upper_dir, const char *path, char *out) {
    char tmp[PATH_MAX], tmp2[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);    // Copy path (dirname() modifies its argument!)
    snprintf(tmp2, sizeof(tmp2), "%s", path);  // Second copy for basename()
```

`dirname()` and `basename()` are destructive — they modify the string passed to them. So we need two copies to safely call both functions on the same path.

```c
    char *dir_part = dirname(tmp);      // Parent directory of the path
    char *base_part = basename(tmp2);   // Filename portion of the path
```

Example: For `/subdir/file.txt`:
- `dir_part`  = `/subdir`
- `base_part` = `file.txt`

```c
    if (strcmp(dir_part, "/") == 0 || strcmp(dir_part, ".") == 0)
        snprintf(out, PATH_MAX, "%s/.wh.%s", upper_dir, base_part);
    else
        snprintf(out, PATH_MAX, "%s%s/.wh.%s", upper_dir, dir_part, base_part);
}
```

Why two cases?

- If the file is at the root of the mount (e.g., `/file.txt`), `dirname` returns `"/"` or `"."`.
  - Whiteout goes to: `upper_dir/.wh.file.txt`
- If the file is in a subdirectory (e.g., `/subdir/file.txt`), `dirname` returns `/subdir`.
  - Whiteout goes to: `upper_dir/subdir/.wh.file.txt`

Example outputs:
- `/file.txt` → `upper/.wh.file.txt`
- `/subdir/file.txt` → `upper/subdir/.wh.file.txt`

---

#### `resolve_path()`

This is the most critical function in the entire project. It answers: **"Where is this virtual file actually stored on disk?"**

```c
int resolve_path(const char *path, char *resolved) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    // Get the global state (lower_dir and upper_dir) from FUSE context
    struct stat st;
```

**Step 1: Check for whiteout first**

```c
    char wh_path[PATH_MAX];
    make_whiteout_path(state->upper_dir, path, wh_path);
    if (lstat(wh_path, &st) == 0)
        return -ENOENT;
```

- Build the whiteout path for this virtual path.
- `lstat()` checks if the whiteout file exists (without following symlinks).
- If it exists (`== 0` means success), the file has been "deleted" from the user's perspective.
- Return `-ENOENT` = "No such file or directory". The caller sees this as a missing file.

Why check whiteout first? Because the actual file might still physically exist in lower. Without checking whiteout first, we would incorrectly show the user a file they already deleted.

**Step 2: Check upper directory**

```c
    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);
    if (lstat(upper_path, &st) == 0) {
        strncpy(resolved, upper_path, PATH_MAX);
        return 0;
    }
```

- Build the real path in upper.
- If the file exists in upper, use it. Upper always has priority.
- Copy the real path into `resolved` and return success.

**Step 3: Check lower directory**

```c
    char lower_path[PATH_MAX];
    make_path(state->lower_dir, path, lower_path);
    if (lstat(lower_path, &st) == 0) {
        strncpy(resolved, lower_path, PATH_MAX);
        return 0;
    }
```

- If not in upper, check lower.
- If found in lower, return lower's real path.

**Step 4: File does not exist anywhere**

```c
    return -ENOENT;
}
```

The resolution priority is: **whiteout check → upper → lower → not found**.

---

#### `is_in_upper()`

```c
int is_in_upper(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    struct stat st;

    make_path(state->upper_dir, path, upper_path);
    return (lstat(upper_path, &st) == 0);
    // Returns 1 if file exists in upper, 0 if not
}
```

Simple helper used before writes. "Does this file already have a copy in upper?"
If yes → write directly to upper. If no → trigger COW first.

---

### cow.c

This implements the Copy-on-Write mechanism. It physically copies a file from lower to upper so that the write can proceed safely.

```c
#include "cow.h"
#include "path_utils.h"
#include "state.h"

#include <fcntl.h>    // open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC
#include <unistd.h>   // read(), write(), close()
#include <sys/stat.h> // fstat(), struct stat, mkdir()
#include <string.h>   // strncpy
#include <libgen.h>   // dirname()
#include <errno.h>    // errno, EIO
#include <stdio.h>    // snprintf
```

```c
int cow_copy(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    char lower_path[PATH_MAX], upper_path[PATH_MAX];
    make_path(state->lower_dir, path, lower_path);  // e.g. /project/lower/test.txt
    make_path(state->upper_dir, path, upper_path);  // e.g. /project/upper/test.txt
```

Build the source (lower) and destination (upper) real paths.

**Parent directory creation (the mkdir -p equivalent):**

```c
    char upper_dir_copy[PATH_MAX];
    strncpy(upper_dir_copy, upper_path, PATH_MAX);
    char *parent = dirname(upper_dir_copy);
    // parent = "/project/upper/subdir" (the directory that needs to exist before we write)

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", parent);

    char *p = tmp + 1;  // Start one past the leading '/' to avoid mangling the root
    while (*p) {
        if (*p == '/') {
            *p = '\0';           // Temporarily null-terminate at this '/'
            mkdir(tmp, 0755);    // Create the partial path (e.g., "/project/upper")
            *p = '/';            // Restore the '/'
        }
        p++;
    }
    mkdir(tmp, 0755);  // Create the final full parent directory
```

This is a manual `mkdir -p`. It walks through the path string character by character, temporarily replacing each `/` with a null terminator to get a prefix, calls `mkdir()` on that prefix, then restores the `/`. This creates all intermediate directories before creating the final parent.

Why? If you are copying `lower/a/b/c/file.txt` to `upper/`, but `upper/a/b/c/` does not exist yet, the `open()` call to create the destination file would fail. This mkdir loop ensures the directory hierarchy exists in upper before we try to write to it.

**Open source file:**

```c
    int src = open(lower_path, O_RDONLY);  // Open source (lower) for reading
    if (src < 0) return -errno;            // Return negative errno on failure
```

```c
    struct stat st;
    if (fstat(src, &st) < 0) {    // Get file metadata (especially st_mode for permissions)
        close(src);
        return -errno;
    }
```

`fstat()` on the open file descriptor gives us the file's metadata — size, permissions (`st_mode`), timestamps, etc. We specifically need `st_mode` to create the destination file with the same permissions as the original.

**Open destination file:**

```c
    int dst = open(upper_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    // O_WRONLY  -> open for writing only
    // O_CREAT   -> create the file if it does not exist
    // O_TRUNC   -> if it exists already, truncate it to zero (start fresh)
    // st.st_mode-> preserve the original file's permissions
    if (dst < 0) {
        close(src);
        return -errno;
    }
```

**The copy loop:**

```c
    char buf[65536];   // 64KB buffer (larger buffer = fewer system calls = faster copy)
    ssize_t n;

    while ((n = read(src, buf, sizeof(buf))) > 0) {
        // Read up to 64KB from source into buf
        if (write(dst, buf, n) != n) {
            // Write exactly n bytes to destination
            // If write returns fewer than n bytes, something went wrong (disk full, etc.)
            close(src); close(dst);
            return -EIO;   // -EIO = Input/Output error
        }
    }
```

This is a classic file copy loop:
- Read up to 64KB at a time from `src`
- Write exactly that many bytes to `dst`
- Repeat until `read()` returns 0 (EOF, meaning the entire file has been read)
- If `write()` writes fewer bytes than expected, that is an error

```c
    close(src);
    close(dst);
    return 0;  // Success
}
```

After this function returns, `upper_path` is an exact byte-for-byte copy of `lower_path` with the same permissions. Now writes can safely be applied to the upper copy.

---

### ops_read.c

Handles all read-side FUSE operations.

```c
#include "ops_read.h"
#include "path_utils.h"
#include "cow.h"
#include "state.h"

#include <fcntl.h>    // O_RDONLY, O_ACCMODE
#include <unistd.h>   // pread, close
#include <dirent.h>   // DIR, struct dirent, opendir, readdir, closedir
#include <string.h>   // strcmp, strncmp
#include <stdlib.h>   // malloc, free
#include <errno.h>    // errno
#include <stdio.h>    // snprintf
```

---

#### `unionfs_getattr()`

Called whenever someone does `stat()`, `ls -l`, or anything that needs file metadata.

```c
int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;   // We do not use fi in this implementation — suppress compiler warning
```

```c
    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    // Ask: "where does this virtual path actually live?" (upper or lower)
    if (res < 0) return res;
    // If resolve_path returned -ENOENT (not found or whiteout), pass that error back to FUSE
```

```c
    if (lstat(resolved, stbuf) < 0)
        return -errno;
    // lstat() fills in stbuf with: file size, permissions, timestamps, inode, etc.
    // lstat (not stat) because we do not want to follow symlinks

    return 0;
}
```

What the user experiences: They run `ls -l` → the OS calls `getattr` on every file → FUSE calls this function → this function resolves the path to either upper or lower → calls `lstat` on the real file → returns metadata to FUSE → displayed by `ls`.

---

#### `unionfs_open()`

Called when a program calls `open()` on a file.

```c
int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res < 0) return res;
    // First verify the file exists at all (and is not whiteout-deleted)
```

```c
    if ((fi->flags & O_ACCMODE) != O_RDONLY && !is_in_upper(path)) {
        res = cow_copy(path);
        if (res < 0) return res;
    }
```

This is the **COW trigger at open time**.

- `fi->flags & O_ACCMODE` extracts the access mode flags from the open call.
- `!= O_RDONLY` means the file is being opened for writing (O_WRONLY or O_RDWR).
- `!is_in_upper(path)` means the file currently only exists in lower.
- If both are true: trigger `cow_copy()` to bring the file from lower to upper before allowing the write.

Why at open time? Because we want the copy to happen once, atomically, before any writes happen. If we waited until `write()` was called, partial writes could happen to an incomplete state.

```c
    return 0;
    // We do not store file descriptors in fi->fh here.
    // Every read/write re-opens the file using the resolved path.
    // Simpler but slightly less efficient than caching file descriptors.
}
```

---

#### `unionfs_read()`

Called when a program reads file contents.

```c
int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res < 0) return res;
    // Find where the file actually is (upper or lower)
```

```c
    int fd = open(resolved, O_RDONLY);
    if (fd < 0) return -errno;
    // Open the actual file on disk (in either upper or lower)
```

```c
    res = pread(fd, buf, size, offset);
    // pread() reads 'size' bytes from the file starting at 'offset'
    // pread is like read() but lets you specify a position without seeking
    // This matters because FUSE might call read() multiple times at different offsets
    if (res < 0) res = -errno;

    close(fd);
    return res;
    // Returns number of bytes actually read (or negative error code)
}
```

---

#### `unionfs_readdir()`

This is the most complex function in the project. Called when you do `ls` on a directory — it must return a merged list of files from both upper and lower, with duplicates and whiteouts handled correctly.

```c
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags)
{
    (void) offset; (void) fi; (void) flags;
    // Unused parameters — we use a simple directory scanning approach
    // that does not need offsets or cached file info

    struct mini_unionfs_state *state = UNIONFS_DATA;
    DIR *dp;             // Directory pointer (like FILE* but for directories)
    struct dirent *de;   // Directory entry (one file/dir inside a directory)
```

**The "seen" list for deduplication:**

```c
    struct seen_entry {
        char name[NAME_MAX + 1];    // NAME_MAX = 255, +1 for null terminator
        struct seen_entry *next;    // Linked list pointer
    };
    struct seen_entry *seen_head = NULL;   // Start with empty list
```

This linked list tracks which filenames we have already added to the directory listing. It prevents showing the same file twice (once from upper, once from lower).

```c
    #define NAME_SEEN(nm) ({ \
        int found = 0; \
        struct seen_entry *cur = seen_head; \
        while (cur) { \
            if (strcmp(cur->name, (nm)) == 0) { found = 1; break; } \
            cur = cur->next; \
        } \
        found; \
    })
    // This macro walks the linked list and returns 1 if (nm) is already in the list
```

```c
    #define MARK_SEEN(nm) do { \
        struct seen_entry *e = malloc(sizeof(*e)); \
        strncpy(e->name, (nm), NAME_MAX); \
        e->name[NAME_MAX] = '\0'; \
        e->next = seen_head; \
        seen_head = e; \
    } while(0)
    // This macro adds (nm) to the front of the linked list (prepend is O(1))
```

**Standard directory entries:**

```c
    filler(buf, ".", NULL, 0, 0);   // Add "." (current directory)
    filler(buf, "..", NULL, 0, 0);  // Add ".." (parent directory)
    // filler() is a FUSE-provided callback to add an entry to the directory listing
```

**Scan upper directory first (upper has priority):**

```c
    char upper_dir_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_dir_path);

    dp = opendir(upper_dir_path);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;  // Skip . and .. — already added above
```

```c
            if (strncmp(de->d_name, ".wh.", 4) == 0)
                continue;  // Skip whiteout files — never show .wh.foo to the user
```

This is critical: whiteout files (`.wh.foo`) are internal implementation details. The user must never see them in a directory listing.

```c
            filler(buf, de->d_name, NULL, 0, 0);   // Add this file to the listing
            MARK_SEEN(de->d_name);                  // Remember we have seen this name
        }
        closedir(dp);
    }
```

**Scan lower directory second:**

```c
    char lower_dir_path[PATH_MAX];
    make_path(state->lower_dir, path, lower_dir_path);

    dp = opendir(lower_dir_path);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            if (NAME_SEEN(de->d_name))
                continue;
            // If upper already had this filename, skip it.
            // Upper's version wins — even if it is a completely different file.
```

```c
            char wh_buf[PATH_MAX];
            char vpath[PATH_MAX];
            snprintf(vpath, sizeof(vpath), "%s/%s", path, de->d_name);
            // Build the virtual path for this file (e.g., "/subdir/file.txt")

            make_whiteout_path(state->upper_dir, vpath, wh_buf);
            // Build the whiteout path for this file

            struct stat wh_st;
            if (lstat(wh_buf, &wh_st) == 0)
                continue;
            // Check if a whiteout exists for this lower-layer file.
            // If yes, this file was "deleted" — do not show it.
```

```c
            filler(buf, de->d_name, NULL, 0, 0);   // Add this lower-layer file
            MARK_SEEN(de->d_name);
        }
        closedir(dp);
    }
```

**Cleanup:**

```c
    while (seen_head) {
        struct seen_entry *tmp = seen_head;
        seen_head = seen_head->next;
        free(tmp);          // Free each node in the linked list to avoid memory leaks
    }

    return 0;
}
```

**Summary of readdir logic:**
1. Add `.` and `..`
2. Scan upper: add all non-whiteout files, mark them as seen
3. Scan lower: for each file NOT already seen AND NOT whiteout-deleted, add it
4. Result: merged listing with upper taking priority, deleted files hidden

---

### ops_write.c

Handles all write-side FUSE operations.

```c
#include "ops_write.h"
#include "path_utils.h"
#include "cow.h"
#include "state.h"
#include <fcntl.h>    // open, O_CREAT, O_WRONLY, O_TRUNC
#include <unistd.h>   // pwrite, close, unlink, rmdir
#include <sys/stat.h> // mkdir
#include <errno.h>    // errno
```

---

#### `unionfs_create()`

Called when a user creates a new file (e.g., `touch newfile.txt` or `> newfile.txt`).

```c
int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;

    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);
    // New files always go to upper — never lower
```

```c
    int fd = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    // O_CREAT -> create if not exists
    // O_WRONLY -> open for writing
    // O_TRUNC  -> truncate to 0 if already exists
    // mode     -> permissions (e.g., 0644)
    if (fd < 0) return -errno;
    close(fd);   // We do not need to keep it open — FUSE will call open() separately
```

```c
    char wh_path[PATH_MAX];
    make_whiteout_path(state->upper_dir, path, wh_path);
    unlink(wh_path);
    // If a whiteout existed for this name (meaning the user had previously deleted
    // a lower-layer file with this name), remove the whiteout.
    // Creating a new file with the same name "resurrects" it — the whiteout is stale.

    return 0;
}
```

Scenario this handles: User deletes `foo.txt` (whiteout created). User creates a new `foo.txt`. The new create removes the whiteout so the new file is visible again.

---

#### `unionfs_write()`

Called when data is written to a file.

```c
int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    if (!is_in_upper(path)) {
        int res = cow_copy(path);
        if (res < 0) return res;
    }
    // COW: if the file only exists in lower, copy it to upper first.
    // After this, we are guaranteed the file exists in upper.
```

```c
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);

    int fd = open(upper_path, O_WRONLY);
    if (fd < 0) return -errno;

    int res = pwrite(fd, buf, size, offset);
    // pwrite() writes 'size' bytes from 'buf' at position 'offset' in the file
    // It is like lseek + write, but atomic (no separate seek step needed)
    if (res < 0) res = -errno;

    close(fd);
    return res;
    // Returns number of bytes written (or negative error)
}
```

---

#### `unionfs_unlink()`

Called when a user deletes a file (`rm filename`).

```c
int unionfs_unlink(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    if (is_in_upper(path)) {
        // Case 1: The file exists in upper (either COW-copied or newly created)
        char upper_path[PATH_MAX];
        make_path(state->upper_dir, path, upper_path);
        if (unlink(upper_path) < 0) return -errno;
        // Simply delete it from upper.
        // Note: if the file was a COW copy of a lower file, deleting the upper copy
        // would re-expose the lower original. A production implementation would also
        // create a whiteout here to fully hide the lower version.
    } else {
        // Case 2: The file only exists in lower (never been touched)
        // We cannot delete the lower file directly (it is read-only by design)
        // Solution: create a whiteout in upper to hide it
        char wh_path[PATH_MAX];
        make_whiteout_path(state->upper_dir, path, wh_path);
        int fd = open(wh_path, O_CREAT | O_WRONLY, 0000);
        // Create the whiteout file with permissions 0000 (no access for anyone)
        // The permissions do not matter much — its existence is the signal
        if (fd < 0) return -errno;
        close(fd);
    }

    return 0;
}
```

The whiteout trick in action: The user sees the file disappear. Under the hood, the lower file still exists, but the whiteout tells `resolve_path()` to return `-ENOENT` for it. To the user, it is gone.

---

#### `unionfs_mkdir()`

```c
int unionfs_mkdir(const char *path, mode_t mode) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);
    if (mkdir(upper_path, mode) < 0) return -errno;
    return 0;
    // New directories are always created in upper.
    // Once in upper, readdir will show them in the merged view.
}
```

---

#### `unionfs_rmdir()`

```c
int unionfs_rmdir(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);
    if (rmdir(upper_path) < 0) return -errno;
    return 0;
    // Only handles directories in upper. A production implementation would
    // also need to handle removing directories that exist in lower
    // (would require a directory-level whiteout mechanism).
}
```

---

#### `unionfs_truncate()`

Called when a file's size is changed (e.g., `truncate -s 0 file.txt`, or when opening with `O_TRUNC`).

```c
int unionfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;

    if (!is_in_upper(path)) {
        int res = cow_copy(path);
        if (res < 0) return res;
    }
    // COW again: if the file is only in lower, bring it to upper first.
    // We cannot truncate a lower-layer file directly.
```

```c
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    make_path(state->upper_dir, path, upper_path);

    if (truncate(upper_path, size) < 0)
        return -errno;
    // truncate() sets the file's size to exactly 'size' bytes.
    // If size < current size: file is cut (data beyond 'size' is lost).
    // If size > current size: file is padded with zero bytes.

    return 0;
}
```

---

### main.c

The entry point. Sets up FUSE with the union filesystem operations.

```c
#define FUSE_USE_VERSION 31   // Tell the FUSE library we are targeting FUSE API version 3.1
                               // Must be defined BEFORE including fuse.h

#include <fuse.h>    // The FUSE library — provides fuse_main(), fuse_get_context(), etc.
#include <stdlib.h>  // calloc, free
#include <stdio.h>   // fprintf, stderr
#include <limits.h>  // PATH_MAX

#include "include/state.h"     // struct mini_unionfs_state
#include "include/ops_read.h"  // unionfs_getattr, unionfs_readdir, etc.
#include "include/ops_write.h" // unionfs_write, unionfs_create, etc.
```

**The operation table:**

```c
static struct fuse_operations unionfs_oper = {
    .getattr  = unionfs_getattr,   // stat()   -> get file metadata
    .readdir  = unionfs_readdir,   // ls       -> list directory
    .open     = unionfs_open,      // open()   -> open a file (COW trigger)
    .read     = unionfs_read,      // read()   -> read file data
    .write    = unionfs_write,     // write()  -> write file data (COW trigger)
    .create   = unionfs_create,    // creat()  -> create a new file
    .unlink   = unionfs_unlink,    // unlink() -> delete a file (whiteout if in lower)
    .mkdir    = unionfs_mkdir,     // mkdir()  -> create directory in upper
    .rmdir    = unionfs_rmdir,     // rmdir()  -> remove directory from upper
    .truncate = unionfs_truncate,  // truncate()-> resize file (COW trigger)
};
```

This struct is a C structure with function pointer fields. Each `.fieldname = function` sets one function pointer. When FUSE needs to perform a filesystem operation, it calls the corresponding function pointer from this struct.

Think of it as a plugin interface: FUSE defines the interface (the struct), you fill in your implementations.

```c
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_point>\n", argv[0]);
        return 1;
    }
    // We need exactly 3 directory arguments:
    // argv[1] = lower directory path
    // argv[2] = upper directory path
    // argv[3] = mount point path
```

```c
    struct mini_unionfs_state *state = calloc(1, sizeof(*state));
    // calloc(count, size) = malloc + zero-initialize all bytes
    // Allocate one mini_unionfs_state struct
```

```c
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);
    // realpath() converts a relative path to an absolute canonical path
    // (resolves "..", symlinks, etc.)
    // e.g., "lower" -> "/home/user/project/lower"
    // Passing NULL as second arg means realpath allocates the output buffer for us
```

**Building FUSE's argument list:**

```c
    char *fuse_argv[4];
    fuse_argv[0] = argv[0];   // Program name (fuse_main needs this)
    fuse_argv[1] = argv[3];   // Mount point (where to mount the filesystem)
    fuse_argv[2] = "-f";      // Foreground mode — do not daemonize (stay in terminal)
    fuse_argv[3] = NULL;      // NULL terminator for the array
```

We pass only 3 args (index 0, 1, 2) to `fuse_main`. We purposely skip `argv[1]` (lower) and `argv[2]` (upper) because FUSE does not need to know about them — our code handles those internally via the `state` struct.

```c
    return fuse_main(3, fuse_argv, &unionfs_oper, state);
    // fuse_main() does everything:
    //   - Parses FUSE arguments
    //   - Mounts the filesystem at fuse_argv[1]
    //   - Stores 'state' as private_data (accessible via UNIONFS_DATA macro)
    //   - Enters the event loop: listens for kernel filesystem calls
    //   - For each kernel call -> calls the matching function in unionfs_oper
    //   - Runs until unmounted or Ctrl+C
}
```

`fuse_main` is a blocking call. The program stays running as long as the filesystem is mounted. This is the event loop — it is the same concept as a web server waiting for HTTP requests, except here it is waiting for filesystem syscalls.

---

### Makefile

```makefile
CC = gcc              # Use the GCC compiler

# -Wall:    Enable all standard warnings
# -Wextra:  Enable extra warnings beyond -Wall
# -g:       Include debug symbols (for gdb/valgrind)
# -Iinclude: Tell compiler to look in ./include/ for header files
# -DFUSE_USE_VERSION=31: Define this as a compiler flag (same effect as #define)
CFLAGS = -Wall -Wextra -g -Iinclude -DFUSE_USE_VERSION=31

# pkg-config is a utility that tells you compile/link flags for installed libraries
# "pkg-config fuse3 --cflags" outputs something like: -I/usr/include/fuse3
FUSE_CFLAGS = $(shell pkg-config fuse3 --cflags)

# "pkg-config fuse3 --libs" outputs something like: -lfuse3 -lpthread
FUSE_LIBS   = $(shell pkg-config fuse3 --libs)

SRC = main.c path_utils.c cow.c ops_read.c ops_write.c   # All .c source files
OBJ = $(SRC:.c=.o)   # Pattern substitution: replace .c with .o -> all object files
TARGET = mini_unionfs # Name of the output binary

all: $(TARGET)        # Default target: build the binary

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(FUSE_LIBS)
	# Link all .o files together + FUSE library -> produces the binary

%.o: %.c
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -c $< -o $@
	# Pattern rule: to make any .o, compile the corresponding .c
	# $< = the .c file (source), $@ = the .o file (target)
	# -c = compile only, do not link

clean_obj:
	rm -f $(OBJ)      # Delete only object files, keep the binary

clean_all:
	rm -f $(OBJ) $(TARGET)   # Delete everything

run: $(TARGET)
	./$(TARGET) lower upper mount -f
	# Helper target: build and run with default directories
```

---

### Dockerfile and run_in_docker.sh

**Dockerfile:**

```dockerfile
FROM ubuntu:22.04
# Base image: Ubuntu 22.04 LTS

ENV DEBIAN_FRONTEND=noninteractive
# Prevents apt from asking timezone questions during install

RUN apt-get update && apt-get install -y \
    fuse3 \           # The FUSE runtime (kernel module + /dev/fuse device)
    libfuse3-dev \    # FUSE development headers + libfuse3.so for compilation
    pkg-config \      # Needed by Makefile to auto-detect FUSE flags
    build-essential \ # gcc, make, and other essential build tools
    sudo \            # Needed for mounting (requires elevated privileges)
    && rm -rf /var/lib/apt/lists/*   # Clean apt cache to reduce image size

WORKDIR /app    # Set /app as the working directory inside the container

CMD ["bash"]    # Default command: start a bash shell
```

Why Docker? FUSE requires the Linux kernel's FUSE module (`/dev/fuse`). macOS does not have this. Docker runs a real Linux kernel inside a VM on macOS, so FUSE works there.

**run_in_docker.sh:**

```bash
cd "$(dirname "$0")"   # cd to the script's own directory (works when run from anywhere)

IMAGE_NAME="mini_unionfs_env"   # Docker image name

docker build -t "$IMAGE_NAME" .  # Build the Docker image from Dockerfile

docker run \
    --rm \              # Remove container when it exits (no leftover containers)
    -it \               # Interactive terminal (stdin + TTY — lets you type commands)
    -v "$PWD:/app" \    # Mount current directory into /app inside the container
                        # So your source files are available and editable inside Docker
    --privileged \      # Give the container full Linux capabilities
    --device /dev/fuse \# Expose the host's /dev/fuse device to the container
                        # REQUIRED for FUSE to work — this is the kernel communication channel
    "$IMAGE_NAME"
```

The `--privileged` and `--device /dev/fuse` flags are what make FUSE work inside Docker. Without them, the container would not have access to the FUSE device that the kernel uses to communicate with the FUSE process.

---

## 10. Complete Data Flow: What Happens When You Do X

### `cat mount/test.txt` (reading a file that was modified)

```
1. Shell calls open("/mount/test.txt")
2. Linux kernel: "this path is on a FUSE mount, call the FUSE program"
3. FUSE calls unionfs_open("/test.txt", fi)
   -> resolve_path("/test.txt"):
       check whiteout: upper/.wh.test.txt -> does not exist
       check upper:    upper/test.txt     -> EXISTS (modified earlier)
       return upper/test.txt
   -> fi->flags is O_RDONLY -> no COW needed
   -> return 0 (success)

4. Shell calls read() on the opened file
5. FUSE calls unionfs_read("/test.txt", buf, size, offset, fi)
   -> resolve_path("/test.txt") -> upper/test.txt
   -> open(upper/test.txt, O_RDONLY)
   -> pread(fd, buf, size, offset)
   -> returns "Hello from lower!\nI appended this!\n"
   -> close(fd)
   -> user sees the modified content
```

### `echo "new data" >> mount/test.txt` (appending — file only exists in lower)

```
1. Shell calls open("/mount/test.txt", O_WRONLY | O_APPEND)
2. FUSE calls unionfs_open("/test.txt", fi)
   -> resolve_path: finds it in lower (upper does not have it yet)
   -> fi->flags != O_RDONLY AND !is_in_upper -> trigger cow_copy("/test.txt")
   -> cow_copy:
       opens lower/test.txt for reading
       creates upper/test.txt (same permissions)
       copies all bytes: "Hello from lower!\n"
       closes both files

3. Shell calls write() with "new data\n"
4. FUSE calls unionfs_write("/test.txt", "new data\n", 9, offset, fi)
   -> is_in_upper -> now true (we just COW'd it)
   -> opens upper/test.txt for writing
   -> pwrite(fd, "new data\n", 9, offset)
   -> closes fd
   -> lower/test.txt is UNCHANGED: still "Hello from lower!\n"
   -> upper/test.txt: "Hello from lower!\nnew data\n"
```

### `rm mount/config.cfg` (deleting a file that only exists in lower)

```
1. Shell calls unlink("/mount/config.cfg")
2. FUSE calls unionfs_unlink("/config.cfg")
   -> is_in_upper("/config.cfg") -> false (only in lower)
   -> make_whiteout_path: upper/.wh.config.cfg
   -> open(upper/.wh.config.cfg, O_CREAT | O_WRONLY, 0000)
   -> close -> whiteout file now exists in upper

3. Now, any access to /config.cfg:
   -> resolve_path checks whiteout -> upper/.wh.config.cfg EXISTS
   -> returns -ENOENT immediately (before even checking lower)
   -> the file appears deleted to the user

4. lower/config.cfg still exists on disk, completely untouched.
```

### `ls mount/` (listing a directory with mixed content)

```
1. Shell calls opendir("/mount/"), then readdir()
2. FUSE calls unionfs_readdir("/", buf, filler, ...)

   Add "." and ".."

   Scan upper/:
     - test.txt     -> add to listing, mark seen
     - .wh.config.cfg -> skip (starts with ".wh." — internal whiteout marker)

   Scan lower/:
     - test.txt   -> NAME_SEEN("test.txt") is true -> skip
                     (upper already has this, upper wins)
     - config.cfg -> check whiteout: upper/.wh.config.cfg EXISTS -> skip
                     (file was deleted, hide it)
     - scripts/   -> not seen, no whiteout -> add to listing, mark seen

   Result shown to user: test.txt, scripts/
   (config.cfg hidden by whiteout, lower/test.txt hidden by upper version)
```

### `touch mount/newfile.txt` (creating a brand new file)

```
1. Shell calls open("/mount/newfile.txt", O_CREAT | O_WRONLY, 0644)
2. FUSE calls unionfs_create("/newfile.txt", 0644, fi)
   -> make_path: upper/newfile.txt
   -> open(upper/newfile.txt, O_CREAT | O_WRONLY | O_TRUNC, 0644)
   -> close (empty file now exists in upper)
   -> check for stale whiteout: upper/.wh.newfile.txt -> unlink it (if exists)
   -> return 0

3. newfile.txt is now visible in mount/, served from upper/newfile.txt
4. lower/ is completely unaffected
```

---

## 11. How to Build and Run

### On Linux (native)

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install fuse3 libfuse3-dev pkg-config build-essential

# Build
make

# Create directories
mkdir -p lower upper mount

# Add a test file to lower (simulating a "base" system)
echo "Hello from lower!" > lower/test.txt

# Mount the union filesystem
./mini_unionfs lower upper mount

# In another terminal, interact with mount/:
ls mount/            # See merged contents
cat mount/test.txt   # Reads from lower (upper does not have it yet)

echo "change!" >> mount/test.txt  # COW: copies to upper, then writes
cat mount/test.txt   # Shows modified version (from upper)
cat lower/test.txt   # Original UNCHANGED: "Hello from lower!"

ls upper/            # See upper/test.txt was created by COW

rm mount/test.txt    # Creates upper/.wh.test.txt (whiteout)
ls mount/            # test.txt is gone (whiteout hides it)
ls lower/            # lower/test.txt still exists!
ls upper/            # upper/.wh.test.txt exists

# Unmount when done
fusermount -u mount
```

### On macOS (via Docker)

```bash
# Make the script executable (first time only)
chmod +x run_in_docker.sh

# Launch Docker container with FUSE support
./run_in_docker.sh

# Inside the container, you are in /app with your source code:
make
mkdir -p lower upper mount
echo "Hello from lower!" > lower/test.txt
./mini_unionfs lower upper mount &   # Run in background (or use a second terminal)

# Test it
ls mount/
cat mount/test.txt

# Unmount
fusermount -u mount
```

---

## Summary: The Complete Mental Model

```
+-----------------------------------------------------+
|                   USER / SHELL                       |
|   cat file.txt  |  echo "x" > file.txt  |  rm file  |
+--------------------+--------------------------------+
                     |
                     | syscalls (open, read, write, unlink...)
                     |
+--------------------v--------------------------------+
|                 LINUX KERNEL                         |
|         "this path is on a FUSE mount"               |
+--------------------+--------------------------------+
                     |
                     | /dev/fuse (communication channel)
                     |
+--------------------v--------------------------------+
|              mini_unionfs (this program)             |
|                                                      |
|  resolve_path() -> check whiteout -> check upper    |
|                                   -> check lower     |
|                                                      |
|  cow_copy()    -> copy lower -> upper before write   |
|                                                      |
|  readdir()     -> merge upper + lower, deduplicate, |
|                   hide whiteouts, hide overridden    |
+------------------+------------------+---------------+
                   |                  |
       +-----------v------+   +-------v-----------+
       |   upper/         |   |    lower/          |
       |  (writable)      |   |  (read-only)       |
       |                  |   |                    |
       | new_file.txt     |   | base_file.txt      |
       | test.txt (COW'd) |   | test.txt  <--------+-- hidden by upper version
       | .wh.del.txt      |   | del.txt   <--------+-- hidden by whiteout
       +------------------+   +--------------------+
```

**The three laws of this project:**

1. **Reads always go upper-first** — upper's version wins over lower's version.
2. **Writes always go to upper** — lower is never modified, ever.
3. **Deletes create whiteouts** — the lower file stays on disk, but becomes invisible at the mount point.

That is the entire union filesystem, implemented in approximately 300 lines of C.
