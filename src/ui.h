/* ui.h - layout, theme, software-antialiased painting */
#ifndef FM_UI_H
#define FM_UI_H

#include <windows.h>
#include "fileops.h"

#define FM_ROWS 9

typedef enum {
    HOT_NONE = 0,
    HOT_MIN = 1,
    HOT_CLOSE = 2,
    HOT_DROP = 3,
    HOT_COPY = 10,   /* + index 0..8 */
    HOT_NOW = 20,    /* + index 0..2 */
    HOT_RESET = 30,
    HOT_APPLY = 31,
    HOT_SYNC = 32,
    HOT_SCROLL = 33,
    HOT_SEC_INFO = 34,
    HOT_FOOT_HASH = 35,
    HOT_PICKER = 36,   /* + index 0..2 */
    HOT_COUNT = 40
} FmHot;

typedef struct {
    RECT hdr;
    RECT btn_min;
    RECT btn_close;
    RECT drop;
    RECT sec_info;
    RECT row[FM_ROWS];
    RECT val[FM_ROWS];
    RECT cpy[FM_ROWS];
    RECT sec_edit;
    RECT lbl[4];
    RECT box[4];
    RECT now[4];
    RECT pick[4];
    RECT sync_box;
    RECT sync_lbl;
    RECT btn_reset;
    RECT btn_apply;
    RECT status;
    RECT hint;
    RECT foot;
    RECT view;
    RECT thumb;
    int content_h;
    int need_scroll;
} FmLayout;

typedef struct {
    HWND hwnd;
    HDC memdc;
    HBITMAP bmp;
    unsigned int *px;
    int bw, bh;
    int dpi;
    float scale;
    int client_w, client_h;

    HFONT f_title, f_body, f_label, f_mono, f_sec, f_btn, f_now, f_copy, f_hint, f_foot, f_footmono;

    int has_file;
    int info_open;
    int foot_copied;
    FileInfo info;
    wchar_t txt_path[FM_PATH_MAX];
    wchar_t txt_size[64];
    wchar_t txt_mime[96];
    wchar_t txt_md5[64];
    wchar_t txt_sha1[72];
    wchar_t txt_sha256[96];
    wchar_t txt_ctime[40];
    wchar_t txt_mtime[40];
    wchar_t txt_atime[40];
    wchar_t txt_foot1[128];
    wchar_t txt_foot2[160];

    int hash_busy;
    int hash_pct;
    unsigned long long hash_token;

    HWND ed_name;
    HWND ed_time[3];
    int sync_on;
    int suppress_sync;

    wchar_t status_text[768];
    int status_kind;   /* 0 hidden, 1 success, 2 error */
    int status_h;

    int copy_flash;
    int dragover;

    int scroll_y;
    int max_scroll;
    int hot;
    int pressed;

    float anim[HOT_COUNT];
    float anim_to[HOT_COUNT];

    FmLayout L;
    RECT clip;
    RECT ed_last[4];
    RECT ed_vis[4];
    int ed_clip[4];
} FmApp;

BOOL fm_ui_init(FmApp *a);
void fm_ui_free(FmApp *a);
void fm_ui_set_dpi(FmApp *a, int dpi);
void fm_ui_set_size(FmApp *a, int w, int h);
void fm_ui_layout(FmApp *a);
void fm_ui_render(FmApp *a);
void fm_ui_paint(FmApp *a);
void fm_ui_sync_children(FmApp *a);
int fm_ui_hit(FmApp *a, int x, int y);
void fm_ui_set_hot(FmApp *a, int hot);
void fm_ui_anim_tick(FmApp *a, float dt);
int fm_ui_anim_active(FmApp *a);
void fm_ui_scroll_by(FmApp *a, int dy);
void fm_ui_scroll_to(FmApp *a, int y);
void fm_ui_set_status(FmApp *a, int kind, const wchar_t *text);
const wchar_t *fm_ui_row_label(int i);
const wchar_t *fm_ui_row_value(FmApp *a, int i);
int fm_ui_row_copyable(FmApp *a, int i);

#endif /* FM_UI_H */
