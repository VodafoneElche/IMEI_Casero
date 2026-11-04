/* Análisis del IMEI: limpieza, partes y dígito de control (Luhn). */
#pragma once
#include "common.h"

enum { NOTE_NONE = 0, NOTE_COMPUTED14, NOTE_IMEISV };

typedef struct {
    char digits[48];
    int ndig;
    char tac[9];     /* 8 primeros dígitos */
    char serial[7];  /* 6 siguientes */
    char check;      /* dígito de control escrito (o calculado) */
    char expected;   /* dígito de control correcto */
    char imei[16];   /* IMEI de 15 dígitos resultante */
    int complete;    /* hay al menos 14 dígitos */
    int valid;       /* el dígito de control cuadra */
    int tooLong;
    int note;
} ImeiInfo;

char imei_check_digit(const char *first14);
void imei_analyze(const wchar_t *input, ImeiInfo *o);
