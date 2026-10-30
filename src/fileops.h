/* fileops.h - file metadata, rename, timestamp writing */
#ifndef FM_FILEOPS_H
#define FM_FILEOPS_H

#include <windows.h>
#include "util.h"

typedef struct {
    wchar_t path[FM_PATH_MAX];
    unsigned long long size;
    wchar_t mime[96];
    FILETIME ctime;
    FILETIME mtime;
    FILETIME atime;
    DWORD attrs;
} FileInfo;

BOOL file_load_info(const wchar_t *path, FileInfo *out, wchar_t *err, size_t errcap);
void file_mime_lookup(const wchar_t *path, wchar_t *out, size_t cap);
BOOL file_rename(const wchar_t *path, const wchar_t *new_name,
                 wchar_t *new_path, size_t cap, wchar_t *err, size_t errcap);
BOOL file_apply_times(const wchar_t *path, const FILETIME *ctime,
                      const FILETIME *atime, const FILETIME *mtime,
                      wchar_t *err, size_t errcap);
BOOL file_name_valid(const wchar_t *name, wchar_t *err, size_t errcap);

#endif /* FM_FILEOPS_H */
