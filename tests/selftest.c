/* Pruebas del núcleo. Uso: selftest.exe [--red]  (con --red también consulta Wikipedia y GitHub) */
#include "../src/common.h"
#include "../src/imei.h"
#include "../src/tacdb.h"
#include "../src/wiki.h"
#include "../src/update.h"
#include "../src/makers.h"

static int fails;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (cond) printf("  ok    ");            \
        else {                                   \
            printf("  FALLO ");                  \
            fails++;                             \
        }                                        \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

HINSTANCE g_inst;

static void print_specs(const Specs *s) {
    printf("    [%s] %s%s | fecha: %s | %d campos | web: %s\n", s->idioma, s->titulo, s->aproximado ? " (aprox.)" : "", s->fecha ? s->fecha : "-", s->n,
           s->web ? s->web : "-");
    for (int i = 0; i < s->n && i < 4; i++) printf("      %s: %.70s\n", s->f[i].campo, s->f[i].valor);
}

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    int net = argc > 1 && !wcscmp(argv[1], L"--red");
    if (argc > 2 && !wcscmp(argv[1], L"--probar-actualizacion")) {
        /* Prueba la descarga y sustitución del propio .exe (el archivo descargado reemplaza a selftest.exe) */
        UpdateInfo u = {"9.9.9", ""};
        char *url = w2u(argv[2]);
        safe_copy(u.url, sizeof(u.url), url);
        free(url);
        wchar_t err[512] = L"";
        int ok = update_apply(&u, err, COUNTOF(err));
        printf(ok ? "actualizacion aplicada\n" : "error: %ls\n", err);
        return ok ? 0 : 1;
    }

    printf("IMEI\n");
    ImeiInfo a;
    imei_analyze(L"35 109121 489937 3", &a);
    CHECK(a.complete && a.valid && !strcmp(a.imei, "351091214899373"), "IMEI válido con espacios");
    imei_analyze(L"351091214899374", &a);
    CHECK(a.complete && !a.valid && a.expected == '3', "dígito de control incorrecto (debería ser 3)");
    imei_analyze(L"35109121489937", &a);
    CHECK(a.complete && a.valid && a.note == NOTE_COMPUTED14 && !strcmp(a.imei, "351091214899373"), "14 dígitos: se calcula el control");
    imei_analyze(L"3510912148993701", &a);
    CHECK(a.note == NOTE_IMEISV && !strcmp(a.imei, "351091214899373"), "IMEISV de 16 dígitos");
    imei_analyze(L"3510912", &a);
    CHECK(!a.complete && a.ndig == 7, "incompleto");

    printf("Base TAC\n");
    CHECK(tacdb_init(), "base incluida cargada");
    unsigned n;
    tacdb_counts(&n, NULL);
    CHECK(n > 250000, "%u TAC", n);
    Device d;
    CHECK(tacdb_lookup("35109121", &d) && !strcmp(d.name, "Apple iPhone 15 Pro") && !strcmp(d.ref, "A3102"), "35109121 -> %s (%s, %s)", d.name, d.ref, d.year);
    CHECK(tacdb_lookup("01006860", &d) && !strcmp(d.brand, "Ericsson"), "TAC con cero inicial -> %s", d.name);
    CHECK(!tacdb_lookup("99999999", &d), "TAC inexistente");

    printf("Utilidades\n");
    CHECK(version_compare("1.10.0", "1.9.3") > 0 && version_compare("v1.2.0", "1.2.0") == 0, "comparación de versiones");
    CHECK(is_official_url("https://www.samsung.com/us/x", "Samsung") && !is_official_url("https://web.archive.org/x", "Apple"), "dominios oficiales");
    CHECK(wiki_title_score("IPhone 15 Pro", "Apple iPhone 15 Pro", "Apple") > 0 && wiki_title_score("IPhone 14 Pro", "Apple iPhone 15 Pro", "Apple") < 0,
          "puntuación de títulos");
    const char *html = "<table class=\"infobox hproduct\"><tbody><tr><th class=\"infobox-label\">First released</th><td>September 22, 2023<span style=\"display:none\"> (<span>2023-09-22</span>)</span><sup class=\"reference\">[1]</sup></td></tr>"
                       "<tr><th>Display</th><td>6.1&#160;in<br>OLED &amp; 120&nbsp;Hz</td></tr><tr><th>Weight</th><td>187 g</td></tr>"
                       "<tr><th>Website</th><td><a href=\"https://www.apple.com/iphone/\">apple.com</a></td></tr></tbody></table>";
    Specs s = {0};
    wiki_parse_infobox(html, "en", &s);
    CHECK(s.fecha && !strcmp(s.fecha, "22 de septiembre de 2023"), "fecha de lanzamiento: %s", s.fecha ? s.fecha : "-");
    CHECK(s.n == 2 && !strcmp(s.f[0].campo, "Pantalla") && !strcmp(s.f[0].valor, "6.1 in\nOLED & 120 Hz"), "infobox: %d campos, pantalla «%s»", s.n,
          s.n ? s.f[0].valor : "-");
    CHECK(s.web && !strcmp(s.web, "https://www.apple.com/iphone/"), "web del fabricante");
    specs_free(&s);

    if (net) {
        printf("Red (Wikipedia)\n");
        static const char *models[][2] = {{"Apple", "Apple iPhone 15 Pro"}, {"Samsung", "Samsung Galaxy A55"}, {"Samsung", "Samsung Galaxy M31"},
                                          {"Xiaomi", "Xiaomi Redmi Note 14S"}, {"Apple", "Apple iPhone 6s"}};
        for (size_t i = 0; i < COUNTOF(models); i++) {
            DWORD t0 = GetTickCount();
            int r = wiki_specs(models[i][0], models[i][1], &s);
            printf("  %s -> %s (%lu ms)\n", models[i][1], r == WIKI_FOUND ? "encontrada" : r == WIKI_NOT_FOUND ? "sin ficha" : "ERROR DE RED",
                   GetTickCount() - t0);
            if (r == WIKI_FOUND) print_specs(&s);
            if (i == 0) CHECK(r == WIKI_FOUND && s.fecha && !strcmp(s.fecha, "22 de septiembre de 2023") && s.n >= 10, "iPhone 15 Pro con fecha y ficha completa");
            if (i == 1) CHECK(r == WIKI_FOUND && s.n >= 10, "Galaxy A55 encontrada (redirige a «Samsung Galaxy A55 5G»)");
            specs_free(&s);
        }
        printf("Red (GitHub)\n");
        UpdateInfo u;
        int r = update_check(&u);
        CHECK(r != UPDATE_ERROR, "comprobación de actualizaciones (resultado %d, última %s)", r, u.version[0] ? u.version : "ninguna");
    }
    printf(fails ? "\n%d pruebas fallidas\n" : "\nTodo correcto\n", fails);
    return fails ? 1 : 0;
}
