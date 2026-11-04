#include "tacdb.h"
#include "resource.h"

/* Formato "TACB": u32 versión, u32 n, u32 tamaño_pool, n × (u32 tac, u32 offset), pool.
 * Cada cadena del pool: marca \x1f nombre \x1f referencia \x1f año \0 (UTF-8). */
typedef struct {
    const unsigned char *recs;
    const char *pool;
    uint32_t n, poolSize;
} TacTable;

static TacTable g_bundled, g_imported;
static Buf g_importedData;

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int table_from_memory(TacTable *t, const unsigned char *data, size_t size) {
    memset(t, 0, sizeof(*t));
    if (size < 16 || memcmp(data, "TACB", 4) != 0 || rd32(data + 4) != 1) return 0;
    uint32_t n = rd32(data + 8), pool = rd32(data + 12);
    if ((uint64_t)16 + (uint64_t)n * 8 + pool > size) return 0;
    t->recs = data + 16;
    t->pool = (const char *)(data + 16 + (size_t)n * 8);
    t->n = n;
    t->poolSize = pool;
    return 1;
}

static const char *table_find(const TacTable *t, uint32_t tac) {
    uint32_t lo = 0, hi = t->n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t v = rd32(t->recs + (size_t)mid * 8);
        if (v == tac) {
            uint32_t off = rd32(t->recs + (size_t)mid * 8 + 4);
            return off < t->poolSize ? t->pool + off : NULL;
        }
        if (v < tac) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

static void import_paths(wchar_t *bin, wchar_t *meta) {
    if (bin) app_path(bin, MAX_PATH, L"tac_importado.bin");
    if (meta) app_path(meta, MAX_PATH, L"tac_importado.txt");
}

static void load_imported(void) {
    wchar_t bin[MAX_PATH];
    import_paths(bin, NULL);
    buf_free(&g_importedData);
    memset(&g_imported, 0, sizeof(g_imported));
    if (read_file(bin, &g_importedData) && !table_from_memory(&g_imported, (unsigned char *)g_importedData.p, g_importedData.n))
        buf_free(&g_importedData);
}

int tacdb_init(void) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_TACDB), RT_RCDATA);
    if (r) {
        HGLOBAL g = LoadResource(NULL, r);
        const void *data = g ? LockResource(g) : NULL;
        if (data) table_from_memory(&g_bundled, data, SizeofResource(NULL, r));
    }
    load_imported();
    return g_bundled.n > 0;
}

static void split_field(const char **p, char *dst, size_t cap) {
    const char *s = *p;
    const char *e = strchr(s, '\x1f');
    size_t len = e ? (size_t)(e - s) : strlen(s);
    char tmp[512];
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, s, len);
    tmp[len] = 0;
    safe_copy(dst, cap, tmp);
    *p = e ? e + 1 : s + strlen(s);
}

int tacdb_lookup(const char *tac8, Device *out) {
    memset(out, 0, sizeof(*out));
    if (strlen(tac8) != 8) return 0;
    uint32_t tac = (uint32_t)strtoul(tac8, NULL, 10);
    const char *s = table_find(&g_imported, tac);
    out->source = TAC_SRC_IMPORTED;
    if (!s) {
        s = table_find(&g_bundled, tac);
        out->source = TAC_SRC_BUNDLED;
    }
    if (!s) {
        out->source = TAC_SRC_NONE;
        return 0;
    }
    split_field(&s, out->brand, sizeof(out->brand));
    split_field(&s, out->name, sizeof(out->name));
    split_field(&s, out->ref, sizeof(out->ref));
    split_field(&s, out->year, sizeof(out->year));
    return out->name[0] != 0;
}

void tacdb_counts(unsigned *bundled, unsigned *imported) {
    if (bundled) *bundled = g_bundled.n;
    if (imported) *imported = g_imported.n;
}

void tacdb_import_info(wchar_t *name, size_t ncap, char date[24]) {
    wchar_t meta[MAX_PATH];
    Buf b;
    name[0] = 0;
    date[0] = 0;
    import_paths(NULL, meta);
    if (!g_imported.n || !read_file(meta, &b)) return;
    char *tab = strchr(b.p, '\t');
    if (tab) {
        *tab = 0;
        safe_copy(date, 24, tab + 1);
        char *nl = strpbrk(date, "\r\n");
        if (nl) *nl = 0;
    }
    wchar_t *w = u2w(b.p);
    wcsncpy(name, w, ncap - 1);
    name[ncap - 1] = 0;
    free(w);
    buf_free(&b);
}

void tacdb_clear_import(void) {
    wchar_t bin[MAX_PATH], meta[MAX_PATH];
    import_paths(bin, meta);
    DeleteFileW(bin);
    DeleteFileW(meta);
    load_imported();
}

/* ---------------------------------------------------------------- Importación CSV */

typedef struct {
    uint32_t tac;
    char *text;
} ImportRec;

static int cmp_rec(const void *a, const void *b) {
    uint32_t x = ((const ImportRec *)a)->tac, y = ((const ImportRec *)b)->tac;
    return x < y ? -1 : x > y;
}

/* Lee un campo CSV (con comillas) y avanza. Devuelve 0 al llegar al fin de línea. */
static int csv_field(const char **pp, const char *end, char sep, Buf *out) {
    const char *p = *pp;
    out->n = 0;
    buf_add(out, "", 0);
    int quoted = 0;
    if (p < end && *p == '"') {
        quoted = 1;
        p++;
    }
    while (p < end) {
        char c = *p;
        if (quoted) {
            if (c == '"' && p + 1 < end && p[1] == '"') {
                buf_addc(out, '"');
                p += 2;
                continue;
            }
            if (c == '"') {
                quoted = 0;
                p++;
                continue;
            }
            buf_addc(out, c);
            p++;
        } else {
            if (c == sep) {
                *pp = p + 1;
                return 1;
            }
            if (c == '\n' || c == '\r') break;
            buf_addc(out, c);
            p++;
        }
    }
    while (p < end && (*p == '\r')) p++;
    if (p < end && *p == '\n') p++;
    *pp = p;
    return 0;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (unsigned char)s[n - 1] <= ' ') s[--n] = 0;
    size_t i = 0;
    while (s[i] && (unsigned char)s[i] <= ' ') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static void lower_ascii_str(char *s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s += 32;
}

#define MAX_COLS 64

int tacdb_import_csv(const wchar_t *path, wchar_t *err, size_t errcap) {
    Buf file, f;
    if (!read_file(path, &file) || !file.n) {
        swprintf(err, errcap, L"No se ha podido leer el archivo o está vacío.");
        buf_free(&file);
        return -1;
    }
    const char *p = file.p, *end = file.p + file.n;
    if (file.n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;

    /* Separador: el más frecuente en la primera línea */
    const char *eol = memchr(p, '\n', (size_t)(end - p));
    if (!eol) eol = end;
    const char seps[] = {',', ';', '\t', '|'};
    char sep = ',';
    int best = -1;
    for (int i = 0; i < 4; i++) {
        int c = 0;
        for (const char *q = p; q < eol; q++) c += *q == seps[i];
        if (c > best) best = c, sep = seps[i];
    }

    buf_init(&f);
    char *head[MAX_COLS] = {0};
    int ncols = 0, more = 1;
    const char *rowStart = p;
    while (more && ncols < MAX_COLS) {
        more = csv_field(&p, end, sep, &f);
        head[ncols] = xstrdup(f.p);
        trim(head[ncols]);
        lower_ascii_str(head[ncols]);
        ncols++;
    }
    int iTac = -1, iBrand = -1, iName = -1, iModel = -1, iDate = -1;
    static const char *brandKeys[] = {"manufacturer", "brand", "marca", "fabricante", "applicant"};
    static const char *nameKeys[] = {"marketing", "nombre comercial", "name", "modelo", "model"};
    for (int i = 0; i < ncols; i++) {
        if (iTac < 0 && strstr(head[i], "tac")) iTac = i;
        for (size_t k = 0; k < COUNTOF(brandKeys) && iBrand < 0; k++)
            if (strstr(head[i], brandKeys[k])) iBrand = i;
        if (iDate < 0 && (strstr(head[i], "allocation date") || strstr(head[i], "fecha") || strstr(head[i], "date"))) iDate = i;
    }
    for (size_t k = 0; k < COUNTOF(nameKeys) && iName < 0; k++)
        for (int i = 0; i < ncols && iName < 0; i++)
            if (strstr(head[i], nameKeys[k])) iName = i;
    for (int i = 0; i < ncols; i++)
        if (i != iName && (strstr(head[i], "model name") || !strcmp(head[i], "model") || !strcmp(head[i], "modelo"))) {
            iModel = i;
            break;
        }
    if (iTac < 0) {
        /* Sin cabecera reconocible: se asume tac, marca, nombre */
        iTac = 0, iBrand = 1, iName = 2, iModel = -1, iDate = -1;
        p = rowStart;
    }
    for (int i = 0; i < ncols; i++) free(head[i]);

    ImportRec *recs = NULL;
    size_t n = 0, cap = 0;
    char *cols[MAX_COLS];
    while (p < end) {
        int nc = 0;
        more = 1;
        while (more && nc < MAX_COLS) {
            more = csv_field(&p, end, sep, &f);
            cols[nc++] = xstrdup(f.p);
        }
        while (more) more = csv_field(&p, end, sep, &f); /* columnas de más */
        char digits[16] = {0};
        int nd = 0;
        if (iTac < nc)
            for (const char *q = cols[iTac]; *q && nd < 15; q++)
                if (*q >= '0' && *q <= '9') digits[nd++] = *q;
        const char *brand = iBrand >= 0 && iBrand < nc ? cols[iBrand] : "";
        const char *name = iName >= 0 && iName < nc ? cols[iName] : "";
        if (nd >= 6 && nd <= 8 && *name) {
            Buf t;
            buf_init(&t);
            char b2[64], firstWord[64];
            safe_copy(b2, sizeof(b2), brand);
            trim(b2);
            safe_copy(firstWord, sizeof(firstWord), b2);
            char *sp = strchr(firstWord, ' ');
            if (sp) *sp = 0;
            buf_adds(&t, b2);
            buf_addc(&t, '\x1f');
            if (b2[0] && !starts_with_ci(name, firstWord)) {
                buf_adds(&t, b2);
                buf_addc(&t, ' ');
            }
            buf_adds(&t, name);
            buf_addc(&t, '\x1f');
            if (iModel >= 0 && iModel < nc) buf_adds(&t, cols[iModel]);
            buf_addc(&t, '\x1f');
            if (iDate >= 0 && iDate < nc) {
                const char *d = cols[iDate];
                for (; *d; d++)
                    if ((d[0] == '1' && d[1] == '9') || (d[0] == '2' && d[1] == '0'))
                        if (d[2] >= '0' && d[2] <= '9' && d[3] >= '0' && d[3] <= '9') {
                            buf_add(&t, d, 4);
                            break;
                        }
            }
            for (char *q = t.p; *q; q++)
                if (*q == '\n' || *q == '\r') *q = ' ';
            if (n == cap) {
                cap = cap ? cap * 2 : 4096;
                recs = realloc(recs, cap * sizeof(*recs));
                if (!recs) abort();
            }
            recs[n].tac = (uint32_t)strtoul(digits, NULL, 10);
            recs[n].text = buf_take(&t);
            n++;
        }
        for (int i = 0; i < nc; i++) free(cols[i]);
    }
    buf_free(&f);
    buf_free(&file);

    if (!n) {
        swprintf(err, errcap, L"No se ha encontrado ninguna fila con un TAC válido (8 dígitos).");
        free(recs);
        return -1;
    }
    qsort(recs, n, sizeof(*recs), cmp_rec);

    Buf out, pool;
    buf_init(&out);
    buf_init(&pool);
    size_t uniq = 0;
    for (size_t i = 0; i < n; i++)
        if (!i || recs[i].tac != recs[i - 1].tac) uniq++;
    uint32_t hdr[3] = {1, (uint32_t)uniq, 0};
    buf_add(&out, "TACB", 4);
    buf_add(&out, hdr, sizeof(hdr));
    for (size_t i = 0; i < n; i++) {
        if (i + 1 < n && recs[i + 1].tac == recs[i].tac) continue; /* duplicado: gana la última fila */
        uint32_t rec[2] = {recs[i].tac, (uint32_t)pool.n};
        buf_add(&out, rec, sizeof(rec));
        buf_add(&pool, recs[i].text, strlen(recs[i].text) + 1);
    }
    uint32_t poolSize = (uint32_t)pool.n;
    memcpy(out.p + 12, &poolSize, 4);
    buf_add(&out, pool.p, pool.n);
    for (size_t i = 0; i < n; i++) free(recs[i].text);
    free(recs);
    buf_free(&pool);

    wchar_t bin[MAX_PATH], meta[MAX_PATH];
    import_paths(bin, meta);
    int ok = write_file_atomic(bin, out.p, out.n);
    buf_free(&out);
    if (!ok) {
        swprintf(err, errcap, L"No se ha podido guardar la base importada.");
        return -1;
    }
    const wchar_t *fname = wcsrchr(path, L'\\');
    char *u = w2u(fname ? fname + 1 : path);
    char iso[24];
    now_iso(iso);
    Buf m;
    buf_init(&m);
    buf_printf(&m, "%s\t%s\n", u, iso);
    write_file_atomic(meta, m.p, m.n);
    buf_free(&m);
    free(u);
    load_imported();
    return (int)uniq;
}
