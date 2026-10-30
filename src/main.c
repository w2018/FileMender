/* main.c - 文件属性修改器 / FileMender, pure C + Win32 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <commctrl.h>
#include <ole2.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#include "ui.h"
#include "fileops.h"
#include "hash.h"
#include "util.h"

#define ID_EDIT_NAME   1001
#define ID_EDIT_TIME0  1002
#define ID_EDIT_TIME1  1003
#define ID_EDIT_TIME2  1004

#define WM_HASH_PROGRESS (WM_APP + 1)
#define WM_HASH_DONE     (WM_APP + 2)
#define WM_APPLY_REQ     (WM_APP + 3)

#define TIMER_ANIM    1
#define TIMER_STATUS  2
#define TIMER_COPY    3

#ifndef FM_VERSION_TEXT
#define FM_VERSION_TEXT "1.0.0"
#endif
#define FM_AUTHOR L"曾先生"

static FmApp g_app;
static HINSTANCE g_inst;
static wchar_t g_self_hash[65];

static void fit_window(HWND hwnd, int recenter);
static void get_work_area(HWND hwnd, RECT *wa);
static volatile LONG64 g_gen = 0;
static int g_drag_active = 0;
static int g_drag_grab = 0;
static int g_drag_thumb_h = 0;

typedef struct {
    wchar_t path[FM_PATH_MAX];
    unsigned long long size;
    unsigned long long token;
    HWND hwnd;
} HashJob;

typedef struct {
    unsigned long long token;
    wchar_t md5[33];
    wchar_t sha1[41];
    wchar_t sha256[65];
} HashResult;

static void compute_self_hash(void)
{
    wchar_t path[FM_PATH_MAX];
    wchar_t xp[FM_PATH_MAX];
    HANDLE h;
    unsigned char *buf;
    DWORD got = 0;
    sha256_ctx ctx;
    unsigned char d[32];

    g_self_hash[0] = 0;
    if (!GetModuleFileNameW(NULL, path, FM_PATH_MAX)) {
        return;
    }
    h = CreateFileW(extended_path(xp, FM_PATH_MAX, path), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    buf = (unsigned char *)malloc(1u << 16);
    if (!buf) {
        CloseHandle(h);
        return;
    }
    sha256_init(&ctx);
    while (ReadFile(h, buf, 1u << 16, &got, NULL) && got > 0) {
        sha256_update(&ctx, buf, got);
    }
    sha256_final(&ctx, d);
    bytes_to_hex(d, 32, g_self_hash);
    free(buf);
    CloseHandle(h);
}

static void build_footer_text(void)
{
    wchar_t ver[64];

    u8_to_w(FM_VERSION_TEXT, ver, 64);
    swprintf(g_app.txt_foot1, 128, L"版本 %s   ·   作者 %s", ver, FM_AUTHOR);
    if (g_self_hash[0]) {
        swprintf(g_app.txt_foot2, 160, L"SHA-256 %s", g_self_hash);
    } else {
        wcscpy(g_app.txt_foot2, L"SHA-256 -");
    }
}

static void invalidate(void)
{
    InvalidateRect(g_app.hwnd, NULL, FALSE);
}

static int scale(int v)
{
    return MulDiv(v, g_app.dpi, 96);
}

/* design px -> device px, honouring the UI scale used by the layout engine */
static int spx(int v)
{
    return (int)((double)v * (double)g_app.scale * (double)g_app.dpi / 96.0 + 0.5);
}

/* ------------------------------------------------------------- state --- */

static const wchar_t *file_base_name(const wchar_t *path)
{
    const wchar_t *p = wcsrchr(path, L'\\');
    return p ? p + 1 : path;
}

static void dtp_set_ft(HWND dtp, const FILETIME *ft)
{
    FILETIME local;
    SYSTEMTIME st;

    if (!ft) {
        SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_NONE, 0);
        return;
    }
    if (!FileTimeToLocalFileTime(ft, &local) || !FileTimeToSystemTime(&local, &st)) {
        SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_NONE, 0);
        return;
    }
    SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
}

static void dtp_set_st(HWND dtp, const SYSTEMTIME *st)
{
    SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)st);
}

static BOOL dtp_get_ft(HWND dtp, FILETIME *out)
{
    SYSTEMTIME st;
    FILETIME local;

    memset(&st, 0, sizeof(st));
    if (SendMessageW(dtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) != GDT_VALID) {
        return FALSE;
    }
    if (!SystemTimeToFileTime(&st, &local)) {
        return FALSE;
    }
    return LocalFileTimeToFileTime(&local, out);
}

static void dtp_get_text(HWND dtp, wchar_t *buf, size_t cap)
{
    SYSTEMTIME st;

    buf[0] = 0;
    memset(&st, 0, sizeof(st));
    if (SendMessageW(dtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) == GDT_VALID) {
        swprintf(buf, cap, L"%04d-%02d-%02d %02d:%02d:%02d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    }
}

static void dtp_set_now(HWND dtp)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    st.wMilliseconds = 0;
    SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
}

static void fill_info_texts(void)
{
    FileInfo *fi = &g_app.info;
    wchar_t t[64];

    wcsncpy(g_app.txt_path, fi->path, FM_PATH_MAX - 1);
    g_app.txt_path[FM_PATH_MAX - 1] = 0;
    fmt_bytes(fi->size, g_app.txt_size, 64);
    wcsncpy(g_app.txt_mime, fi->mime, 95);
    g_app.txt_mime[95] = 0;
    fmt_filetime_local(&fi->ctime, g_app.txt_ctime, 40);
    fmt_filetime_local(&fi->mtime, g_app.txt_mtime, 40);
    fmt_filetime_local(&fi->atime, g_app.txt_atime, 40);

    (void)t;
    dtp_set_ft(g_app.ed_time[0], &fi->ctime);
    dtp_set_ft(g_app.ed_time[1], &fi->mtime);
    dtp_set_ft(g_app.ed_time[2], &fi->atime);
}

static void set_hash_busy_texts(void)
{
    wcscpy(g_app.txt_md5, L"计算中…");
    wcscpy(g_app.txt_sha1, L"计算中…");
    wcscpy(g_app.txt_sha256, L"计算中…");
}

static void clear_state(void)
{
    g_gen++;
    g_app.has_file = 0;
    g_app.info_open = 0;
    g_app.hash_busy = 0;
    g_app.hash_pct = 0;
    g_app.scroll_y = 0;
    g_app.copy_flash = -1;
    g_app.status_kind = 0;
    g_app.status_text[0] = 0;
    g_app.txt_path[0] = 0;
    g_app.txt_size[0] = 0;
    g_app.txt_mime[0] = 0;
    g_app.txt_md5[0] = 0;
    g_app.txt_sha1[0] = 0;
    g_app.txt_sha256[0] = 0;
    g_app.txt_ctime[0] = 0;
    g_app.txt_mtime[0] = 0;
    g_app.txt_atime[0] = 0;
    memset(&g_app.info, 0, sizeof(g_app.info));
    SendMessageW(g_app.ed_name, WM_SETTEXT, 0, (LPARAM)L"");
    SendMessageW(g_app.ed_time[0], DTM_SETSYSTEMTIME, GDT_NONE, 0);
    SendMessageW(g_app.ed_time[1], DTM_SETSYSTEMTIME, GDT_NONE, 0);
    SendMessageW(g_app.ed_time[2], DTM_SETSYSTEMTIME, GDT_NONE, 0);
    g_app.sync_on = 0;
    KillTimer(g_app.hwnd, TIMER_STATUS);
    KillTimer(g_app.hwnd, TIMER_COPY);
}

/* -------------------------------------------------------------- hash --- */

static DWORD WINAPI hash_thread(void *param)
{
    HashJob *job = (HashJob *)param;
    HANDLE h;
    wchar_t xp[FM_PATH_MAX];
    md5_ctx m;
    sha1_ctx s1;
    sha256_ctx s2;
    unsigned char *buf;
    DWORD got = 0;
    unsigned long long done = 0;
    int last_pct = -1;
    unsigned char d16[16], d20[20], d32[32];
    HashResult *res;

    h = CreateFileW(extended_path(xp, FM_PATH_MAX, job->path), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        res = (HashResult *)calloc(1, sizeof(HashResult));
        res->token = job->token;
        PostMessageW(job->hwnd, WM_HASH_DONE, 0, (LPARAM)res);
        free(job);
        return 0;
    }

    buf = (unsigned char *)malloc(1u << 20);
    if (!buf) {
        CloseHandle(h);
        res = (HashResult *)calloc(1, sizeof(HashResult));
        res->token = job->token;
        PostMessageW(job->hwnd, WM_HASH_DONE, 0, (LPARAM)res);
        free(job);
        return 0;
    }

    md5_init(&m);
    sha1_init(&s1);
    sha256_init(&s2);

    for (;;) {
        if (g_gen != (LONG64)job->token) {
            break; /* superseded: user picked another file */
        }
        if (!ReadFile(h, buf, 1u << 20, &got, NULL) || got == 0) {
            break;
        }
        md5_update(&m, buf, got);
        sha1_update(&s1, buf, got);
        sha256_update(&s2, buf, got);
        done += got;
        if (job->size > 0) {
            int pct = (int)(done * 100ull / job->size);
            if (pct != last_pct) {
                last_pct = pct;
                PostMessageW(job->hwnd, WM_HASH_PROGRESS, (WPARAM)job->token, (LPARAM)pct);
            }
        }
    }

    free(buf);
    CloseHandle(h);

    res = (HashResult *)calloc(1, sizeof(HashResult));
    res->token = job->token;
    if (g_gen == (LONG64)job->token) {
        md5_final(&m, d16);
        sha1_final(&s1, d20);
        sha256_final(&s2, d32);
        bytes_to_hex(d16, 16, res->md5);
        bytes_to_hex(d20, 20, res->sha1);
        bytes_to_hex(d32, 32, res->sha256);
        PostMessageW(job->hwnd, WM_HASH_DONE, 0, (LPARAM)res);
    } else {
        free(res);
    }
    free(job);
    return 0;
}

static void start_hash(const wchar_t *path, unsigned long long size)
{
    HashJob *job = (HashJob *)calloc(1, sizeof(HashJob));
    HANDLE th;

    g_gen++;
    job->token = (unsigned long long)g_gen;
    job->size = size;
    job->hwnd = g_app.hwnd;
    wcsncpy(job->path, path, FM_PATH_MAX - 1);
    g_app.hash_token = job->token;
    g_app.hash_busy = 1;
    g_app.hash_pct = 0;
    set_hash_busy_texts();

    th = CreateThread(NULL, 0, hash_thread, job, 0, NULL);
    if (th) {
        CloseHandle(th);
    } else {
        g_app.hash_busy = 0;
        wcscpy(g_app.txt_md5, L"计算失败");
        wcscpy(g_app.txt_sha1, L"-");
        wcscpy(g_app.txt_sha256, L"-");
    }
}

/* ------------------------------------------------------------ actions -- */

static void normalize_path(wchar_t *p)
{
    for (; *p; p++) {
        if (*p == L'/') {
            *p = L'\\';
        }
    }
}

static void do_load(const wchar_t *raw)
{
    FileInfo fi;
    wchar_t err[512];
    wchar_t path[FM_PATH_MAX];

    wcsncpy(path, raw, FM_PATH_MAX - 1);
    path[FM_PATH_MAX - 1] = 0;
    normalize_path(path);

    if (!file_load_info(path, &fi, err, 512)) {
        wchar_t msg[640];
        swprintf(msg, 640, L"无法读取该文件：%s", err);
        fm_ui_set_status(&g_app, 2, msg);
        g_app.has_file = 0;
        invalidate();
        return;
    }

    g_app.has_file = 1;
    g_app.info_open = 0;
    g_app.info = fi;
    g_app.scroll_y = 0;
    g_app.status_kind = 0;
    g_app.status_text[0] = 0;
    g_app.copy_flash = -1;

    fill_info_texts();
    SendMessageW(g_app.ed_name, WM_SETTEXT, 0, (LPARAM)file_base_name(fi.path));

    start_hash(fi.path, fi.size);
    fm_ui_layout(&g_app);
    fit_window(g_app.hwnd, 0);
    invalidate();
}

static void do_reset(void)
{
    clear_state();
    fm_ui_layout(&g_app);
    fit_window(g_app.hwnd, 0);
    invalidate();
}

static void trim_inplace(wchar_t *s)
{
    size_t n;
    wchar_t *p;

    while (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n') {
        memmove(s, s + 1, (wcslen(s)) * sizeof(wchar_t));
    }
    n = wcslen(s);
    while (n && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' || s[n - 1] == L'\n')) {
        s[--n] = 0;
    }
    (void)p;
}

static void fail_with(const wchar_t *text)
{
    fm_ui_set_status(&g_app, 2, text);
    invalidate();
}

static void do_apply(void)
{
    wchar_t name[512];
    wchar_t err[512];
    wchar_t msg[768];
    wchar_t line[256];
    wchar_t newpath[FM_PATH_MAX];
    FILETIME ct, mt, at;
    FILETIME *pct = NULL, *pmt = NULL, *pat = NULL;
    int did_rename = 0;
    size_t used;
    unsigned i;

    if (!g_app.has_file) {
        return;
    }
    GetWindowTextW(g_app.ed_name, name, 512);
    trim_inplace(name);
    if (!name[0]) {
        fail_with(L"文件名不能为空");
        return;
    }
    if (!file_name_valid(name, err, 512)) {
        fail_with(err);
        return;
    }

    if (dtp_get_ft(g_app.ed_time[0], &ct)) {
        pct = &ct;
    }
    if (dtp_get_ft(g_app.ed_time[1], &mt)) {
        pmt = &mt;
    }
    if (dtp_get_ft(g_app.ed_time[2], &at)) {
        pat = &at;
    }
    if (!pct && !pmt && !pat) {
        fail_with(L"请先选择至少一项时间");
        return;
    }

    msg[0] = 0;
    used = 0;

    if (wcscmp(name, file_base_name(g_app.info.path)) != 0) {
        if (!file_rename(g_app.info.path, name, newpath, FM_PATH_MAX, err, 512)) {
            fail_with(err);
            return;
        }
        wcsncpy(g_app.info.path, newpath, FM_PATH_MAX - 1);
        g_app.info.path[FM_PATH_MAX - 1] = 0;
        did_rename = 1;
    }

    if (!file_apply_times(g_app.info.path, pct, pat, pmt, err, 512)) {
        /* renamed already? report honestly */
        if (did_rename) {
            swprintf(msg, 768, L"文件已重命名，但写入时间戳失败：%s", err);
        } else {
            swprintf(msg, 768, L"写入时间戳失败：%s", err);
        }
        fill_info_texts();
        fm_ui_set_status(&g_app, 2, msg);
        fit_window(g_app.hwnd, 0);
        invalidate();
        return;
    }

    used += (size_t)swprintf(msg + used, 768 - used, L"已应用修改：");
    if (did_rename) {
        used += (size_t)swprintf(msg + used, 768 - used, L"\n· 重命名为：%s", name);
    }
    for (i = 0; i < 3; i++) {
        wchar_t tbuf[128];
        static const wchar_t *names[3] = {L"创建时间", L"修改时间", L"访问时间"};
        dtp_get_text(g_app.ed_time[i], tbuf, 128);
        if (!tbuf[0]) {
            continue;
        }
        swprintf(line, 256, L"\n· %s → %s", names[i], tbuf);
        if (used + wcslen(line) < 767) {
            wcscpy(msg + used, line);
            used += wcslen(line);
        }
    }

    /* refresh displayed metadata */
    {
        FileInfo fi;
        if (file_load_info(g_app.info.path, &fi, err, 512)) {
            FILETIME keep_c = fi.ctime;
            g_app.info = fi;
            g_app.info.ctime = keep_c;
            fill_info_texts();
            SendMessageW(g_app.ed_name, WM_SETTEXT, 0, (LPARAM)file_base_name(fi.path));
        }
    }

    fm_ui_set_status(&g_app, 1, msg);
    fit_window(g_app.hwnd, 0);
    SetTimer(g_app.hwnd, TIMER_STATUS, 5000, NULL);
    invalidate();
}

static void copy_row(int idx)
{
    const wchar_t *v = fm_ui_row_value(&g_app, idx);

    if (!fm_ui_row_copyable(&g_app, idx)) {
        return;
    }
    if (clipboard_copy_text(g_app.hwnd, v)) {
        g_app.copy_flash = idx;
        SetTimer(g_app.hwnd, TIMER_COPY, 1500, NULL);
        invalidate();
    }
}

static void set_now(int idx)
{
    if (g_app.sync_on) {
        int i;
        g_app.suppress_sync = 1;
        for (i = 0; i < 3; i++) {
            dtp_set_now(g_app.ed_time[i]);
        }
        g_app.suppress_sync = 0;
    } else {
        dtp_set_now(g_app.ed_time[idx]);
    }
    invalidate();
}

static void pick_file(void)
{
    OPENFILENAMEW ofn;
    wchar_t path[FM_PATH_MAX];

    path[0] = 0;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.hwnd;
    ofn.lpstrFilter = L"所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = FM_PATH_MAX;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"选择要修改的文件";
    if (GetOpenFileNameW(&ofn)) {
        do_load(path);
    }
}

static void update_hot(int x, int y)
{
    int h = fm_ui_hit(&g_app, x, y);
    if (h != g_app.hot) {
        fm_ui_set_hot(&g_app, h);
        SetTimer(g_app.hwnd, TIMER_ANIM, 16, NULL);
        invalidate();
    }
}

static void set_cursor_for(int hot)
{
    LPWSTR cur = IDC_ARROW;
    switch (hot) {
    case HOT_MIN:
    case HOT_CLOSE:
    case HOT_DROP:
    case HOT_RESET:
    case HOT_APPLY:
    case HOT_SYNC:
    case HOT_SEC_INFO:
    case HOT_FOOT_HASH:
        cur = IDC_HAND;
        break;
    default:
        if (hot >= HOT_COPY && hot < HOT_COPY + FM_ROWS) {
            cur = IDC_HAND;
        } else if (hot >= HOT_NOW && hot < HOT_NOW + 3) {
            cur = IDC_HAND;
        }
        break;
    }
    SetCursor(LoadCursorW(NULL, cur));
}

/* ---------------------------------------------------------- edit ctrl -- */

static void focus_next(HWND cur, int back)
{
    HWND order[4];
    int i, idx = -1;

    order[0] = g_app.ed_name;
    order[1] = g_app.ed_time[0];
    order[2] = g_app.ed_time[1];
    order[3] = g_app.ed_time[2];
    for (i = 0; i < 4; i++) {
        if (order[i] == cur) {
            idx = i;
        }
    }
    if (idx < 0) {
        return;
    }
    idx = (idx + (back ? -1 : 1) + 4) % 4;
    SetFocus(order[idx]);
    SendMessageW(order[idx], EM_SETSEL, 0, -1);
}

static LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR ref)
{
    (void)id;
    (void)ref;
    switch (msg) {
    case WM_MOUSEWHEEL:
        SendMessageW(GetParent(hwnd), WM_MOUSEWHEEL, wp, lp);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            PostMessageW(GetParent(hwnd), WM_APPLY_REQ, 0, 0);
            return 0;
        }
        if (wp == VK_TAB) {
            focus_next(hwnd, GetKeyState(VK_SHIFT) < 0);
            return 0;
        }
        if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            SetFocus(GetParent(hwnd));
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == L'\r' || wp == L'\n') {
            return 0;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static HWND create_dtp(HWND parent, int id)
{
    HWND h = CreateWindowExW(0, DATETIMEPICK_CLASS, L"",
                             WS_CHILD | DTS_SHORTDATEFORMAT,
                             0, 0, 10, 20, parent, (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(h, DTM_SETFORMATW, 0, (LPARAM)L"yyyy-MM-dd HH:mm:ss");
    SendMessageW(h, WM_SETFONT, (WPARAM)g_app.f_body, TRUE);
    return h;
}

static HWND create_edit(HWND parent, int id, const wchar_t *cue)
{
    HWND h = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
                             0, 0, 10, 20, parent, (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_app.f_body, TRUE);
    SendMessageW(h, EM_SETCUEBANNER, TRUE, (LPARAM)cue);
    SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    SetWindowSubclass(h, edit_proc, 1, 0);
    return h;
}

/* ------------------------------------------------------- drop target -- */

typedef struct {
    IDropTargetVtbl *vtbl;
    LONG ref;
} FmDropTarget;

static FmDropTarget g_drop = {NULL, 1};

static BOOL drop_has_files(IDataObject *obj)
{
    FORMATETC fmt;

    if (!obj) {
        return FALSE;
    }
    fmt.cfFormat = CF_HDROP;
    fmt.ptd = NULL;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;
    return obj->lpVtbl->QueryGetData(obj, &fmt) == S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_QueryInterface(IDropTarget *This, const IID *riid, void **ppv)
{
    (void)This;
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = NULL;
    if (riid && (memcmp(riid, &IID_IUnknown, sizeof(IID)) == 0 ||
                 memcmp(riid, &IID_IDropTarget, sizeof(IID)) == 0)) {
        *ppv = (void *)&g_drop;
        g_drop.ref++;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dt_AddRef(IDropTarget *This)
{
    (void)This;
    return (ULONG)InterlockedIncrement(&g_drop.ref);
}

static ULONG STDMETHODCALLTYPE dt_Release(IDropTarget *This)
{
    (void)This;
    return (ULONG)InterlockedDecrement(&g_drop.ref);
}

static HRESULT STDMETHODCALLTYPE dt_DragEnter(IDropTarget *This, IDataObject *obj,
                                              DWORD key, POINTL pt, DWORD *effect)
{
    (void)This;
    (void)key;
    (void)pt;
    if (drop_has_files(obj)) {
        if (effect) {
            *effect = DROPEFFECT_COPY;
        }
        if (!g_app.dragover) {
            g_app.dragover = 1;
            invalidate();
        }
        return S_OK;
    }
    if (effect) {
        *effect = DROPEFFECT_NONE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_DragOver(IDropTarget *This, DWORD key, POINTL pt, DWORD *effect)
{
    (void)This;
    (void)key;
    (void)pt;
    if (effect) {
        *effect = DROPEFFECT_COPY;
    }
    if (!g_app.dragover) {
        g_app.dragover = 1;
        invalidate();
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_DragLeave(IDropTarget *This)
{
    (void)This;
    if (g_app.dragover) {
        g_app.dragover = 0;
        invalidate();
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_Drop(IDropTarget *This, IDataObject *obj,
                                         DWORD key, POINTL pt, DWORD *effect)
{
    FORMATETC fmt;
    STGMEDIUM stg;

    (void)This;
    (void)key;
    (void)pt;
    if (effect) {
        *effect = DROPEFFECT_COPY;
    }
    if (!obj) {
        return S_OK;
    }
    fmt.cfFormat = CF_HDROP;
    fmt.ptd = NULL;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;

    if (obj->lpVtbl->GetData(obj, &fmt, &stg) == S_OK) {
        wchar_t path[FM_PATH_MAX];
        if (DragQueryFileW((HDROP)stg.hGlobal, 0, path, FM_PATH_MAX)) {
            g_app.dragover = 0;
            do_load(path);
        }
        ReleaseStgMedium(&stg);
    }
    if (g_app.dragover) {
        g_app.dragover = 0;
        invalidate();
    }
    SetForegroundWindow(g_app.hwnd);
    return S_OK;
}

static IDropTargetVtbl g_drop_vtbl = {
    dt_QueryInterface,
    dt_AddRef,
    dt_Release,
    dt_DragEnter,
    dt_DragOver,
    dt_DragLeave,
    dt_Drop
};


/* ------------------------------------------------------------- window -- */

static void apply_dwm(HWND hwnd)
{
    typedef HRESULT(WINAPI * PFN_SET)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dll = LoadLibraryW(L"dwmapi.dll");
    if (!dll) {
        return;
    }
    {
        PFN_SET fn = (PFN_SET)(void *)GetProcAddress(dll, "DwmSetWindowAttribute");
        if (fn) {
            DWORD pref = 2; /* DWMWCP_ROUND */
            fn(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof(pref));
        }
    }
    FreeLibrary(dll);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        RECT rc;
        g_app.hwnd = hwnd;
        fm_ui_set_dpi(&g_app, (int)GetDpiForWindow(hwnd));
        g_app.ed_name = create_edit(hwnd, ID_EDIT_NAME, L"输入新文件名（含扩展名）");
        g_app.ed_time[0] = create_dtp(hwnd, ID_EDIT_TIME0);
        g_app.ed_time[1] = create_dtp(hwnd, ID_EDIT_TIME1);
        g_app.ed_time[2] = create_dtp(hwnd, ID_EDIT_TIME2);
        SetWindowSubclass(g_app.ed_time[0], edit_proc, 2, 0);
        SetWindowSubclass(g_app.ed_time[1], edit_proc, 2, 0);
        SetWindowSubclass(g_app.ed_time[2], edit_proc, 2, 0);
        GetClientRect(hwnd, &rc);
        fm_ui_set_size(&g_app, rc.right, rc.bottom);
        fm_ui_layout(&g_app);
        DragAcceptFiles(hwnd, TRUE);
        apply_dwm(hwnd);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        fm_ui_paint(&g_app);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) {
            return 0;
        }
        fm_ui_set_size(&g_app, LOWORD(lp), HIWORD(lp));
        fm_ui_layout(&g_app);
        invalidate();
        return 0;
    }
    case WM_DPICHANGED: {
        RECT *pr = (RECT *)lp;
        fm_ui_set_dpi(&g_app, HIWORD(wp));
        SetWindowPos(hwnd, NULL, pr->left, pr->top, pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        {
            int i;
            SendMessageW(g_app.ed_name, WM_SETFONT, (WPARAM)g_app.f_body, TRUE);
            for (i = 0; i < 3; i++) {
                SendMessageW(g_app.ed_time[i], WM_SETFONT, (WPARAM)g_app.f_body, TRUE);
            }
        }
        fm_ui_layout(&g_app);
        invalidate();
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wp) {
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        invalidate();
        return TRUE;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = scale(420);
        mm->ptMinTrackSize.y = scale(180);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        int b, x, y, hz = 0, vt = 0;
        LRESULT hit;

        ScreenToClient(hwnd, &pt);
        GetClientRect(hwnd, &rc);
        x = pt.x;
        y = pt.y;
        b = scale(6);

        if (x < b) hz = 1;
        if (x >= rc.right - b) hz = 2;
        if (y < b) vt = 1;
        if (y >= rc.bottom - b) vt = 2;
        if (hz || vt) {
            if (vt == 1) return hz == 1 ? HTTOPLEFT : (hz == 2 ? HTTOPRIGHT : HTTOP);
            if (vt == 2) return hz == 1 ? HTBOTTOMLEFT : (hz == 2 ? HTBOTTOMRIGHT : HTBOTTOM);
            return hz == 1 ? HTLEFT : HTRIGHT;
        }

        hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit != HTCLIENT) {
            return hit;
        }
        if (y < g_app.L.hdr.bottom) {
            if (x < g_app.L.btn_min.left) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        return HTCLIENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            set_cursor_for(fm_ui_hit(&g_app, p.x, p.y));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);

        if (g_drag_active) {
            int track_h = g_app.L.view.bottom - g_app.L.view.top;
            int pos = y - g_drag_grab - g_app.L.view.top;
            int span = track_h - g_drag_thumb_h;
            if (span < 1) span = 1;
            if (pos < 0) pos = 0;
            if (pos > span) pos = span;
            fm_ui_scroll_to(&g_app, (int)((double)pos * g_app.max_scroll / span));
            if (g_app.hot != HOT_SCROLL) {
                fm_ui_set_hot(&g_app, HOT_SCROLL);
            }
            invalidate();
            return 0;
        }
        update_hot(x, y);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (!g_drag_active) {
            fm_ui_set_hot(&g_app, HOT_NONE);
            SetTimer(hwnd, TIMER_ANIM, 16, NULL);
            invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int h;
        h = fm_ui_hit(&g_app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        SetFocus(hwnd);
        if (h == HOT_SCROLL) {
            g_drag_active = 1;
            g_drag_grab = GET_Y_LPARAM(lp) - g_app.L.thumb.top;
            g_drag_thumb_h = g_app.L.thumb.bottom - g_app.L.thumb.top;
            SetCapture(hwnd);
            return 0;
        }
        g_app.pressed = h;
        { FILE *f = fopen("C:////Users////zxl////AppData////Local////Temp////fm5.log", "a");
          if (f) { fprintf(f, "DOWN h=%d at %d,%d box1=%ld,%ld,%ld,%ld pick1=%ld,%ld,%ld,%ld scroll=%d\n", h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
             (long)g_app.L.box[1].left,(long)g_app.L.box[1].top,(long)g_app.L.box[1].right,(long)g_app.L.box[1].bottom,
             (long)g_app.L.pick[1].left,(long)g_app.L.pick[1].top,(long)g_app.L.pick[1].right,(long)g_app.L.pick[1].bottom, g_app.scroll_y); fclose(f); }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int h = fm_ui_hit(&g_app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (g_drag_active) {
            g_drag_active = 0;
            ReleaseCapture();
            return 0;
        }
        if (h != g_app.pressed) {
            g_app.pressed = HOT_NONE;
            return 0;
        }
        g_app.pressed = HOT_NONE;
        switch (h) {
        case HOT_CLOSE:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        case HOT_MIN:
            ShowWindow(hwnd, SW_MINIMIZE);
            break;
        case HOT_DROP:
            pick_file();
            break;
        case HOT_RESET:
            do_reset();
            break;
        case HOT_APPLY:
            do_apply();
            break;
        case HOT_FOOT_HASH:
            if (g_self_hash[0] && clipboard_copy_text(g_app.hwnd, g_self_hash)) {
                g_app.foot_copied = 1;
                SetTimer(g_app.hwnd, TIMER_COPY, 1500, NULL);
                invalidate();
            }
            break;
        case HOT_SEC_INFO:
            g_app.info_open = !g_app.info_open;
            fm_ui_layout(&g_app);
            fit_window(g_app.hwnd, 0);
            invalidate();
            break;
        case HOT_SYNC:
            g_app.sync_on = !g_app.sync_on;
            if (g_app.sync_on) {
                SYSTEMTIME st;
                memset(&st, 0, sizeof(st));
                if (SendMessageW(g_app.ed_time[0], DTM_GETSYSTEMTIME, 0, (LPARAM)&st) == GDT_VALID) {
                    g_app.suppress_sync = 1;
                    dtp_set_st(g_app.ed_time[1], &st);
                    dtp_set_st(g_app.ed_time[2], &st);
                    g_app.suppress_sync = 0;
                }
            }
            invalidate();
            break;
        default:
            if (h >= HOT_COPY && h < HOT_COPY + FM_ROWS) {
                copy_row(h - HOT_COPY);
            } else if (h >= HOT_NOW && h < HOT_NOW + 3) {
                set_now(h - HOT_NOW);
            }
            break;
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (g_app.max_scroll > 0) {
            fm_ui_scroll_by(&g_app, -delta * scale(60) / 120);
            invalidate();
        }
        return 0;
    }
    case WM_COMMAND: {
        int code = HIWORD(wp);
        if (code == EN_CHANGE) {
            return 0;
        }
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
            invalidate();
            return 0;
        }
        break;
    }
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh && nh->code == DTN_DATETIMECHANGE && g_app.sync_on && !g_app.suppress_sync) {
            int i, src = -1;
            for (i = 0; i < 3; i++) {
                if (g_app.ed_time[i] == nh->hwndFrom) {
                    src = i;
                }
            }
            if (src >= 0) {
                SYSTEMTIME st;
                memset(&st, 0, sizeof(st));
                if (SendMessageW(nh->hwndFrom, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) == GDT_VALID) {
                    g_app.suppress_sync = 1;
                    for (i = 0; i < 3; i++) {
                        if (i != src) {
                            dtp_set_st(g_app.ed_time[i], &st);
                        }
                    }
                    g_app.suppress_sync = 0;
                }
            }
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(0x33, 0x33, 0x33));
        SetBkColor(dc, RGB(0xff, 0xff, 0xff));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t path[FM_PATH_MAX];
        if (DragQueryFileW(drop, 0, path, FM_PATH_MAX)) {
            do_load(path);
        }
        DragFinish(drop);
        SetForegroundWindow(hwnd);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            do_apply();
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == TIMER_ANIM) {
            fm_ui_anim_tick(&g_app, 0.016f);
            if (!fm_ui_anim_active(&g_app)) {
                KillTimer(hwnd, TIMER_ANIM);
            }
            invalidate();
            return 0;
        }
        if (wp == TIMER_STATUS) {
            KillTimer(hwnd, TIMER_STATUS);
            g_app.status_kind = 0;
            g_app.status_text[0] = 0;
            fm_ui_layout(&g_app);
            invalidate();
            return 0;
        }
        if (wp == TIMER_COPY) {
            KillTimer(hwnd, TIMER_COPY);
            g_app.copy_flash = -1;
            g_app.foot_copied = 0;
            invalidate();
            return 0;
        }
        break;
    case WM_HASH_PROGRESS: {
        unsigned long long token = (unsigned long long)wp;
        if (token == g_app.hash_token && g_app.has_file) {
            int pct = (int)lp;
            if (pct != g_app.hash_pct) {
                g_app.hash_pct = pct;
                swprintf(g_app.txt_md5, 64, L"计算中… %d%%", pct);
                swprintf(g_app.txt_sha1, 72, L"计算中… %d%%", pct);
                swprintf(g_app.txt_sha256, 96, L"计算中… %d%%", pct);
                invalidate();
            }
        }
        return 0;
    }
    case WM_HASH_DONE: {
        HashResult *res = (HashResult *)lp;
        if (res) {
            if (res->token == g_app.hash_token && g_app.has_file && res->md5[0]) {
                wcsncpy(g_app.txt_md5, res->md5, 63);
                g_app.txt_md5[63] = 0;
                wcsncpy(g_app.txt_sha1, res->sha1, 71);
                g_app.txt_sha1[71] = 0;
                wcsncpy(g_app.txt_sha256, res->sha256, 95);
                g_app.txt_sha256[95] = 0;
                g_app.hash_busy = 0;
                invalidate();
            } else if (res->token == g_app.hash_token && g_app.has_file) {
                wcscpy(g_app.txt_md5, L"计算失败");
                wcscpy(g_app.txt_sha1, L"-");
                wcscpy(g_app.txt_sha256, L"-");
                g_app.hash_busy = 0;
                invalidate();
            }
            free(res);
        }
        return 0;
    }
    case WM_APPLY_REQ:
        do_apply();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        RevokeDragDrop(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void get_work_area(HWND hwnd, RECT *wa)
{
    MONITORINFO mi;

    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, wa, 0)) {
        wa->left = 0;
        wa->top = 0;
        wa->right = GetSystemMetrics(SM_CXSCREEN);
        wa->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
        *wa = mi.rcWork;
    }
    if (wa->right - wa->left < 320 || wa->bottom - wa->top < 320) {
        wa->left = 0;
        wa->top = 0;
        wa->right = GetSystemMetrics(SM_CXSCREEN);
        wa->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
}

/* Shrink-wrap the window around the current content; keep it on screen. */
static void fit_window(HWND hwnd, int recenter)
{
    RECT wa, wr, cr;
    int need, w, h;

    get_work_area(hwnd, &wa);
    w = scale(620);
    if (w > wa.right - wa.left) {
        w = wa.right - wa.left;
    }

    /* make the client width final first, so text wrapping (and therefore the
       measured content height) matches what will be painted */
    GetWindowRect(hwnd, &wr);
    SetWindowPos(hwnd, NULL, wr.left, wr.top, w, wr.bottom - wr.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    GetClientRect(hwnd, &cr);
    fm_ui_set_size(&g_app, cr.right, cr.bottom);
    fm_ui_layout(&g_app);
    GetWindowRect(hwnd, &wr);

    need = g_app.L.foot.bottom + g_app.scroll_y + spx(24);
    if (g_app.scroll_y != 0) {
        need = g_app.L.content_h + g_app.L.view.top;
    }
    if (need < scale(280)) {
        need = scale(280);
    }
    h = need;
    if (h > wa.bottom - wa.top - scale(40)) {
        h = wa.bottom - wa.top - scale(40);
    }

    if (recenter) {
        SetWindowPos(hwnd, NULL, wa.left + (wa.right - wa.left - w) / 2,
                     wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        int top = wr.top;
        if (top + h > wa.bottom) {
            top = wa.bottom - h;
        }
        if (top < wa.top) {
            top = wa.top;
        }
        SetWindowPos(hwnd, NULL, wr.left, top, wr.right - wr.left, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    fm_ui_layout(&g_app);
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE prev, PWSTR cmdline, int show)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;
    int argc = 0;
    LPWSTR *argv;

    (void)prev;
    (void)show;
    (void)cmdline;
    g_inst = hInst;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_DATE_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    OleInitialize(NULL);

    fm_ui_init(&g_app);
    g_app.hwnd = NULL;
    g_app.pressed = HOT_NONE;
    compute_self_hash();
    build_footer_text();

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"FileMenderWnd";
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, L"FileMenderWnd",
                           L"文件属性修改器",
                           WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                           0, 0, 620, 720, NULL, NULL, hInst, NULL);
    if (!hwnd) {
        return 1;
    }
    SetWindowTextW(hwnd, L"文件属性修改器");
    /* force the client area to match the window rect (borderless frame) */
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    g_drop.vtbl = &g_drop_vtbl;
    RegisterDragDrop(hwnd, (IDropTarget *)&g_drop);
    ShowWindow(hwnd, SW_SHOW);
    fit_window(hwnd, 1);
    UpdateWindow(hwnd);

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        do_load(argv[1]);
    }
    if (argv) {
        LocalFree(argv);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    fm_ui_free(&g_app);
    OleUninitialize();
    return 0;
}
