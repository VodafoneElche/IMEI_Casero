#include "store.h"
#include "../vendor/cjson/cJSON.h"

History g_hist;

/* ------------------------------------------------------------ escape TSV */

static void tsv_put(Buf *b, const char *s) {
    for (; s && *s; s++) {
        if (*s == '\t') buf_adds(b, "\\t");
        else if (*s == '\n') buf_adds(b, "\\n");
        else if (*s == '\r') continue;
        else if (*s == '\\') buf_adds(b, "\\\\");
        else buf_addc(b, *s);
    }
}

/* Separa una línea TSV en campos (modifica la línea). */
static int tsv_split(char *line, char **out, int max) {
    int n = 0;
    char *s = line;
    while (n < max) {
        out[n++] = s;
        char *w = s;
        while (*s && *s != '\t') {
            if (*s == '\\' && s[1]) {
                s++;
                *w++ = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s;
                s++;
            } else {
                *w++ = *s++;
            }
        }
        int last = *s == 0;
        *w = 0;
        if (last) break;
        s++;
    }
    return n;
}

static void hist_path(wchar_t *out) {
    app_path(out, MAX_PATH, L"historial.tsv");
}

static void hist_push_back(const HistItem *it) {
    if (g_hist.n == g_hist.cap) {
        g_hist.cap = g_hist.cap ? g_hist.cap * 2 : 128;
        g_hist.items = realloc(g_hist.items, sizeof(HistItem) * g_hist.cap);
        if (!g_hist.items) abort();
    }
    g_hist.items[g_hist.n++] = *it;
}

static const char *jstr(cJSON *o, const char *k) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

/* Importa el historial de la versión anterior (Electron) la primera vez. */
static void migrate_old_history(void) {
    wchar_t old[MAX_PATH];
    Buf b;
    app_path(old, MAX_PATH, L"historial.json");
    if (!read_file(old, &b)) return;
    cJSON *root = cJSON_Parse(b.p);
    buf_free(&b);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }
    cJSON *o;
    cJSON_ArrayForEach(o, root) {
        HistItem it = {0};
        safe_copy(it.id, sizeof(it.id), jstr(o, "id"));
        const char *f = jstr(o, "fecha");
        safe_copy(it.fecha, 20, f); /* "2026-10-14T12:10:19" */
        if (strlen(it.fecha) == 19) strcat(it.fecha, "Z");
        safe_copy(it.imei, sizeof(it.imei), jstr(o, "imei"));
        it.valido = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "valido"));
        safe_copy(it.marca, sizeof(it.marca), jstr(o, "marca"));
        safe_copy(it.modelo, sizeof(it.modelo), jstr(o, "modelo"));
        safe_copy(it.referencia, sizeof(it.referencia), jstr(o, "referencia"));
        safe_copy(it.anio, sizeof(it.anio), jstr(o, "anio"));
        safe_copy(it.icloud, sizeof(it.icloud), *jstr(o, "icloud") ? jstr(o, "icloud") : "sin comprobar");
        safe_copy(it.icloudFecha, 20, jstr(o, "icloudFecha"));
        if (strlen(it.icloudFecha) == 19) strcat(it.icloudFecha, "Z");
        if (!it.id[0]) snprintf(it.id, sizeof(it.id), "m%d", g_hist.n);
        if (it.imei[0]) hist_push_back(&it);
    }
    cJSON_Delete(root);
    if (g_hist.n) hist_save();
}

void hist_load(void) {
    wchar_t path[MAX_PATH];
    Buf b;
    g_hist.n = 0;
    hist_path(path);
    if (!read_file(path, &b)) {
        migrate_old_history();
        return;
    }
    char *line = b.p;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (*line && *line != '#') {
            char *c[10];
            int n = tsv_split(line, c, 10);
            if (n >= 10) {
                HistItem it = {0};
                safe_copy(it.id, sizeof(it.id), c[0]);
                safe_copy(it.fecha, sizeof(it.fecha), c[1]);
                safe_copy(it.imei, sizeof(it.imei), c[2]);
                it.valido = c[3][0] == '1';
                safe_copy(it.marca, sizeof(it.marca), c[4]);
                safe_copy(it.modelo, sizeof(it.modelo), c[5]);
                safe_copy(it.referencia, sizeof(it.referencia), c[6]);
                safe_copy(it.anio, sizeof(it.anio), c[7]);
                safe_copy(it.icloud, sizeof(it.icloud), c[8]);
                safe_copy(it.icloudFecha, sizeof(it.icloudFecha), c[9]);
                hist_push_back(&it);
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    buf_free(&b);
}

int hist_save(void) {
    wchar_t path[MAX_PATH];
    Buf b;
    buf_init(&b);
    buf_adds(&b, "#historial v1: id fecha imei valido marca modelo referencia anio icloud icloudFecha\n");
    int n = g_hist.n > 5000 ? 5000 : g_hist.n;
    for (int i = 0; i < n; i++) {
        const HistItem *it = &g_hist.items[i];
        const char *f[10] = {it->id, it->fecha, it->imei, it->valido ? "1" : "0", it->marca, it->modelo, it->referencia, it->anio, it->icloud, it->icloudFecha};
        for (int k = 0; k < 10; k++) {
            if (k) buf_addc(&b, '\t');
            tsv_put(&b, f[k]);
        }
        buf_addc(&b, '\n');
    }
    hist_path(path);
    int ok = write_file_atomic(path, b.p, b.n);
    buf_free(&b);
    return ok;
}

int hist_add(const HistItem *src) {
    hist_push_back(src); /* asegura capacidad */
    memmove(&g_hist.items[1], &g_hist.items[0], sizeof(HistItem) * (g_hist.n - 1));
    g_hist.items[0] = *src;
    hist_save();
    return 0;
}

int hist_find(const char *id) {
    for (int i = 0; i < g_hist.n; i++)
        if (!strcmp(g_hist.items[i].id, id)) return i;
    return -1;
}

void hist_clear(void) {
    g_hist.n = 0;
    hist_save();
}

static void csv_cell(Buf *b, const char *s) {
    int quote = strpbrk(s, "\";\n") != NULL;
    if (!quote) {
        buf_adds(b, s);
        return;
    }
    buf_addc(b, '"');
    for (; *s; s++) {
        if (*s == '"') buf_addc(b, '"');
        buf_addc(b, *s);
    }
    buf_addc(b, '"');
}

int hist_export_csv(const wchar_t *path) {
    Buf b;
    buf_init(&b);
    buf_adds(&b, "\xEF\xBB\xBF"); /* BOM para que Excel lea bien los acentos */
    buf_adds(&b, "Fecha;IMEI;IMEI v\xC3\xA1lido;Marca;Modelo;Referencia;A\xC3\xB1o;Estado iCloud (Buscar mi iPhone);Fecha estado iCloud\r\n");
    for (int i = 0; i < g_hist.n; i++) {
        const HistItem *it = &g_hist.items[i];
        wchar_t wf[32], wi[32];
        iso_to_local_text(it->fecha, wf, COUNTOF(wf), 1);
        iso_to_local_text(it->icloudFecha, wi, COUNTOF(wi), 1);
        char *f = w2u(wf), *fi = w2u(wi);
        char imei[32];
        snprintf(imei, sizeof(imei), "=\"%s\"", it->imei); /* que Excel no lo convierta en número */
        const char *cells[9] = {f, imei, it->valido ? "S\xC3\xAD" : "No", it->marca, it->modelo, it->referencia, it->anio, it->icloud, fi};
        for (int k = 0; k < 9; k++) {
            if (k) buf_addc(&b, ';');
            if (k == 1) buf_adds(&b, imei);
            else csv_cell(&b, cells[k]);
        }
        buf_adds(&b, "\r\n");
        free(f);
        free(fi);
    }
    int ok = write_file_atomic(path, b.p, b.n);
    buf_free(&b);
    return ok;
}

/* ------------------------------------------------------------ caché de fichas */

void specs_free(Specs *s) {
    free(s->titulo);
    free(s->url);
    free(s->fecha);
    free(s->web);
    for (int i = 0; i < s->n; i++) {
        free(s->f[i].campo);
        free(s->f[i].valor);
    }
    memset(s, 0, sizeof(*s));
}

static void cache_file(const char *key, wchar_t *out) {
    uint64_t h = 1469598103934665603ULL; /* FNV-1a */
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        unsigned c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        h = (h ^ c) * 1099511628211ULL;
    }
    wchar_t dir[MAX_PATH];
    app_path(dir, MAX_PATH, L"fichas");
    CreateDirectoryW(dir, NULL);
    swprintf(out, MAX_PATH, L"%ls\\%016llx.txt", dir, (unsigned long long)h);
}

int cache_get(const char *key, Specs *out) {
    wchar_t path[MAX_PATH];
    Buf b;
    memset(out, 0, sizeof(*out));
    cache_file(key, path);
    if (!read_file(path, &b)) return 0;
    char *line = b.p;
    int row = 0, ok = 0;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *c[3];
        int n = tsv_split(line, c, 3);
        if (row == 0) {
            ok = n >= 2 && !strcmp(c[0], "#ficha v1") && !strcmp(c[1], key);
            if (!ok) break;
        } else if (n >= 2 && !strcmp(c[0], "idioma")) safe_copy(out->idioma, sizeof(out->idioma), c[1]);
        else if (n >= 2 && !strcmp(c[0], "titulo")) out->titulo = xstrdup(c[1]);
        else if (n >= 2 && !strcmp(c[0], "url")) out->url = xstrdup(c[1]);
        else if (n >= 2 && !strcmp(c[0], "fecha")) out->fecha = *c[1] ? xstrdup(c[1]) : NULL;
        else if (n >= 2 && !strcmp(c[0], "web")) out->web = *c[1] ? xstrdup(c[1]) : NULL;
        else if (n >= 2 && !strcmp(c[0], "aproximado")) out->aproximado = c[1][0] == '1';
        else if (n >= 3 && !strcmp(c[0], "campo") && out->n < (int)COUNTOF(out->f)) {
            out->f[out->n].campo = xstrdup(c[1]);
            out->f[out->n].valor = xstrdup(c[2]);
            out->n++;
        }
        row++;
        line = nl ? nl + 1 : NULL;
    }
    buf_free(&b);
    if (!ok || !out->titulo) {
        specs_free(out);
        return 0;
    }
    return 1;
}

void cache_put(const char *key, const Specs *s) {
    wchar_t path[MAX_PATH];
    Buf b;
    buf_init(&b);
#define KV(k, v)              \
    do {                      \
        buf_adds(&b, k "\t"); \
        tsv_put(&b, v);       \
        buf_addc(&b, '\n');   \
    } while (0)
    KV("#ficha v1", key);
    KV("idioma", s->idioma);
    KV("titulo", s->titulo);
    KV("url", s->url);
    KV("fecha", s->fecha ? s->fecha : "");
    KV("web", s->web ? s->web : "");
    KV("aproximado", s->aproximado ? "1" : "0");
#undef KV
    for (int i = 0; i < s->n; i++) {
        buf_adds(&b, "campo\t");
        tsv_put(&b, s->f[i].campo);
        buf_addc(&b, '\t');
        tsv_put(&b, s->f[i].valor);
        buf_addc(&b, '\n');
    }
    cache_file(key, path);
    write_file_atomic(path, b.p, b.n);
    buf_free(&b);
}

/* ------------------------------------------------------------ logo */

static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* La versión anterior guardaba el logo como data:URL dentro de ajustes.json. */
static void migrate_old_logo(const wchar_t *dest) {
    wchar_t old[MAX_PATH];
    Buf b;
    app_path(old, MAX_PATH, L"ajustes.json");
    if (!read_file(old, &b)) return;
    cJSON *root = cJSON_Parse(b.p);
    buf_free(&b);
    const char *logo = jstr(root, "logo");
    if ((starts_with_ci(logo, "data:image/png;base64,") || starts_with_ci(logo, "data:image/jpeg;base64,"))) {
        const char *s = strchr(logo, ',') + 1;
        Buf out;
        buf_init(&out);
        int acc = 0, bits = 0;
        for (; *s; s++) {
            int v = b64v((unsigned char)*s);
            if (v < 0) continue;
            acc = (acc << 6) | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                buf_addc(&out, (char)((acc >> bits) & 0xFF));
            }
        }
        if (out.n) write_file_atomic(dest, out.p, out.n);
        buf_free(&out);
    }
    cJSON_Delete(root);
}

void logo_file(wchar_t *out, size_t cap) {
    static int migrated;
    swprintf(out, cap, L"%ls\\logo.img", app_dir());
    if (!migrated) {
        migrated = 1;
        if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES) {
            wchar_t flag[MAX_PATH];
            app_path(flag, MAX_PATH, L"logo_migrado.txt");
            if (GetFileAttributesW(flag) == INVALID_FILE_ATTRIBUTES) {
                migrate_old_logo(out);
                write_file_atomic(flag, "1", 1);
            }
        }
    }
}
