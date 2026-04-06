#include "fs_state.h"
#include "path_utils.h"

/*
 * make_whiteout - Create a whiteout file in upper_dir for a given path.
 * Ensures the parent directory exists in upper.
 */
static int make_whiteout(const char *path) {
    char wh_path[PATH_MAX];
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX - 1);
    tmp[PATH_MAX - 1] = '\0';

    char *slash = strrchr(tmp, '/');
    const char *filename = slash ? slash + 1 : tmp;
    char dirpart[PATH_MAX];

    if (slash) {
        size_t dlen = (size_t)(slash - tmp);
        if (dlen == 0) dirpart[0] = '\0';
        else { strncpy(dirpart, tmp, dlen); dirpart[dlen] = '\0'; }
        snprintf(wh_path, sizeof(wh_path), "%s%s/%s%s",
                 UNIONFS_DATA->upper_dir, dirpart, WHITEOUT_PREFIX, filename);

        /* Ensure parent dir exists in upper */
        char parent[PATH_MAX];
        snprintf(parent, sizeof(parent), "%s%s", UNIONFS_DATA->upper_dir, dirpart);
        char mkpath[PATH_MAX];
        snprintf(mkpath, sizeof(mkpath), "%s", parent);
        for (char *p = mkpath + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(mkpath, 0755);
                *p = '/';
            }
        }
        mkdir(mkpath, 0755);
    } else {
        snprintf(wh_path, sizeof(wh_path), "%s/%s%s",
                 UNIONFS_DATA->upper_dir, WHITEOUT_PREFIX, filename);
    }

    int fd = open(wh_path, O_CREAT | O_WRONLY | O_TRUNC, 0000);
    if (fd == -1) return -errno;
    close(fd);
    return 0;
}

/*
 * unionfs_unlink - Delete a file.
 *
 * Cases:
 *  1. File is in upper_dir  -> physically delete it.
 *     If it also exists in lower_dir -> create a whiteout to hide the lower copy.
 *  2. File is only in lower_dir -> create a whiteout file in upper_dir.
 */
int unionfs_unlink(const char *path) {
    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);

    int in_upper = (access(up_path, F_OK) == 0);
    int in_lower = file_in_lower(path);

    if (in_upper) {
        /* Remove the upper copy */
        if (unlink(up_path) == -1) return -errno;
    }

    if (in_lower) {
        /* Create whiteout to hide the lower layer file */
        int res = make_whiteout(path);
        if (res != 0) return res;
    }

    if (!in_upper && !in_lower) {
        return -ENOENT;
    }

    return 0;
}
