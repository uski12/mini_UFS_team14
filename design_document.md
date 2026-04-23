# Mini-UnionFS Design Document

**Project:** Mini-UnionFS — A Userspace Union File System  
**Language:** C (FUSE3 / libfuse)  
**Environment:** Ubuntu 22.04 LTS  

---

## 1. Introduction

Mini-UnionFS is a simplified Union File System built in userspace using FUSE (Filesystem in Userspace). It replicates the core mechanism used by Docker's OverlayFS driver: multiple directory layers are merged into a single virtual mount point. The filesystem accepts two underlying directories:

| Layer | Path | Access |
|-------|------|--------|
| **lower_dir** | Base image (e.g., a read-only OS image) | Read-only |
| **upper_dir** | Container layer (accumulates changes) | Read-write |

The user interacts with a unified **mount_point** that presents a merged view of both layers.

---

## 2. Architecture Overview

```
 User Process
     │
     │  POSIX syscalls (read, write, unlink, …)
     ▼
 Linux VFS (kernel)
     │
     │  FUSE kernel module → forwards to userspace
     ▼
 mini_unionfs (our program)
     │
     ├─ resolve_path() ──► upper_dir  (read-write)
     │                          │
     └──────────────────► lower_dir  (read-only base)
```

FUSE intercepts every filesystem call at the kernel level and routes it to our registered callback functions (the `fuse_operations` struct). We intercept the call, resolve which underlying path it should act on, and perform the real filesystem operation there.

---

## 3. Key Data Structures

### 3.1 `mini_unionfs_state`

```c
struct mini_unionfs_state {
    char *lower_dir;   // absolute path to the lower (base) directory
    char *upper_dir;   // absolute path to the upper (writable) directory
};
```

This struct is allocated in `main()` and passed as the `private_data` pointer to `fuse_main()`. Every FUSE callback retrieves it via `fuse_get_context()->private_data` through the `UNIONFS_DATA` macro.

### 3.2 Whiteout Files

Whiteouts are zero-byte sentinel files stored in `upper_dir`. For a file named `foo.txt` deleted from `lower_dir`, we create `upper_dir/.wh.foo.txt`. The naming convention mirrors Docker's OverlayFS whiteout format.

### 3.3 Seen-Name List (readdir deduplication)

During `readdir`, we maintain a lightweight singly-linked list of `seen_entry` nodes (heap-allocated, freed at end of call) to track which filenames have already been added to the listing from the upper layer, preventing duplicates when the same file exists in both layers.

---

## 4. Core Algorithm: Path Resolution

The `resolve_path(path, out)` function is the single most important routine in the implementation. It is called by almost every FUSE callback.

```
resolve_path(path):
  1.  Build wh_path = upper_dir + "/.wh." + basename(path)
  2.  If lstat(wh_path) succeeds  →  return -ENOENT  (whiteout active)
  3.  Build upper_path = upper_dir + path
  4.  If lstat(upper_path) succeeds  →  out = upper_path, return 0
  5.  Build lower_path = lower_dir + path
  6.  If lstat(lower_path) succeeds  →  out = lower_path, return 0
  7.  return -ENOENT
```

This guarantees: **upper always wins**, **whiteouts hide lower files**, **unknown files are cleanly rejected**.

---

## 5. FUSE Operation Mapping

| FUSE Operation | Implementation Summary |
|----------------|------------------------|
| `getattr` | `resolve_path()` then `lstat()` on resolved path |
| `readdir` | Scans upper, then lower; deduplicates; hides whiteouts & `.wh.*` files |
| `open` | Triggers CoW if file is only in lower and opened for writing |
| `read` | `resolve_path()` → `pread()` |
| `write` | CoW if needed → `pwrite()` to upper path |
| `create` | `open(O_CREAT)` in upper; removes stale whiteout if present |
| `unlink` | If in upper: physical delete. If lower-only: create `.wh.<name>` in upper |
| `mkdir` | `mkdir()` directly in upper layer |
| `rmdir` | `rmdir()` directly in upper layer |
| `truncate` | CoW if needed → `truncate()` on upper path |

---

## 6. Copy-on-Write (CoW)

CoW is triggered in `unionfs_open()` and `unionfs_write()` when the target file exists only in `lower_dir`. The `cow_copy()` function:

1. Builds `lower_path` and `upper_path`.
2. Creates any missing parent directories in `upper_dir` (via an iterative `mkdir`).
3. Opens the source in `lower_dir` for reading, stats it to get the permission mode.
4. Creates the destination in `upper_dir` with `O_CREAT | O_TRUNC` and the same mode.
5. Copies the file contents in 64 KiB chunks.

After CoW, the lower file is **never touched**. All subsequent reads and writes resolve to the upper copy.

---

## 7. Whiteout (Deletion) Mechanism

When a user calls `unlink()` (or `rm`) on a file:

- **If the file is in `upper_dir`**: It is physically deleted (`unlink()`).
- **If the file originates from `lower_dir`**: We cannot delete the lower file (it is read-only conceptually). Instead we create `upper_dir/.wh.<filename>` — a zero-byte marker. The `resolve_path()` function checks for this marker first, causing the file to appear non-existent in any future lookup.

This means the lower layer is never mutated, matching Docker's layer immutability guarantee.

---

## 8. Edge Cases Handled

| Scenario | Handling |
|----------|----------|
| File in both upper and lower | `resolve_path` returns upper path (upper wins) |
| Deleted file re-created | `create` removes stale whiteout with `unlink(wh_path)` |
| Write to lower-only file | CoW triggered before any write via `cow_copy()` |
| `readdir` shows `.wh.*` filenames | Filtered out: `strncmp(name, ".wh.", 4) == 0` causes skip |
| `readdir` lower file has whiteout | Whiteout checked per-entry before calling `filler()` |
| Parent dirs missing during CoW | Iterative `mkdir` loop creates the entire path in upper |
| `truncate` on lower-only file | CoW triggered before truncate, same as write |
| Empty path `/` getattr | Resolved normally through upper then lower root dirs |

---

## 9. Build & Usage

```bash
# Install dependencies (Ubuntu 22.04)
sudo apt-get install -y fuse3 libfuse3-dev pkg-config build-essential

# Build
make

# Run
./mini_unionfs /path/to/lower /path/to/upper /path/to/mountpoint

# Test
chmod +x test_unionfs.sh
./test_unionfs.sh

# Unmount
fusermount -u /path/to/mountpoint
```

---

## 10. Limitations & Future Work

- **Directory whiteouts**: Currently `rmdir` only removes directories that physically exist in upper. A full implementation would also create directory-level whiteouts.
- **`rename` / `link`**: Not implemented (not required by the spec). Would require CoW of the source before rename.
- **Permissions**: The implementation propagates permission modes from the original file during CoW but does not implement `chmod`/`chown` FUSE callbacks.
- **Performance**: The `readdir` deduplication uses a linear linked list. For very large directories, a hash set would be faster.
