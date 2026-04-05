#include "fs_state.h"
#include "path_utils.h"
#include "cow.h"

/*
 * unionfs_open - Open a file.
 *
 * If opened for writing and the file is in lower_dir only,
 * trigger Copy-on-Write first.
 */
int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved[PATH_MAX];

    /* For write access, perform CoW before opening */
    if (fi->flags & (O_WRONLY | O_RDWR | O_APPEND | O_TRUNC)) {
        int res = cow_copy_to_upper(path);
        if (res != 0) return res;
    }

    int ret = resolve_path(path, resolved);
    if (ret != 0) return ret;

    int fd = open(resolved, fi->flags);
    if (fd == -1) return -errno;

    close(fd);
    return 0;
}

/*
 * unionfs_read - Read data from a file.
 */
int unionfs_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];
    int ret = resolve_path(path, resolved);
    if (ret != 0) return ret;

    int fd = open(resolved, O_RDONLY);
    if (fd == -1) return -errno;

    ssize_t res = pread(fd, buf, size, offset);
    close(fd);

    if (res == -1) return -errno;
    return (int) res;
}

/*
 * unionfs_write - Write data to a file.
 *
 * Always writes to upper_dir. CoW is performed if needed.
 */
int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    /* CoW: copy from lower if not already in upper */
    int res = cow_copy_to_upper(path);
    if (res != 0) return res;

    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    int fd = open(up_path, O_WRONLY);
    if (fd == -1) return -errno;

    ssize_t written = pwrite(fd, buf, size, offset);
    close(fd);

    if (written == -1) return -errno;
    return (int) written;
}

/*
 * unionfs_create - Create a new file in upper_dir.
 */
int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi) {
    (void) fi;

    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    /* Ensure parent directory exists in upper */
    char up_copy[PATH_MAX];
    strncpy(up_copy, up_path, PATH_MAX - 1);
    up_copy[PATH_MAX - 1] = '\0';
    char *last_slash = strrchr(up_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        char tmp[PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%s", up_copy);
        for (char *p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(tmp, 0755);
                *p = '/';
            }
        }
        mkdir(tmp, 0755);
    }

    /* Remove any whiteout that might exist for this path */
    char wh_path[PATH_MAX];
    char tmp2[PATH_MAX];
    strncpy(tmp2, path, PATH_MAX - 1);
    tmp2[PATH_MAX - 1] = '\0';
    char *slash2 = strrchr(tmp2, '/');
    const char *fname = slash2 ? slash2 + 1 : tmp2;
    char dirpart[PATH_MAX];
    if (slash2) {
        size_t dlen = (size_t)(slash2 - tmp2);
        if (dlen == 0) dirpart[0] = '\0';
        else { strncpy(dirpart, tmp2, dlen); dirpart[dlen] = '\0'; }
        snprintf(wh_path, sizeof(wh_path), "%s%s/%s%s",
                 UNIONFS_DATA->upper_dir, dirpart, WHITEOUT_PREFIX, fname);
    } else {
        snprintf(wh_path, sizeof(wh_path), "%s/%s%s",
                 UNIONFS_DATA->upper_dir, WHITEOUT_PREFIX, fname);
    }
    unlink(wh_path); /* Remove whiteout if re-creating a deleted file */

    int fd = open(up_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd == -1) return -errno;
    close(fd);

    return 0;
}

/*
 * unionfs_truncate - Truncate a file.
 *
 * Performs CoW if needed, then truncates in upper.
 */
int unionfs_truncate(const char *path, off_t size,
                     struct fuse_file_info *fi) {
    (void) fi;

    int res = cow_copy_to_upper(path);
    if (res != 0) return res;

    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    /* If not in upper yet (new file scenario), resolve normally */
    if (access(up_path, F_OK) != 0) {
        char resolved[PATH_MAX];
        res = resolve_path(path, resolved);
        if (res != 0) return res;
        if (truncate(resolved, size) == -1) return -errno;
        return 0;
    }

    if (truncate(up_path, size) == -1) return -errno;
    return 0;
}

/*
 * unionfs_utimens - Update access/modification times.
 */
int unionfs_utimens(const char *path, const struct timespec ts[2],
                    struct fuse_file_info *fi) {
    (void) fi;

    /* If in lower only, copy to upper first */
    if (!file_in_upper(path) && file_in_lower(path)) {
        int res = cow_copy_to_upper(path);
        if (res != 0) return res;
    }

    char resolved[PATH_MAX];
    int ret = resolve_path(path, resolved);
    if (ret != 0) return ret;

    if (utimensat(0, resolved, ts, AT_SYMLINK_NOFOLLOW) == -1)
        return -errno;
    return 0;
}

/*
 * unionfs_chmod - Change file permissions.
 */
int unionfs_chmod(const char *path, mode_t mode,
                  struct fuse_file_info *fi) {
    (void) fi;

    if (!file_in_upper(path) && file_in_lower(path)) {
        int res = cow_copy_to_upper(path);
        if (res != 0) return res;
    }

    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    if (access(up_path, F_OK) == 0) {
        if (chmod(up_path, mode) == -1) return -errno;
        return 0;
    }

    char resolved[PATH_MAX];
    int ret = resolve_path(path, resolved);
    if (ret != 0) return ret;
    if (chmod(resolved, mode) == -1) return -errno;
    return 0;
}
