/* util.c - helpers implementation (Win32, C11) */
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

void u8_to_w(const char *src, wchar_t *dst, size_t cap)
{
    if (!cap) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    if (!MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)cap)) {
        dst[0] = 0;
    }
}

void fmt_bytes(unsigned long long n, wchar_t *out, size_t cap)
{
    if (n < 1024ull) {
        swprintf(out, cap, L"%llu B", n);
    } else if (n < 1024ull * 1024ull) {
        swprintf(out, cap, L"%.2f KB", (double)n / 1024.0);
    } else if (n < 1024ull * 1024ull * 1024ull) {
        swprintf(out, cap, L"%.2f MB", (double)n / (1024.0 * 1024.0));
    } else {
        swprintf(out, cap, L"%.2f GB", (double)n / (1024.0 * 1024.0 * 1024.0));
    }
}

void fmt_filetime_local(const FILETIME *ft, wchar_t *out, size_t cap)
{
    FILETIME local;
    SYSTEMTIME st;

    if (!ft || (ft->dwLowDateTime == 0 && ft->dwHighDateTime == 0)) {
        swprintf(out, cap, L"-");
        return;
    }
    if (!FileTimeToLocalFileTime(ft, &local) || !FileTimeToSystemTime(&local, &st)) {
        swprintf(out, cap, L"-");
        return;
    }
    swprintf(out, cap, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static const wchar_t *read_int(const wchar_t *p, int *v, int maxdigits)
{
    int n = 0;
    int acc = 0;

    while (*p >= L'0' && *p <= L'9' && n < maxdigits) {
        acc = acc * 10 + (int)(*p - L'0');
        p++;
        n++;
    }
    if (!n) {
        return NULL;
    }
    *v = acc;
    return p;
}

/* Accepts "YYYY-MM-DD HH:MM:SS", "YYYY-MM-DDTHH:MM", "YYYY/MM/DD HH:MM" etc.
   Seconds / time part may be omitted. Trailing spaces are ignored. */
BOOL parse_time_text(const wchar_t *s, FILETIME *out)
{
    const wchar_t *p = s;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    SYSTEMTIME st;
    FILETIME local;

    if (!s) {
        return FALSE;
    }
    while (*p == L' ' || *p == L'\t') {
        p++;
    }
    if (!(p = read_int(p, &y, 4)) || y < 1601 || y > 30827) {
        return FALSE;
    }
    if (*p != L'-' && *p != L'/' && *p != L'.') {
        return FALSE;
    }
    p++;
    if (!(p = read_int(p, &mo, 2))) {
        return FALSE;
    }
    if (*p != L'-' && *p != L'/' && *p != L'.') {
        return FALSE;
    }
    p++;
    if (!(p = read_int(p, &d, 2))) {
        return FALSE;
    }
    if (*p == L'T' || *p == L't') {
        p++;
    } else if (*p == L' ') {
        p++;
    }
    while (*p == L' ') {
        p++;
    }
    if (*p >= L'0' && *p <= L'9') {
        if (!(p = read_int(p, &h, 2))) {
            return FALSE;
        }
        if (*p == L':') {
            p++;
            if (!(p = read_int(p, &mi, 2))) {
                return FALSE;
            }
            if (*p == L':') {
                p++;
                if (!(p = read_int(p, &se, 2))) {
                    return FALSE;
                }
            }
        }
    }
    while (*p == L' ' || *p == L'\t') {
        p++;
    }
    if (*p) {
        return FALSE;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59) {
        return FALSE;
    }
    memset(&st, 0, sizeof(st));
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)se;
    if (!SystemTimeToFileTime(&st, &local)) {
        return FALSE;
    }
    return LocalFileTimeToFileTime(&local, out) ? TRUE : FALSE;
}

void bytes_to_hex(const unsigned char *b, size_t n, wchar_t *out)
{
    static const wchar_t hex[] = L"0123456789abcdef";
    size_t i;

    for (i = 0; i < n; i++) {
        out[i * 2] = hex[b[i] >> 4];
        out[i * 2 + 1] = hex[b[i] & 15];
    }
    out[n * 2] = 0;
}

BOOL clipboard_copy_text(HWND owner, const wchar_t *text)
{
    size_t len;
    HGLOBAL mem;
    void *dst;
    BOOL ok = FALSE;

    if (!text) {
        return FALSE;
    }
    len = wcslen(text);
    if (!len) {
        return FALSE;
    }
    if (!OpenClipboard(owner)) {
        return FALSE;
    }
    if (EmptyClipboard()) {
        mem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
        if (mem) {
            dst = GlobalLock(mem);
            if (dst) {
                memcpy(dst, text, len * sizeof(wchar_t));
                ((wchar_t *)dst)[len] = 0;
                GlobalUnlock(mem);
                if (SetClipboardData(CF_UNICODETEXT, mem)) {
                    ok = TRUE;
                } else {
                    GlobalFree(mem);
                }
            } else {
                GlobalFree(mem);
            }
        }
    }
    CloseClipboard();
    return ok;
}

void error_text(DWORD err, wchar_t *out, size_t cap)
{
    wchar_t *msg = NULL;

    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       (LPWSTR)&msg, 0, NULL) && msg) {
        size_t n = wcslen(msg);
        while (n && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n' || msg[n - 1] == L' ')) {
            msg[--n] = 0;
        }
        wcsncpy(out, msg, cap - 1);
        out[cap - 1] = 0;
        LocalFree(msg);
    } else {
        swprintf(out, cap, L"错误代码 %lu", (unsigned long)err);
    }
}

/* Long paths need the \\?\ prefix; short ones are used as-is. */
const wchar_t *extended_path(wchar_t *buf, size_t cap, const wchar_t *path)
{
    if (!path) {
        buf[0] = 0;
        return buf;
    }
    if (wcslen(path) < MAX_PATH) {
        wcsncpy(buf, path, cap - 1);
        buf[cap - 1] = 0;
        return buf;
    }
    if (wcsncmp(path, L"\\\\?\\", 4) == 0) {
        wcsncpy(buf, path, cap - 1);
        buf[cap - 1] = 0;
        return buf;
    }
    if (path[0] == L'\\' && path[1] == L'\\') {
        swprintf(buf, cap, L"\\\\?\\UNC\\%s", path + 2);
    } else {
        swprintf(buf, cap, L"\\\\?\\%s", path);
    }
    return buf;
}
