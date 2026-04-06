#include "fs_state.h"
#include "path_utils.h"

/*
 * unionfs_mkdir - Create a directory.
 *
 * New directories always go into upper_dir.
 * Also removes any whiteout if re-creating a previously deleted dir.
 */
int unionfs_mkdir(const char *path, mode_t mode) {
    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    /* Remove whiteout for this directory name if it exists */
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX - 1);
    tmp[PATH_MAX - 1] = '\0';
    char *slash = strrchr(tmp, '/');
    const char *dirname = slash ? slash + 1 : tmp;
    char dirpart[PATH_MAX];
    char wh_path[PATH_MAX];

    if (slash) {
        size_t dlen = (size_t)(slash - tmp);
        if (dlen == 0) dirpart[0] = '\0';
        else { strncpy(dirpart, tmp, dlen); dirpart[dlen] = '\0'; }
        snprintf(wh_path, sizeof(wh_path), "%s%s/%s%s",
                 UNIONFS_DATA->upper_dir, dirpart, WHITEOUT_PREFIX, dirname);
    } else {
        snprintf(wh_path, sizeof(wh_path), "%s/%s%s",
                 UNIONFS_DATA->upper_dir, WHITEOUT_PREFIX, dirname);
    }
    unlink(wh_path);

    /* Ensure parent exists */
    char up_parent[PATH_MAX];
    strncpy(up_parent, up_path, PATH_MAX - 1);
    up_parent[PATH_MAX - 1] = '\0';
    char *last = strrchr(up_parent, '/');
    if (last) {
        *last = '\0';
        char mkpath[PATH_MAX];
        snprintf(mkpath, sizeof(mkpath), "%s", up_parent);
        for (char *p = mkpath + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(mkpath, 0755);
                *p = '/';
            }
        }
        mkdir(mkpath, 0755);
    }

    if (mkdir(up_path, mode) == -1) return -errno;
    return 0;
}

/*
 * unionfs_rmdir - Remove a directory.
 *
 * Cases:
 *  1. Dir exists in upper_dir -> remove it (must be empty).
 *     If also in lower, create whiteout.
 *  2. Dir only in lower_dir -> create a whiteout.
 */
int unionfs_rmdir(const char *path) {
    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    int in_upper = (access(up_path, F_OK) == 0);
    int in_lower = file_in_lower(path);

    if (in_upper) {
        if (rmdir(up_path) == -1) return -errno;
    }

    if (in_lower) {
        /* Create a whiteout marker for the directory */
        char tmp[PATH_MAX];
        strncpy(tmp, path, PATH_MAX - 1);
        tmp[PATH_MAX - 1] = '\0';
        char *slash = strrchr(tmp, '/');
        const char *dname = slash ? slash + 1 : tmp;
        char dirpart[PATH_MAX];
        char wh_path[PATH_MAX];

        if (slash) {
            size_t dlen = (size_t)(slash - tmp);
            if (dlen == 0) dirpart[0] = '\0';
            else { strncpy(dirpart, tmp, dlen); dirpart[dlen] = '\0'; }
            snprintf(wh_path, sizeof(wh_path), "%s%s/%s%s",
                     UNIONFS_DATA->upper_dir, dirpart, WHITEOUT_PREFIX, dname);

            /* Ensure parent exists */
            char parent[PATH_MAX];
            snprintf(parent, sizeof(parent), "%s%s", UNIONFS_DATA->upper_dir, dirpart);
            char mkpath[PATH_MAX];
            snprintf(mkpath, sizeof(mkpath), "%s", parent);
            for (char *p = mkpath + 1; *p; p++) {
                if (*p == '/') { *p = '\0'; mkdir(mkpath, 0755); *p = '/'; }
            }
            mkdir(mkpath, 0755);
        } else {
            snprintf(wh_path, sizeof(wh_path), "%s/%s%s",
                     UNIONFS_DATA->upper_dir, WHITEOUT_PREFIX, dname);
        }

        int fd = open(wh_path, O_CREAT | O_WRONLY | O_TRUNC, 0000);
        if (fd == -1) return -errno;
        close(fd);
    }

    if (!in_upper && !in_lower) return -ENOENT;
    return 0;
}
