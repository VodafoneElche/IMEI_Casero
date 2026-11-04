/* Utilidades de dibujo compartidas por la ventana principal y la de ajustes. */
#pragma once
#include "common.h"

/* Paleta en rojos oscuros */
#define C_PAPEL RGB(0x17, 0x0A, 0x0C)
#define C_SUPERFICIE RGB(0x23, 0x10, 0x13)
#define C_LATERAL RGB(0x1D, 0x0C, 0x0F)
#define C_BANDA RGB(0x6E, 0x0B, 0x15)
#define C_ROJO RGB(0xA5, 0x0E, 0x1E)
#define C_ROJO_HOVER RGB(0xBF, 0x14, 0x24)
#define C_TINTA RGB(0xF7, 0xED, 0xEE)
#define C_GRAFITO RGB(0xC4, 0xA3, 0xA7)
#define C_LINEA RGB(0x4A, 0x24, 0x2A)
#define C_TAC RGB(0xF0, 0x62, 0x6D)
#define C_SERIE RGB(0xB8, 0x95, 0x99)
#define C_OK RGB(0x5C, 0xC2, 0x8E)
#define C_OK_SUAVE RGB(0x15, 0x29, 0x1F)
#define C_ERROR RGB(0xFF, 0x8C, 0x7A)
#define C_ERROR_SUAVE RGB(0x4A, 0x1A, 0x16)
#define C_AVISO RGB(0xE6, 0xB6, 0x5C)
#define C_AVISO_SUAVE RGB(0x33, 0x24, 0x0F)
#define C_PLACEHOLDER RGB(0x6B, 0x3B, 0x41)
#define C_BLANCO RGB(0xFF, 0xFF, 0xFF)

enum {
    F_BRAND, F_LABEL, F_INPUT, F_PART, F_SMALL, F_SMALL_B, F_DEVICE, F_BODY, F_BODY_B, F_H3, F_BTN, F_H2, F_COUNT
};

/* Estilos de botón (fondo del padre en los 4 bits altos) */
enum { BTN_NORMAL = 0, BTN_PRIMARY = 1, BTN_GHOST = 2, BTN_ON_BAND = 3, BTN_ON_BAND_FILLED = 4, BTN_BIG = 8 };
enum { BG_PAPEL = 0, BG_LATERAL = 1, BG_SUPERFICIE = 2, BG_BANDA = 3 };
#define BTN_STYLE(kind, bg) ((kind) | ((bg) << 4))

extern HINSTANCE g_inst;
extern UINT g_dpi;
extern HFONT g_font[F_COUNT];

int S(int px);
void ui_fonts_load(void);  /* registra Atkinson Hyperlegible desde los recursos */
void ui_fonts_create(UINT dpi);
void ui_dark_titlebar(HWND hwnd);

void fill_rect(HDC dc, const RECT *r, COLORREF c);
void round_rect(HDC dc, const RECT *r, int radius, COLORREF fill, COLORREF border, int borderW);
void hline(HDC dc, int x0, int x1, int y, COLORREF c);
/* Texto en una línea (con puntos suspensivos si no cabe). */
void text_line(HDC dc, const wchar_t *s, const RECT *r, int font, COLORREF c, UINT align);
/* Texto con saltos de línea en un ancho dado. Devuelve el alto; dibuja solo si paint. */
int text_wrap(HDC dc, const wchar_t *s, int x, int y, int w, int font, COLORREF c, int paint);
int text_width(HDC dc, const wchar_t *s, int font);
int font_height(HDC dc, int font);

HWND make_button(HWND parent, int id, const wchar_t *text, int style);
void draw_button(const DRAWITEMSTRUCT *di);
COLORREF bg_color(int bg);
