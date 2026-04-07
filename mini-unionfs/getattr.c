#include "fs_state.h"
#include "path_utils.h"

/*
 * unionfs_getattr - Get file/directory attributes.
 *
 * Resolves the virtual path to the real path and calls lstat().
 * If path is whiteout-ed, returns -ENOENT.
 */
int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res != 0)
        return res;  /* -ENOENT */

    if (lstat(resolved, stbuf) == -1)
        return -errno;

    return 0;
}
