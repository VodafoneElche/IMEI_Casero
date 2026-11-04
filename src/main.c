/* Comprobador IMEI — ventana principal (Win32 nativo). */
#include "common.h"
#include "ui.h"
#include "imei.h"
#include "tacdb.h"
#include "store.h"
#include "wiki.h"
#include "makers.h"
#include "update.h"
#include "settings.h"
#include "logo.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#define WM_APP_SPECS (WM_APP + 1)
#define WM_APP_UPDATE_FOUND (WM_APP + 2)
#define WM_APP_UPDATE_DONE (WM_APP + 3)
#define WM_APP_PASTED (WM_APP + 4)

enum { IDC_EDIT = 1001, IDC_CHECK, IDC_SETTINGS, IDC_UPDATE, IDC_SEARCH, IDC_LIST, IDC_EXPORT, IDC_CLEAR };
enum { HIT_NONE = 0, HIT_IUNLOCKER, HIT_OFFICIAL, HIT_COPY, HIT_ICLOUD0, HIT_ICLOUD1, HIT_ICLOUD2, HIT_SOURCE };
enum { SPEC_NONE = 0, SPEC_LOADING, SPEC_OK, SPEC_NOTFOUND, SPEC_NETERR };

#define BAND_H 56
#define SIDE_W 320
#define PAD_X 40
#define CONTENT_MAX 880

static const wchar_t *MAIN_CLASS = L"ComprobadorIMEI.Main";
static const wchar_t *PANEL_CLASS = L"ComprobadorIMEI.Panel";
static const wchar_t *TOAST_CLASS = L"ComprobadorIMEI.Toast";

static HWND g_main, g_edit, g_btnCheck, g_btnSettings, g_btnUpdate, g_panel, g_search, g_list, g_btnExport, g_btnClear, g_toast;
static HBRUSH g_brSuperficie, g_brPapel, g_brLateral;
static WNDPROC g_editOrig;
static int g_editFocus, g_searchFocus;

/* Comprobación en pantalla */
static struct {
    int has;
    ImeiInfo a;
    char imei[20];
    int found;
    Device dev;
    char histId[24];
    wchar_t notice[1024];
    int noticeError;
    int specState;
    Specs specs;
    unsigned reqId;
} cur;

static int g_scroll, g_contentH;
static struct { RECT r; int id; int enabled; } g_hits[16];
static int g_nhits, g_hover;
static int g_filter[5000], g_nfilter;
static UpdateInfo g_update;
static int g_updating;
static wchar_t g_toastText[400];

/* Zonas de la ventana principal (se calculan en layout) */
static RECT rcFrame, rcParts, rcHint, rcLabel, rcSearchFrame, rcSideHead, rcListArea;

/* ================================================================ utilidades */

static void toast(const wchar_t *msg) {
    wcsncpy(g_toastText, msg, COUNTOF(g_toastText) - 1);
    RECT rc;
    GetClientRect(g_main, &rc);
    HDC dc = GetDC(g_main);
    int mw = rc.right - S(SIDE_W);
    int w = text_width(dc, msg, F_BODY) + S(36);
    if (w > mw - S(40)) w = mw - S(40);
    int h = text_wrap(dc, msg, 0, 0, w - S(32), F_BODY, 0, 0) + S(22);
    ReleaseDC(g_main, dc);
    SetWindowPos(g_toast, HWND_TOP, (mw - w) / 2, rc.bottom - h - S(24), w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(g_toast, NULL, TRUE);
    SetTimer(g_toast, 1, 3800, NULL);
}

static void copy_text(const char *u8) {
    wchar_t *w = u2w(u8);
    size_t n = (wcslen(w) + 1) * sizeof(wchar_t);
    if (OpenClipboard(g_main)) {
        EmptyClipboard();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
        if (g) {
            memcpy(GlobalLock(g), w, n);
            GlobalUnlock(g);
            SetClipboardData(CF_UNICODETEXT, g);
        }
        CloseClipboard();
    }
    free(w);
}

static void open_url(const char *u8) {
    if (!starts_with_ci(u8, "https://") && !starts_with_ci(u8, "http://")) return;
    wchar_t *w = u2w(u8);
    ShellExecuteW(g_main, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
}

static void fmt_thousands(unsigned v, wchar_t *out, size_t cap) {
    wchar_t tmp[32];
    swprintf(tmp, COUNTOF(tmp), L"%u", v);
    size_t n = wcslen(tmp), o = 0;
    for (size_t i = 0; i < n && o + 2 < cap; i++) {
        out[o++] = tmp[i];
        if ((n - i - 1) % 3 == 0 && i + 1 < n) out[o++] = L'.';
    }
    out[o] = 0;
}

static HistItem *cur_hist(void) {
    int i = hist_find(cur.histId);
    return i >= 0 ? &g_hist.items[i] : NULL;
}

/* ================================================================ historial */

static void history_refresh(void) {
    wchar_t q[128];
    GetWindowTextW(g_search, q, COUNTOF(q));
    char *qu = w2u(q);
    char qd[32] = {0};
    int nd = 0;
    for (char *p = qu; *p && nd < 31; p++)
        if (*p >= '0' && *p <= '9') qd[nd++] = *p;
    int qlen = (int)strlen(qu);
    g_nfilter = 0;
    for (int i = 0; i < g_hist.n && g_nfilter < (int)COUNTOF(g_filter); i++) {
        HistItem *h = &g_hist.items[i];
        int match = !qlen;
        if (!match && nd && strstr(h->imei, qd)) match = 1;
        if (!match) {
            char both[256];
            snprintf(both, sizeof(both), "%s %s", h->marca, h->modelo);
            match = str_contains_ci(both, qu);
        }
        if (match) g_filter[g_nfilter++] = i;
    }
    free(qu);
    SendMessageW(g_list, LB_SETCOUNT, (WPARAM)g_nfilter, 0);
    ShowWindow(g_list, g_nfilter ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_list, NULL, TRUE);
    InvalidateRect(g_main, &rcSideHead, FALSE);
    InvalidateRect(g_main, &rcListArea, FALSE);
}

static void draw_history_item(const DRAWITEMSTRUCT *di) {
    if ((int)di->itemID < 0 || (int)di->itemID >= g_nfilter) return;
    HistItem *h = &g_hist.items[g_filter[di->itemID]];
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    int current = cur.has && !strcmp(h->id, cur.histId);
    fill_rect(dc, &r, current ? C_PAPEL : C_LATERAL);
    hline(dc, r.left, r.right, r.top, C_LINEA);
    int x0 = r.left + S(18), x1 = r.right - S(18);
    wchar_t *model = u2w(h->modelo[0] ? h->modelo : "\xE2\x80\x94");
    wchar_t time[32];
    FILETIME ft, now;
    SYSTEMTIME st, loc, today;
    time[0] = 0;
    if (iso_to_filetime(h->fecha, &ft) && FileTimeToSystemTime(&ft, &st) && SystemTimeToTzSpecificLocalTime(NULL, &st, &loc)) {
        GetSystemTimeAsFileTime(&now);
        GetLocalTime(&today);
        if (loc.wYear == today.wYear && loc.wMonth == today.wMonth && loc.wDay == today.wDay)
            swprintf(time, COUNTOF(time), L"%02u:%02u", loc.wHour, loc.wMinute);
        else
            swprintf(time, COUNTOF(time), L"%02u/%02u/%02u", loc.wDay, loc.wMonth, loc.wYear % 100);
    }
    HDC mdc = dc;
    int tw = text_width(mdc, time, F_SMALL) + S(8);
    RECT a = {x0, r.top + S(8), x1 - tw, r.top + S(30)};
    text_line(dc, model, &a, F_BODY_B, C_TINTA, DT_LEFT);
    RECT t = {x1 - tw, r.top + S(8), x1, r.top + S(30)};
    text_line(dc, time, &t, F_SMALL, C_GRAFITO, DT_RIGHT);
    wchar_t *imei = u2w(h->imei);
    RECT b = {x0, r.top + S(30), x1 - S(90), r.bottom - S(6)};
    text_line(dc, imei, &b, F_SMALL, C_GRAFITO, DT_LEFT);
    if (is_apple(h->marca)) {
        const wchar_t *st2 = !strcmp(h->icloud, "OFF") ? L"iCloud OFF" : !strcmp(h->icloud, "ON") ? L"iCloud ON" : L"iCloud ?";
        COLORREF c = !strcmp(h->icloud, "OFF") ? C_OK : !strcmp(h->icloud, "ON") ? C_ERROR : C_GRAFITO;
        RECT s = {x1 - S(90), r.top + S(30), x1, r.bottom - S(6)};
        text_line(dc, st2, &s, F_SMALL_B, c, DT_RIGHT);
    }
    free(model);
    free(imei);
}

/* ================================================================ ficha técnica (hilo) */

typedef struct {
    unsigned id;
    char brand[64], name[160], key[300];
} SpecJob;

typedef struct {
    int result;
    Specs specs;
} SpecResult;

static DWORD WINAPI spec_thread(LPVOID p) {
    SpecJob *job = p;
    SpecResult *res = calloc(1, sizeof(*res));
    res->result = wiki_specs(job->brand, job->name, &res->specs);
    if (res->result == WIKI_FOUND) cache_put(job->key, &res->specs);
    if (!PostMessageW(g_main, WM_APP_SPECS, job->id, (LPARAM)res)) {
        specs_free(&res->specs);
        free(res);
    }
    free(job);
    return 0;
}

static void spec_key(char *out, size_t cap) {
    snprintf(out, cap, "wiki|%s|%s", cur.dev.brand, cur.dev.name);
}

static void panel_relayout(void);

static void load_specs(void) {
    specs_free(&cur.specs);
    cur.reqId++;
    if (!cur.found) {
        cur.specState = SPEC_NONE;
        return;
    }
    char key[300];
    spec_key(key, sizeof(key));
    if (cache_get(key, &cur.specs)) {
        cur.specState = SPEC_OK;
        return;
    }
    cur.specState = SPEC_LOADING;
    SpecJob *job = calloc(1, sizeof(*job));
    job->id = cur.reqId;
    safe_copy(job->brand, sizeof(job->brand), cur.dev.brand);
    safe_copy(job->name, sizeof(job->name), cur.dev.name);
    safe_copy(job->key, sizeof(job->key), key);
    HANDLE t = CreateThread(NULL, 0, spec_thread, job, 0, NULL);
    if (t) CloseHandle(t);
    else {
        free(job);
        cur.specState = SPEC_NETERR;
    }
}

/* ================================================================ comprobación */

static void build_notice(void) {
    wchar_t *n = cur.notice;
    size_t cap = COUNTOF(cur.notice), len = 0;
    n[0] = 0;
    cur.noticeError = 0;
#define ADD(...)                                                     \
    do {                                                             \
        if (len) len += swprintf(n + len, cap - len, L" ");          \
        len += swprintf(n + len, cap - len, __VA_ARGS__);            \
    } while (0)
    if (cur.a.complete && !cur.a.valid) {
        ADD(L"El último dígito no cuadra: con estos 14 primeros dígitos el IMEI debería terminar en %c. Revisa que esté bien escrito antes de tramitar nada.", cur.a.expected);
        cur.noticeError = 1;
    }
    if (!cur.a.complete) ADD(L"El IMEI está incompleto: se ha identificado el modelo con los 8 primeros dígitos, pero no se puede validar.");
    if (cur.a.note == NOTE_COMPUTED14) ADD(L"Has escrito 14 dígitos; se ha calculado el dígito de control.");
    if (cur.a.note == NOTE_IMEISV) ADD(L"Es un IMEISV (16 dígitos); se ha convertido a IMEI.");
    if (!cur.found) ADD(L"Este TAC no está en la base de datos. Puede ser un modelo muy reciente o poco común. Si la empresa tiene la base oficial de la GSMA, impórtala desde Ajustes.");
#undef ADD
}

static void do_check(const wchar_t *raw, int fromHist) {
    ImeiInfo a;
    imei_analyze(raw, &a);
    if (a.ndig < 8) {
        toast(L"Escribe al menos los 8 primeros dígitos (el TAC) para identificar el modelo.");
        SetFocus(g_edit);
        return;
    }
    if (a.tooLong) {
        toast(L"Un IMEI tiene 15 dígitos. Revisa lo que has pegado.");
        return;
    }
    const char *imei = a.complete ? a.imei : a.digits;
    Device d;
    int found = tacdb_lookup(a.tac, &d);

    char id[24] = {0};
    if (fromHist >= 0) {
        safe_copy(id, sizeof(id), g_hist.items[fromHist].id);
    } else if (g_hist.n && !strcmp(g_hist.items[0].imei, imei)) {
        /* la misma comprobación repetida en menos de 2 minutos reutiliza la entrada */
        FILETIME ft, now;
        GetSystemTimeAsFileTime(&now);
        if (iso_to_filetime(g_hist.items[0].fecha, &ft)) {
            ULARGE_INTEGER x = {{ft.dwLowDateTime, ft.dwHighDateTime}}, y = {{now.dwLowDateTime, now.dwHighDateTime}};
            if (y.QuadPart - x.QuadPart < 120ULL * 10000000ULL) safe_copy(id, sizeof(id), g_hist.items[0].id);
        }
    }
    if (!id[0]) {
        HistItem it = {0};
        snprintf(it.id, sizeof(it.id), "%llu-%04x", (unsigned long long)GetTickCount64(), (unsigned)(rand() & 0xFFFF));
        now_iso(it.fecha);
        safe_copy(it.imei, sizeof(it.imei), imei);
        it.valido = a.complete && a.valid;
        safe_copy(it.marca, sizeof(it.marca), found ? d.brand : "");
        safe_copy(it.modelo, sizeof(it.modelo), found ? d.name : "Modelo no encontrado");
        safe_copy(it.referencia, sizeof(it.referencia), found ? d.ref : "");
        safe_copy(it.anio, sizeof(it.anio), found ? d.year : "");
        safe_copy(it.icloud, sizeof(it.icloud), "sin comprobar");
        hist_add(&it);
        safe_copy(id, sizeof(id), it.id);
    }

    cur.has = 1;
    cur.a = a;
    safe_copy(cur.imei, sizeof(cur.imei), imei);
    cur.found = found;
    cur.dev = d;
    safe_copy(cur.histId, sizeof(cur.histId), id);
    build_notice();

    wchar_t *w = u2w(imei);
    SetWindowTextW(g_edit, w);
    SendMessageW(g_edit, EM_SETSEL, wcslen(w), wcslen(w));
    free(w);

    load_specs();
    g_scroll = 0;
    panel_relayout();
    history_refresh();
}

static void set_icloud(const char *value) {
    HistItem *h = cur_hist();
    if (!h) return;
    safe_copy(h->icloud, sizeof(h->icloud), value);
    if (!strcmp(value, "sin comprobar")) h->icloudFecha[0] = 0;
    else now_iso(h->icloudFecha);
    hist_save();
    InvalidateRect(g_panel, NULL, FALSE);
    InvalidateRect(g_list, NULL, FALSE);
}

static void action(int hit) {
    if (!cur.has) return;
    switch (hit) {
    case HIT_IUNLOCKER:
        copy_text(cur.imei);
        open_url("https://iunlocker.com/es/check_icloud.php");
        toast(L"IMEI copiado. En iUnlocker pégalo con Ctrl+V y pulsa «verificar»; después marca aquí el resultado.");
        break;
    case HIT_OFFICIAL: {
        if (!cur.found) break;
        if (cur.specState == SPEC_OK && cur.specs.web && is_official_url(cur.specs.web, cur.dev.brand)) {
            open_url(cur.specs.web);
        } else {
            char *u = official_search_url(cur.dev.name, cur.dev.brand);
            open_url(u);
            free(u);
        }
        break;
    }
    case HIT_COPY:
        copy_text(cur.imei);
        toast(L"IMEI copiado.");
        break;
    case HIT_ICLOUD0: set_icloud("sin comprobar"); break;
    case HIT_ICLOUD1: set_icloud("OFF"); break;
    case HIT_ICLOUD2: set_icloud("ON"); break;
    case HIT_SOURCE:
        if (cur.specs.url) open_url(cur.specs.url);
        break;
    }
}

/* ================================================================ panel de resultado */

static void add_hit(int x, int y, int w, int h, int id, int enabled) {
    if (g_nhits >= (int)COUNTOF(g_hits)) return;
    g_hits[g_nhits].r = (RECT){x, y, x + w, y + h};
    g_hits[g_nhits].id = id;
    g_hits[g_nhits].enabled = enabled;
    g_nhits++;
}

/* Botón dibujado dentro del panel. Devuelve su ancho. */
static int panel_button(HDC dc, const wchar_t *label, int x, int y, int kind, int id, int enabled, int paint) {
    int w = text_width(dc, label, F_BTN) + S(32), h = S(40);
    if (paint) {
        RECT r = {x, y, x + w, y + h};
        int hover = g_hover == id && enabled;
        COLORREF fill = C_SUPERFICIE, border = hover ? C_GRAFITO : C_LINEA, text = C_TINTA;
        if (kind == BTN_PRIMARY) fill = border = hover ? C_ROJO_HOVER : C_ROJO, text = C_BLANCO;
        if (kind == BTN_GHOST) fill = C_PAPEL, border = hover ? C_LINEA : C_PAPEL, text = hover ? C_TINTA : C_GRAFITO;
        if (!enabled) text = C_PLACEHOLDER;
        round_rect(dc, &r, S(8), fill, border, S(1));
        text_line(dc, label, &r, F_BTN, text, DT_CENTER);
    }
    add_hit(x, y, w, h, id, enabled);
    return w;
}

static int panel_draw(HDC dc, int width, int paint) {
    g_nhits = 0;
    int cw = width - 2 * S(PAD_X);
    if (cw > S(CONTENT_MAX)) cw = S(CONTENT_MAX);
    int x = S(PAD_X), y = S(8) - g_scroll;

    if (!cur.has) {
        y += S(16);
        y += text_wrap(dc, L"Escribe un IMEI y pulsa Comprobar. Verás la marca y el modelo, la fecha de lanzamiento y su ficha técnica. Si es un dispositivo Apple, podrás abrir la comprobación de iCloud con el IMEI ya copiado.",
                       x, y, cw > S(560) ? S(560) : cw, F_BODY, C_GRAFITO, paint);
        y += S(10);
        y += text_wrap(dc, L"El IMEI no guarda la fecha de fabricación: la app muestra la fecha de lanzamiento del modelo.", x, y, cw > S(560) ? S(560) : cw,
                       F_SMALL, C_GRAFITO, paint);
        return y + g_scroll + S(24);
    }

    /* Nombre del dispositivo */
    wchar_t *name = u2w(cur.found ? cur.dev.name : "Modelo no encontrado");
    int nh = text_wrap(dc, name, x, y, cw, F_DEVICE, C_TINTA, paint);
    if (cur.found) {
        const char *mk = maker_name(cur.dev.brand);
        if (!starts_with_ci(cur.dev.name, mk)) {
            wchar_t *m = u2w(mk);
            int nw = text_width(dc, name, F_DEVICE);
            if (nw + S(14) + text_width(dc, m, F_BODY) < cw) {
                RECT r = {x + nw + S(14), y, x + cw, y + nh - S(4)};
                if (paint) text_line(dc, m, &r, F_BODY, C_GRAFITO, DT_LEFT | DT_BOTTOM);
            }
            free(m);
        }
    }
    free(name);
    y += nh + S(16);

    /* Datos principales */
    if (paint) hline(dc, x, x + cw, y, C_LINEA);
    y += S(12);
    wchar_t lab[4][32], val[4][160], sub[4][200];
    int nf = 0;
    wchar_t *w;
    wcscpy(lab[nf], L"IMEI");
    w = u2w(cur.imei);
    wcsncpy(val[nf], w, 159);
    free(w);
    wcscpy(sub[nf], cur.a.complete ? (cur.a.valid ? L"Dígito de control correcto" : L"Dígito de control incorrecto") : L"Incompleto");
    nf++;
    wcscpy(lab[nf], L"TAC");
    w = u2w(cur.a.tac);
    wcsncpy(val[nf], w, 159);
    free(w);
    wcscpy(sub[nf], cur.found ? (cur.dev.source == TAC_SRC_IMPORTED ? L"Base importada" : L"Base incluida") : L"No está en la base");
    nf++;
    if (cur.found && cur.dev.ref[0]) {
        char ref[200];
        safe_copy(ref, sizeof(ref), cur.dev.ref);
        char *bar = strstr(ref, " | ");
        Buf rest;
        buf_init(&rest);
        buf_add(&rest, "", 0);
        if (bar) {
            *bar = 0;
            for (char *p = bar + 3; *p; p++) {
                if (!strncmp(p, " | ", 3)) {
                    buf_adds(&rest, ", ");
                    p += 2;
                } else buf_addc(&rest, *p);
            }
        }
        wcscpy(lab[nf], L"Referencia");
        w = u2w(ref);
        wcsncpy(val[nf], w, 159);
        val[nf][159] = 0;
        free(w);
        w = u2w(rest.p);
        wcsncpy(sub[nf], w, 199);
        sub[nf][199] = 0;
        free(w);
        buf_free(&rest);
        nf++;
    }
    wcscpy(lab[nf], L"Lanzamiento");
    if (cur.specState == SPEC_OK && cur.specs.fecha) {
        w = u2w(cur.specs.fecha);
        wcsncpy(val[nf], w, 159);
        free(w);
        wcscpy(sub[nf], L"Según Wikipedia");
    } else if (cur.found && cur.dev.year[0]) {
        w = u2w(cur.dev.year);
        wcsncpy(val[nf], w, 159);
        free(w);
        wcscpy(sub[nf], L"Año según la base TAC");
    } else {
        wcscpy(val[nf], L"\x2014");
        sub[nf][0] = 0;
    }
    nf++;
    int cols = cw / S(170);
    if (cols > nf) cols = nf;
    if (cols < 1) cols = 1;
    int colW = (cw - S(24) * (cols - 1)) / cols;
    for (int i = 0; i < nf; i += cols) {
        int rowH = 0;
        for (int j = i; j < nf && j < i + cols; j++) {
            int cx = x + (j - i) * (colW + S(24)), cy = y + S(4);
            RECT r = {cx, cy, cx + colW, cy + S(18)};
            if (paint) text_line(dc, lab[j], &r, F_SMALL, C_GRAFITO, DT_LEFT);
            cy += S(18);
            cy += text_wrap(dc, val[j], cx, cy, colW, F_BODY_B, C_TINTA, paint);
            if (sub[j][0]) cy += text_wrap(dc, sub[j], cx, cy, colW, F_SMALL, C_GRAFITO, paint);
            if (cy - y > rowH) rowH = cy - y;
        }
        y += rowH + S(4);
    }
    y += S(8);
    if (paint) hline(dc, x, x + cw, y, C_LINEA);
    y += S(1);

    /* Avisos */
    if (cur.notice[0]) {
        y += S(12);
        int nw = cw > S(720) ? S(720) : cw;
        int th = text_wrap(dc, cur.notice, 0, 0, nw - S(32), F_BODY, 0, 0);
        if (paint) {
            RECT r = {x, y, x + nw, y + th + S(20)};
            fill_rect(dc, &r, cur.noticeError ? C_ERROR_SUAVE : C_AVISO_SUAVE);
            RECT bar = {x, y, x + S(4), y + th + S(20)};
            fill_rect(dc, &bar, cur.noticeError ? C_ERROR : C_AVISO);
            text_wrap(dc, cur.notice, x + S(18), y + S(10), nw - S(32), F_BODY, C_TINTA, 1);
        }
        y += th + S(20);
    }

    /* Acciones */
    y += S(18);
    int bx = x;
    int apple = cur.found && is_apple(cur.dev.brand);
    if (apple) bx += panel_button(dc, L"Comprobar iCloud en iUnlocker", bx, y, BTN_PRIMARY, HIT_IUNLOCKER, 1, paint) + S(8);
    bx += panel_button(dc, L"Ver ficha en la web oficial", bx, y, BTN_NORMAL, HIT_OFFICIAL, cur.found, paint) + S(8);
    panel_button(dc, L"Copiar IMEI", bx, y, BTN_GHOST, HIT_COPY, 1, paint);
    y += S(40);

    /* Buscar mi iPhone */
    if (apple) {
        y += S(18);
        HistItem *h = cur_hist();
        const char *st = h ? h->icloud : "sin comprobar";
        const wchar_t *title = str_contains_ci(cur.dev.name, "ipad") ? L"Buscar mi iPad" : str_contains_ci(cur.dev.name, "watch") ? L"Buscar mi Apple Watch" : L"Buscar mi iPhone";
        static const wchar_t *opts[3] = {L"Sin comprobar", L"Desactivado · se puede enviar", L"Activado · no enviar"};
        static const char *vals[3] = {"sin comprobar", "OFF", "ON"};
        int tw = text_width(dc, title, F_BODY_B);
        int segW = 0;
        for (int i = 0; i < 3; i++) segW += text_width(dc, opts[i], F_SMALL) + S(24);
        wchar_t src[96] = L"";
        if (h && h->icloudFecha[0]) {
            wchar_t t[32];
            iso_to_local_text(h->icloudFecha, t, COUNTOF(t), 1);
            swprintf(src, COUNTOF(src), L"Marcado · %ls", t);
        }
        int srcW = src[0] ? text_width(dc, src, F_SMALL) : 0;
        int oneLine = S(14) + tw + S(14) + segW + S(14) + srcW + S(14) <= cw;
        int boxH = oneLine ? S(56) : S(100);
        if (paint) {
            RECT box = {x, y, x + cw, y + boxH};
            round_rect(dc, &box, S(8), C_SUPERFICIE, C_LINEA, S(1));
            RECT tr = {x + S(14), y + S(12), x + S(14) + tw, y + S(44)};
            text_line(dc, title, &tr, F_BODY_B, C_TINTA, DT_LEFT);
        }
        int sx = oneLine ? x + S(14) + tw + S(14) : x + S(14), sy = oneLine ? y + S(12) : y + S(52);
        if (paint) {
            RECT seg = {sx, sy, sx + segW, sy + S(32)};
            round_rect(dc, &seg, S(8), C_SUPERFICIE, C_LINEA, S(1));
        }
        int ox = sx;
        for (int i = 0; i < 3; i++) {
            int ow = text_width(dc, opts[i], F_SMALL) + S(24);
            int on = !strcmp(st, vals[i]);
            if (paint) {
                RECT o = {ox + S(1), sy + S(1), ox + ow - S(1), sy + S(31)};
                COLORREF bgc = on ? (i == 1 ? C_OK_SUAVE : i == 2 ? C_ERROR_SUAVE : C_PAPEL) : C_SUPERFICIE;
                COLORREF tc = on ? (i == 1 ? C_OK : i == 2 ? C_ERROR : C_TINTA) : (g_hover == HIT_ICLOUD0 + i ? C_TINTA : C_GRAFITO);
                fill_rect(dc, &o, bgc);
                if (i) {
                    RECT sep = {ox, sy + S(1), ox + S(1), sy + S(31)};
                    fill_rect(dc, &sep, C_LINEA);
                }
                text_line(dc, opts[i], &o, on ? F_SMALL_B : F_SMALL, tc, DT_CENTER);
            }
            add_hit(ox, sy, ow, S(32), HIT_ICLOUD0 + i, 1);
            ox += ow;
        }
        if (src[0] && paint) {
            RECT sr = oneLine ? (RECT){ox + S(14), sy, x + cw - S(14), sy + S(32)} : (RECT){x + S(14) + tw + S(14), y + S(12), x + cw - S(14), y + S(44)};
            text_line(dc, src, &sr, F_SMALL, C_GRAFITO, DT_LEFT);
        }
        y += boxH;
    }

    /* Ficha técnica */
    y += S(30);
    RECT hr = {x, y, x + cw, y + S(28)};
    if (paint) text_line(dc, L"Ficha técnica", &hr, F_H3, C_TINTA, DT_LEFT);
    if (cur.specState == SPEC_OK) {
        wchar_t src[300];
        char titulo[200];
        safe_copy(titulo, sizeof(titulo), cur.specs.titulo);
        if (titulo[0] == 'I' && (!strncmp(titulo + 1, "Phone", 5) || !strncmp(titulo + 1, "Pad", 3) || !strncmp(titulo + 1, "Pod", 3))) titulo[0] = 'i';
        wchar_t *t = u2w(titulo);
        swprintf(src, COUNTOF(src), L"Fuente: Wikipedia (%ls) · %ls", !strcmp(cur.specs.idioma, "en") ? L"inglés" : L"español", t);
        free(t);
        int sw = text_width(dc, src, F_SMALL);
        int htw = text_width(dc, L"Ficha técnica", F_H3);
        if (sw > cw - htw - S(16)) sw = cw - htw - S(16);
        RECT sr = {x + cw - sw, y, x + cw, y + S(28)};
        if (paint) {
            text_line(dc, src, &sr, F_SMALL, g_hover == HIT_SOURCE ? C_TINTA : C_TAC, DT_RIGHT);
            hline(dc, sr.left, sr.right, sr.bottom - S(5), g_hover == HIT_SOURCE ? C_TINTA : C_TAC);
        }
        add_hit(sr.left, sr.top, sw, S(28), HIT_SOURCE, 1);
    }
    y += S(32);

    wchar_t status[600] = L"";
    switch (cur.specState) {
    case SPEC_NONE: wcscpy(status, L"Sin modelo identificado no se puede buscar la ficha."); break;
    case SPEC_LOADING: wcscpy(status, L"Buscando la ficha técnica en Wikipedia…"); break;
    case SPEC_NOTFOUND: wcscpy(status, L"No hay ficha en Wikipedia para este modelo. Ábrela en la web oficial del fabricante con el botón de arriba."); break;
    case SPEC_NETERR: wcscpy(status, L"No se ha podido consultar Wikipedia. Comprueba la conexión a internet o usa «Ver ficha en la web oficial»."); break;
    case SPEC_OK:
        if (cur.specs.aproximado) {
            wchar_t *t = u2w(cur.specs.titulo);
            swprintf(status, COUNTOF(status), L"La ficha es del artículo «%ls», que puede agrupar varias variantes del modelo.", t);
            free(t);
        }
        break;
    }
    if (status[0]) y += text_wrap(dc, status, x, y, cw, F_BODY, C_GRAFITO, paint) + S(8);

    if (cur.specState == SPEC_OK) {
        int lw = S(190), vx = x + lw + S(16), vw = cw - lw - S(16);
        if (vw > S(640)) vw = S(640);
        for (int i = -1; i < cur.specs.n; i++) {
            const char *campo = i < 0 ? "Fecha de lanzamiento" : cur.specs.f[i].campo;
            const char *valor = i < 0 ? cur.specs.fecha : cur.specs.f[i].valor;
            if (!valor || !*valor) continue;
            wchar_t *wc = u2w(campo), *wv = u2w(valor);
            int h1 = text_wrap(dc, wc, x, 0, lw, F_BODY, 0, 0);
            int h2 = text_wrap(dc, wv, vx, 0, vw, i < 0 ? F_BODY_B : F_BODY, 0, 0);
            int rh = (h1 > h2 ? h1 : h2) + S(18);
            if (paint) {
                hline(dc, x, x + cw, y, C_LINEA);
                text_wrap(dc, wc, x, y + S(9), lw, F_BODY, C_GRAFITO, 1);
                text_wrap(dc, wv, vx, y + S(9), vw, i < 0 ? F_BODY_B : F_BODY, C_TINTA, 1);
            }
            y += rh;
            free(wc);
            free(wv);
        }
    }
    return y + g_scroll + S(40);
}

static void panel_relayout(void) {
    RECT rc;
    GetClientRect(g_panel, &rc);
    HDC dc = GetDC(g_panel);
    g_contentH = panel_draw(dc, rc.right, 0);
    ReleaseDC(g_panel, dc);
    int maxScroll = g_contentH - rc.bottom;
    if (maxScroll < 0) maxScroll = 0;
    if (g_scroll > maxScroll) g_scroll = maxScroll;
    if (g_scroll < 0) g_scroll = 0;
    InvalidateRect(g_panel, NULL, FALSE);
    InvalidateRect(g_main, &rcFrame, FALSE);
}

static void panel_scroll(int delta) {
    RECT rc;
    GetClientRect(g_panel, &rc);
    int maxScroll = g_contentH - rc.bottom;
    if (maxScroll < 0) maxScroll = 0;
    int s = g_scroll + delta;
    if (s < 0) s = 0;
    if (s > maxScroll) s = maxScroll;
    if (s != g_scroll) {
        g_scroll = s;
        InvalidateRect(g_panel, NULL, FALSE);
    }
}

static int hit_at(int x, int y) {
    for (int i = 0; i < g_nhits; i++)
        if (g_hits[i].enabled && x >= g_hits[i].r.left && x < g_hits[i].r.right && y >= g_hits[i].r.top && y < g_hits[i].r.bottom) return g_hits[i].id;
    return HIT_NONE;
}

static LRESULT CALLBACK panel_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom > 0 ? rc.bottom : 1);
        HGDIOBJ ob = SelectObject(mem, bmp);
        fill_rect(mem, &rc, C_PAPEL);
        g_contentH = panel_draw(mem, rc.right, 1);
        if (g_contentH > rc.bottom) { /* indicador de desplazamiento */
            int trackH = rc.bottom - S(16);
            int barH = trackH * rc.bottom / g_contentH;
            if (barH < S(30)) barH = S(30);
            int barY = S(8) + (trackH - barH) * g_scroll / (g_contentH - rc.bottom);
            RECT bar = {rc.right - S(8), barY, rc.right - S(4), barY + barH};
            round_rect(mem, &bar, S(4), C_LINEA, C_LINEA, 0);
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        panel_relayout();
        return 0;
    case WM_MOUSEWHEEL:
        panel_scroll(-GET_WHEEL_DELTA_WPARAM(w) * S(60) / WHEEL_DELTA);
        return 0;
    case WM_MOUSEMOVE: {
        int hit = hit_at(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (hit != g_hover) {
            g_hover = hit;
            InvalidateRect(h, NULL, FALSE);
        }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hover) {
            g_hover = 0;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && g_hover) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP: {
        int hit = hit_at(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (hit) action(hit);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_DOWN) panel_scroll(S(40));
        if (w == VK_UP) panel_scroll(-S(40));
        if (w == VK_NEXT) panel_scroll(S(400));
        if (w == VK_PRIOR) panel_scroll(-S(400));
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK toast_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        fill_rect(dc, &rc, C_PAPEL);
        round_rect(dc, &rc, S(8), C_TINTA, C_TINTA, 0);
        text_wrap(dc, g_toastText, S(16), S(11), rc.right - S(32), F_BODY, C_PAPEL, 1);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        KillTimer(h, 1);
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_LBUTTONDOWN:
        ShowWindow(h, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ================================================================ cuadro del IMEI */

static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CHAR && (w == '\r' || w == '\n')) {
        wchar_t t[64];
        GetWindowTextW(h, t, COUNTOF(t));
        do_check(t, -1);
        return 0;
    }
    if (m == WM_CHAR && w == 1) { /* Ctrl+A */
        SendMessageW(h, EM_SETSEL, 0, -1);
        return 0;
    }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) {
        SendMessageW(h, EM_SETSEL, 0, -1);
        return 0;
    }
    if (m == WM_PASTE) {
        LRESULT r = CallWindowProcW(g_editOrig, h, m, w, l);
        PostMessageW(g_main, WM_APP_PASTED, 0, 0);
        return r;
    }
    return CallWindowProcW(g_editOrig, h, m, w, l);
}

/* ================================================================ actualización */

static DWORD WINAPI update_check_thread(LPVOID p) {
    int manual = (int)(INT_PTR)p;
    if (!manual) Sleep(1500);
    UpdateInfo *info = calloc(1, sizeof(*info));
    int r = update_check(info);
    if (!PostMessageW(g_main, WM_APP_UPDATE_FOUND, (WPARAM)(r | (manual << 8)), (LPARAM)info)) free(info);
    return 0;
}

static DWORD WINAPI update_apply_thread(LPVOID p) {
    (void)p;
    wchar_t *err = calloc(512, sizeof(wchar_t));
    int ok = update_apply(&g_update, err, 512);
    PostMessageW(g_main, WM_APP_UPDATE_DONE, ok, (LPARAM)err);
    return 0;
}

void app_check_updates(int manual) {
    HANDLE t = CreateThread(NULL, 0, update_check_thread, (LPVOID)(INT_PTR)manual, 0, NULL);
    if (t) CloseHandle(t);
}

static void start_update(void) {
    if (g_updating || !g_update.url[0]) return;
    g_updating = 1;
    SetWindowTextW(g_btnUpdate, L"Descargando actualización…");
    EnableWindow(g_btnUpdate, FALSE);
    HANDLE t = CreateThread(NULL, 0, update_apply_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/* ================================================================ disposición */

static void layout(void) {
    RECT rc;
    GetClientRect(g_main, &rc);
    int W = rc.right, H = rc.bottom;
    int mw = W - S(SIDE_W);
    int cw = mw - 2 * S(PAD_X);
    if (cw > S(CONTENT_MAX)) cw = S(CONTENT_MAX);
    int x = S(PAD_X);
    int y = S(BAND_H) + S(30);
    rcLabel = (RECT){x, y, x + cw, y + S(22)};
    y += S(30);
    int btnW = S(150), frameH = S(82);
    rcFrame = (RECT){x, y, x + cw - btnW - S(12), y + frameH};
    HDC dc = GetDC(g_main);
    int eh = font_height(dc, F_INPUT);
    ReleaseDC(g_main, dc);
    MoveWindow(g_edit, rcFrame.left + S(18), y + (frameH - eh) / 2, rcFrame.right - rcFrame.left - S(36), eh, TRUE);
    MoveWindow(g_btnCheck, rcFrame.right + S(12), y, btnW, frameH, TRUE);
    y += frameH + S(12);
    rcParts = (RECT){x, y, x + cw, y + S(64)};
    y += S(64) + S(10);
    rcHint = (RECT){x, y, x + cw, y + S(20)};
    y += S(20) + S(16);
    MoveWindow(g_panel, 0, y, mw, H - y, TRUE);

    /* botones de la cabecera */
    int sbw = S(100);
    MoveWindow(g_btnSettings, W - S(16) - sbw, (S(BAND_H) - S(36)) / 2, sbw, S(36), TRUE);
    HDC dc2 = GetDC(g_main);
    wchar_t ut[96];
    GetWindowTextW(g_btnUpdate, ut, COUNTOF(ut));
    int uw = text_width(dc2, ut, F_BTN) + S(32);
    ReleaseDC(g_main, dc2);
    MoveWindow(g_btnUpdate, W - S(16) - sbw - S(10) - uw, (S(BAND_H) - S(36)) / 2, uw, S(36), TRUE);

    /* historial */
    int sx = mw;
    rcSideHead = (RECT){sx, S(BAND_H), W, S(BAND_H) + S(52)};
    rcSearchFrame = (RECT){sx + S(18), S(BAND_H) + S(54), W - S(18), S(BAND_H) + S(90)};
    MoveWindow(g_search, rcSearchFrame.left + S(10), rcSearchFrame.top + S(8), rcSearchFrame.right - rcSearchFrame.left - S(20), S(20), TRUE);
    int footH = S(58);
    rcListArea = (RECT){sx + S(1), rcSearchFrame.bottom + S(10), W, H - footH};
    MoveWindow(g_list, rcListArea.left, rcListArea.top, rcListArea.right - rcListArea.left, rcListArea.bottom - rcListArea.top, TRUE);
    int fy = H - footH + S(12);
    MoveWindow(g_btnExport, sx + S(18), fy, S(176), S(34), TRUE);
    MoveWindow(g_btnClear, sx + S(18) + S(184), fy, S(80), S(34), TRUE);
    SendMessageW(g_list, LB_SETITEMHEIGHT, 0, S(58));
    InvalidateRect(g_main, NULL, FALSE);
}

static void paint_main(HDC dc, const RECT *rc) {
    int W = rc->right, H = rc->bottom;
    int mw = W - S(SIDE_W);
    RECT main = {0, 0, mw, H};
    fill_rect(dc, &main, C_PAPEL);

    /* cabecera */
    RECT band = {0, 0, W, S(BAND_H)};
    fill_rect(dc, &band, C_BANDA);
    int bx = S(PAD_X);
    int lh = S(32);
    if (logo_draw(dc, bx, (S(BAND_H) - lh) / 2, lh, S(180), &bx)) {
        bx += S(14);
        RECT sep = {bx, (S(BAND_H) - S(26)) / 2, bx + S(1), (S(BAND_H) + S(26)) / 2};
        fill_rect(dc, &sep, RGB(0xB5, 0x6D, 0x74));
        bx += S(15);
    }
    RECT title = {bx, 0, W / 2, S(BAND_H)};
    text_line(dc, APP_NAME_W, &title, F_BRAND, C_BLANCO, DT_LEFT);

    /* IMEI */
    text_line(dc, L"IMEI del dispositivo", &rcLabel, F_LABEL, C_GRAFITO, DT_LEFT);
    round_rect(dc, &rcFrame, S(8), C_SUPERFICIE, g_editFocus ? C_TAC : C_LINEA, S(2));

    wchar_t t[64];
    GetWindowTextW(g_edit, t, COUNTOF(t));
    ImeiInfo a;
    imei_analyze(t, &a);
    char tac[9], ser[7], chk[2] = {0};
    snprintf(tac, sizeof(tac), "%-8s", a.tac);
    snprintf(ser, sizeof(ser), "%-6s", a.serial);
    for (int i = 0; i < 8; i++)
        if (tac[i] == ' ') tac[i] = '.';
    for (int i = 0; i < 6; i++)
        if (ser[i] == ' ') ser[i] = '.';
    chk[0] = a.complete ? a.check : '.';
    int total = rcParts.right - rcParts.left - S(8);
    int wc = total * 2 / 16;
    if (wc < S(120)) wc = S(120);
    int wt = (total - wc) * 8 / 14, ws = total - wc - wt;
    struct { const char *v; const wchar_t *lab; COLORREF c; int w; } parts[3] = {
        {tac, L"TAC · modelo", C_TAC, wt}, {ser, L"Número de serie", C_SERIE, ws}, {chk, L"Control", C_LINEA, wc}};
    wchar_t checkLab[40];
    if (a.complete) {
        parts[2].c = a.valid ? C_OK : C_ERROR;
        if (a.valid) parts[2].lab = L"Control correcto";
        else {
            swprintf(checkLab, COUNTOF(checkLab), L"Debería ser %c", a.expected);
            parts[2].lab = checkLab;
        }
    } else if (a.tooLong) {
        parts[2].lab = L"Demasiados dígitos";
    }
    int px = rcParts.left;
    for (int i = 0; i < 3; i++) {
        RECT r = {px, rcParts.top, px + parts[i].w, rcParts.bottom};
        fill_rect(dc, &r, C_SUPERFICIE);
        RECT top = {px, rcParts.top, px + parts[i].w, rcParts.top + S(4)};
        fill_rect(dc, &top, parts[i].c);
        wchar_t *v = u2w(parts[i].v);
        for (wchar_t *q = v; *q; q++)
            if (*q == L'.') *q = 0x00B7;
        RECT vr = {px + S(12), rcParts.top + S(8), r.right - S(6), rcParts.top + S(38)};
        text_line(dc, v, &vr, F_PART, C_TINTA, DT_LEFT);
        free(v);
        RECT lr = {px + S(12), rcParts.top + S(38), r.right - S(6), rcParts.bottom - S(6)};
        text_line(dc, parts[i].lab, &lr, i == 2 && a.complete ? F_SMALL_B : F_SMALL, i == 2 && a.complete ? parts[i].c : C_GRAFITO, DT_LEFT);
        px += parts[i].w + S(4);
    }
    text_line(dc, L"Marca *#06# en el teléfono o míralo en la caja. Puedes pegarlo con espacios o guiones; con Intro se comprueba.", &rcHint, F_SMALL,
              C_GRAFITO, DT_LEFT);

    /* historial */
    RECT side = {mw, S(BAND_H), W, H};
    fill_rect(dc, &side, C_LATERAL);
    RECT border = {mw, S(BAND_H), mw + S(1), H};
    fill_rect(dc, &border, C_LINEA);
    RECT hh = {mw + S(18), S(BAND_H) + S(14), W - S(18), S(BAND_H) + S(44)};
    text_line(dc, L"Historial", &hh, F_BODY_B, C_TINTA, DT_LEFT);
    if (g_hist.n) {
        wchar_t n[32];
        fmt_thousands((unsigned)g_hist.n, n, COUNTOF(n));
        RECT nr = hh;
        nr.left += text_width(dc, L"Historial", F_BODY_B) + S(8);
        text_line(dc, n, &nr, F_SMALL, C_GRAFITO, DT_LEFT);
    }
    round_rect(dc, &rcSearchFrame, S(8), C_PAPEL, g_searchFocus ? C_TAC : C_LINEA, S(1));
    if (!g_nfilter) {
        RECT er = {rcListArea.left + S(17), rcListArea.top + S(8), rcListArea.right - S(18), rcListArea.top + S(60)};
        text_wrap(dc, g_hist.n ? L"Nada coincide con la búsqueda." : L"Aquí aparecerán los IMEI que compruebes.", er.left, er.top,
                  er.right - er.left, F_SMALL, C_GRAFITO, 1);
    }
    hline(dc, mw + S(1), W, H - S(58), C_LINEA);
}

/* ================================================================ ventana principal */

static void open_history_item(int listIndex) {
    if (listIndex < 0 || listIndex >= g_nfilter) return;
    int idx = g_filter[listIndex];
    wchar_t *w = u2w(g_hist.items[idx].imei);
    do_check(w, idx);
    free(w);
}

static void export_history(void) {
    wchar_t file[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf(file, MAX_PATH, L"historial-imei-%04u-%02u-%02u.csv", st.wYear, st.wMonth, st.wDay);
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = g_main;
    of.lpstrFilter = L"CSV para Excel (*.csv)\0*.csv\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = L"csv";
    of.lpstrTitle = L"Exportar historial";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&of)) return;
    toast(hist_export_csv(file) ? L"Historial exportado." : L"No se ha podido guardar el archivo. Comprueba que no esté abierto en Excel.");
}

static void clear_history(void) {
    if (MessageBoxW(g_main, L"¿Borrar todo el historial de comprobaciones?\n\nSi lo necesitas, expórtalo antes a CSV. Esta acción no se puede deshacer.",
                    APP_NAME_W, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        return;
    hist_clear();
    history_refresh();
    InvalidateRect(g_main, NULL, FALSE);
    toast(L"Historial borrado.");
}

void app_logo_changed(void) {
    InvalidateRect(g_main, NULL, FALSE);
}

void app_tac_changed(void) {
    if (cur.has) {
        wchar_t *w = u2w(cur.imei);
        int idx = hist_find(cur.histId);
        do_check(w, idx);
        free(w);
    }
}

static LRESULT CALLBACK main_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_main = h;
        g_dpi = GetDpiForWindow(h);
        ui_fonts_create(g_dpi);
        ui_dark_titlebar(h);
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | ES_AUTOHSCROLL, 0, 0, 10, 10, h,
                                 (HMENU)IDC_EDIT, g_inst, NULL);
        SendMessageW(g_edit, EM_LIMITTEXT, 40, 0);
        SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"15 dígitos");
        g_editOrig = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
        g_btnCheck = make_button(h, IDC_CHECK, L"Comprobar", BTN_STYLE(BTN_PRIMARY | BTN_BIG, BG_PAPEL));
        g_btnSettings = make_button(h, IDC_SETTINGS, L"Ajustes", BTN_STYLE(BTN_ON_BAND, BG_BANDA));
        g_btnUpdate = make_button(h, IDC_UPDATE, L"Actualizar", BTN_STYLE(BTN_ON_BAND_FILLED, BG_BANDA));
        ShowWindow(g_btnUpdate, SW_HIDE);
        g_panel = CreateWindowExW(0, PANEL_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 10, 10, h, NULL, g_inst, NULL);
        g_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | ES_AUTOHSCROLL, 0, 0, 10, 10, h,
                                   (HMENU)IDC_SEARCH, g_inst, NULL);
        SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Buscar IMEI o modelo");
        g_list = CreateWindowExW(0, L"LISTBOX", NULL,
                                 WS_CHILD | WS_VSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_NODATA, 0, 0,
                                 10, 10, h, (HMENU)IDC_LIST, g_inst, NULL);
        SetWindowTheme(g_list, L"DarkMode_Explorer", NULL);
        g_btnExport = make_button(h, IDC_EXPORT, L"Exportar a Excel (CSV)", BTN_STYLE(BTN_NORMAL, BG_LATERAL));
        g_btnClear = make_button(h, IDC_CLEAR, L"Borrar", BTN_STYLE(BTN_GHOST, BG_LATERAL));
        g_toast = CreateWindowExW(0, TOAST_CLASS, NULL, WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, h, NULL, g_inst, NULL);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font[F_INPUT], FALSE);
        SendMessageW(g_search, WM_SETFONT, (WPARAM)g_font[F_SMALL], FALSE);
        layout();
        history_refresh();
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        ui_fonts_create(g_dpi);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font[F_INPUT], FALSE);
        SendMessageW(g_search, WM_SETFONT, (WPARAM)g_font[F_SMALL], FALSE);
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        panel_relayout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        mm->ptMinTrackSize.x = S(940);
        mm->ptMinTrackSize.y = S(640);
        return 0;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        paint_main(mem, &rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, C_TINTA);
        if ((HWND)l == g_search) {
            SetBkColor(dc, C_PAPEL);
            return (LRESULT)g_brPapel;
        }
        SetBkColor(dc, C_SUPERFICIE);
        return (LRESULT)g_brSuperficie;
    }
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)g_brLateral;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)l;
        if (mi->CtlType == ODT_LISTBOX) mi->itemHeight = S(58);
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)l;
        if (di->CtlType == ODT_BUTTON) draw_button(di);
        else if (di->CtlType == ODT_LISTBOX) draw_history_item(di);
        return TRUE;
    }
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        HWND under = WindowFromPoint(pt);
        if (under == g_list) return SendMessageW(g_list, m, w, l);
        panel_scroll(-GET_WHEEL_DELTA_WPARAM(w) * S(60) / WHEEL_DELTA);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == IDC_EDIT && code == EN_CHANGE) InvalidateRect(h, &rcParts, FALSE);
        if (id == IDC_EDIT && (code == EN_SETFOCUS || code == EN_KILLFOCUS)) {
            g_editFocus = code == EN_SETFOCUS;
            InvalidateRect(h, &rcFrame, FALSE);
        }
        if (id == IDC_SEARCH && code == EN_CHANGE) history_refresh();
        if (id == IDC_SEARCH && (code == EN_SETFOCUS || code == EN_KILLFOCUS)) {
            g_searchFocus = code == EN_SETFOCUS;
            InvalidateRect(h, &rcSearchFrame, FALSE);
        }
        if (id == IDC_LIST && code == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
            open_history_item(sel);
            SendMessageW(g_list, LB_SETCURSEL, (WPARAM)-1, 0);
        }
        if (id == IDC_CHECK) {
            wchar_t t[64];
            GetWindowTextW(g_edit, t, COUNTOF(t));
            do_check(t, -1);
        }
        if (id == IDC_SETTINGS) settings_open(h);
        if (id == IDC_UPDATE) start_update();
        if (id == IDC_EXPORT) export_history();
        if (id == IDC_CLEAR) clear_history();
        return 0;
    }
    case WM_APP_PASTED: {
        InvalidateRect(h, &rcParts, FALSE);
        wchar_t t[64];
        GetWindowTextW(g_edit, t, COUNTOF(t));
        ImeiInfo a;
        imei_analyze(t, &a);
        if (a.complete && a.valid) do_check(t, -1);
        return 0;
    }
    case WM_APP_SPECS: {
        SpecResult *res = (SpecResult *)l;
        if ((unsigned)w == cur.reqId && cur.has) {
            specs_free(&cur.specs);
            if (res->result == WIKI_FOUND) {
                cur.specs = res->specs;
                cur.specState = SPEC_OK;
            } else {
                cur.specState = res->result == WIKI_NET_ERROR ? SPEC_NETERR : SPEC_NOTFOUND;
                specs_free(&res->specs);
            }
            panel_relayout();
        } else {
            specs_free(&res->specs);
        }
        free(res);
        return 0;
    }
    case WM_APP_UPDATE_FOUND: {
        UpdateInfo *info = (UpdateInfo *)l;
        int r = (int)(signed char)(w & 0xFF), manual = (int)(w >> 8);
        if (r == UPDATE_AVAILABLE) {
            g_update = *info;
            wchar_t t[96], *v = u2w(info->version);
            swprintf(t, COUNTOF(t), L"Actualizar a la versión %ls", v);
            free(v);
            SetWindowTextW(g_btnUpdate, t);
            ShowWindow(g_btnUpdate, SW_SHOW);
            layout();
        }
        if (manual) settings_update_result(r, info->version);
        free(info);
        return 0;
    }
    case WM_APP_UPDATE_DONE: {
        wchar_t *err = (wchar_t *)l;
        if (w) {
            update_relaunch();
            DestroyWindow(h);
        } else {
            g_updating = 0;
            EnableWindow(g_btnUpdate, TRUE);
            wchar_t t[96], *v = u2w(g_update.version);
            swprintf(t, COUNTOF(t), L"Actualizar a la versión %ls", v);
            free(v);
            SetWindowTextW(g_btnUpdate, t);
            MessageBoxW(h, err, APP_NAME_W, MB_OK | MB_ICONWARNING);
        }
        free(err);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev;
    g_inst = inst;
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"ComprobadorIMEI.Instancia");
    if (GetLastError() == ERROR_ALREADY_EXISTS && !wcsstr(cmd, L"--actualizado")) {
        HWND other = FindWindowW(MAIN_CLASS, NULL);
        if (other) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    if (wcsstr(cmd, L"--actualizado")) {
        /* espera a que se cierre la versión anterior */
        WaitForSingleObject(mutex, 5000);
    }
    srand((unsigned)GetTickCount());
    update_cleanup();
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    ui_fonts_load();
    tacdb_init();
    hist_load();
    logo_reload();
    g_brSuperficie = CreateSolidBrush(C_SUPERFICIE);
    g_brPapel = CreateSolidBrush(C_PAPEL);
    g_brLateral = CreateSolidBrush(C_LATERAL);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassExW(&wc);
    WNDCLASSEXW pc = {sizeof(pc)};
    pc.lpfnWndProc = panel_proc;
    pc.hInstance = inst;
    pc.hCursor = LoadCursor(NULL, IDC_ARROW);
    pc.lpszClassName = PANEL_CLASS;
    RegisterClassExW(&pc);
    WNDCLASSEXW tc = {sizeof(tc)};
    tc.lpfnWndProc = toast_proc;
    tc.hInstance = inst;
    tc.hCursor = LoadCursor(NULL, IDC_ARROW);
    tc.lpszClassName = TOAST_CLASS;
    RegisterClassExW(&tc);
    settings_register();

    UINT dpi = GetDpiForSystem();
    int ww = MulDiv(1240, (int)dpi, 96), wh = MulDiv(860, (int)dpi, 96);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (ww > work.right - work.left) ww = work.right - work.left;
    if (wh > work.bottom - work.top) wh = work.bottom - work.top;
    HWND h = CreateWindowExW(0, MAIN_CLASS, APP_NAME_W, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + (work.right - work.left - ww) / 2,
                             work.top + (work.bottom - work.top - wh) / 2, ww, wh, NULL, NULL, inst, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    SetFocus(g_edit);
    const wchar_t *arg = wcsstr(cmd, L"--imei");
    if (arg) { /* "ComprobadorIMEI.exe --imei 351091214899373" abre la app ya comprobando ese IMEI */
        wchar_t val[64] = {0};
        int k = 0;
        for (arg += 6; *arg && (*arg == L' ' || *arg == L'='); arg++) {}
        for (; *arg && *arg != L' ' && k < 63; arg++) val[k++] = *arg;
        SetWindowTextW(g_edit, val);
        do_check(val, -1);
    }
    if (wcsstr(cmd, L"--actualizado")) {
        wchar_t t[96];
        swprintf(t, COUNTOF(t), L"Actualizado a la versión %ls.", APP_VERSION_W);
        toast(t);
    }
    app_check_updates(0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
            /* Tab entre el IMEI, Comprobar y la búsqueda del historial */
            HWND order[] = {g_edit, g_btnCheck, g_search, g_list, g_btnExport, g_btnClear, g_btnSettings};
            HWND f = GetFocus();
            int n = (int)COUNTOF(order), i;
            for (i = 0; i < n && order[i] != f; i++) {}
            if (i < n && GetAncestor(f, GA_ROOT) == g_main) {
                int back = GetKeyState(VK_SHIFT) < 0;
                for (int k = 1; k <= n; k++) {
                    HWND next = order[(i + (back ? n - k : k)) % n];
                    if (IsWindowVisible(next) && IsWindowEnabled(next)) {
                        SetFocus(next);
                        break;
                    }
                }
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (mutex) ReleaseMutex(mutex);
    return 0;
}
