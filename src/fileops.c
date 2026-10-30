/* fileops.c - file metadata read, same-folder rename, timestamp write */
#include "fileops.h"
#include <string.h>
#include <wchar.h>

static BOOL reg_query_string(HKEY root, const wchar_t *subkey, const wchar_t *value,
                             wchar_t *out, DWORD cap)
{
    HKEY key;
    DWORD type = 0;
    DWORD size = cap * sizeof(wchar_t);
    LONG rc;

    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return FALSE;
    }
    rc = RegQueryValueExW(key, value, NULL, &type, (LPBYTE)out, &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return FALSE;
    }
    out[cap - 1] = 0;
    return out[0] != 0;
}

void file_mime_lookup(const wchar_t *path, wchar_t *out, size_t cap)
{
    const wchar_t *dot;
    const wchar_t *slash;
    wchar_t key[128];
    wchar_t progid[128];
    wchar_t tmp[128];
    wchar_t sub[256];

    out[0] = 0;
    slash = wcsrchr(path, L'\\');
    dot = wcsrchr(path, L'.');
    if (!dot || (slash && dot < slash) || dot[1] == 0) {
        wcsncpy(out, L"未知 / 无 MIME", cap - 1);
        out[cap - 1] = 0;
        return;
    }
    swprintf(key, 128, L"%s", dot);
    if (reg_query_string(HKEY_CLASSES_ROOT, key, L"Content Type", tmp, 128)) {
        wcsncpy(out, tmp, cap - 1);
        out[cap - 1] = 0;
        return;
    }
    if (reg_query_string(HKEY_CLASSES_ROOT, key, NULL, progid, 128)) {
        swprintf(sub, 256, L"%s", progid);
        if (reg_query_string(HKEY_CLASSES_ROOT, sub, L"Content Type", tmp, 128)) {
            wcsncpy(out, tmp, cap - 1);
            out[cap - 1] = 0;
            return;
        }
    }
    wcsncpy(out, L"未知 / 无 MIME", cap - 1);
    out[cap - 1] = 0;
}

BOOL file_load_info(const wchar_t *path, FileInfo *out, wchar_t *err, size_t errcap)
{
    HANDLE h;
    wchar_t xp[FM_PATH_MAX];
    LARGE_INTEGER size;
    DWORD attrs;

    memset(out, 0, sizeof(*out));
    wcsncpy(out->path, path, FM_PATH_MAX - 1);
    out->path[FM_PATH_MAX - 1] = 0;

    attrs = GetFileAttributesW(extended_path(xp, FM_PATH_MAX, path));
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (err) {
            error_text(GetLastError(), err, errcap);
        }
        return FALSE;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        if (err) {
            wcsncpy(err, L"暂不支持文件夹", errcap - 1);
            err[errcap - 1] = 0;
        }
        return FALSE;
    }
    out->attrs = attrs;

    h = CreateFileW(extended_path(xp, FM_PATH_MAX, path), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) {
            error_text(GetLastError(), err, errcap);
        }
        return FALSE;
    }
    if (!GetFileSizeEx(h, &size)) {
        size.QuadPart = 0;
    }
    out->size = (unsigned long long)size.QuadPart;
    GetFileTime(h, &out->ctime, &out->atime, &out->mtime);
    CloseHandle(h);

    file_mime_lookup(path, out->mime, 96);
    return TRUE;
}

static int is_reserved_device(const wchar_t *name)
{
    static const wchar_t *devs[] = {
        L"CON", L"PRN", L"AUX", L"NUL",
        L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
        L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
    };
    wchar_t base[16];
    size_t i, n = 0;

    for (i = 0; name[i] && name[i] != L'.' && n < 15; i++) {
        wchar_t c = name[i];
        if (c >= L'a' && c <= L'z') {
            c = (wchar_t)(c - 32);
        }
        base[n++] = c;
    }
    base[n] = 0;
    for (i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        if (wcscmp(base, devs[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

BOOL file_name_valid(const wchar_t *name, wchar_t *err, size_t errcap)
{
    static const wchar_t bad[] = L"\\/:*?\"<>|";
    size_t len, i;

    if (!name || !name[0]) {
        wcsncpy(err, L"文件名不能为空", errcap - 1);
        err[errcap - 1] = 0;
        return FALSE;
    }
    len = wcslen(name);
    if (len > 255) {
        wcsncpy(err, L"文件名过长（最多 255 个字符）", errcap - 1);
        err[errcap - 1] = 0;
        return FALSE;
    }
    for (i = 0; i < len; i++) {
        if (wcschr(bad, name[i])) {
            wcsncpy(err, L"文件名不能包含 \\ / : * ? \" < > | 等字符", errcap - 1);
            err[errcap - 1] = 0;
            return FALSE;
        }
        if (name[i] < 32) {
            wcsncpy(err, L"文件名不能包含控制字符", errcap - 1);
            err[errcap - 1] = 0;
            return FALSE;
        }
    }
    if (name[len - 1] == L' ' || name[len - 1] == L'.') {
        wcsncpy(err, L"文件名不能以空格或点结尾", errcap - 1);
        err[errcap - 1] = 0;
        return FALSE;
    }
    if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
        wcsncpy(err, L"文件名无效", errcap - 1);
        err[errcap - 1] = 0;
        return FALSE;
    }
    /* reserved DOS device names, with or without extension */
    if (is_reserved_device(name)) {
        wcsncpy(err, L"该名称是系统保留名，无法使用", errcap - 1);
        err[errcap - 1] = 0;
        return FALSE;
    }
    return TRUE;
}

BOOL file_rename(const wchar_t *path, const wchar_t *new_name,
                 wchar_t *new_path, size_t cap, wchar_t *err, size_t errcap)
{
    wchar_t dir[FM_PATH_MAX];
    wchar_t target[FM_PATH_MAX];
    wchar_t xp_old[FM_PATH_MAX];
    wchar_t xp_new[FM_PATH_MAX];
    const wchar_t *slash;
    size_t dirlen;

    if (!file_name_valid(new_name, err, errcap)) {
        return FALSE;
    }
    slash = wcsrchr(path, L'\\');
    if (!slash) {
        wcsncpy(err, L"路径无效", errcap - 1);
        err[errcap - 1] = 0;
        return FALSE;
    }
    dirlen = (size_t)(slash - path) + 1;
    wcsncpy(dir, path, dirlen);
    dir[dirlen] = 0;
    swprintf(target, FM_PATH_MAX, L"%s%s", dir, new_name);

    if (_wcsicmp(target, path) == 0) {
        wcsncpy(new_path, path, cap - 1);
        new_path[cap - 1] = 0;
        return TRUE; /* nothing to do */
    }
    if (GetFileAttributesW(extended_path(xp_new, FM_PATH_MAX, target)) != INVALID_FILE_ATTRIBUTES) {
        wcsncpy(err, L"目标位置已存在同名文件", errcap - 1);
        err[errcap - 1] = 0;
        return FALSE;
    }
    if (!MoveFileExW(extended_path(xp_old, FM_PATH_MAX, path),
                     extended_path(xp_new, FM_PATH_MAX, target), 0)) {
        if (err) {
            error_text(GetLastError(), err, errcap);
        }
        return FALSE;
    }
    wcsncpy(new_path, target, cap - 1);
    new_path[cap - 1] = 0;
    return TRUE;
}

BOOL file_apply_times(const wchar_t *path, const FILETIME *ctime,
                      const FILETIME *atime, const FILETIME *mtime,
                      wchar_t *err, size_t errcap)
{
    HANDLE h;
    wchar_t xp[FM_PATH_MAX];
    BOOL ok;

    h = CreateFileW(extended_path(xp, FM_PATH_MAX, path), FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) {
            error_text(GetLastError(), err, errcap);
        }
        return FALSE;
    }
    ok = SetFileTime(h, ctime, atime, mtime);
    if (!ok && err) {
        error_text(GetLastError(), err, errcap);
    }
    CloseHandle(h);
    return ok;
}
