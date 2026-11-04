/* Fabricantes: nombre y dominios de su web oficial. */
#pragma once
#include "common.h"

typedef struct {
    const char *key;
    const char *maker;
    const char *domains[3];
} Maker;

const Maker *maker_for(const char *brand); /* nunca NULL */
const char *maker_name(const char *brand);
int is_apple(const char *brand);
int is_official_url(const char *url, const char *brand);
char *official_search_url(const char *name, const char *brand); /* malloc */
