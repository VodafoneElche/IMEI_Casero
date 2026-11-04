/* Actualización automática desde las Releases de GitHub. */
#pragma once
#include "common.h"

typedef struct {
    char version[32]; /* "1.2.0" */
    char url[1024];   /* descarga del .exe */
} UpdateInfo;

enum { UPDATE_ERROR = -1, UPDATE_NONE = 0, UPDATE_AVAILABLE = 1 };

int version_compare(const char *a, const char *b);
int update_check(UpdateInfo *out);
/* Descarga el .exe nuevo y lo pone en lugar del actual. 1 = listo para reiniciar. */
int update_apply(const UpdateInfo *info, wchar_t *err, size_t errcap);
void update_relaunch(void);
void update_cleanup(void);
