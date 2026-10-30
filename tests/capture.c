/* capture.c - diagnostic: grab a window via PrintWindow into a BMP */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND h = FindWindowW(argc > 3 ? argv[3] : L"FileMenderWnd", NULL);
    RECT r;
    HDC screen, mem;
    HBITMAP bmp;
    BITMAPINFO bi;
    void *bits = NULL;
    int w, hh, y;
    FILE *f;
    unsigned char hdr[54];
    const wchar_t *out = argc > 1 ? argv[1] : L"cap.bmp";

    if (!h) {
        printf("window not found\n");
        return 1;
    }
    GetWindowRect(h, &r);
    w = r.right - r.left;
    hh = r.bottom - r.top;
    {
        RECT cr;
        GetClientRect(h, &cr);
        printf("visible=%d rect=%ld,%ld,%ld,%ld\n", IsWindowVisible(h), (long)r.left, (long)r.top, (long)r.right, (long)r.bottom);
        printf("dpi=%u client=%ldx%ld screen=%dx%d\n", GetDpiForWindow(h),
               (long)(cr.right - cr.left), (long)(cr.bottom - cr.top),
               GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }

    if (argc > 2) {
        int n = atoi(argv[2] ? "1" : "0");
        (void)n;
    }
    if (argc > 2) {
        int notches = _wtoi(argv[2]);
        int i;
        for (i = 0; i < notches; i++) {
            PostMessageW(h, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)-120), 0);
            Sleep(40);
        }
        Sleep(250);
    }

    screen = GetDC(NULL);
    printf("screen dc = %p\n", (void *)screen);
    if (!screen) {
        return 2;
    }
    mem = CreateCompatibleDC(screen);
    bmp = CreateCompatibleBitmap(screen, w, hh);
    SelectObject(mem, bmp);
    if (argc > 4 && _wcsicmp(argv[4], L"screen") == 0) {
        HDC sdc = GetDC(NULL);
        BitBlt(mem, 0, 0, w, hh, sdc, r.left, r.top, SRCCOPY | CAPTUREBLT);
        ReleaseDC(NULL, sdc);
        printf("bitblt from screen\n");
    } else if (argc > 4 && _wcsicmp(argv[4], L"print0") == 0) {
        printf("printwindow(0) = %d\n", PrintWindow(h, mem, 0));
    } else {
        printf("printwindow = %d\n", PrintWindow(h, mem, 2));
    }

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    GetDIBits(mem, bmp, 0, (UINT)hh, NULL, &bi, DIB_RGB_COLORS);
    bits = malloc((size_t)w * hh * 4);
    GetDIBits(mem, bmp, 0, (UINT)hh, bits, &bi, DIB_RGB_COLORS);

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    *(unsigned int *)(hdr + 2) = 54u + (unsigned int)w * hh * 4u;
    *(unsigned int *)(hdr + 10) = 54u;
    *(unsigned int *)(hdr + 14) = 40u;
    *(int *)(hdr + 18) = w;
    *(int *)(hdr + 22) = -hh;
    *(unsigned short *)(hdr + 26) = 1;
    *(unsigned short *)(hdr + 28) = 32;
    *(unsigned int *)(hdr + 34) = (unsigned int)w * hh * 4u;
    f = _wfopen(out, L"wb");
    if (f) {
        fwrite(hdr, 1, 54, f);
        for (y = 0; y < hh; y++) {
            fwrite((unsigned char *)bits + (size_t)y * w * 4, 4, (size_t)w, f);
        }
        fclose(f);
        printf("saved %dx%d\n", w, hh);
    }
    free(bits);
    return 0;
}
