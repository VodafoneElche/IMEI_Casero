/* HTTPS con WinHTTP (usa el proxy y los certificados de Windows). */
#pragma once
#include "common.h"

/* Devuelve 1 si hubo respuesta (mira *status), 0 si falló la conexión. */
int http_get(const char *url, const wchar_t *accept, Buf *out, int *status);
int http_download(const char *url, const wchar_t *path, int *status);
void url_encode(Buf *b, const char *s);
