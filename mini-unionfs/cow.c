#include "fs_state.h"
#include "path_utils.h"
#include "cow.h"

/*
 * copy_file - Copy a file from src to dst, preserving content.
 * Creates parent directories in dst if needed.
 * Returns 0 on success, -errno on failure.
 */
static int copy_file(const char *src, const char *dst) {
    /* Ensure parent directory exists */
    char dst_copy[PATH_MAX];
    strncpy(dst_copy, dst, PATH_MAX - 1);
    dst_copy[PATH_MAX - 1] = '\0';

    char *last_slash = strrchr(dst_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        /* mkdir -p equivalent */
        char tmp[PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%s", dst_copy);
        for (char *p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(tmp, 0755);
                *p = '/';
            }
        }
        mkdir(tmp, 0755);
    }

    /* Open source */
    int fd_src = open(src, O_RDONLY);
    if (fd_src == -1) return -errno;

    /* Get source permissions */
    struct stat st;
    if (fstat(fd_src, &st) == -1) {
        close(fd_src);
        return -errno;
    }

    /* Open/create destination */
    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (fd_dst == -1) {
        close(fd_src);
        return -errno;
    }

    /* Copy data */
    char buf[65536];
    ssize_t bytes_read;
    while ((bytes_read = read(fd_src, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < bytes_read) {
            ssize_t w = write(fd_dst, buf + written, (size_t)(bytes_read - written));
            if (w == -1) {
                close(fd_src);
                close(fd_dst);
                return -errno;
            }
            written += w;
        }
    }

    close(fd_src);
    close(fd_dst);

    if (bytes_read == -1) return -errno;
    return 0;
}

/*
 * cow_copy_to_upper - Perform Copy-on-Write.
 *
 * If the file exists ONLY in lower_dir (not yet in upper_dir),
 * copy it to upper_dir so we can safely modify it there.
 *
 * Returns 0 on success, -errno on failure.
 */
int cow_copy_to_upper(const char *path) {
    if (file_in_upper(path)) {
        /* Already in upper — nothing to do */
        return 0;
    }

    char lo_path[PATH_MAX];
    char up_path[PATH_MAX];
    snprintf(lo_path, sizeof(lo_path), "%s%s", UNIONFS_DATA->lower_dir, path);
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    if (access(lo_path, F_OK) != 0) {
        /* Not in lower either — new file, caller handles creation */
        return 0;
    }

    return copy_file(lo_path, up_path);
}
