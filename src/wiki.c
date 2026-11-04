#include "wiki.h"
#include "net.h"
#include "../vendor/cjson/cJSON.h"

static const char *MONTHS_ES[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio",
                                  "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};

typedef struct {
    const char *keys[5];
    const char *es;
    int kind; /* 1 = fecha de lanzamiento, 2 = fecha (fin de comercialización) */
} Field;

static const Field EN_FIELDS[] = {
    {{"first released", "released", "release date", "launch date"}, "Fecha de lanzamiento", 1},
    {{"discontinued"}, "Fin de comercializaci\xC3\xB3n", 2},
    {{"display", "screen"}, "Pantalla", 0},
    {{"system on chip", "soc", "chipset"}, "Procesador (SoC)", 0},
    {{"cpu"}, "CPU", 0},
    {{"gpu"}, "GPU", 0},
    {{"memory", "ram"}, "RAM", 0},
    {{"storage"}, "Almacenamiento", 0},
    {{"rear camera", "primary camera", "camera"}, "C\xC3\xA1mara trasera", 0},
    {{"front camera", "secondary camera"}, "C\xC3\xA1mara frontal", 0},
    {{"battery"}, "Bater\xC3\xAD" "a", 0},
    {{"charging"}, "Carga", 0},
    {{"operating system", "os"}, "Sistema operativo", 0},
    {{"compatible networks", "networks"}, "Redes", 0},
    {{"connectivity"}, "Conectividad", 0},
    {{"modem"}, "M\xC3\xB3" "dem", 0},
    {{"sim"}, "SIM", 0},
    {{"water resistance"}, "Resistencia al agua", 0},
    {{"dimensions"}, "Dimensiones", 0},
    {{"weight", "mass"}, "Peso", 0},
    {{"colors", "colours"}, "Colores", 0},
    {{"model"}, "Referencias", 0},
};

static const Field ES_FIELDS[] = {
    {{"fecha de lanzamiento", "lanzamiento", "publicaci\xC3\xB3n"}, "Fecha de lanzamiento", 1},
    {{"pantalla"}, "Pantalla", 0},
    {{"procesador", "soc", "chipset", "cpu"}, "Procesador (SoC)", 0},
    {{"gpu"}, "GPU", 0},
    {{"memoria", "ram"}, "RAM", 0},
    {{"almacenamiento"}, "Almacenamiento", 0},
    {{"c\xC3\xA1mara", "c\xC3\xA1mara trasera", "c\xC3\xA1mara principal"}, "C\xC3\xA1mara trasera", 0},
    {{"c\xC3\xA1mara frontal", "c\xC3\xA1mara secundaria"}, "C\xC3\xA1mara frontal", 0},
    {{"bater\xC3\xAD" "a"}, "Bater\xC3\xAD" "a", 0},
    {{"sistema operativo"}, "Sistema operativo", 0},
    {{"conectividad", "redes"}, "Conectividad", 0},
    {{"dimensiones"}, "Dimensiones", 0},
    {{"peso"}, "Peso", 0},
};

/* ------------------------------------------------------------ normalización */

/* minúsculas ASCII, letras/dígitos/UTF-8 se conservan, lo demás pasa a espacio; espacios colapsados */
static char *norm(const char *s) {
    Buf b;
    buf_init(&b);
    buf_add(&b, "", 0);
    int space = 1;
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        unsigned c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        int keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80;
        if (c == 0xC2 && p[1] == 0xA0) { /* espacio duro */
            p++;
            keep = 0;
        }
        if (keep) {
            buf_addc(&b, (char)c);
            space = 0;
        } else if (!space) {
            buf_addc(&b, ' ');
            space = 1;
        }
    }
    while (b.n && b.p[b.n - 1] == ' ') b.p[--b.n] = 0;
    return buf_take(&b);
}

#define MAX_TOK 24
typedef struct {
    char *store;
    char *t[MAX_TOK];
    int n;
} Tokens;

static void tokenize(const char *s, Tokens *tk) {
    tk->store = norm(s);
    tk->n = 0;
    for (char *p = strtok(tk->store, " "); p && tk->n < MAX_TOK; p = strtok(NULL, " ")) tk->t[tk->n++] = p;
}

static int tk_has(const Tokens *tk, const char *w) {
    for (int i = 0; i < tk->n; i++)
        if (!strcmp(tk->t[i], w)) return 1;
    return 0;
}

static int has_digit(const char *s) {
    for (; *s; s++)
        if (*s >= '0' && *s <= '9') return 1;
    return 0;
}

int wiki_title_score(const char *title, const char *name, const char *brand) {
    Tokens tt, nt;
    char *b = norm(brand);
    tokenize(title, &tt);
    tokenize(name, &nt);
    int score = 0, hit = 0, extra = 0, ok = 1;
    for (int i = 0; i < nt.n; i++) /* el número de modelo tiene que estar en el título */
        if (strcmp(nt.t[i], b) && has_digit(nt.t[i]) && !tk_has(&tt, nt.t[i])) ok = 0;
    if (ok) {
        for (int i = 0; i < nt.n; i++)
            if (strcmp(nt.t[i], b) && tk_has(&tt, nt.t[i])) hit++;
        for (int i = 0; i < tt.n; i++)
            if (strcmp(tt.t[i], b) && !tk_has(&nt, tt.t[i])) extra++;
        score = hit * 3 - extra;
    } else {
        score = -1;
    }
    free(tt.store);
    free(nt.store);
    free(b);
    return score;
}

/* ------------------------------------------------------------ HTML */

static int is_tag(const char *p, const char *name) {
    size_t n = strlen(name);
    if (p[0] != '<') return 0;
    if (!starts_with_ci(p + 1, name)) return 0;
    char c = p[1 + n];
    return c == ' ' || c == '>' || c == '/' || c == '\t' || c == '\n' || c == '\r';
}

static int is_close(const char *p, const char *name) {
    size_t n = strlen(name);
    return p[0] == '<' && p[1] == '/' && starts_with_ci(p + 2, name) && (p[2 + n] == '>' || p[2 + n] == ' ');
}

static const char *tag_end(const char *p, const char *end) {
    char q = 0;
    for (; p < end; p++) {
        if (q) {
            if (*p == q) q = 0;
        } else if (*p == '"' || *p == '\'') {
            q = *p;
        } else if (*p == '>') {
            return p + 1;
        }
    }
    return end;
}

/* Desde justo después de la etiqueta de apertura, devuelve el inicio de la etiqueta de cierre equivalente. */
static const char *find_close(const char *p, const char *end, const char *name) {
    int depth = 1;
    while (p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) return end;
        if (is_close(lt, name)) {
            if (--depth == 0) return lt;
        } else if (is_tag(lt, name)) {
            depth++;
        }
        p = lt + 1;
    }
    return end;
}

static const char *find_tag(const char *p, const char *end, const char *name) {
    while (p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) return NULL;
        if (is_tag(lt, name)) return lt;
        p = lt + 1;
    }
    return NULL;
}

static void put_utf8(Buf *b, unsigned cp) {
    if (cp < 0x80) buf_addc(b, (char)cp);
    else if (cp < 0x800) {
        buf_addc(b, (char)(0xC0 | (cp >> 6)));
        buf_addc(b, (char)(0x80 | (cp & 63)));
    } else if (cp < 0x10000) {
        buf_addc(b, (char)(0xE0 | (cp >> 12)));
        buf_addc(b, (char)(0x80 | ((cp >> 6) & 63)));
        buf_addc(b, (char)(0x80 | (cp & 63)));
    } else {
        buf_addc(b, (char)(0xF0 | (cp >> 18)));
        buf_addc(b, (char)(0x80 | ((cp >> 12) & 63)));
        buf_addc(b, (char)(0x80 | ((cp >> 6) & 63)));
        buf_addc(b, (char)(0x80 | (cp & 63)));
    }
}

static const char *entity(const char *p, const char *end, Buf *b) {
    static const struct { const char *n; unsigned cp; } named[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}, {"ndash", 0x2013},
        {"mdash", 0x2014}, {"minus", 0x2212}, {"times", 0xD7}, {"deg", 0xB0}, {"micro", 0xB5}, {"hellip", 0x2026},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022},
        {"middot", 0xB7}, {"thinsp", ' '}, {"ensp", ' '}, {"emsp", ' '}, {"shy", 0}, {"zwj", 0}, {"zwnj", 0}, {"prime", 0x2032},
        {"Prime", 0x2033}, {"frac12", 0xBD}, {"plusmn", 0xB1},
    };
    const char *semi = memchr(p, ';', (size_t)(end - p) < 12 ? (size_t)(end - p) : 12);
    if (!semi) {
        buf_addc(b, '&');
        return p + 1;
    }
    unsigned cp = 0;
    int ok = 0;
    if (p[1] == '#') {
        char *e;
        cp = (p[2] == 'x' || p[2] == 'X') ? (unsigned)strtoul(p + 3, &e, 16) : (unsigned)strtoul(p + 2, &e, 10);
        ok = e == semi && cp > 0 && cp < 0x110000;
    } else {
        size_t len = (size_t)(semi - p - 1);
        for (size_t i = 0; i < COUNTOF(named); i++)
            if (strlen(named[i].n) == len && !strncmp(named[i].n, p + 1, len)) {
                cp = named[i].cp;
                ok = 1;
                break;
            }
    }
    if (!ok) {
        buf_addc(b, '&');
        return p + 1;
    }
    if (cp == 0xA0 || cp == 0x2009 || cp == 0x202F) cp = ' ';
    if (cp) put_utf8(b, cp);
    return semi + 1;
}

/* Texto visible de un fragmento HTML: sin referencias ni estilos; <br>, <li>, <p>, <div> pasan a salto de línea. */
static char *html_text(const char *p, const char *end, size_t maxLen) {
    Buf raw;
    buf_init(&raw);
    buf_add(&raw, "", 0);
    while (p < end) {
        if (*p == '<') {
            const char *te = tag_end(p, end);
            int closing = p[1] == '/';
            char name[16] = {0};
            int i = 0;
            for (const char *q = p + 1 + closing; q < te && i < 15 && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9')); q++)
                name[i++] = (char)((*q >= 'A' && *q <= 'Z') ? *q + 32 : *q);
            if (!closing && (!strcmp(name, "style") || !strcmp(name, "script"))) {
                p = find_close(te, end, name);
                p = tag_end(p, end);
                continue;
            }
            if (!closing && name[0]) {
                /* elementos que no se muestran como dato */
                Buf attrs;
                buf_init(&attrs);
                buf_add(&attrs, p, (size_t)(te - p));
                int skip = strstr(attrs.p, "class=\"reference") || strstr(attrs.p, " reference\"") || strstr(attrs.p, "noprint") ||
                           strstr(attrs.p, "mw-editsection") || strstr(attrs.p, "mw-ref");
                buf_free(&attrs);
                if (skip && te[-2] != '/') {
                    p = find_close(te, end, name);
                    p = tag_end(p, end);
                    continue;
                }
            }
            if (!strcmp(name, "br") || (!closing && (!strcmp(name, "li") || !strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "tr"))))
                buf_addc(&raw, '\n');
            p = te;
        } else if (*p == '&') {
            p = entity(p, end, &raw);
        } else {
            buf_addc(&raw, *p == '\t' || *p == '\r' ? ' ' : *p);
            p++;
        }
    }
    /* recortar cada línea, colapsar espacios y quitar líneas vacías */
    Buf out;
    buf_init(&out);
    buf_add(&out, "", 0);
    char *line = raw.p;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        Buf l;
        buf_init(&l);
        buf_add(&l, "", 0);
        int sp = 1;
        for (char *c = line; *c; c++) {
            if (*c == ' ') {
                if (!sp) buf_addc(&l, ' ');
                sp = 1;
            } else {
                buf_addc(&l, *c);
                sp = 0;
            }
        }
        while (l.n && l.p[l.n - 1] == ' ') l.p[--l.n] = 0;
        if (l.n) {
            if (out.n) buf_addc(&out, '\n');
            buf_add(&out, l.p, l.n);
        }
        buf_free(&l);
        line = nl ? nl + 1 : NULL;
    }
    buf_free(&raw);
    if (out.n > maxLen) {
        size_t n = maxLen;
        while (n && ((unsigned char)out.p[n] & 0xC0) == 0x80) n--;
        out.p[n] = 0;
        out.n = n;
    }
    return buf_take(&out);
}

static char *format_date_es(int y, int m, int d) {
    char tmp[64];
    if (m < 1 || m > 12) return NULL;
    snprintf(tmp, sizeof(tmp), "%d de %s de %d", d, MONTHS_ES[m - 1], y);
    return xstrdup(tmp);
}

/* "September 22, 2023; 3 years ago (2023-09-22)" -> "22 de septiembre de 2023" */
static char *release_value(const char *text) {
    const char *nl = strchr(text, '\n');
    size_t firstLen = nl ? (size_t)(nl - text) : strlen(text);
    for (int pass = 0; pass < 2; pass++) {
        size_t lim = pass == 0 ? firstLen : strlen(text);
        for (size_t i = 0; i + 12 <= lim; i++) {
            const char *s = text + i;
            int y, m, d;
            char close;
            if (s[0] == '(' && sscanf(s + 1, "%4d-%2d-%2d%c", &y, &m, &d, &close) == 4 && close == ')' && s[5] == '-')
                return format_date_es(y, m, d);
        }
    }
    char *first = xstrndup(text, firstLen);
    char *semi = strchr(first, ';');
    if (semi) *semi = 0;
    size_t n = strlen(first);
    while (n && first[n - 1] == ' ') first[--n] = 0;
    return first;
}

static const Field *match_field(const char *label, const char *lang) {
    const Field *list = !strcmp(lang, "en") ? EN_FIELDS : ES_FIELDS;
    size_t n = !strcmp(lang, "en") ? COUNTOF(EN_FIELDS) : COUNTOF(ES_FIELDS);
    char *l = norm(label);
    const Field *found = NULL;
    for (size_t i = 0; i < n && !found; i++)
        for (int k = 0; k < 5 && list[i].keys[k] && !found; k++) {
            char *kn = norm(list[i].keys[k]);
            if (!strcmp(l, kn)) found = &list[i];
            free(kn);
        }
    free(l);
    return found;
}

static int field_order(const char *campo, const char *lang) {
    const Field *list = !strcmp(lang, "en") ? EN_FIELDS : ES_FIELDS;
    size_t n = !strcmp(lang, "en") ? COUNTOF(EN_FIELDS) : COUNTOF(ES_FIELDS);
    for (size_t i = 0; i < n; i++)
        if (!strcmp(list[i].es, campo)) return (int)i;
    return 99;
}

/* Lee la tabla "infobox" del artículo. Devuelve el número de campos encontrados (sin contar la fecha). */
int wiki_parse_infobox(const char *html, const char *lang, Specs *out) {
    const char *end = html + strlen(html);
    const char *cls = strstr(html, "class=\"infobox");
    if (!cls) return 0;
    const char *t = cls;
    while (t > html && !is_tag(t, "table")) t--;
    if (!is_tag(t, "table")) return 0;
    const char *tbodyStart = tag_end(t, end);
    const char *tEnd = find_close(tbodyStart, end, "table");
    const char *p = tbodyStart;
    char seen[64] = {0};
    while (p < tEnd) {
        const char *tr = find_tag(p, tEnd, "tr");
        if (!tr) break;
        const char *rs = tag_end(tr, tEnd);
        const char *re = find_close(rs, tEnd, "tr");
        p = re + 1;
        const char *th = find_tag(rs, re, "th");
        const char *td = find_tag(rs, re, "td");
        if (!th || !td || td < th) continue;
        const char *ths = tag_end(th, re), *the = find_close(ths, re, "th");
        const char *tds = tag_end(td, re), *tde = find_close(tds, re, "td");
        char *label = html_text(ths, the, 120);
        char *ln = norm(label);
        if (!strcmp(ln, "website") || !strcmp(ln, "sitio web") || !strcmp(ln, "web")) {
            const char *h = strstr(tds, "href=\"http");
            if (h && h < tde && !out->web) {
                const char *q = strchr(h + 6, '"');
                if (q && q < tde) out->web = xstrndup(h + 6, (size_t)(q - h - 6));
            }
            free(ln);
            free(label);
            continue;
        }
        free(ln);
        const Field *f = match_field(label, lang);
        free(label);
        if (!f) continue;
        int idx = (int)(f - (!strcmp(lang, "en") ? EN_FIELDS : ES_FIELDS));
        if (idx < 64 && seen[idx]) continue;
        if (idx < 64) seen[idx] = 1;
        char *text = html_text(tds, tde, 600);
        if (!*text) {
            free(text);
            continue;
        }
        if (f->kind == 1) {
            free(out->fecha);
            out->fecha = release_value(text);
            free(text);
            continue;
        }
        if (f->kind == 2) {
            char *v = release_value(text);
            free(text);
            text = v;
        }
        if (out->n < (int)COUNTOF(out->f)) {
            out->f[out->n].campo = xstrdup(f->es);
            out->f[out->n].valor = text;
            out->n++;
        } else {
            free(text);
        }
    }
    /* mismo orden que la tabla de campos */
    for (int i = 1; i < out->n; i++)
        for (int j = i; j > 0 && field_order(out->f[j - 1].campo, lang) > field_order(out->f[j].campo, lang); j--) {
            SpecField tmp = out->f[j];
            out->f[j] = out->f[j - 1];
            out->f[j - 1] = tmp;
        }
    return out->n;
}

/* ------------------------------------------------------------ API de Wikipedia */

static int api(const char *lang, Buf *query, cJSON **out) {
    Buf url, body;
    int status = 0;
    buf_init(&url);
    buf_printf(&url, "https://%s.wikipedia.org/w/api.php?format=json&formatversion=2&%s", lang, query->p);
    int ok = 0;
    for (int attempt = 0; attempt < 3; attempt++) { /* reintento si Wikipedia limita la frecuencia o falla */
        if (attempt) {
            buf_free(&body);
            Sleep(700 * attempt);
        }
        ok = http_get(url.p, L"application/json", &body, &status);
        if (ok && status != 429 && status < 500) break;
    }
    buf_free(&url);
    *out = NULL;
    if (!ok || status != 200) {
        buf_free(&body);
        return 0;
    }
    *out = cJSON_Parse(body.p);
    buf_free(&body);
    return *out != NULL;
}

/* Pide el primer apartado del artículo. 1 = existe, 0 = no existe, -1 = error de red. */
static int fetch_page(const char *lang, const char *title, char **realTitle, char **html) {
    Buf q;
    buf_init(&q);
    buf_adds(&q, "action=parse&prop=text&section=0&redirects=1&page=");
    url_encode(&q, title);
    cJSON *root;
    int ok = api(lang, &q, &root);
    buf_free(&q);
    if (!ok) return -1;
    cJSON *parse = cJSON_GetObjectItemCaseSensitive(root, "parse");
    cJSON *t = cJSON_GetObjectItemCaseSensitive(parse, "title");
    cJSON *x = cJSON_GetObjectItemCaseSensitive(parse, "text");
    int found = cJSON_IsString(t) && cJSON_IsString(x);
    if (found) {
        *realTitle = xstrdup(t->valuestring);
        *html = xstrdup(x->valuestring);
    }
    cJSON_Delete(root);
    return found;
}

static int search_best(const char *lang, const char *query, const char *name, const char *brand, char **best) {
    Buf q;
    buf_init(&q);
    buf_adds(&q, "action=query&list=search&srlimit=8&srnamespace=0&srsearch=");
    url_encode(&q, query);
    cJSON *root;
    int ok = api(lang, &q, &root);
    buf_free(&q);
    if (!ok) return -1;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "query"), "search");
    int bestScore = 0;
    *best = NULL;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(it, "title");
        if (!cJSON_IsString(t)) continue;
        int s = wiki_title_score(t->valuestring, name, brand);
        if (s > bestScore) {
            bestScore = s;
            free(*best);
            *best = xstrdup(t->valuestring);
        }
    }
    cJSON_Delete(root);
    return *best != NULL;
}

static int add_candidate(char **list, int n, const char *s) {
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i], s)) return n;
    list[n] = xstrdup(s);
    return n + 1;
}

static void wiki_url(const char *lang, const char *title, char **out) {
    Buf b, t;
    buf_init(&b);
    buf_init(&t);
    for (const char *p = title; *p; p++) buf_addc(&t, *p == ' ' ? '_' : *p);
    buf_printf(&b, "https://%s.wikipedia.org/wiki/", lang);
    url_encode(&b, t.p);
    buf_free(&t);
    *out = buf_take(&b);
}

int wiki_specs(const char *brand, const char *name, Specs *out) {
    memset(out, 0, sizeof(*out));
    /* candidatos de título */
    char *cands[8];
    int nc = 0;
    size_t bl = strlen(brand);
    char noBrand[256], withBrand[256], tmp[300];
    safe_copy(noBrand, sizeof(noBrand), (bl && starts_with_ci(name, brand) && name[bl] == ' ') ? name + bl + 1 : name);
    if (bl && starts_with_ci(name, brand)) safe_copy(withBrand, sizeof(withBrand), name);
    else {
        snprintf(tmp, sizeof(tmp), "%s %s", brand, name);
        safe_copy(withBrand, sizeof(withBrand), tmp);
    }
    nc = add_candidate(cands, nc, withBrand);
    nc = add_candidate(cands, nc, noBrand);
    snprintf(tmp, sizeof(tmp), "%s 5G", withBrand);
    nc = add_candidate(cands, nc, tmp);
    snprintf(tmp, sizeof(tmp), "%s 5G", noBrand);
    nc = add_candidate(cands, nc, tmp);
    if (!_stricmp(brand, "redmi") || !_stricmp(brand, "poco")) {
        snprintf(tmp, sizeof(tmp), "Xiaomi %s", noBrand);
        nc = add_candidate(cands, nc, tmp);
        snprintf(tmp, sizeof(tmp), "Xiaomi %s", name);
        nc = add_candidate(cands, nc, tmp);
    }

    static const char *langs[] = {"en", "es"};
    int result = WIKI_NOT_FOUND, netErrors = 0, attempts = 0;
    for (int li = 0; li < 2 && result != WIKI_FOUND; li++) {
        const char *lang = langs[li];
        char *title = NULL, *html = NULL;
        for (int i = 0; i < nc && !title; i++) {
            char *t = NULL, *h = NULL;
            attempts++;
            int r = fetch_page(lang, cands[i], &t, &h);
            if (r < 0) netErrors++;
            if (r == 1 && wiki_title_score(t, name, brand) >= 0) {
                title = t;
                html = h;
            } else {
                free(t);
                free(h);
            }
        }
        if (!title) {
            char *best = NULL;
            attempts++;
            int r = search_best(lang, cands[0], name, brand, &best);
            if (r < 0) netErrors++;
            if (r == 1) {
                attempts++;
                int f = fetch_page(lang, best, &title, &html);
                if (f < 0) netErrors++;
            }
            free(best);
        }
        if (title && html) {
            Specs s = {0};
            int n = wiki_parse_infobox(html, lang, &s);
            if (n >= 2 || s.fecha) {
                *out = s;
                safe_copy(out->idioma, sizeof(out->idioma), lang);
                out->titulo = xstrdup(title);
                wiki_url(lang, title, &out->url);
                /* ¿el artículo es del modelo exacto o agrupa variantes? */
                Tokens want, got;
                char *bn = norm(brand);
                tokenize(name, &want);
                tokenize(title, &got);
                for (int i = 0; i < want.n; i++)
                    if (strcmp(want.t[i], bn) && strcmp(want.t[i], "5g") && !tk_has(&got, want.t[i])) out->aproximado = 1;
                free(want.store);
                free(got.store);
                free(bn);
                result = WIKI_FOUND;
            } else {
                specs_free(&s);
            }
        }
        free(title);
        free(html);
    }
    for (int i = 0; i < nc; i++) free(cands[i]);
    if (result != WIKI_FOUND && netErrors && netErrors == attempts) return WIKI_NET_ERROR;
    return result;
}
