/* drive.c - end-to-end driver: rename + retime a file through the real UI */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <commctrl.h>

int wmain(int argc, wchar_t **argv)
{
    HWND h, e1, e2, e3, e4;
    wchar_t expect[1024];

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 4) {
        printf("usage: drive <new-name> <ctime> <expected-path>\n");
        return 1;
    }
    h = FindWindowW(L"FileMenderWnd", NULL);
    if (!h) {
        printf("window not found\n");
        return 2;
    }
    e1 = GetDlgItem(h, 1001);
    e2 = GetDlgItem(h, 1002);
    e3 = GetDlgItem(h, 1003);
    e4 = GetDlgItem(h, 1004);
    if (!e1 || !e2 || !e3 || !e4) {
        printf("edits not found: %p %p %p %p\n", (void *)e1, (void *)e2, (void *)e3, (void *)e4);
        return 3;
    }
    {
        wchar_t before[512];
        GetWindowTextW(e1, before, 512);
        wprintf(L"name field before: %s\n", before);
    }

    {
        SYSTEMTIME st;
        memset(&st, 0, sizeof(st));
        swscanf(argv[2], L"%hu-%hu-%hu %hu:%hu:%hu", &st.wYear, &st.wMonth, &st.wDay,
                &st.wHour, &st.wMinute, &st.wSecond);
        SendMessageW(e2, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
    }
    SendMessageW(e1, WM_SETTEXT, 0, (LPARAM)argv[1]);
    Sleep(150);
    if (!(argc > 4 && _wcsicmp(argv[4], L"noenter") == 0)) {
        PostMessageW(e2, WM_KEYDOWN, VK_RETURN, 0);
    }
    Sleep(700);

    wcscpy(expect, argv[3]);
    if (GetFileAttributesW(expect) != INVALID_FILE_ATTRIBUTES) {
        printf("PASS renamed file exists\n");
    } else {
        printf("FAIL renamed file missing: %ls\n", expect);
        return 4;
    }
    {
        SYSTEMTIME st;
        memset(&st, 0, sizeof(st));
        SendMessageW(e2, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
        wprintf(L"after apply: ctime field=%04d-%02d-%02d %02d:%02d:%02d\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    }
    return 0;
}
