/* Datos locales en %APPDATA%\Comprobador IMEI: historial, caché de fichas y logo. */
#pragma once
#include "common.h"

typedef struct {
    char id[24];
    char fecha[24];       /* ISO UTC */
    char imei[20];
    int valido;
    char marca[64];
    char modelo[160];
    char referencia[200];
    char anio[8];
    char icloud[16];      /* "sin comprobar", "OFF" u "ON" */
    char icloudFecha[24]; /* ISO UTC, vacío si no se ha marcado */
} HistItem;

typedef struct {
    HistItem *items; /* el más reciente primero */
    int n, cap;
} History;

extern History g_hist;

void hist_load(void);
int hist_save(void);
int hist_add(const HistItem *src); /* inserta al principio; devuelve 0 (su índice) */
int hist_find(const char *id);     /* índice o -1 */
int hist_export_csv(const wchar_t *path);
void hist_clear(void);

/* Ficha técnica */
typedef struct {
    char *campo;
    char *valor;
} SpecField;

typedef struct {
    char idioma[4];
    char *titulo, *url, *fecha, *web;
    int aproximado;
    int n;
    SpecField f[40];
} Specs;

void specs_free(Specs *s);
int cache_get(const char *key, Specs *out);
void cache_put(const char *key, const Specs *s);

/* Logo de la empresa (copia del archivo elegido) */
void logo_file(wchar_t *out, size_t cap);
