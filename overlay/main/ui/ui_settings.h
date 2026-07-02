#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Tworzy (raz) i pokazuje NASZ ekran ustawien:
 *  - wyszukiwarka miasta (lokalizacja pod pogode),
 *  - serwer NTP,
 *  - przejscia do stockowych ekranow: WiFi oraz czas/data/strefa,
 *  - powrot na ekran glowny (ikonka w prawym gornym rogu). */
void ui_settings_open(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_H */
