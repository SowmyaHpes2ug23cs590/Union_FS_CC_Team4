#include "fs_state.h"
#include "path_utils.h"

/*
 * unionfs_readdir - List directory contents (merged view).
 *
 * Algorithm:
 *  1. Scan upper_dir/<path>: add every non-whiteout entry.
 *     Collect whiteout names so we can suppress them from lower.
 *  2. Scan lower_dir/<path>: add entries NOT already in upper AND NOT whiteout-ed.
 *
 * This ensures:
 *   - Upper entries take precedence.
 *   - Whiteout files suppress lower entries.
 *   - No duplicate entries.
 */
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    char up_path[PATH_MAX];
    char lo_path[PATH_MAX];

    snprintf(up_path, sizeof(up_path), "%s%s", UNIONFS_DATA->upper_dir, path);
    snprintf(lo_path, sizeof(lo_path), "%s%s", UNIONFS_DATA->lower_dir, path);

    /* We collect: names seen in upper (real files), and whiteout names */
    /* Use simple dynamic string set — max 4096 entries should be fine */
#define MAX_ENTRIES 4096
    char *seen[MAX_ENTRIES];
    char *whiteouts[MAX_ENTRIES];
    int seen_count = 0;
    int wh_count = 0;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    /* --- Scan upper directory --- */
    DIR *dp_upper = opendir(up_path);
    if (dp_upper) {
        struct dirent *de;
        while ((de = readdir(dp_upper)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            if (is_whiteout(de->d_name)) {
                /* Record which lower entry this hides */
                const char *hidden = de->d_name + strlen(WHITEOUT_PREFIX);
                if (wh_count < MAX_ENTRIES)
                    whiteouts[wh_count++] = strdup(hidden);
                /* Do NOT emit whiteout file to user */
                continue;
            }

            /* Real upper entry — emit and mark as seen */
            struct stat st;
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", up_path, de->d_name);
            if (lstat(full, &st) == 0)
                filler(buf, de->d_name, &st, 0, 0);
            else
                filler(buf, de->d_name, NULL, 0, 0);

            if (seen_count < MAX_ENTRIES)
                seen[seen_count++] = strdup(de->d_name);
        }
        closedir(dp_upper);
    }

    /* --- Scan lower directory --- */
    DIR *dp_lower = opendir(lo_path);
    if (dp_lower) {
        struct dirent *de;
        while ((de = readdir(dp_lower)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            /* Skip if whiteout-ed */
            int hidden = 0;
            for (int i = 0; i < wh_count; i++) {
                if (strcmp(whiteouts[i], de->d_name) == 0) {
                    hidden = 1;
                    break;
                }
            }
            if (hidden) continue;

            /* Skip if already emitted from upper */
            int already = 0;
            for (int i = 0; i < seen_count; i++) {
                if (strcmp(seen[i], de->d_name) == 0) {
                    already = 1;
                    break;
                }
            }
            if (already) continue;

            /* Emit lower entry */
            struct stat st;
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", lo_path, de->d_name);
            if (lstat(full, &st) == 0)
                filler(buf, de->d_name, &st, 0, 0);
            else
                filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(dp_lower);
    }

    /* Free allocated strings */
    for (int i = 0; i < seen_count; i++) free(seen[i]);
    for (int i = 0; i < wh_count; i++) free(whiteouts[i]);

    return 0;
}
