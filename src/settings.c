#include "settings.h"
#include "ui.h"
#include "logo.h"
#include "tacdb.h"
#include "update.h"
#include <commdlg.h>

enum { IDS_LOGO = 2001, IDS_LOGO_CLEAR, IDS_IMPORT, IDS_IMPORT_CLEAR, IDS_UPDATES, IDS_CLOSE };

static const wchar_t *SETTINGS_CLASS = L"ComprobadorIMEI.Ajustes";
static HWND g_dlg, g_bLogo, g_bLogoClear, g_bImport, g_bImportClear, g_bUpdates, g_bClose;
static wchar_t g_updateStatus[200];
static int g_contentH;

static void fmt_n(unsigned v, wchar_t *out, size_t cap) {
    wchar_t tmp[32];
    swprintf(tmp, COUNTOF(tmp), L"%u", v);
    size_t n = wcslen(tmp), o = 0;
    for (size_t i = 0; i < n && o + 2 < cap; i++) {
        out[o++] = tmp[i];
        if ((n - i - 1) % 3 == 0 && i + 1 < n) out[o++] = L'.';
    }
    out[o] = 0;
}

static void tac_text(wchar_t *out, size_t cap) {
    unsigned b, im;
    wchar_t nb[32], ni[32], name[MAX_PATH];
    char date[24];
    tacdb_counts(&b, &im);
    fmt_n(b, nb, COUNTOF(nb));
    if (!im) {
        swprintf(out, cap, L"Base incluida: %ls modelos (TAC). No hay ninguna base importada.", nb);
        return;
    }
    fmt_n(im, ni, COUNTOF(ni));
    tacdb_import_info(name, COUNTOF(name), date);
    wchar_t d[32];
    iso_to_local_text(date, d, COUNTOF(d), 0);
    swprintf(out, cap, L"Base incluida: %ls modelos. Importados: %ls desde «%ls» (%ls).", nb, ni, name, d);
}

/* Dibuja (o solo mide) el contenido y coloca los botones. Devuelve el alto total. */
static int settings_draw(HDC dc, int width, int paint) {
    int x = S(26), w = width - S(52), y = S(22);
    RECT r;
#define SECTION(title)                                         \
    do {                                                       \
        y += S(14);                                            \
        if (paint) hline(dc, x, x + w, y, C_LINEA);            \
        y += S(12);                                            \
        r = (RECT){x, y, x + w, y + S(24)};                    \
        if (paint) text_line(dc, title, &r, F_BODY_B, C_TINTA, DT_LEFT); \
        y += S(30);                                            \
    } while (0)
#define PARA(text)                                                         \
    do {                                                                   \
        y += text_wrap(dc, text, x, y, w, F_SMALL, C_GRAFITO, paint) + S(10); \
    } while (0)
#define BUTTONS(b1, w1, b2, w2)                                                 \
    do {                                                                        \
        if (!paint) {                                                           \
            MoveWindow(b1, x, y, S(w1), S(34), TRUE);                           \
            if (b2) MoveWindow(b2, x + S(w1) + S(8), y, S(w2), S(34), TRUE);    \
        }                                                                       \
        y += S(34) + S(10);                                                     \
    } while (0)

    r = (RECT){x, y, x + w, y + S(30)};
    if (paint) text_line(dc, L"Ajustes", &r, F_H2, C_TINTA, DT_LEFT);
    y += S(30);

    SECTION(L"Logo de la empresa");
    PARA(L"Se muestra en la cabecera. Usa el archivo oficial del logo en PNG o JPG, mejor en blanco o con fondo transparente. Se guarda solo en este equipo.");
    if (!paint) {
        SetWindowTextW(g_bLogo, logo_present() ? L"Cambiar logo…" : L"Cargar logo…");
        ShowWindow(g_bLogoClear, logo_present() ? SW_SHOW : SW_HIDE);
    }
    BUTTONS(g_bLogo, 140, g_bLogoClear, 120);

    SECTION(L"Base de datos de modelos (TAC)");
    wchar_t t[600];
    tac_text(t, COUNTOF(t));
    PARA(t);
    unsigned im;
    tacdb_counts(NULL, &im);
    if (!paint) ShowWindow(g_bImportClear, im ? SW_SHOW : SW_HIDE);
    BUTTONS(g_bImport, 250, g_bImportClear, 160);
    PARA(L"Lo importado tiene prioridad sobre la base incluida. Sirve para cargar la base oficial de la GSMA si la empresa tiene acceso.");

    SECTION(L"Actualizaciones");
    swprintf(t, COUNTOF(t), L"Versión instalada: %ls. Al arrancar, la app comprueba si hay una versión nueva publicada en GitHub y te ofrece instalarla con un clic.",
             APP_VERSION_W);
    PARA(t);
    BUTTONS(g_bUpdates, 200, NULL, 0);
    if (g_updateStatus[0]) PARA(g_updateStatus);

    y += S(4);
    if (paint) hline(dc, x, x + w, y, C_LINEA);
    y += S(12);
    PARA(L"Base incluida: tac-database (MIT), recopilada de fuentes públicas como Osmocom. Fichas de Wikipedia bajo licencia CC BY-SA. Tipografía Atkinson Hyperlegible (OFL). Código: github.com/VodafoneElche/IMEI_Casero");
    y += S(6);
    if (!paint) MoveWindow(g_bClose, x + w - S(110), y, S(110), S(36), TRUE);
    y += S(36) + S(22);
#undef SECTION
#undef PARA
#undef BUTTONS
    return y;
}

static void relayout(void) {
    RECT rc;
    GetClientRect(g_dlg, &rc);
    HDC dc = GetDC(g_dlg);
    g_contentH = settings_draw(dc, rc.right, 0);
    ReleaseDC(g_dlg, dc);
    RECT wr = {0, 0, rc.right, g_contentH};
    AdjustWindowRectExForDpi(&wr, (DWORD)GetWindowLongPtrW(g_dlg, GWL_STYLE), FALSE, 0, g_dpi);
    RECT cur;
    GetWindowRect(g_dlg, &cur);
    SetWindowPos(g_dlg, NULL, 0, 0, cur.right - cur.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(g_dlg, NULL, FALSE);
}

static void pick_logo(void) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = g_dlg;
    of.lpstrFilter = L"Imagen (PNG, JPG)\0*.png;*.jpg;*.jpeg\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Elegir el logo de la empresa";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&of)) return;
    wchar_t err[200];
    if (!logo_set_from_file(file, err, COUNTOF(err))) MessageBoxW(g_dlg, err, APP_NAME_W, MB_OK | MB_ICONWARNING);
    app_logo_changed();
    relayout();
}

static void import_tac(void) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = g_dlg;
    of.lpstrFilter = L"CSV (*.csv, *.txt)\0*.csv;*.txt\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Importar base de datos TAC (CSV)";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&of)) return;
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    wchar_t err[300];
    int n = tacdb_import_csv(file, err, COUNTOF(err));
    if (n < 0) {
        MessageBoxW(g_dlg, err, APP_NAME_W, MB_OK | MB_ICONWARNING);
    } else {
        wchar_t nn[32], msg[120];
        fmt_n((unsigned)n, nn, COUNTOF(nn));
        swprintf(msg, COUNTOF(msg), L"Importados %ls TAC.", nn);
        MessageBoxW(g_dlg, msg, APP_NAME_W, MB_OK | MB_ICONINFORMATION);
        app_tac_changed();
    }
    relayout();
}

void settings_update_result(int result, const char *version) {
    if (!g_dlg) return;
    wchar_t *v = u2w(version);
    if (result == UPDATE_AVAILABLE)
        swprintf(g_updateStatus, COUNTOF(g_updateStatus), L"Hay una versión nueva (%ls). Pulsa «Actualizar» en la cabecera para instalarla.", v);
    else if (result == UPDATE_NONE)
        swprintf(g_updateStatus, COUNTOF(g_updateStatus), L"Tienes la última versión.");
    else
        swprintf(g_updateStatus, COUNTOF(g_updateStatus), L"No se ha podido comprobar. Revisa la conexión a internet.");
    free(v);
    EnableWindow(g_bUpdates, TRUE);
    relayout();
}

static LRESULT CALLBACK settings_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_dlg = h;
        ui_dark_titlebar(h);
        g_bLogo = make_button(h, IDS_LOGO, L"Cargar logo…", BTN_STYLE(BTN_NORMAL, BG_SUPERFICIE));
        g_bLogoClear = make_button(h, IDS_LOGO_CLEAR, L"Quitar logo", BTN_STYLE(BTN_GHOST, BG_SUPERFICIE));
        g_bImport = make_button(h, IDS_IMPORT, L"Importar CSV (GSMA u Osmocom)", BTN_STYLE(BTN_NORMAL, BG_SUPERFICIE));
        g_bImportClear = make_button(h, IDS_IMPORT_CLEAR, L"Quitar importación", BTN_STYLE(BTN_GHOST, BG_SUPERFICIE));
        g_bUpdates = make_button(h, IDS_UPDATES, L"Buscar actualizaciones", BTN_STYLE(BTN_NORMAL, BG_SUPERFICIE));
        g_bClose = make_button(h, IDS_CLOSE, L"Cerrar", BTN_STYLE(BTN_PRIMARY, BG_SUPERFICIE));
        g_updateStatus[0] = 0;
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
        fill_rect(mem, &rc, C_SUPERFICIE);
        settings_draw(mem, rc.right, 1);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        draw_button((DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDS_LOGO: pick_logo(); break;
        case IDS_LOGO_CLEAR:
            logo_remove();
            app_logo_changed();
            relayout();
            break;
        case IDS_IMPORT: import_tac(); break;
        case IDS_IMPORT_CLEAR:
            tacdb_clear_import();
            app_tac_changed();
            relayout();
            break;
        case IDS_UPDATES:
            EnableWindow(g_bUpdates, FALSE);
            swprintf(g_updateStatus, COUNTOF(g_updateStatus), L"Comprobando…");
            relayout();
            app_check_updates(1);
            break;
        case IDS_CLOSE: DestroyWindow(h); break;
        }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_dlg = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void settings_register(void) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = settings_proc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = SETTINGS_CLASS;
    RegisterClassExW(&wc);
}

void settings_open(HWND owner) {
    if (g_dlg) {
        SetForegroundWindow(g_dlg);
        return;
    }
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = S(580), h = S(640);
    HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME, SETTINGS_CLASS, L"Ajustes", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                             orc.left + (orc.right - orc.left - w) / 2, orc.top + S(60), w, h, owner, NULL, g_inst, NULL);
    if (!d) return;
    relayout();
    ShowWindow(d, SW_SHOW);
    EnableWindow(owner, FALSE);
    MSG msg;
    while (g_dlg && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && g_dlg) {
            DestroyWindow(g_dlg);
            continue;
        }
        if (!IsDialogMessageW(d, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}
