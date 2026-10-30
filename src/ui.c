/* ui.c - design tokens, layout, software-antialiased GDI painting */
#include "ui.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define RRGB(r, g, b) ((unsigned int)(((r) << 16) | ((g) << 8) | (b)))

#define C_PRIMARY      RRGB(0x4a, 0x90, 0xe2)
#define C_PRIMARY_HOV  RRGB(0x35, 0x7a, 0xbd)
#define C_TEXT         RRGB(0x33, 0x33, 0x33)
#define C_MUTED        RRGB(0x88, 0x88, 0x88)
#define C_BORDER       RRGB(0xe0, 0xe0, 0xe0)
#define C_CARD         RRGB(0xff, 0xff, 0xff)
#define C_SUCCESS      RRGB(0x67, 0xc2, 0x3a)
#define C_DANGER       RRGB(0xe7, 0x4c, 0x3c)
#define C_BTN          RRGB(0xf0, 0xf0, 0xf0)
#define C_BTN_HOV      RRGB(0xe4, 0xe4, 0xe4)
#define C_BTN2_HOV     RRGB(0xe0, 0xe0, 0xe0)
#define C_ROWLINE      RRGB(0xf0, 0xf0, 0xf0)
#define C_DROP_TINT    RRGB(0xf6, 0xf9, 0xfe)
#define C_OK_BG        RRGB(0xf0, 0xf9, 0xeb)
#define C_OK_BORDER    RRGB(0xe1, 0xf3, 0xd8)
#define C_ERR_BG       RRGB(0xfe, 0xf0, 0xf0)
#define C_ERR_BORDER   RRGB(0xf5, 0xd6, 0xd6)
#define C_CHECK_BORDER RRGB(0xdc, 0xdf, 0xe6)
#define C_FOLDER_BACK  RRGB(0x3e, 0x7f, 0xcb)
#define C_FOLDER_FRONT RRGB(0x6f, 0xa9, 0xe9)
#define C_SCROLL       RRGB(0xcf, 0xcf, 0xcf)
#define C_SCROLL_HOV   RRGB(0xb2, 0xb2, 0xb2)

static const wchar_t *ROW_LABELS[FM_ROWS] = {
    L"路径", L"大小", L"类型", L"MD5", L"SHA-1", L"SHA-256",
    L"创建时间", L"修改时间", L"访问时间"
};

static const wchar_t *EDIT_LABELS[4] = {
    L"文件名", L"创建时间", L"修改时间", L"访问时间"
};

static int S(FmApp *a, int v)
{
    double f = (double)v * (double)a->scale * (double)a->dpi / 96.0;
    return (int)(f + 0.5);
}

static unsigned int mix(unsigned int c1, unsigned int c2, float t)
{
    int r1 = (int)((c1 >> 16) & 255), g1 = (int)((c1 >> 8) & 255), b1 = (int)(c1 & 255);
    int r2 = (int)((c2 >> 16) & 255), g2 = (int)((c2 >> 8) & 255), b2 = (int)(c2 & 255);
    int r = (int)(r1 + (r2 - r1) * t);
    int g = (int)(g1 + (g2 - g1) * t);
    int b = (int)(b1 + (b2 - b1) * t);
    return RRGB(r, g, b);
}

static COLORREF cr(unsigned int c)
{
    return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

/* ------------------------------------------------------------ buffer --- */

static void px_blend(FmApp *a, int x, int y, unsigned int c, float cov)
{
    unsigned int *p;
    unsigned int d;
    int r, g, b, dr, dg, db;

    if (cov <= 0.004f) {
        return;
    }
    if (x < a->clip.left || x >= a->clip.right || y < a->clip.top || y >= a->clip.bottom) {
        return;
    }
    p = a->px + (size_t)y * (size_t)a->bw + (size_t)x;
    if (cov >= 0.996f) {
        *p = c;
        return;
    }
    d = *p;
    dr = (int)((d >> 16) & 255);
    dg = (int)((d >> 8) & 255);
    db = (int)(d & 255);
    r = (int)((c >> 16) & 255);
    g = (int)((c >> 8) & 255);
    b = (int)(c & 255);
    r = r + (int)((dr - r) * (1.0f - cov));
    g = g + (int)((dg - g) * (1.0f - cov));
    b = b + (int)((db - b) * (1.0f - cov));
    *p = RRGB(r, g, b);
}

static void fill_rect(FmApp *a, int x0, int y0, int x1, int y1, unsigned int c)
{
    int x, y;
    int xs = x0 > a->clip.left ? x0 : a->clip.left;
    int xe = x1 < a->clip.right ? x1 : a->clip.right;
    int ys = y0 > a->clip.top ? y0 : a->clip.top;
    int ye = y1 < a->clip.bottom ? y1 : a->clip.bottom;

    if (xs < 0) xs = 0;
    if (ys < 0) ys = 0;
    if (xe > a->bw) xe = a->bw;
    if (ye > a->bh) ye = a->bh;
    if (xs >= xe || ys >= ye) {
        return;
    }
    for (y = ys; y < ye; y++) {
        unsigned int *p = a->px + (size_t)y * (size_t)a->bw + (size_t)xs;
        for (x = xs; x < xe; x++) {
            *p++ = c;
        }
    }
}

static void fill_round(FmApp *a, int x0, int y0, int x1, int y1, int rad, unsigned int c)
{
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
    float r = (float)rad;
    int xs = x0, xe = x1, ys = y0, ye = y1, x, y;

    if (r > hw) r = hw;
    if (r > hh) r = hh;
    if (xs < a->clip.left) xs = a->clip.left;
    if (xe > a->clip.right) xe = a->clip.right;
    if (ys < a->clip.top) ys = a->clip.top;
    if (ye > a->clip.bottom) ye = a->clip.bottom;
    if (xs < 0) xs = 0;
    if (ys < 0) ys = 0;
    if (xe > a->bw) xe = a->bw;
    if (ye > a->bh) ye = a->bh;

    for (y = ys; y < ye; y++) {
        float py = (float)y + 0.5f;
        float qy = fabsf(py - cy) - (hh - r);
        unsigned int *row = a->px + (size_t)y * (size_t)a->bw;
        for (x = xs; x < xe; x++) {
            float px = (float)x + 0.5f;
            float qx = fabsf(px - cx) - (hw - r);
            float d, cov;
            if (qx <= 0.0f && qy <= 0.0f) {
                row[x] = c;
                continue;
            }
            if (qx > 0.0f && qy > 0.0f) {
                d = sqrtf(qx * qx + qy * qy) - r;
            } else {
                d = (qx > qy ? qx : qy) - r;
            }
            cov = 0.5f - d;
            if (cov <= 0.0f) {
                continue;
            }
            if (cov >= 1.0f) {
                row[x] = c;
            } else {
                unsigned int dd = row[x];
                int r0 = (int)((dd >> 16) & 255), g0 = (int)((dd >> 8) & 255), b0 = (int)(dd & 255);
                int r1 = (int)((c >> 16) & 255), g1 = (int)((c >> 8) & 255), b1 = (int)(c & 255);
                row[x] = RRGB(r1 + (int)((r0 - r1) * (1.0f - cov)),
                              g1 + (int)((g0 - g1) * (1.0f - cov)),
                              b1 + (int)((b0 - b1) * (1.0f - cov)));
            }
        }
    }
}

static void stroke_round(FmApp *a, int x0, int y0, int x1, int y1, int rad, float t, unsigned int c)
{
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
    float r = (float)rad, half = t * 0.5f;
    int xs = x0 - 2, xe = x1 + 2, ys = y0 - 2, ye = y1 + 2, x, y;

    if (r > hw) r = hw;
    if (r > hh) r = hh;
    if (xs < a->clip.left) xs = a->clip.left;
    if (xe > a->clip.right) xe = a->clip.right;
    if (ys < a->clip.top) ys = a->clip.top;
    if (ye > a->clip.bottom) ye = a->clip.bottom;

    for (y = ys; y < ye; y++) {
        float py = (float)y + 0.5f;
        float qy = fabsf(py - cy) - (hh - r);
        for (x = xs; x < xe; x++) {
            float px = (float)x + 0.5f;
            float qx = fabsf(px - cx) - (hw - r);
            float d, cov;
            if (qx > 0.0f && qy > 0.0f) {
                d = sqrtf(qx * qx + qy * qy) - r;
            } else {
                d = (qx > qy ? qx : qy) - r;
            }
            d = fabsf(d) - half;
            cov = 0.5f - d;
            if (cov > 0.0f) {
                px_blend(a, x, y, c, cov > 1.0f ? 1.0f : cov);
            }
        }
    }
}

static void fill_circle(FmApp *a, float cx, float cy, float rad, unsigned int c)
{
    int xs = (int)floorf(cx - rad - 1), xe = (int)ceilf(cx + rad + 1);
    int ys = (int)floorf(cy - rad - 1), ye = (int)ceilf(cy + rad + 1);
    int x, y;

    for (y = ys; y < ye; y++) {
        for (x = xs; x < xe; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy) - rad;
            float cov = 0.5f - d;
            if (cov > 0.0f) {
                px_blend(a, x, y, c, cov > 1.0f ? 1.0f : cov);
            }
        }
    }
}

static void stroke_circle(FmApp *a, float cx, float cy, float rad, float t, unsigned int c)
{
    int xs = (int)floorf(cx - rad - t - 1), xe = (int)ceilf(cx + rad + t + 1);
    int ys = (int)floorf(cy - rad - t - 1), ye = (int)ceilf(cy + rad + t + 1);
    int x, y;
    float half = t * 0.5f;

    for (y = ys; y < ye; y++) {
        for (x = xs; x < xe; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - rad) - half;
            float cov = 0.5f - d;
            if (cov > 0.0f) {
                px_blend(a, x, y, c, cov > 1.0f ? 1.0f : cov);
            }
        }
    }
}

static void stroke_line(FmApp *a, float x0, float y0, float x1, float y1, float t, unsigned int c)
{
    float vx = x1 - x0, vy = y1 - y0;
    float len2 = vx * vx + vy * vy;
    float half = t * 0.5f;
    int xs = (int)floorf((x0 < x1 ? x0 : x1) - t - 1);
    int xe = (int)ceilf((x0 > x1 ? x0 : x1) + t + 1);
    int ys = (int)floorf((y0 < y1 ? y0 : y1) - t - 1);
    int ye = (int)ceilf((y0 > y1 ? y0 : y1) + t + 1);
    int x, y;

    for (y = ys; y < ye; y++) {
        for (x = xs; x < xe; x++) {
            float px = (float)x + 0.5f - x0, py = (float)y + 0.5f - y0;
            float h = len2 > 0.0f ? (px * vx + py * vy) / len2 : 0.0f;
            float dx, dy, d, cov;
            if (h < 0.0f) h = 0.0f;
            if (h > 1.0f) h = 1.0f;
            dx = px - vx * h;
            dy = py - vy * h;
            d = sqrtf(dx * dx + dy * dy) - half;
            cov = 0.5f - d;
            if (cov > 0.0f) {
                px_blend(a, x, y, c, cov > 1.0f ? 1.0f : cov);
            }
        }
    }
}

/* point at distance s along the rounded-rect outline (clockwise from top-left) */
static void path_point(float x0, float y0, float x1, float y1, float r, float s,
                       float *ox, float *oy)
{
    float l1 = (x1 - x0) - 2 * r;
    float arc = 1.57079632679489661923f * r;
    float l2 = (y1 - y0) - 2 * r;
    const float PI = 3.14159265358979323846f;

    if (s < l1) {
        *ox = x0 + r + s;
        *oy = y0;
        return;
    }
    s -= l1;
    if (s < arc) {
        float ang = -PI / 2 + s / r;
        *ox = (x1 - r) + r * cosf(ang);
        *oy = (y0 + r) + r * sinf(ang);
        return;
    }
    s -= arc;
    if (s < l2) {
        *ox = x1;
        *oy = y0 + r + s;
        return;
    }
    s -= l2;
    if (s < arc) {
        float ang = 0 + s / r;
        *ox = (x1 - r) + r * cosf(ang);
        *oy = (y1 - r) + r * sinf(ang);
        return;
    }
    s -= arc;
    if (s < l1) {
        *ox = x1 - r - s;
        *oy = y1;
        return;
    }
    s -= l1;
    if (s < arc) {
        float ang = PI / 2 + s / r;
        *ox = (x0 + r) + r * cosf(ang);
        *oy = (y1 - r) + r * sinf(ang);
        return;
    }
    s -= arc;
    if (s < l2) {
        *ox = x0;
        *oy = y1 - r - s;
        return;
    }
    s -= l2;
    {
        float ang = PI + s / r;
        *ox = (x0 + r) + r * cosf(ang);
        *oy = (y0 + r) + r * sinf(ang);
    }
}

static void stroke_round_dash(FmApp *a, int x0, int y0, int x1, int y1, int rad,
                              float t, float dash, float gap, unsigned int c)
{
    float r = (float)rad;
    float w = (float)(x1 - x0), h = (float)(y1 - y0);
    float perim = 2 * (w - 2 * r) + 2 * (h - 2 * r) + 2 * 3.14159265f * r;
    float step = 0.6f;
    float s;

    for (s = 0.0f; s < perim; s += step) {
        float m = fmodf(s, dash + gap);
        float ox, oy;
        if (m >= dash) {
            continue;
        }
        path_point((float)x0, (float)y0, (float)x1, (float)y1, r, s, &ox, &oy);
        fill_circle(a, ox, oy, t * 0.5f, c);
    }
}

static void dashed_hline(FmApp *a, int x0, int x1, int y, unsigned int c, int on, int off)
{
    int x = x0;
    if (on < 1) on = 1;
    if (off < 1) off = 1;
    while (x < x1) {
        int e = x + on;
        if (e > x1) e = x1;
        fill_rect(a, x, y, e, y + 1, c);
        x = e + off;
    }
}

/* -------------------------------------------------------------- text --- */

static void text_out(FmApp *a, const wchar_t *s, RECT rc, HFONT f, unsigned int color, UINT fmt)
{
    HGDIOBJ old;

    if (!s || !s[0]) {
        return;
    }
    old = SelectObject(a->memdc, f);
    SetBkMode(a->memdc, TRANSPARENT);
    SetTextColor(a->memdc, cr(color));
    DrawTextW(a->memdc, s, -1, &rc, fmt | DT_NOPREFIX | DT_SINGLELINE);
    SelectObject(a->memdc, old);
}

static int text_w(FmApp *a, HFONT f, const wchar_t *s)
{
    SIZE sz;
    HGDIOBJ old;

    if (!s || !s[0]) {
        return 0;
    }
    old = SelectObject(a->memdc, f);
    GetTextExtentPoint32W(a->memdc, s, (int)wcslen(s), &sz);
    SelectObject(a->memdc, old);
    return sz.cx;
}

static int text_h(FmApp *a, HFONT f, const wchar_t *s, int width, UINT extra)
{
    RECT rc;
    HGDIOBJ old;

    rc.left = 0;
    rc.top = 0;
    rc.right = width;
    rc.bottom = 0;
    old = SelectObject(a->memdc, f);
    DrawTextW(a->memdc, s, -1, &rc, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK | extra);
    SelectObject(a->memdc, old);
    return rc.bottom - rc.top;
}

/* ------------------------------------------------------------- fonts --- */

static HFONT mkfont(int px, int weight, const wchar_t *face)
{
    if (px < 8) px = 8;
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

void fm_ui_set_dpi(FmApp *a, int dpi)
{
    if (dpi < 96) {
        dpi = 96;
    }
    a->dpi = dpi;
    if (a->f_title) DeleteObject(a->f_title);
    if (a->f_body) DeleteObject(a->f_body);
    if (a->f_label) DeleteObject(a->f_label);
    if (a->f_mono) DeleteObject(a->f_mono);
    if (a->f_sec) DeleteObject(a->f_sec);
    if (a->f_btn) DeleteObject(a->f_btn);
    if (a->f_now) DeleteObject(a->f_now);
    if (a->f_copy) DeleteObject(a->f_copy);
    if (a->f_hint) DeleteObject(a->f_hint);
    if (a->f_foot) DeleteObject(a->f_foot);
    if (a->f_footmono) DeleteObject(a->f_footmono);

    a->f_title = mkfont(S(a, 16), FW_MEDIUM, L"Microsoft YaHei UI");
    a->f_body = mkfont(S(a, 14), FW_NORMAL, L"Microsoft YaHei UI");
    a->f_label = mkfont(S(a, 14), FW_MEDIUM, L"Microsoft YaHei UI");
    a->f_mono = mkfont(S(a, 12), FW_NORMAL, L"Consolas");
    a->f_sec = mkfont(S(a, 13), FW_SEMIBOLD, L"Microsoft YaHei UI");
    a->f_btn = mkfont(S(a, 15), FW_MEDIUM, L"Microsoft YaHei UI");
    a->f_now = mkfont(S(a, 13), FW_NORMAL, L"Microsoft YaHei UI");
    a->f_copy = mkfont(S(a, 12), FW_NORMAL, L"Microsoft YaHei UI");
    a->f_hint = mkfont(S(a, 12), FW_NORMAL, L"Microsoft YaHei UI");
    a->f_foot = mkfont(S(a, 11), FW_NORMAL, L"Microsoft YaHei UI");
    a->f_footmono = mkfont(S(a, 11), FW_NORMAL, L"Consolas");
}

/* ------------------------------------------------------------- init ---- */

BOOL fm_ui_init(FmApp *a)
{
    memset(a, 0, sizeof(*a));
    a->dpi = 96;
    a->scale = 0.92f;
    a->copy_flash = -1;
    a->status_kind = 0;
    fm_ui_set_dpi(a, 96);
    return TRUE;
}

void fm_ui_free(FmApp *a)
{
    if (a->memdc) {
        DeleteDC(a->memdc);
        a->memdc = NULL;
    }
    if (a->bmp) {
        DeleteObject(a->bmp);
        a->bmp = NULL;
    }
    a->px = NULL;
}

/* ----------------------------------------------------------- children -- */

void fm_ui_sync_children(FmApp *a)
{
    int i;
    RECT v = a->L.view;

    if (!a->ed_name) {
        return;
    }
    if (!a->has_file) {
        ShowWindow(a->ed_name, SW_HIDE);
        for (i = 0; i < 3; i++) {
            ShowWindow(a->ed_time[i], SW_HIDE);
        }
        return;
    }
    {
        HWND all[4];
        const RECT *boxes[4];
        all[0] = a->ed_name;
        all[1] = a->ed_time[0];
        all[2] = a->ed_time[1];
        all[3] = a->ed_time[2];
        boxes[0] = &a->L.box[0];
        boxes[1] = &a->L.box[1];
        boxes[2] = &a->L.box[2];
        boxes[3] = &a->L.box[3];
        for (i = 0; i < 4; i++) {
            RECT b = *boxes[i];
            int pad = S(a, 1);
            int eh = S(a, 22);
            int x, y, w, h;
            RECT vis;
            int visible;

            if (i > 0) {
                w = (b.right - b.left) - S(a, 2);
                h = (b.bottom - b.top) - S(a, 2);
                x = b.left + S(a, 1);
                y = b.top + S(a, 1);
            } else {
                w = (b.right - b.left) - pad * 2;
                h = eh;
                x = b.left + pad + S(a, 8);
                y = b.top + ((b.bottom - b.top) - eh) / 2;
                w -= S(a, 8);
            }

            vis = b;
            visible = IntersectRect(&vis, &b, &v) && vis.bottom > vis.top;
            if (!visible) {
                ShowWindow(all[i], SW_HIDE);
                continue;
            }
            {
                RECT nr = {x, y, x + w, y + h};
                if (!EqualRect(&nr, &a->ed_last[i])) {
                    a->ed_last[i] = nr;
                    MoveWindow(all[i], x, y, w, h, TRUE);
                }
            }
            {
                int clipped = (vis.top > b.top || vis.bottom < b.bottom);
                if (clipped) {
                    if (!a->ed_clip[i] || !EqualRect(&a->ed_vis[i], &vis)) {
                        HRGN rgn = CreateRectRgn(vis.left - x, vis.top - y,
                                                 vis.right - x, vis.bottom - y);
                        SetWindowRgn(all[i], rgn, TRUE);
                        a->ed_clip[i] = 1;
                        a->ed_vis[i] = vis;
                    }
                } else if (a->ed_clip[i]) {
                    SetWindowRgn(all[i], NULL, TRUE);
                    a->ed_clip[i] = 0;
                }
            }
            ShowWindow(all[i], SW_SHOWNA);
        }
    }
}

/* ------------------------------------------------------------ layout --- */

static int status_box_height(FmApp *a, int width)
{
    int h;

    if (!a->status_kind) {
        return 0;
    }
    h = text_h(a, a->f_body, a->status_text, width - S(a, 24) - S(a, 22), 0);
    if (h < S(a, 18)) {
        h = S(a, 18);
    }
    return h + S(a, 24);
}

static void layout_pass(FmApp *a)
{
    FmLayout *L = &a->L;
    int W = a->client_w, H = a->client_h;
    int pad = S(a, 24);
    int hdr = S(a, 44);
    int x = pad, w = W - pad * 2;
    int y;
    int i;
    int copy_w, now_w, row_h = S(a, 46), box_h = S(a, 39), line_h = S(a, 19);
    int hint_h;

    memset(L, 0, sizeof(*L));
    L->hdr = (RECT){0, 0, W, hdr};
    L->view = (RECT){0, hdr, W, H};
    {
        int bw = S(a, 40);
        L->btn_min = (RECT){W - bw * 2, 0, W - bw, hdr};
        L->btn_close = (RECT){W - bw, 0, W, hdr};
    }

    y = hdr + pad - a->scroll_y;
    L->drop = (RECT){x, y, x + w, y + S(a, 126)};
    y = L->drop.bottom + S(a, 20);

    if (a->has_file) {
        L->sec_info = (RECT){x, y, x + w, y + S(a, 25)};
        y += S(a, 25) + S(a, 10);

        copy_w = text_w(a, a->f_copy, L"已复制") + S(a, 9) + S(a, 4);
        {
            int w2 = text_w(a, a->f_copy, L"复制");
            if (w2 > copy_w) copy_w = w2;
        }
        copy_w += S(a, 20);

        if (a->info_open) {
            for (i = 0; i < FM_ROWS; i++) {
                L->row[i] = (RECT){x, y, x + w, y + row_h};
                L->cpy[i] = (RECT){x + w - copy_w, y + S(a, 9), x + w, y + row_h - S(a, 9)};
                L->val[i] = (RECT){x + S(a, 98), y, L->cpy[i].left - S(a, 10), y + row_h};
                y += row_h;
            }
        }

        y += S(a, 22);
        L->sec_edit = (RECT){x, y, x + w, y + S(a, 25)};
        y += S(a, 25) + S(a, 10);

        now_w = text_w(a, a->f_now, L"设为现在") + S(a, 28);
        for (i = 0; i < 4; i++) {
            L->lbl[i] = (RECT){x, y, x + S(a, 88), y + box_h};
            L->now[i] = (RECT){x + w - now_w, y, x + w, y + box_h};
            L->box[i] = (RECT){x + S(a, 98), y, L->now[i].left - S(a, 10), y + box_h};
            y += box_h + S(a, 14);
        }

        y += S(a, 6);
        L->sync_box = (RECT){x, y + (line_h - S(a, 15)) / 2, x + S(a, 15), y + (line_h - S(a, 15)) / 2 + S(a, 15)};
        L->sync_lbl = (RECT){x + S(a, 15) + S(a, 8), y, x + w, y + line_h};
        y += line_h + S(a, 4);

        y += S(a, 26);
        L->btn_reset = (RECT){x, y, x + (w - S(a, 12)) / 2, y + S(a, 45)};
        L->btn_apply = (RECT){x + (w - S(a, 12)) / 2 + S(a, 12), y, x + w, y + S(a, 45)};
        y += S(a, 45);

        if (a->status_kind) {
            int sh = status_box_height(a, w);
            y += S(a, 16);
            L->status = (RECT){x, y, x + w, y + sh};
            y += sh;
        }
    }

    y += S(a, 14);
    hint_h = text_h(a, a->f_hint, L"提示：修改时间戳会直接写入文件系统，部分网络盘或只读介质可能拒绝写入；哈希在本地计算，不经网络传输。",
                    w, 0);
    if (hint_h < S(a, 19)) hint_h = S(a, 19);
    L->hint = (RECT){x, y, x + w, y + hint_h};
    y += hint_h;

    y += S(a, 16);
    L->foot = (RECT){x, y, x + w, y + S(a, 34)};
    y += S(a, 34);

    L->content_h = (y + pad) - (hdr - a->scroll_y);
}

void fm_ui_layout(FmApp *a)
{
    int view_h = a->client_h;
    int guard = 0;

    for (;;) {
        layout_pass(a);
        view_h = a->L.view.bottom - a->L.view.top;
        a->max_scroll = a->L.content_h - view_h;
        if (a->max_scroll < 0) {
            a->max_scroll = 0;
        }
        if (a->scroll_y > a->max_scroll) {
            a->scroll_y = a->max_scroll;
            if (++guard < 3) {
                continue;
            }
        }
        if (a->scroll_y < 0) {
            a->scroll_y = 0;
            if (++guard < 3) {
                continue;
            }
        }
        break;
    }

    if (a->max_scroll > 0 && a->max_scroll <= S(a, 3)) {
        a->max_scroll = 0;
        a->scroll_y = 0;
    }

    /* scrollbar thumb */
    a->L.thumb = (RECT){0, 0, 0, 0};
    a->L.need_scroll = a->max_scroll > 0;
    if (a->L.need_scroll) {
        int track_top = a->L.view.top;
        int track_h = view_h;
        int th = (int)((double)track_h * (double)view_h / (double)a->L.content_h);
        int ty;
        int sbw = S(a, 6);
        if (th < S(a, 34)) th = S(a, 34);
        ty = track_top + (int)((double)(track_h - th) * (double)a->scroll_y / (double)a->max_scroll);
        a->L.thumb = (RECT){a->client_w - sbw - S(a, 4), ty, a->client_w - S(a, 4), ty + th};
    }
}

/* ------------------------------------------------------------ paints --- */

static void paint_clock(FmApp *a, float cx, float cy, float rad, unsigned int c, float t)
{
    stroke_circle(a, cx, cy, rad, t, c);
    stroke_line(a, cx, cy, cx, cy - rad * 0.55f, t, c);
    stroke_line(a, cx, cy, cx + rad * 0.45f, cy + rad * 0.28f, t, c);
}

static void paint_folder(FmApp *a, int x, int y, int w, int h)
{
    int tab_bottom = y + (int)(h * 0.46f);
    int body_top = y + (int)(h * 0.28f);

    fill_round(a, x, y + (int)(h * 0.04f), x + (int)(w * 0.44f), tab_bottom, S(a, 2), C_FOLDER_BACK);
    fill_round(a, x, body_top, x + w, y + h, S(a, 3), C_FOLDER_FRONT);
}

static void paint_header(FmApp *a)
{
    FmLayout *L = &a->L;
    RECT rc;

    fill_rect(a, L->hdr.left, L->hdr.top, L->hdr.right, L->hdr.bottom, C_PRIMARY);

    paint_clock(a, (float)(L->hdr.left + S(a, 24)),
                (float)((L->hdr.top + L->hdr.bottom) / 2), (float)S(a, 7), C_CARD, (float)S(a, 2));

    rc = L->hdr;
    rc.left += S(a, 40);
    rc.right -= S(a, 90);
    text_out(a, L"文件属性修改器", rc, a->f_title, C_CARD, DT_LEFT | DT_VCENTER);

    /* window buttons */
    {
        float t = a->anim[HOT_MIN];
        if (t > 0.01f) {
            fill_rect(a, L->btn_min.left, L->btn_min.top, L->btn_min.right, L->btn_min.bottom,
                      mix(C_PRIMARY, C_CARD, 0.18f * t));
        }
        {
            float cx = (L->btn_min.left + L->btn_min.right) * 0.5f;
            float cy = (L->btn_min.top + L->btn_min.bottom) * 0.5f;
            stroke_line(a, cx - S(a, 5), cy, cx + S(a, 5), cy, (float)S(a, 1.4f), C_CARD);
        }
    }
    {
        float t = a->anim[HOT_CLOSE];
        if (t > 0.01f) {
            fill_rect(a, L->btn_close.left, L->btn_close.top, L->btn_close.right, L->btn_close.bottom,
                      mix(C_PRIMARY, C_CARD, 0.22f * t));
        }
        {
            float cx = (L->btn_close.left + L->btn_close.right) * 0.5f;
            float cy = (L->btn_close.top + L->btn_close.bottom) * 0.5f;
            float e = (float)S(a, 5);
            stroke_line(a, cx - e, cy - e, cx + e, cy + e, (float)S(a, 1.4f), C_CARD);
            stroke_line(a, cx - e, cy + e, cx + e, cy - e, (float)S(a, 1.4f), C_CARD);
        }
    }
}

static void paint_drop(FmApp *a)
{
    FmLayout *L = &a->L;
    RECT d = L->drop;
    float t = a->anim[HOT_DROP];
    unsigned int border = mix(C_BORDER, C_PRIMARY, t);
    unsigned int bg = mix(C_CARD, C_DROP_TINT, t);
    int r = S(a, 8);
    int iconw = S(a, 42), iconh = S(a, 32);
    int cx = (d.left + d.right) / 2;
    int ty;

    if (a->dragover) {
        border = C_PRIMARY;
        bg = C_DROP_TINT;
    }
    fill_round(a, d.left, d.top, d.right, d.bottom, r, bg);
    stroke_round_dash(a, d.left + 1, d.top + 1, d.right - 1, d.bottom - 1, r,
                      (float)S(a, 2), (float)S(a, 7), (float)S(a, 5), border);

    paint_folder(a, cx - iconw / 2, d.top + S(a, 28), iconw, iconh);

    ty = d.top + S(a, 28) + iconh + S(a, 10);
    {
        RECT rc = {d.left + S(a, 10), ty, d.right - S(a, 10), ty + S(a, 20)};
        text_out(a, L"点击选择文件，或将文件拖拽到此处", rc, a->f_body, C_MUTED, DT_CENTER | DT_VCENTER);
    }
}

static void paint_section_title(FmApp *a, RECT rc, const wchar_t *text, int collapsible, int open)
{
    RECT tr = rc;
    HGDIOBJ old;
    int extra = S(a, 1);

    tr.bottom = tr.top + S(a, 18);
    old = SelectObject(a->memdc, a->f_sec);
    SetTextCharacterExtra(a->memdc, extra);
    SelectObject(a->memdc, old);
    text_out(a, text, tr, a->f_sec, C_PRIMARY, DT_LEFT | DT_VCENTER);
    old = SelectObject(a->memdc, a->f_sec);
    SetTextCharacterExtra(a->memdc, 0);
    SelectObject(a->memdc, old);

    fill_rect(a, rc.left, rc.bottom - 1, rc.right, rc.bottom, C_BORDER);

    if (collapsible) {
        unsigned int c = mix(C_PRIMARY, C_PRIMARY_HOV, a->anim[HOT_SEC_INFO]);
        float cx = (float)(rc.right - S(a, 8));
        float cy = (float)(rc.top + S(a, 9));
        float h = (float)S(a, 4);
        float w = (float)S(a, 4);
        float t = (float)S(a, 2) < 1.5f ? 1.5f : (float)S(a, 2) * 0.8f;
        if (open) {
            stroke_line(a, cx - w, cy - h * 0.45f, cx, cy + h * 0.55f, t, c);
            stroke_line(a, cx, cy + h * 0.55f, cx + w, cy - h * 0.45f, t, c);
        } else {
            stroke_line(a, cx - w * 0.5f, cy - h, cx + w * 0.55f, cy, t, c);
            stroke_line(a, cx + w * 0.55f, cy, cx - w * 0.5f, cy + h, t, c);
        }
    }
}

static void paint_copy_button(FmApp *a, RECT b, int idx)
{
    int hovered = (a->hot == HOT_COPY + idx);
    int flash = (a->copy_flash == idx);
    unsigned int bg = flash ? C_SUCCESS : (hovered ? C_BTN_HOV : C_BTN);
    unsigned int bord = flash ? C_SUCCESS : C_BORDER;
    unsigned int fg = flash ? C_CARD : C_TEXT;
    RECT tr;

    fill_round(a, b.left, b.top, b.right, b.bottom, S(a, 6), bg);
    stroke_round(a, b.left, b.top, b.right, b.bottom, S(a, 6), 1.0f, bord);

    tr = b;
    if (flash) {
        int tw = text_w(a, a->f_copy, L"已复制");
        int gap = S(a, 4);
        int tickw = S(a, 9);
        int total = tw + gap + tickw;
        int left = (b.left + b.right) / 2 - total / 2;
        RECT t2 = b;
        float bx, by;

        t2.left = left;
        t2.right = left + tw;
        text_out(a, L"已复制", t2, a->f_copy, fg, DT_LEFT | DT_VCENTER);

        bx = (float)(left + tw + gap);
        by = (float)((b.top + b.bottom) / 2);
        stroke_line(a, bx, by + S(a, 1), bx + S(a, 3.2f), by + S(a, 4), 1.7f, fg);
        stroke_line(a, bx + S(a, 3.2f), by + S(a, 4), bx + S(a, 8.4f), by - S(a, 3.4f), 1.7f, fg);
    } else {
        text_out(a, L"复制", tr, a->f_copy, fg, DT_CENTER | DT_VCENTER);
    }
}

static void paint_info(FmApp *a)
{
    FmLayout *L = &a->L;
    int i;

    paint_section_title(a, L->sec_info, L"文件信息", 1, a->info_open);
    if (!a->info_open) {
        return;
    }
    for (i = 0; i < FM_ROWS; i++) {
        RECT lr = L->row[i];
        RECT tr;
        const wchar_t *val = fm_ui_row_value(a, i);

        tr = lr;
        tr.right = lr.left + S(a, 88);
        text_out(a, ROW_LABELS[i], tr, a->f_body, C_MUTED, DT_LEFT | DT_VCENTER);

        text_out(a, val, L->val[i], a->f_mono, C_TEXT,
                 DT_LEFT | DT_VCENTER | (i == 0 ? DT_PATH_ELLIPSIS : DT_END_ELLIPSIS));

        paint_copy_button(a, L->cpy[i], i);

        if (i < FM_ROWS - 1) {
            dashed_hline(a, lr.left, lr.right, lr.bottom - 1, C_ROWLINE, S(a, 3), S(a, 3));
        }
    }
}

static void paint_edit(FmApp *a)
{
    FmLayout *L = &a->L;
    int i;

    paint_section_title(a, L->sec_edit, L"修改设置", 0, 0);

    for (i = 0; i < 4; i++) {
        RECT b = L->box[i];
        BOOL focus = (a->ed_name && GetFocus() == (i == 0 ? a->ed_name : a->ed_time[i - 1]));
        unsigned int bord = focus ? C_PRIMARY : C_BORDER;
        RECT lr = L->lbl[i];
        RECT nr;

        fill_round(a, b.left, b.top, b.right, b.bottom, S(a, 8), C_CARD);
        stroke_round(a, b.left, b.top, b.right, b.bottom, S(a, 8), 1.0f, bord);


        text_out(a, EDIT_LABELS[i], lr, a->f_label, C_TEXT, DT_LEFT | DT_VCENTER);

        if (i > 0) {
            float t = a->anim[HOT_NOW + i - 1];
            unsigned int bg = mix(C_BTN, C_BTN_HOV, t);
            fill_round(a, L->now[i].left, L->now[i].top, L->now[i].right, L->now[i].bottom,
                       S(a, 8), bg);
            stroke_round(a, L->now[i].left, L->now[i].top, L->now[i].right, L->now[i].bottom,
                         S(a, 8), 1.0f, C_BORDER);
            nr = L->now[i];
            text_out(a, L"设为现在", nr, a->f_now, C_TEXT, DT_CENTER | DT_VCENTER);
        }
    }

    /* sync checkbox */
    {
        RECT b = L->sync_box;
        int r = S(a, 3);
        float t = a->anim[HOT_SYNC];
        if (a->sync_on) {
            fill_round(a, b.left, b.top, b.right, b.bottom, r, C_PRIMARY);
            {
                float x0 = (float)b.left, y0 = (float)b.top;
                float w = (float)(b.right - b.left);
                stroke_line(a, x0 + w * 0.24f, y0 + w * 0.52f, x0 + w * 0.44f, y0 + w * 0.72f, 1.8f, C_CARD);
                stroke_line(a, x0 + w * 0.44f, y0 + w * 0.72f, x0 + w * 0.78f, y0 + w * 0.30f, 1.8f, C_CARD);
            }
        } else {
            fill_round(a, b.left, b.top, b.right, b.bottom, r, C_CARD);
            stroke_round(a, b.left, b.top, b.right, b.bottom, r, 1.0f, mix(C_CHECK_BORDER, C_PRIMARY, t));
        }
        text_out(a, L"三个时间联动同步", L->sync_lbl, a->f_body,
                 mix(C_MUTED, C_TEXT, t), DT_LEFT | DT_VCENTER);
    }
}

static void paint_actions(FmApp *a)
{
    FmLayout *L = &a->L;
    RECT r = L->btn_reset;

    fill_round(a, r.left, r.top, r.right, r.bottom, S(a, 8), mix(C_BTN, C_BTN2_HOV, a->anim[HOT_RESET]));
    text_out(a, L"重置", r, a->f_btn, C_TEXT, DT_CENTER | DT_VCENTER);

    r = L->btn_apply;
    fill_round(a, r.left, r.top, r.right, r.bottom, S(a, 8),
               mix(C_PRIMARY, C_PRIMARY_HOV, a->anim[HOT_APPLY]));
    text_out(a, L"应用修改", r, a->f_btn, C_CARD, DT_CENTER | DT_VCENTER);
}

static void paint_status(FmApp *a)
{
    RECT b = a->L.status;
    int ok = (a->status_kind == 1);
    unsigned int bg = ok ? C_OK_BG : C_ERR_BG;
    unsigned int bd = ok ? C_OK_BORDER : C_ERR_BORDER;
    unsigned int fg = ok ? C_SUCCESS : C_DANGER;
    RECT tr;
    float cx, cy, rad;

    if (!a->status_kind) {
        return;
    }
    fill_round(a, b.left, b.top, b.right, b.bottom, S(a, 8), bg);
    stroke_round(a, b.left, b.top, b.right, b.bottom, S(a, 8), 1.0f, bd);

    cx = (float)(b.left + S(a, 12) + S(a, 7));
    cy = (float)(b.top + S(a, 12) + S(a, 9));
    rad = (float)S(a, 7);
    stroke_circle(a, cx, cy, rad, 1.4f, fg);
    if (ok) {
        stroke_line(a, cx - rad * 0.45f, cy, cx - rad * 0.08f, cy + rad * 0.38f, 1.5f, fg);
        stroke_line(a, cx - rad * 0.08f, cy + rad * 0.38f, cx + rad * 0.48f, cy - rad * 0.38f, 1.5f, fg);
    } else {
        stroke_line(a, cx - rad * 0.4f, cy - rad * 0.4f, cx + rad * 0.4f, cy + rad * 0.4f, 1.5f, fg);
        stroke_line(a, cx - rad * 0.4f, cy + rad * 0.4f, cx + rad * 0.4f, cy - rad * 0.4f, 1.5f, fg);
    }

    tr = b;
    tr.left += S(a, 12) + S(a, 22);
    tr.right -= S(a, 12);
    tr.top += S(a, 12);
    tr.bottom -= S(a, 12);
    {
        HGDIOBJ old = SelectObject(a->memdc, a->f_body);
        SetBkMode(a->memdc, TRANSPARENT);
        SetTextColor(a->memdc, cr(fg));
        DrawTextW(a->memdc, a->status_text, -1, &tr, DT_NOPREFIX | DT_WORDBREAK);
        SelectObject(a->memdc, old);
    }
}

static void paint_hint(FmApp *a)
{
    RECT rc = a->L.hint;
    HGDIOBJ old = SelectObject(a->memdc, a->f_hint);
    SetBkMode(a->memdc, TRANSPARENT);
    SetTextColor(a->memdc, cr(C_MUTED));
    DrawTextW(a->memdc, L"提示：修改时间戳会直接写入文件系统，部分网络盘或只读介质可能拒绝写入；哈希在本地计算，不经网络传输。",
              -1, &rc, DT_NOPREFIX | DT_WORDBREAK);
    SelectObject(a->memdc, old);
}

static void paint_footer(FmApp *a)
{
    RECT r = a->L.foot;
    RECT r1 = r, r2 = r;
    unsigned int c1 = RRGB(0x99, 0x99, 0x99);
    unsigned int c2 = RRGB(0x99, 0x99, 0x99);

    r1.bottom = r1.top + S(a, 16);
    text_out(a, a->txt_foot1, r1, a->f_foot, c1, DT_LEFT | DT_VCENTER);

    r2.top = r1.bottom + S(a, 2);
    if (a->foot_copied) {
        c2 = C_SUCCESS;
    } else if (a->hot == HOT_FOOT_HASH) {
        c2 = mix(C_MUTED, C_TEXT, 0.6f);
    }
    text_out(a, a->txt_foot2, r2, a->f_footmono, c2, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
}

static void paint_scrollbar(FmApp *a)
{
    RECT t;

    if (!a->L.need_scroll) {
        return;
    }
    t = a->L.thumb;
    if (t.bottom <= t.top) {
        return;
    }
    fill_round(a, t.left, t.top, t.right, t.bottom, S(a, 3),
               a->anim[HOT_SCROLL] > 0.5f ? C_SCROLL_HOV : C_SCROLL);
}

void fm_ui_render(FmApp *a)
{
    RECT full;

    if (!a->memdc || !a->px) {
        return;
    }
    full = (RECT){0, 0, a->bw, a->bh};
    a->clip = full;
    fill_rect(a, 0, 0, a->bw, a->bh, C_CARD);

    SelectClipRgn(a->memdc, NULL);

    paint_header(a);

    a->clip = a->L.view;
    IntersectClipRect(a->memdc, a->L.view.left, a->L.view.top, a->L.view.right, a->L.view.bottom);

    paint_drop(a);
    if (a->has_file) {
        paint_info(a);
        paint_edit(a);
        paint_actions(a);
        paint_status(a);
    }
    paint_hint(a);
    paint_footer(a);

    SelectClipRgn(a->memdc, NULL);
    a->clip = full;
    paint_scrollbar(a);
}

void fm_ui_paint(FmApp *a)
{
    HDC dc;

    if (!a->memdc || !a->px || !a->hwnd) {
        return;
    }
    fm_ui_render(a);
    fm_ui_sync_children(a);
    dc = GetDC(a->hwnd);
    BitBlt(dc, 0, 0, a->bw, a->bh, a->memdc, 0, 0, SRCCOPY);
    ReleaseDC(a->hwnd, dc);
}

/* --------------------------------------------------------- hit / anim -- */

const wchar_t *fm_ui_row_label(int i)
{
    return (i >= 0 && i < FM_ROWS) ? ROW_LABELS[i] : L"";
}

const wchar_t *fm_ui_row_value(FmApp *a, int i)
{
    switch (i) {
    case 0: return a->txt_path;
    case 1: return a->txt_size;
    case 2: return a->txt_mime;
    case 3: return a->txt_md5;
    case 4: return a->txt_sha1;
    case 5: return a->txt_sha256;
    case 6: return a->txt_ctime;
    case 7: return a->txt_mtime;
    case 8: return a->txt_atime;
    default: return L"";
    }
}

int fm_ui_row_copyable(FmApp *a, int i)
{
    const wchar_t *v = fm_ui_row_value(a, i);

    if (!v || !v[0]) {
        return 0;
    }
    if (wcscmp(v, L"-") == 0) {
        return 0;
    }
    if (wcsncmp(v, L"计算中", 3) == 0) {
        return 0;
    }
    if (wcscmp(v, L"计算失败") == 0) {
        return 0;
    }
    return 1;
}

static int pt_in(RECT r, int x, int y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

int fm_ui_hit(FmApp *a, int x, int y)
{
    FmLayout *L = &a->L;
    int i;

    if (pt_in(L->btn_close, x, y)) return HOT_CLOSE;
    if (pt_in(L->btn_min, x, y)) return HOT_MIN;

    if (y < L->view.top) {
        return HOT_NONE;
    }
    if (L->need_scroll && pt_in(L->thumb, x, y)) return HOT_SCROLL;
    if (pt_in(L->drop, x, y)) return HOT_DROP;

    if (!a->has_file) {
        return HOT_NONE;
    }
    if (pt_in(L->sec_info, x, y)) return HOT_SEC_INFO;
    for (i = 0; i < FM_ROWS; i++) {
        if (pt_in(L->cpy[i], x, y) && fm_ui_row_copyable(a, i)) {
            return HOT_COPY + i;
        }
    }
    for (i = 1; i < 4; i++) {
        if (pt_in(L->now[i], x, y)) return HOT_NOW + i - 1;
    }
    if (pt_in(L->sync_box, x, y) || pt_in(L->sync_lbl, x, y)) return HOT_SYNC;
    if (pt_in(L->btn_reset, x, y)) return HOT_RESET;
    if (pt_in(L->btn_apply, x, y)) return HOT_APPLY;
    if (pt_in(L->foot, x, y) && a->txt_foot2[0]) return HOT_FOOT_HASH;

    return HOT_NONE;
}

void fm_ui_set_hot(FmApp *a, int hot)
{
    int i;

    if (a->hot == hot) {
        return;
    }
    a->hot = hot;
    for (i = 0; i < HOT_COUNT; i++) {
        a->anim_to[i] = (i == hot) ? 1.0f : 0.0f;
    }
}

void fm_ui_anim_tick(FmApp *a, float dt)
{
    int i;
    float rate = dt / 0.18f;

    for (i = 0; i < HOT_COUNT; i++) {
        float t = a->anim_to[i];
        float v = a->anim[i];
        if (v < t) {
            v += rate;
            if (v > t) v = t;
        } else if (v > t) {
            v -= rate;
            if (v < t) v = t;
        }
        a->anim[i] = v;
    }
}

int fm_ui_anim_active(FmApp *a)
{
    int i;
    for (i = 0; i < HOT_COUNT; i++) {
        if (fabsf(a->anim[i] - a->anim_to[i]) > 0.01f) {
            return 1;
        }
    }
    return 0;
}

void fm_ui_scroll_to(FmApp *a, int y)
{
    if (y < 0) y = 0;
    if (y > a->max_scroll) y = a->max_scroll;
    if (y != a->scroll_y) {
        a->scroll_y = y;
        fm_ui_layout(a);
    }
}

void fm_ui_scroll_by(FmApp *a, int dy)
{
    fm_ui_scroll_to(a, a->scroll_y + dy);
}

void fm_ui_set_status(FmApp *a, int kind, const wchar_t *text)
{
    a->status_kind = kind;
    if (text) {
        wcsncpy(a->status_text, text, 767);
        a->status_text[767] = 0;
    } else {
        a->status_text[0] = 0;
    }
    fm_ui_layout(a);
}

void fm_ui_set_size(FmApp *a, int w, int h)
{
    BITMAPINFO bi;
    HDC screen;

    if (w <= 0 || h <= 0) {
        return;
    }
    if (a->client_w == w && a->client_h == h && a->px) {
        return;
    }
    fm_ui_free(a);

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    screen = a->hwnd ? GetDC(a->hwnd) : NULL;
    a->memdc = CreateCompatibleDC(screen);
    a->bmp = CreateDIBSection(a->memdc, &bi, DIB_RGB_COLORS, (void **)&a->px, NULL, 0);
    if (screen) {
        ReleaseDC(a->hwnd, screen);
    }
    if (!a->memdc || !a->bmp) {
        return;
    }
    SelectObject(a->memdc, a->bmp);
    SetBkMode(a->memdc, TRANSPARENT);
    a->bw = w;
    a->bh = h;
    a->client_w = w;
    a->client_h = h;
    a->clip = (RECT){0, 0, w, h};
}
