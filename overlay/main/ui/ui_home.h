#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Podreczne kopie biezacej konfiguracji (zlapane wczesnie z modelu),
 * uzywane przez ekrany ustawien/czasu do inicjalizacji widgetow. */
extern struct view_data_display  g_disp_cfg;
extern struct view_data_time_cfg g_time_cfg;

/* uchwyt ekranu ustawien - uzywany przez ui.c przy powrocie z ekranu WiFi */
extern lv_obj_t *g_ui_settings;

/* Ekran glowny w Twoim ukladzie:
 *   - 4 wysrodkowane kafelki (CO2 / TVOC / Temp / Wilgotnosc) w rownych odstepach,
 *   - na dole po srodku: godzina + (ikonka pogody, temperatura, miejscowosc),
 *   - prawy gorny rog: ikonka WiFi (zasieg / przekreslona) oraz trybik ustawien.
 *
 * Ekran sam subskrybuje eventy z view_event_handle i aktualizuje sie na biezaco,
 * wiec wystarczy go raz utworzyc po indicator_view_init(). */
lv_obj_t *ui_home_create(void);

/* Uchwyt do gotowego ekranu (do lv_disp_load_scr / powrotu z ustawien). */
extern lv_obj_t *ui_home;

#ifdef __cplusplus
}
#endif

#endif /* UI_HOME_H */
