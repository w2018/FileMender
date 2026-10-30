/* util.h - small helpers: strings, formatting, time parsing, clipboard, errors */
#ifndef FM_UTIL_H
#define FM_UTIL_H

#include <windows.h>
#include <stddef.h>

#define FM_PATH_MAX 1024

void u8_to_w(const char *src, wchar_t *dst, size_t cap);
void fmt_bytes(unsigned long long n, wchar_t *out, size_t cap);
void fmt_filetime_local(const FILETIME *ft, wchar_t *out, size_t cap);
BOOL parse_time_text(const wchar_t *s, FILETIME *out);
void bytes_to_hex(const unsigned char *b, size_t n, wchar_t *out);
BOOL clipboard_copy_text(HWND owner, const wchar_t *text);
void error_text(DWORD err, wchar_t *out, size_t cap);
const wchar_t *extended_path(wchar_t *buf, size_t cap, const wchar_t *path);

#endif /* FM_UTIL_H */
