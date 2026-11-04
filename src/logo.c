#include "logo.h"
#include "store.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../vendor/stb/stb_image.h"

static unsigned char *g_px; /* RGBA premultiplicado */
static int g_w, g_h;
static HBITMAP g_scaled;
static int g_scaledW, g_scaledH;

static void free_scaled(void) {
    if (g_scaled) DeleteObject(g_scaled);
    g_scaled = NULL;
    g_scaledW = g_scaledH = 0;
}

static unsigned char *decode(const Buf *b, int *w, int *h) {
    int n;
    unsigned char *p = stbi_load_from_memory((const unsigned char *)b->p, (int)b->n, w, h, &n, 4);
    if (!p) return NULL;
    for (int i = 0; i < *w * *h; i++) { /* premultiplicar alfa */
        unsigned a = p[i * 4 + 3];
        p[i * 4 + 0] = (unsigned char)(p[i * 4 + 0] * a / 255);
        p[i * 4 + 1] = (unsigned char)(p[i * 4 + 1] * a / 255);
        p[i * 4 + 2] = (unsigned char)(p[i * 4 + 2] * a / 255);
    }
    return p;
}

void logo_reload(void) {
    wchar_t path[MAX_PATH];
    Buf b;
    free_scaled();
    if (g_px) stbi_image_free(g_px);
    g_px = NULL;
    logo_file(path, MAX_PATH);
    if (!read_file(path, &b)) return;
    g_px = decode(&b, &g_w, &g_h);
    buf_free(&b);
}

int logo_present(void) {
    return g_px != NULL;
}

/* Reescalado por promedio de área (se ve bien al reducir logos grandes). */
static HBITMAP make_scaled(int dw, int dh) {
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), dw, -dh, 1, 32, BI_RGB}};
    void *bits;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) return NULL;
    unsigned char *out = bits;
    for (int y = 0; y < dh; y++) {
        int sy0 = y * g_h / dh, sy1 = (y + 1) * g_h / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = x * g_w / dw, sx1 = (x + 1) * g_w / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < g_h; sy++)
                for (int sx = sx0; sx < sx1 && sx < g_w; sx++) {
                    const unsigned char *p = g_px + ((size_t)sy * g_w + sx) * 4;
                    r += p[0], g += p[1], b += p[2], a += p[3], n++;
                }
            unsigned char *o = out + ((size_t)y * dw + x) * 4;
            o[0] = (unsigned char)(b / n); /* BGRA */
            o[1] = (unsigned char)(g / n);
            o[2] = (unsigned char)(r / n);
            o[3] = (unsigned char)(a / n);
        }
    }
    return bmp;
}

int logo_draw(HDC dc, int x, int y, int h, int maxW, int *right) {
    if (!g_px || !g_h) return 0;
    int w = (int)((long long)g_w * h / g_h);
    if (w > maxW) {
        w = maxW;
        int nh = (int)((long long)g_h * w / g_w);
        y += (h - nh) / 2;
        h = nh;
    }
    if (w < 1 || h < 1) return 0;
    if (!g_scaled || g_scaledW != w || g_scaledH != h) {
        free_scaled();
        g_scaled = make_scaled(w, h);
        g_scaledW = w;
        g_scaledH = h;
    }
    if (!g_scaled) return 0;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ ob = SelectObject(mem, g_scaled);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, ob);
    DeleteDC(mem);
    if (right) *right = x + w;
    return 1;
}

int logo_set_from_file(const wchar_t *path, wchar_t *err, size_t errcap) {
    Buf b;
    if (!read_file(path, &b)) {
        swprintf(err, errcap, L"No se ha podido leer el archivo.");
        return 0;
    }
    if (b.n > 4 * 1024 * 1024) {
        buf_free(&b);
        swprintf(err, errcap, L"El archivo pesa más de 4 MB. Usa una versión más ligera del logo.");
        return 0;
    }
    int w, h;
    unsigned char *p = decode(&b, &w, &h);
    if (!p) {
        buf_free(&b);
        swprintf(err, errcap, L"No se reconoce la imagen. Usa un PNG o un JPG (SVG no está admitido).");
        return 0;
    }
    stbi_image_free(p);
    wchar_t dest[MAX_PATH];
    logo_file(dest, MAX_PATH);
    int ok = write_file_atomic(dest, b.p, b.n);
    buf_free(&b);
    if (!ok) {
        swprintf(err, errcap, L"No se ha podido guardar el logo.");
        return 0;
    }
    logo_reload();
    return 1;
}

void logo_remove(void) {
    wchar_t dest[MAX_PATH];
    logo_file(dest, MAX_PATH);
    DeleteFileW(dest);
    logo_reload();
}
