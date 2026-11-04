#include "ui.h"
#include "resource.h"
#include <dwmapi.h>
#include <commctrl.h>

HINSTANCE g_inst;
UINT g_dpi = 96;
HFONT g_font[F_COUNT];
static int g_haveFont;

int S(int px) {
    return MulDiv(px, (int)g_dpi, 96);
}

void ui_fonts_load(void) {
    int ids[] = {IDR_FONT_REGULAR, IDR_FONT_BOLD};
    for (int i = 0; i < 2; i++) {
        HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(ids[i]), RT_RCDATA);
        if (!r) continue;
        HGLOBAL g = LoadResource(NULL, r);
        DWORD n = 0;
        if (g && AddFontMemResourceEx(LockResource(g), SizeofResource(NULL, r), NULL, &n) && n) g_haveFont = 1;
    }
}

void ui_fonts_create(UINT dpi) {
    static const struct { int px, weight; } spec[F_COUNT] = {
        [F_BRAND] = {17, FW_BOLD},  [F_LABEL] = {15, FW_NORMAL}, [F_INPUT] = {40, FW_BOLD}, [F_PART] = {21, FW_BOLD},
        [F_SMALL] = {13, FW_NORMAL}, [F_SMALL_B] = {12, FW_BOLD}, [F_DEVICE] = {32, FW_BOLD}, [F_BODY] = {15, FW_NORMAL},
        [F_BODY_B] = {15, FW_BOLD}, [F_H3] = {19, FW_BOLD},     [F_BTN] = {14, FW_BOLD},    [F_H2] = {20, FW_BOLD},
    };
    g_dpi = dpi ? dpi : 96;
    for (int i = 0; i < F_COUNT; i++) {
        if (g_font[i]) DeleteObject(g_font[i]);
        g_font[i] = CreateFontW(-S(spec[i].px), 0, 0, 0, spec[i].weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, g_haveFont ? L"Atkinson Hyperlegible" : L"Segoe UI");
    }
}

void ui_dark_titlebar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF cap = C_BANDA, txt = C_BLANCO;
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR, Windows 11 */, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &txt, sizeof(txt));
}

COLORREF bg_color(int bg) {
    switch (bg) {
    case BG_LATERAL: return C_LATERAL;
    case BG_SUPERFICIE: return C_SUPERFICIE;
    case BG_BANDA: return C_BANDA;
    default: return C_PAPEL;
    }
}

void fill_rect(HDC dc, const RECT *r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH));
}

void round_rect(HDC dc, const RECT *r, int radius, COLORREF fill, COLORREF border, int borderW) {
    HPEN pen = CreatePen(PS_INSIDEFRAME, borderW > 0 ? borderW : 1, borderW > 0 ? border : fill);
    HBRUSH br = CreateSolidBrush(fill);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, br);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius, radius);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
    DeleteObject(br);
}

void hline(HDC dc, int x0, int x1, int y, COLORREF c) {
    RECT r = {x0, y, x1, y + (S(1) > 0 ? S(1) : 1)};
    fill_rect(dc, &r, c);
}

void text_line(HDC dc, const wchar_t *s, const RECT *r, int font, COLORREF c, UINT align) {
    HGDIOBJ of = SelectObject(dc, g_font[font]);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = *r;
    DrawTextW(dc, s, -1, &rr, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}

int text_wrap(HDC dc, const wchar_t *s, int x, int y, int w, int font, COLORREF c, int paint) {
    if (!s || !*s || w <= 0) return 0;
    HGDIOBJ of = SelectObject(dc, g_font[font]);
    RECT r = {x, y, x + w, y};
    DrawTextW(dc, s, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    if (paint) {
        SetTextColor(dc, c);
        SetBkMode(dc, TRANSPARENT);
        RECT d = {x, y, x + w, r.bottom};
        DrawTextW(dc, s, -1, &d, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    }
    SelectObject(dc, of);
    return r.bottom - y;
}

int text_width(HDC dc, const wchar_t *s, int font) {
    HGDIOBJ of = SelectObject(dc, g_font[font]);
    SIZE sz = {0};
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, of);
    return sz.cx;
}

int font_height(HDC dc, int font) {
    HGDIOBJ of = SelectObject(dc, g_font[font]);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    return tm.tmHeight;
}

/* ------------------------------------------------------------ botones propios */

static LRESULT CALLBACK button_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)id;
    (void)ref;
    switch (m) {
    case WM_MOUSEMOVE:
        if (!GetPropW(h, L"hover")) {
            SetPropW(h, L"hover", (HANDLE)1);
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(h, L"hover");
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_SETCURSOR:
        if (IsWindowEnabled(h)) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_NCDESTROY:
        RemovePropW(h, L"hover");
        RemoveWindowSubclass(h, button_proc, 0);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

HWND make_button(HWND parent, int id, const wchar_t *text, int style) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_OWNERDRAW, 0, 0, 10, 10, parent,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
    SetWindowLongPtrW(b, GWLP_USERDATA, style);
    SetWindowSubclass(b, button_proc, 0, 0);
    return b;
}

void draw_button(const DRAWITEMSTRUCT *di) {
    int style = (int)GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA);
    int kind = style & 7, bg = style >> 4, big = (style & BTN_BIG) != 0;
    int hover = GetPropW(di->hwndItem, L"hover") != NULL;
    int down = (di->itemState & ODS_SELECTED) != 0;
    int disabled = (di->itemState & ODS_DISABLED) != 0;
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    fill_rect(dc, &r, bg_color(bg));
    COLORREF fill = C_SUPERFICIE, border = C_LINEA, text = C_TINTA;
    switch (kind) {
    case BTN_PRIMARY:
        fill = border = (hover || down) ? C_ROJO_HOVER : C_ROJO;
        text = C_BLANCO;
        break;
    case BTN_GHOST:
        fill = bg_color(bg);
        border = hover ? C_LINEA : fill;
        text = hover ? C_TINTA : C_GRAFITO;
        break;
    case BTN_ON_BAND:
        fill = C_BANDA;
        border = hover ? RGB(0xC7, 0x7B, 0x83) : C_BANDA;
        text = C_BLANCO;
        break;
    case BTN_ON_BAND_FILLED:
        fill = hover ? RGB(0xFF, 0xFF, 0xFF) : RGB(0xF7, 0xED, 0xEE);
        border = fill;
        text = C_BANDA;
        break;
    default:
        if (hover) border = C_GRAFITO;
        break;
    }
    if (disabled) {
        text = C_PLACEHOLDER;
        if (kind == BTN_PRIMARY) fill = border = C_LINEA;
    }
    round_rect(dc, &r, S(8), fill, border, S(1));
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT)) {
        RECT f = r;
        InflateRect(&f, -S(2), -S(2));
        HPEN pen = CreatePen(PS_INSIDEFRAME, S(2), kind == BTN_PRIMARY ? C_BLANCO : C_TAC);
        HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, f.left, f.top, f.right, f.bottom, S(6), S(6));
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
    }
    wchar_t t[128];
    GetWindowTextW(di->hwndItem, t, COUNTOF(t));
    text_line(dc, t, &r, big ? F_H3 : F_BTN, text, DT_CENTER);
}
