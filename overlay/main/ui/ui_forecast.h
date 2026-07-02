#ifndef UI_FORECAST_H
#define UI_FORECAST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Ekran prognozy 3-dniowej. Tworzony raz, subskrybuje VIEW_EVENT_WEATHER_FORECAST.
 * Otwierany po kliknieciu w pogode na ekranie glownym. */
void ui_forecast_open(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_FORECAST_H */
