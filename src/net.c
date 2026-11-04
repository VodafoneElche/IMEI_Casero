#include "net.h"
#include <winhttp.h>

void url_encode(Buf *b, const char *s) {
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            buf_addc(b, (char)c);
        } else {
            buf_addc(b, '%');
            buf_addc(b, hex[c >> 4]);
            buf_addc(b, hex[c & 15]);
        }
    }
}

static HINTERNET session(void) {
    static HINTERNET s;
    if (!s) {
        s = WinHttpOpen(USER_AGENT_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) s = WinHttpOpen(USER_AGENT_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (s) WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    }
    return s;
}

/* Hace la petición y entrega el cuerpo a un búfer o a un archivo. */
static int request(const char *url, const wchar_t *accept, Buf *out, HANDLE file, int *status) {
    *status = 0;
    HINTERNET s = session();
    if (!s) return 0;
    wchar_t *wurl = u2w(url);
    URL_COMPONENTS uc = {sizeof(uc)};
    wchar_t host[256], path[4096];
    uc.lpszHostName = host;
    uc.dwHostNameLength = COUNTOF(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = COUNTOF(path);
    wchar_t extra[4096];
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = COUNTOF(extra);
    int ok = 0;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) goto done;
    wchar_t full[8192];
    swprintf(full, COUNTOF(full), L"%ls%ls", path, extra);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    if (!c) goto done;
    HINTERNET r = WinHttpOpenRequest(c, L"GET", full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (r) {
        wchar_t headers[256] = L"";
        if (accept) swprintf(headers, COUNTOF(headers), L"Accept: %ls\r\n", accept);
        if (WinHttpSendRequest(r, headers[0] ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, NULL)) {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
            *status = (int)code;
            ok = 1;
            char chunk[32768];
            DWORD got;
            for (;;) {
                got = 0;
                if (!WinHttpReadData(r, chunk, sizeof(chunk), &got)) {
                    ok = 0;
                    break;
                }
                if (!got) break;
                if (out) buf_add(out, chunk, got);
                if (file != INVALID_HANDLE_VALUE) {
                    DWORD wr;
                    if (!WriteFile(file, chunk, got, &wr, NULL) || wr != got) {
                        ok = 0;
                        break;
                    }
                }
            }
        }
        WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
done:
    free(wurl);
    return ok;
}

int http_get(const char *url, const wchar_t *accept, Buf *out, int *status) {
    buf_init(out);
    buf_add(out, "", 0);
    return request(url, accept, out, INVALID_HANDLE_VALUE, status);
}

int http_download(const char *url, const wchar_t *path, int *status) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    int ok = request(url, L"application/octet-stream", NULL, f, status);
    CloseHandle(f);
    if (!ok || *status != 200) DeleteFileW(path);
    return ok && *status == 200;
}
