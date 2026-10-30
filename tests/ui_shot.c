/* ui_shot.c - offscreen render of the UI to a BMP, for pixel-level review */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/ui.h"
#include "../src/util.h"

static int write_bmp(const char *path, unsigned int *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    unsigned char hdr[54];
    int y;

    if (!f) {
        return 0;
    }
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    *(unsigned int *)(hdr + 2) = 54u + (unsigned int)w * h * 4u;
    *(unsigned int *)(hdr + 10) = 54u;
    *(unsigned int *)(hdr + 14) = 40u;
    *(int *)(hdr + 18) = w;
    *(int *)(hdr + 22) = h;
    *(unsigned short *)(hdr + 26) = 1;
    *(unsigned short *)(hdr + 28) = 32;
    *(unsigned int *)(hdr + 34) = (unsigned int)w * h * 4u;
    fwrite(hdr, 1, 54, f);
    for (y = h - 1; y >= 0; y--) {
        fwrite(px + (size_t)y * (size_t)w, 4, (size_t)w, f);
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    FmApp app;
    int mode = argc > 2 ? atoi(argv[2]) : 0;
    int w = argc > 3 ? atoi(argv[3]) : 680;
    int h = argc > 4 ? atoi(argv[4]) : 988;
    int dpi = argc > 5 ? atoi(argv[5]) : 96;

    fm_ui_init(&app);
    fm_ui_set_dpi(&app, dpi);
    app.sync_on = (mode == 3);

    if (mode >= 1 && mode <= 6) {
        app.has_file = 1;
        app.info_open = (mode != 6);
        wcscpy(app.txt_path, L"D:\\workbuddy-ai_Work\\FileMender\\tests\\设计稿对照测试文件.txt");
        wcscpy(app.txt_size, L"7.10 KB");
        wcscpy(app.txt_mime, L"text/plain");
        wcscpy(app.txt_md5, L"9e107d9d372bb6826bd81d3542a419d6");
        wcscpy(app.txt_sha1, L"2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
        wcscpy(app.txt_sha256, L"d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
        wcscpy(app.txt_ctime, L"2026-10-30 10:58:31");
        wcscpy(app.txt_mtime, L"2026-10-30 10:59:02");
        wcscpy(app.txt_atime, L"2026-10-30 11:02:44");
        app.status_kind = 1;
        wcscpy(app.status_text, L"已应用修改：\n· 重命名为：报告-最终版.txt\n· 修改时间 → 2026-10-30 11:05:00");
        if (mode == 2) {
            app.status_kind = 2;
            wcscpy(app.status_text, L"目标位置已存在同名文件");
        }
    }

    fm_ui_set_size(&app, w, h);
    fm_ui_layout(&app);
    if (mode == 4 || mode == 5) {
        app.scroll_y = app.max_scroll;
        fm_ui_layout(&app);
    }
    fm_ui_render(&app);

    if (!write_bmp(argc > 1 ? argv[1] : "shot.bmp", app.px, app.bw, app.bh)) {
        return 1;
    }
    printf("rendered %dx%d (dpi %d), content_h=%d max_scroll=%d -> %s\n",
           w, h, dpi, app.L.content_h, app.max_scroll, argv[1]);
    return 0;
}
