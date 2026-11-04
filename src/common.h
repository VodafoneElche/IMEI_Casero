/* Utilidades comunes: cadenas, conversión UTF-8/UTF-16, archivos y rutas. */
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif
#define APP_NAME_W L"Comprobador IMEI"
#define GITHUB_REPO "VodafoneElche/IMEI_Casero"
#define UPDATE_ASSET "ComprobadorIMEI.exe"
#define USER_AGENT_W L"ComprobadorIMEI/" APP_VERSION_W L" (+https://github.com/VodafoneElche/IMEI_Casero)"
#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)
#define APP_VERSION_W WIDEN(APP_VERSION)

#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

/* Búfer dinámico de bytes (siempre terminado en \0). */
typedef struct {
    char *p;
    size_t n, cap;
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_add(Buf *b, const void *data, size_t len);
void buf_adds(Buf *b, const char *s);
void buf_addc(Buf *b, char c);
void buf_printf(Buf *b, const char *fmt, ...);
char *buf_take(Buf *b); /* devuelve la cadena y deja el búfer vacío */

char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void safe_copy(char *dst, size_t cap, const char *src);
int starts_with_ci(const char *s, const char *prefix);
int str_contains_ci(const char *hay, const char *needle);

wchar_t *u2w(const char *s); /* malloc */
char *w2u(const wchar_t *s); /* malloc */

/* %APPDATA%\Comprobador IMEI (se crea si no existe) */
const wchar_t *app_dir(void);
void app_path(wchar_t *out, size_t cap, const wchar_t *name);

int read_file(const wchar_t *path, Buf *out);
int write_file_atomic(const wchar_t *path, const void *data, size_t len);

/* Fechas en ISO 8601 UTC ("2026-11-04T09:12:00Z") */
void now_iso(char out[24]);
int iso_to_filetime(const char *iso, FILETIME *ft);
void iso_to_local_text(const char *iso, wchar_t *out, size_t cap, int withTime);
