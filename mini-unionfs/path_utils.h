#ifndef PATH_UTILS_H
#define PATH_UTILS_H

int resolve_path(const char *path, char *resolved_path);
int is_whiteout(const char *name);
int whiteout_exists(const char *path);
int file_in_upper(const char *path);
int file_in_lower(const char *path);

#endif /* PATH_UTILS_H */
