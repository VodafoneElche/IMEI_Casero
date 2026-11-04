/* Ficha técnica y fecha de lanzamiento a partir de Wikipedia (inglés y, si no, español). */
#pragma once
#include "store.h"

enum { WIKI_FOUND = 1, WIKI_NOT_FOUND = 0, WIKI_NET_ERROR = -1 };

int wiki_specs(const char *brand, const char *name, Specs *out);

/* Utilidades expuestas para pruebas */
int wiki_parse_infobox(const char *html, const char *lang, Specs *out);
int wiki_title_score(const char *title, const char *name, const char *brand);
