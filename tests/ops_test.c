/* ops_test.c - functional test for rename / timestamp / validation logic */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include "../src/fileops.h"
#include "../src/util.h"

static int failures = 0;
static wchar_t dir[FM_PATH_MAX];

static void ok(int cond, const char *what)
{
    if (cond) {
        printf("ok   %s\n", what);
    } else {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static void make_file(const wchar_t *path, const char *text)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD written = 0;
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, text, (DWORD)strlen(text), &written, NULL);
        CloseHandle(h);
    }
}

static int file_exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static long long ft_seconds(const FILETIME *ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft->dwLowDateTime;
    u.HighPart = ft->dwHighDateTime;
    return (long long)(u.QuadPart / 10000000ull);
}

int main(void)
{
    wchar_t pathA[FM_PATH_MAX], pathB[FM_PATH_MAX], pathC[FM_PATH_MAX];
    wchar_t newp[FM_PATH_MAX], err[512];
    FileInfo fi;
    FILETIME t1, t2, t3, got;
    FILETIME rc, ra, rm;
    HANDLE h;

    GetTempPathW(FM_PATH_MAX, dir);
    wcscat(dir, L"fm_ops_test\\");
    CreateDirectoryW(dir, NULL);

    swprintf(pathA, FM_PATH_MAX, L"%sfm_test_a.txt", dir);
    swprintf(pathB, FM_PATH_MAX, L"%sfm_test_b.txt", dir);
    swprintf(pathC, FM_PATH_MAX, L"%s已被占用.txt", dir);
    DeleteFileW(pathA);
    DeleteFileW(pathB);
    DeleteFileW(pathC);

    /* --- name validation --- */
    ok(file_name_valid(L"report.txt", err, 512), "valid name accepted");
    ok(!file_name_valid(L"", err, 512), "empty name rejected");
    ok(!file_name_valid(L"a/b.txt", err, 512), "slash rejected");
    ok(!file_name_valid(L"a:b.txt", err, 512), "colon rejected");
    ok(!file_name_valid(L"con", err, 512), "CON rejected");
    ok(!file_name_valid(L"nul.txt", err, 512), "NUL.txt rejected");
    ok(!file_name_valid(L"trail.", err, 512), "trailing dot rejected");
    ok(!file_name_valid(L"trail ", err, 512), "trailing space rejected");
    ok(file_name_valid(L"CONSOLE.txt", err, 512), "CONSOLE.txt accepted");
    ok(file_name_valid(L"中文 文件名.txt", err, 512), "chinese name accepted");

    /* --- time parsing --- */
    ok(parse_time_text(L"2020-01-02 03:04:05", &t1), "parse full datetime");
    ok(parse_time_text(L"2020-01-02T03:04", &t1), "parse T form without seconds");
    ok(!parse_time_text(L"2020-13-02 03:04:05", &t1), "reject month 13");
    ok(!parse_time_text(L"2020-01-32 03:04:05", &t1), "reject day 32");
    ok(!parse_time_text(L"hello", &t1), "reject garbage");
    ok(!parse_time_text(L"", &t1), "reject empty");

    /* --- load info --- */
    make_file(pathA, "hello filemender");
    ok(file_load_info(pathA, &fi, err, 512), "load file info");
    ok(fi.size == 16, "size correct");
    ok(wcscmp(fi.mime, L"text/plain") == 0, "mime from registry");
    {
        FileInfo d;
        ok(!file_load_info(dir, &d, err, 512), "directory rejected");
    }

    /* --- rename --- */
    ok(file_rename(pathA, L"fm_test_b.txt", newp, FM_PATH_MAX, err, 512), "rename ok");
    ok(file_exists(pathB), "renamed file exists on disk");
    ok(!file_exists(pathA), "old name gone");

    /* name collision must fail, not overwrite */
    make_file(pathA, "second");
    ok(!file_rename(pathA, L"fm_test_b.txt", newp, FM_PATH_MAX, err, 512), "collision rejected");
    ok(file_exists(pathA), "source kept after failed rename");
    DeleteFileW(pathA);

    /* unicode rename */
    ok(file_rename(pathB, L"已被占用.txt", newp, FM_PATH_MAX, err, 512), "unicode rename ok");
    ok(file_exists(pathC), "unicode file exists");

    /* --- timestamps --- */
    {
        SYSTEMTIME st;
        FILETIME local;
        memset(&st, 0, sizeof(st));
        st.wYear = 2001;
        st.wMonth = 2;
        st.wDay = 3;
        st.wHour = 4;
        st.wMinute = 5;
        st.wSecond = 6;
        SystemTimeToFileTime(&st, &local);
        LocalFileTimeToFileTime(&local, &t1);

        st.wYear = 2002;
        st.wMonth = 3;
        st.wDay = 4;
        st.wHour = 5;
        st.wMinute = 6;
        st.wSecond = 7;
        SystemTimeToFileTime(&st, &local);
        LocalFileTimeToFileTime(&local, &t2);

        st.wYear = 2003;
        st.wMonth = 4;
        st.wDay = 5;
        st.wHour = 6;
        st.wMinute = 7;
        st.wSecond = 8;
        SystemTimeToFileTime(&st, &local);
        LocalFileTimeToFileTime(&local, &t3);

        ok(file_apply_times(pathC, &t1, &t2, &t3, err, 512), "apply timestamps");
        h = CreateFileW(pathC, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, 0, NULL);
        ok(h != INVALID_HANDLE_VALUE, "reopen for verify");
        GetFileTime(h, &rc, &ra, &rm);
        CloseHandle(h);
        ok(ft_seconds(&rc) == ft_seconds(&t1), "creation time written");
        ok(ft_seconds(&ra) == ft_seconds(&t2), "access time written");
        ok(ft_seconds(&rm) == ft_seconds(&t3), "modify time written");
        (void)got;
    }

    /* --- partial update: NULL means keep --- */
    {
        FileInfo before, after;
        file_load_info(pathC, &before, err, 512);
        ok(file_apply_times(pathC, NULL, NULL, &t1, err, 512), "partial timestamp update");
        file_load_info(pathC, &after, err, 512);
        ok(ft_seconds(&after.mtime) == ft_seconds(&t1), "mtime updated on partial");
        ok(ft_seconds(&after.ctime) == ft_seconds(&before.ctime), "ctime untouched on partial");
    }

    /* long path handling */
    {
        wchar_t deep[FM_PATH_MAX];
        wchar_t longname[300];
        int i;
        for (i = 0; i < 200; i++) {
            longname[i] = (wchar_t)(L'a' + (i % 26));
        }
        longname[200] = 0;
        swprintf(deep, FM_PATH_MAX, L"%s%s.txt", dir, longname);
        make_file(deep, "x");
        ok(file_load_info(deep, &fi, err, 512), "long file name loads");
        ok(file_rename(deep, L"short.txt", newp, FM_PATH_MAX, err, 512), "rename long -> short");
        DeleteFileW(newp);
    }

    DeleteFileW(pathC);
    RemoveDirectoryW(dir);

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
