#include "fs_state.h"
#include "path_utils.h"
#include "cow.h"

/* ------------------------------------------------------------------ */
/* Forward declarations of operation handlers                          */
/* ------------------------------------------------------------------ */

/* getattr.c */
int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi);

/* readdir.c */
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags);

/* read_write.c */
int unionfs_open(const char *path, struct fuse_file_info *fi);
int unionfs_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi);
int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi);
int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi);
int unionfs_truncate(const char *path, off_t size,
                     struct fuse_file_info *fi);
int unionfs_utimens(const char *path, const struct timespec ts[2],
                    struct fuse_file_info *fi);
int unionfs_chmod(const char *path, mode_t mode,
                  struct fuse_file_info *fi);

/* unlink_whiteout.c */
int unionfs_unlink(const char *path);

/* mkdir_rmdir.c */
int unionfs_mkdir(const char *path, mode_t mode);
int unionfs_rmdir(const char *path);

/* ------------------------------------------------------------------ */
/* FUSE operations struct                                               */
/* ------------------------------------------------------------------ */
static struct fuse_operations unionfs_oper = {
    .getattr  = unionfs_getattr,
    .readdir  = unionfs_readdir,
    .open     = unionfs_open,
    .read     = unionfs_read,
    .write    = unionfs_write,
    .create   = unionfs_create,
    .truncate = unionfs_truncate,
    .utimens  = unionfs_utimens,
    .chmod    = unionfs_chmod,
    .unlink   = unionfs_unlink,
    .mkdir    = unionfs_mkdir,
    .rmdir    = unionfs_rmdir,
};

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    /*
     * Expected usage:
     *   ./mini_unionfs <lower_dir> <upper_dir> <mount_point> [fuse options]
     *
     * We consume the first two positional args (lower/upper) and pass
     * the rest (mount_point + any FUSE flags) to fuse_main.
     */
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <lower_dir> <upper_dir> <mount_point> [fuse options]\n",
            argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = calloc(1, sizeof(*state));
    if (!state) {
        perror("calloc");
        return 1;
    }

    /* Resolve absolute paths for lower and upper directories */
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    if (!state->lower_dir || !state->upper_dir) {
        fprintf(stderr, "Error resolving lower_dir or upper_dir paths.\n");
        free(state->lower_dir);
        free(state->upper_dir);
        free(state);
        return 1;
    }

    /*
     * Build a new argv for fuse_main:
     *   new_argv[0] = program name
     *   new_argv[1] = mount_point   (argv[3])
     *   new_argv[2..] = any extra fuse options (argv[4..])
     *
     * We also add "-f" (foreground) so the process doesn't daemonize,
     * making it easier to test. Remove if you prefer background operation.
     */
    int fuse_argc = argc - 2;          /* drop lower_dir and upper_dir */
    char **fuse_argv = malloc((size_t)(fuse_argc + 2) * sizeof(char *));
    if (!fuse_argv) {
        perror("malloc");
        free(state->lower_dir);
        free(state->upper_dir);
        free(state);
        return 1;
    }

    fuse_argv[0] = argv[0];
    /* argv[3] becomes the mount point -> fuse_argv[1] */
    for (int i = 1; i < fuse_argc; i++) {
        fuse_argv[i] = argv[i + 2];
    }
    fuse_argv[fuse_argc] = NULL;

    fprintf(stderr, "Mini-UnionFS mounting:\n");
    fprintf(stderr, "  lower_dir : %s\n", state->lower_dir);
    fprintf(stderr, "  upper_dir : %s\n", state->upper_dir);
    fprintf(stderr, "  mount_point: %s\n", fuse_argv[1]);

    int ret = fuse_main(fuse_argc, fuse_argv, &unionfs_oper, state);

    free(fuse_argv);
    free(state->lower_dir);
    free(state->upper_dir);
    free(state);
    return ret;
}
