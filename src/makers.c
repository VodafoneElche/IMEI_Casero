#include "makers.h"
#include "net.h"

static const Maker MAKERS[] = {
    {"apple", "Apple", {"apple.com"}},
    {"samsung", "Samsung", {"samsung.com"}},
    {"xiaomi", "Xiaomi", {"mi.com", "po.co"}},
    {"redmi", "Xiaomi", {"mi.com"}},
    {"poco", "Xiaomi (POCO)", {"po.co", "mi.com"}},
    {"huawei", "Huawei", {"consumer.huawei.com", "huawei.com"}},
    {"honor", "Honor", {"honor.com"}},
    {"oppo", "OPPO", {"oppo.com"}},
    {"realme", "realme", {"realme.com"}},
    {"vivo", "vivo", {"vivo.com"}},
    {"oneplus", "OnePlus", {"oneplus.com"}},
    {"motorola", "Motorola", {"motorola.com"}},
    {"google", "Google", {"store.google.com", "support.google.com"}},
    {"sony", "Sony", {"sony.com", "sony.es"}},
    {"nokia", "Nokia / HMD", {"hmd.com", "nokia.com"}},
    {"hmd", "HMD", {"hmd.com"}},
    {"tcl", "TCL", {"tcl.com"}},
    {"alcatel", "Alcatel", {"alcatelmobile.com", "tcl.com"}},
    {"zte", "ZTE", {"ztedevices.com", "zte.com.cn"}},
    {"lg", "LG", {"lg.com"}},
    {"nothing", "Nothing", {"nothing.tech"}},
    {"asus", "ASUS", {"asus.com"}},
    {"lenovo", "Lenovo", {"lenovo.com"}},
    {"tecno", "TECNO", {"tecno-mobile.com"}},
    {"infinix", "Infinix", {"infinixmobility.com"}},
    {"itel", "itel", {"itel-life.com"}},
    {"doro", "Doro", {"doro.com"}},
    {"fairphone", "Fairphone", {"fairphone.com"}},
    {"crosscall", "Crosscall", {"crosscall.com"}},
    {"htc", "HTC", {"htc.com"}},
    {"blackview", "Blackview", {"blackview.hk"}},
    {"ulefone", "Ulefone", {"ulefone.com"}},
    {"wiko", "Wiko", {"wikomobile.com"}},
    {"cat", "Cat phones", {"catphones.com"}},
    {"hisense", "Hisense", {"hisense.com"}},
    {"blackberry", "BlackBerry", {"blackberry.com"}},
    {"vodafone", "Vodafone", {"vodafone.es", "vodafone.com"}},
};

const Maker *maker_for(const char *brand) {
    static Maker unknown;
    char key[64];
    int n = 0;
    for (const char *p = brand; p && *p && n < 63; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c >= 'a' && c <= 'z') key[n++] = c;
    }
    key[n] = 0;
    for (size_t i = 0; i < COUNTOF(MAKERS); i++)
        if (!strcmp(MAKERS[i].key, key)) return &MAKERS[i];
    unknown.key = "";
    unknown.maker = brand;
    unknown.domains[0] = NULL;
    return &unknown;
}

const char *maker_name(const char *brand) {
    return maker_for(brand)->maker;
}

int is_apple(const char *brand) {
    return brand && !_stricmp(brand, "apple");
}

int is_official_url(const char *url, const char *brand) {
    const char *h = strstr(url ? url : "", "://");
    if (!h) return 0;
    h += 3;
    char host[256];
    int n = 0;
    while (h[n] && h[n] != '/' && h[n] != ':' && n < 255) {
        host[n] = (h[n] >= 'A' && h[n] <= 'Z') ? h[n] + 32 : h[n];
        n++;
    }
    host[n] = 0;
    const char *hs = strncmp(host, "www.", 4) ? host : host + 4;
    const Maker *m = maker_for(brand);
    for (int i = 0; i < 3 && m->domains[i]; i++) {
        size_t dl = strlen(m->domains[i]), hl = strlen(hs);
        if (!strcmp(hs, m->domains[i])) return 1;
        if (hl > dl && hs[hl - dl - 1] == '.' && !strcmp(hs + hl - dl, m->domains[i])) return 1;
    }
    return 0;
}

char *official_search_url(const char *name, const char *brand) {
    const Maker *m = maker_for(brand);
    Buf q, url;
    buf_init(&q);
    buf_init(&url);
    buf_printf(&q, "%s especificaciones", name);
    if (m->domains[0]) buf_printf(&q, " site:%s", m->domains[0]);
    buf_adds(&url, "https://www.google.com/search?q=");
    url_encode(&url, q.p);
    buf_free(&q);
    return buf_take(&url);
}
