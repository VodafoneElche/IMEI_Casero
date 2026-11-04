/* Ventana de Ajustes: logo, base TAC importada y actualizaciones. */
#pragma once
#include "common.h"

void settings_register(void);
void settings_open(HWND owner);
void settings_update_result(int result, const char *version);

/* Implementadas en main.c */
void app_check_updates(int manual);
void app_logo_changed(void);
void app_tac_changed(void);
