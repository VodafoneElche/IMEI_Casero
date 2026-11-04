/* Base de datos TAC: la incluida en el .exe (recurso) y una opcional importada por el usuario. */
#pragma once
#include "common.h"

enum { TAC_SRC_NONE = 0, TAC_SRC_BUNDLED = 1, TAC_SRC_IMPORTED = 2 };

typedef struct {
    char brand[64];
    char name[160];
    char ref[200];
    char year[8];
    int source;
} Device;

int tacdb_init(void);
int tacdb_lookup(const char *tac8, Device *out);
void tacdb_counts(unsigned *bundled, unsigned *imported);
/* Nombre del archivo importado y fecha ISO (cadenas vacías si no hay importación). */
void tacdb_import_info(wchar_t *name, size_t ncap, char date[24]);
/* Devuelve el número de TAC importados, o -1 con el motivo en err. */
int tacdb_import_csv(const wchar_t *path, wchar_t *err, size_t errcap);
void tacdb_clear_import(void);
