#ifndef FS_STATE_H
#define FS_STATE_H

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>

/* Global state structure holding paths to our two layers */
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

/* Macro to retrieve our state from FUSE context */
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* Whiteout prefix used to mark deleted files */
#define WHITEOUT_PREFIX ".wh."

/* Build full path into lower layer */
static inline void lower_path(char *buf, size_t size, const char *path) {
    snprintf(buf, size, "%s%s", UNIONFS_DATA->lower_dir, path);
}

/* Build full path into upper layer */
static inline void upper_path(char *buf, size_t size, const char *path) {
    snprintf(buf, size, "%s%s", UNIONFS_DATA->upper_dir, path);
}

/* Build whiteout path in upper layer for a given file path */
static inline void whiteout_path(char *buf, size_t size, const char *path) {
    /* Split into directory and filename */
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX - 1);
    tmp[PATH_MAX - 1] = '\0';

    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = '\0';
        snprintf(buf, size, "%s%s/%s%s", UNIONFS_DATA->upper_dir, tmp, WHITEOUT_PREFIX, slash + 1);
    } else {
        snprintf(buf, size, "%s/%s%s", UNIONFS_DATA->upper_dir, WHITEOUT_PREFIX, path);
    }
}

#endif /* FS_STATE_H */
