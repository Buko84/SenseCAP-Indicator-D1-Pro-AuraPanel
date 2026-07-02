#ifndef UI_TIME_H
#define UI_TIME_H
#ifdef __cplusplus
extern "C" {
#endif

/* Wlasny ekran ustawien czasu: format 12/24h, auto (NTP), strefa czasowa,
 * czas letni (DST), reczne ustawienie daty i godziny, serwer NTP. */
void ui_time_open(void);

/* Ponowne zastosowanie strefy POSIX (z regulami DST) po restarcie,
 * na podstawie indeksu zapisanego w cfg.zone. */
void ui_time_reapply_zone(int idx);

#ifdef __cplusplus
}
#endif
#endif
