#include "common.h"
#include <shlobj.h>

void buf_init(Buf *b) {
    b->p = NULL;
    b->n = b->cap = 0;
}

void buf_free(Buf *b) {
    free(b->p);
    buf_init(b);
}

static void buf_reserve(Buf *b, size_t extra) {
    if (b->n + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->n + extra + 1) cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) abort();
    b->p = p;
    b->cap = cap;
}

void buf_add(Buf *b, const void *data, size_t len) {
    buf_reserve(b, len);
    memcpy(b->p + b->n, data, len);
    b->n += len;
    b->p[b->n] = 0;
}

void buf_adds(Buf *b, const char *s) {
    if (s) buf_add(b, s, strlen(s));
}

void buf_addc(Buf *b, char c) {
    buf_add(b, &c, 1);
}

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (len <= 0) return;
    buf_reserve(b, (size_t)len);
    va_start(ap, fmt);
    vsnprintf(b->p + b->n, (size_t)len + 1, fmt, ap);
    va_end(ap);
    b->n += (size_t)len;
}

char *buf_take(Buf *b) {
    char *p = b->p ? b->p : xstrdup("");
    buf_init(b);
    return p;
}

char *xstrdup(const char *s) {
    return xstrndup(s ? s : "", s ? strlen(s) : 0);
}

char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) abort();
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Copia truncando sin cortar un carácter UTF-8 por la mitad. */
void safe_copy(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(dst, src, n);
    dst[n] = 0;
}

static int lower_ascii(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (lower_ascii((unsigned char)*s) != lower_ascii((unsigned char)*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

int str_contains_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (!n) return 1;
    for (; *hay; hay++)
        if (starts_with_ci(hay, needle)) return 1;
    (void)n;
    return 0;
}

wchar_t *u2w(const char *s) {
    if (!s) s = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc(sizeof(wchar_t) * (n > 0 ? n : 1));
    if (!w) abort();
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0;
    return w;
}

char *w2u(const wchar_t *s) {
    if (!s) s = L"";
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *u = malloc(n > 0 ? n : 1);
    if (!u) abort();
    if (n <= 0 || !WideCharToMultiByte(CP_UTF8, 0, s, -1, u, n, NULL, NULL)) u[0] = 0;
    return u;
}

const wchar_t *app_dir(void) {
    static wchar_t dir[MAX_PATH];
    if (!dir[0]) {
        wchar_t base[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, base) != S_OK) GetTempPathW(MAX_PATH, base);
        swprintf(dir, MAX_PATH, L"%ls\\Comprobador IMEI", base);
        CreateDirectoryW(dir, NULL);
    }
    return dir;
}

void app_path(wchar_t *out, size_t cap, const wchar_t *name) {
    swprintf(out, cap, L"%ls\\%ls", app_dir(), name);
}

int read_file(const wchar_t *path, Buf *out) {
    buf_init(out);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 512LL * 1024 * 1024) {
        CloseHandle(h);
        return 0;
    }
    buf_add(out, "", 0);
    char chunk[65536];
    DWORD got;
    while (ReadFile(h, chunk, sizeof(chunk), &got, NULL) && got) buf_add(out, chunk, got);
    CloseHandle(h);
    return 1;
}

int write_file_atomic(const wchar_t *path, const void *data, size_t len) {
    wchar_t tmp[MAX_PATH + 8];
    swprintf(tmp, COUNTOF(tmp), L"%ls.tmp", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD wr = 0;
    BOOL ok = len == 0 || WriteFile(h, data, (DWORD)len, &wr, NULL);
    CloseHandle(h);
    if (!ok || wr != len) {
        DeleteFileW(tmp);
        return 0;
    }
    return MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
}

void now_iso(char out[24]) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    snprintf(out, 24, "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

int iso_to_filetime(const char *iso, FILETIME *ft) {
    SYSTEMTIME st = {0};
    unsigned y, mo, d, h = 0, mi = 0, s = 0;
    if (!iso || sscanf(iso, "%u-%u-%uT%u:%u:%u", &y, &mo, &d, &h, &mi, &s) < 3) return 0;
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)s;
    return SystemTimeToFileTime(&st, ft) != 0;
}

void iso_to_local_text(const char *iso, wchar_t *out, size_t cap, int withTime) {
    FILETIME ft;
    SYSTEMTIME utc, loc;
    out[0] = 0;
    if (!iso_to_filetime(iso, &ft) || !FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &loc)) return;
    if (withTime)
        swprintf(out, cap, L"%02u/%02u/%04u %02u:%02u", loc.wDay, loc.wMonth, loc.wYear, loc.wHour, loc.wMinute);
    else
        swprintf(out, cap, L"%02u/%02u/%04u", loc.wDay, loc.wMonth, loc.wYear);
}
