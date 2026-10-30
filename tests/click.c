/* click.c - send a synthetic click to the app window (client coordinates) */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    HWND h;
    LPARAM pt;

    if (argc < 3) {
        printf("usage: click <x> <y> [wheel]\n");
        return 1;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    h = FindWindowW(L"FileMenderWnd", NULL);
    if (!h) {
        printf("window not found\n");
        return 2;
    }
    pt = MAKELPARAM((int)_wtoi(argv[1]), (int)_wtoi(argv[2]));
    PostMessageW(h, WM_MOUSEMOVE, 0, pt);
    Sleep(60);
    PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    Sleep(30);
    PostMessageW(h, WM_LBUTTONUP, 0, pt);
    Sleep(200);
    printf("clicked %ld,%ld\n", (long)_wtoi(argv[1]), (long)_wtoi(argv[2]));
    return 0;
}
