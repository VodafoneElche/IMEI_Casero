/* Logo de la empresa en la cabecera (PNG o JPG elegido por el usuario). */
#pragma once
#include "common.h"

void logo_reload(void);
int logo_present(void);
/* Dibuja el logo con alto h (máximo ancho maxW). Devuelve 1 si había logo y deja en *right el borde derecho. */
int logo_draw(HDC dc, int x, int y, int h, int maxW, int *right);
int logo_set_from_file(const wchar_t *path, wchar_t *err, size_t errcap);
void logo_remove(void);
