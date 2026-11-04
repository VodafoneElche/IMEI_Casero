#include "update.h"
#include "net.h"
#include "../vendor/cjson/cJSON.h"

static void parse_version(const char *s, int v[3]) {
    v[0] = v[1] = v[2] = 0;
    while (*s && (*s < '0' || *s > '9')) s++;
    sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
}

int version_compare(const char *a, const char *b) {
    int x[3], y[3];
    parse_version(a, x);
    parse_version(b, y);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

int update_check(UpdateInfo *out) {
    memset(out, 0, sizeof(*out));
    Buf body;
    int status = 0;
    if (!http_get("https://api.github.com/repos/" GITHUB_REPO "/releases/latest", L"application/vnd.github+json", &body, &status) || status != 200) {
        buf_free(&body);
        return status == 404 ? UPDATE_NONE : UPDATE_ERROR; /* 404: todavía no hay ninguna versión publicada */
    }
    cJSON *root = cJSON_Parse(body.p);
    buf_free(&body);
    int result = UPDATE_ERROR;
    cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
    if (cJSON_IsString(tag)) {
        const char *v = tag->valuestring;
        while (*v && (*v < '0' || *v > '9')) v++;
        safe_copy(out->version, sizeof(out->version), v);
        result = UPDATE_NONE;
        if (version_compare(out->version, APP_VERSION) > 0) {
            cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets"), *a;
            cJSON_ArrayForEach(a, assets) {
                cJSON *n = cJSON_GetObjectItemCaseSensitive(a, "name");
                cJSON *u = cJSON_GetObjectItemCaseSensitive(a, "browser_download_url");
                if (cJSON_IsString(n) && cJSON_IsString(u) && !_stricmp(n->valuestring, UPDATE_ASSET)) {
                    safe_copy(out->url, sizeof(out->url), u->valuestring);
                    result = UPDATE_AVAILABLE;
                }
            }
        }
    }
    cJSON_Delete(root);
    return result;
}

static void exe_paths(wchar_t *exe, wchar_t *neu, wchar_t *old) {
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (neu) swprintf(neu, MAX_PATH + 8, L"%ls.new", exe);
    if (old) swprintf(old, MAX_PATH + 8, L"%ls.old", exe);
}

int update_apply(const UpdateInfo *info, wchar_t *err, size_t errcap) {
    wchar_t exe[MAX_PATH], neu[MAX_PATH + 8], old[MAX_PATH + 8];
    exe_paths(exe, neu, old);
    int status = 0;
    if (!http_download(info->url, neu, &status)) {
        swprintf(err, errcap, L"No se ha podido descargar la versión nueva (%d). Comprueba la conexión a internet.", status);
        return 0;
    }
    /* comprobación mínima: es un ejecutable de Windows y tiene un tamaño razonable */
    Buf b;
    int good = read_file(neu, &b) && b.n > 1024 * 1024 && b.p[0] == 'M' && b.p[1] == 'Z';
    buf_free(&b);
    if (!good) {
        DeleteFileW(neu);
        swprintf(err, errcap, L"El archivo descargado no es válido. Inténtalo más tarde.");
        return 0;
    }
    DeleteFileW(old);
    if (!MoveFileExW(exe, old, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(neu);
        swprintf(err, errcap, L"No se puede actualizar en esta carpeta (sin permiso de escritura). Instala la versión nueva con el instalador.");
        return 0;
    }
    if (!MoveFileExW(neu, exe, MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old, exe, MOVEFILE_REPLACE_EXISTING);
        swprintf(err, errcap, L"No se ha podido sustituir el programa. Se mantiene la versión actual.");
        return 0;
    }
    return 1;
}

void update_relaunch(void) {
    wchar_t exe[MAX_PATH];
    exe_paths(exe, NULL, NULL);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    wchar_t cmd[MAX_PATH + 32];
    swprintf(cmd, COUNTOF(cmd), L"\"%ls\" --actualizado", exe);
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void update_cleanup(void) {
    wchar_t exe[MAX_PATH], old[MAX_PATH + 8];
    exe_paths(exe, NULL, old);
    DeleteFileW(old);
}
