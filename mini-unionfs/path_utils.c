#include "fs_state.h"
#include "path_utils.h"

/*
 * resolve_path - Determine the real filesystem path for a virtual path.
 *
 * Resolution order:
 *  1. If a whiteout file exists in upper_dir -> file is deleted, return -ENOENT
 *  2. If file exists in upper_dir -> return upper path
 *  3. If file exists in lower_dir -> return lower path
 *  4. None found -> return -ENOENT
 *
 * Returns 0 on success (resolved_path is filled), -ENOENT if not found.
 */
int resolve_path(const char *path, char *resolved_path) {
    char wh_path[PATH_MAX];
    char up_path[PATH_MAX];
    char lo_path[PATH_MAX];

    /* Build the whiteout path: upper_dir/<parent>/.wh.<filename> */
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX - 1);
    tmp[PATH_MAX - 1] = '\0';

    const char *slash = strrchr(tmp, '/');
    const char *filename = slash ? slash + 1 : tmp;
    char dirpart[PATH_MAX];

    if (slash) {
        size_t dirlen = (size_t)(slash - tmp);
        if (dirlen == 0) {
            /* path is like "/foo" -> dirpart is "" */
            dirpart[0] = '\0';
        } else {
            strncpy(dirpart, tmp, dirlen);
            dirpart[dirlen] = '\0';
        }
        snprintf(wh_path, sizeof(wh_path), "%s%s/%s%s",
                 UNIONFS_DATA->upper_dir, dirpart, WHITEOUT_PREFIX, filename);
    } else {
        snprintf(wh_path, sizeof(wh_path), "%s/%s%s",
                 UNIONFS_DATA->upper_dir, WHITEOUT_PREFIX, filename);
    }

    /* Step 1: whiteout check */
    if (access(wh_path, F_OK) == 0) {
        return -ENOENT;
    }

    /* Step 2: upper_dir check */
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);
    if (access(up_path, F_OK) == 0) {
        strncpy(resolved_path, up_path, PATH_MAX - 1);
        resolved_path[PATH_MAX - 1] = '\0';
        return 0;
    }

    /* Step 3: lower_dir check */
    snprintf(lo_path, sizeof(lo_path), "%s%s", UNIONFS_DATA->lower_dir, path);
    if (access(lo_path, F_OK) == 0) {
        strncpy(resolved_path, lo_path, PATH_MAX - 1);
        resolved_path[PATH_MAX - 1] = '\0';
        return 0;
    }

    return -ENOENT;
}

/*
 * is_whiteout - Check if a filename is a whiteout marker.
 */
int is_whiteout(const char *name) {
    return strncmp(name, WHITEOUT_PREFIX, strlen(WHITEOUT_PREFIX)) == 0;
}

/*
 * whiteout_exists - Check if a whiteout exists in upper for given virtual path.
 */
int whiteout_exists(const char *path) {
    char wh_path[PATH_MAX];
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX - 1);
    tmp[PATH_MAX - 1] = '\0';

    const char *slash = strrchr(tmp, '/');
    const char *filename = slash ? slash + 1 : tmp;
    char dirpart[PATH_MAX];

    if (slash) {
        size_t dirlen = (size_t)(slash - tmp);
        if (dirlen == 0) {
            dirpart[0] = '\0';
        } else {
            strncpy(dirpart, tmp, dirlen);
            dirpart[dirlen] = '\0';
        }
        snprintf(wh_path, sizeof(wh_path), "%s%s/%s%s",
                 UNIONFS_DATA->upper_dir, dirpart, WHITEOUT_PREFIX, filename);
    } else {
        snprintf(wh_path, sizeof(wh_path), "%s/%s%s",
                 UNIONFS_DATA->upper_dir, WHITEOUT_PREFIX, filename);
    }

    return (access(wh_path, F_OK) == 0);
}

/*
 * file_in_upper - Check if a path exists in the upper layer (no whiteout logic).
 */
int file_in_upper(const char *path) {
    char up_path[PATH_MAX];
    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);
    return (access(up_path, F_OK) == 0);
}

/*
 * file_in_lower - Check if a path exists in the lower layer.
 */
int file_in_lower(const char *path) {
    char lo_path[PATH_MAX];
    snprintf(lo_path, sizeof(lo_path), "%s%s", UNIONFS_DATA->lower_dir, path);
    return (access(lo_path, F_OK) == 0);
}
