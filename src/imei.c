#include "imei.h"

char imei_check_digit(const char *first14) {
    int sum = 0;
    for (int i = 0; i < 14; i++) {
        int d = first14[i] - '0';
        if (i % 2 == 1) {
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
    }
    return (char)('0' + (10 - sum % 10) % 10);
}

void imei_analyze(const wchar_t *input, ImeiInfo *o) {
    memset(o, 0, sizeof(*o));
    for (const wchar_t *p = input; p && *p; p++) {
        if (*p >= L'0' && *p <= L'9') {
            if (o->ndig < (int)sizeof(o->digits) - 1) o->digits[o->ndig] = (char)*p;
            o->ndig++;
        }
    }
    if (o->ndig > (int)sizeof(o->digits) - 1) o->ndig = (int)sizeof(o->digits) - 1;
    o->digits[o->ndig] = 0;

    memcpy(o->tac, o->digits, o->ndig < 8 ? (size_t)o->ndig : 8);
    if (o->ndig > 8) memcpy(o->serial, o->digits + 8, o->ndig < 14 ? (size_t)(o->ndig - 8) : 6);
    if (o->ndig >= 15) o->check = o->digits[14];

    if (o->ndig < 14) return;
    o->expected = imei_check_digit(o->digits);
    if (o->ndig == 14 || o->ndig == 16) {
        /* 14: falta el dígito de control. 16: IMEISV (14 + 2 de versión de software). */
        memcpy(o->imei, o->digits, 14);
        o->imei[14] = o->expected;
        o->check = o->expected;
        o->complete = o->valid = 1;
        o->note = o->ndig == 14 ? NOTE_COMPUTED14 : NOTE_IMEISV;
    } else if (o->ndig == 15) {
        memcpy(o->imei, o->digits, 15);
        o->complete = 1;
        o->valid = o->digits[14] == o->expected;
    } else {
        o->tooLong = 1;
    }
}
