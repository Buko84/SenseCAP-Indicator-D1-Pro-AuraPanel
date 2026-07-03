<div align="center">

# SenseCAP Indicator D1 Pro — AuraPanel

**Clock · calendar · live sensor data · weather with a 3-day forecast**
built end-to-end on GitHub Actions into a single `aurapanel-d1pro.bin`.

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.1.4-red)
![Target](https://img.shields.io/badge/MCU-ESP32--S3-blue)
![LVGL](https://img.shields.io/badge/LVGL-8.3-green)
![Weather](https://img.shields.io/badge/weather-Open--Meteo%20(no%20API%20key)-lightgrey)
![License](https://img.shields.io/badge/license-MIT%20%2F%20OFL-informational)

<!-- After pushing to your own repo, replace USER/REPO below to show the build status -->
<!-- ![build](https://github.com/USER/REPO/actions/workflows/build.yml/badge.svg) -->

</div>

---

## Table of contents

- [What it is](#what-it-is)
- [What it looks like](#what-it-looks-like)
- [Features](#features)
- [Screens](#screens)
- [How it works](#how-it-works)
- [Flashing the device](#flashing-the-device)
- [Repository layout](#repository-layout)
- [Customization](#customization)
- [Notes and limitations](#notes-and-limitations)
- [Credits and licenses](#credits-and-licenses)

---

## What it is

This is an **overlay** on top of Seeed's official [`indicator_basis`](https://github.com/Seeed-Solution/sensecap_indicator_esp32) example for the **SenseCAP Indicator D1 Pro** (4" touchscreen, ESP32-S3 + RP2040).

Instead of rewriting everything from scratch, the project **keeps the proven foundation** of the stock firmware (board bring-up, display and touch drivers, communication with the RP2040 that provides sensor data, the Wi-Fi stack, SNTP, NVS storage) and **replaces the whole UI layer** with a custom one, adding **weather** from the free Open-Meteo provider.

The key point: **you don't build anything locally**. You push this repo to GitHub and the workflow clones Seeed's upstream (at a pinned revision), copies the files from `overlay/`, applies a few small edits to Seeed's code, and compiles a ready-to-flash `merged.bin`.

The interface is **English by default**, with **Polish** available as an option in Settings (the choice is saved on the device).

---

## What it looks like

Home screen (480 × 480, dark theme):

```
┌─────────────────────────────────────────────┐
│ Warsaw                      [((•))]   [⚙]   │   ← city + Wi-Fi + settings
│                                             │
│      ┌───────────┐      ┌───────────┐       │
│      │ CO2       │      │ TVOC      │       │
│      │   612 ppm │      │   34 ppb  │       │   ← 4 tiles: tap = history (chart)
│      └───────────┘      └───────────┘       │
│      ┌───────────┐      ┌───────────┐       │
│      │ Temp      │      │ Humidity  │       │
│      │   22.4 °C │      │    47 %   │       │
│      └───────────┘      └───────────┘       │
│                                             │
│       12:34   Wednesday, 2 July 2026        │   ← time + date
│              ☀  23°C  Clear                 │   ← weather (tap = 3-day forecast)
└─────────────────────────────────────────────┘
```

- **4 centered sensor tiles** with even spacing.
- **Top bar:** the city on the left (a long name is truncated with an ellipsis), and on the right the Wi-Fi icon (signal bars, or crossed out when there is no network) plus the gear that opens Settings.
- **Bottom, centered:** large clock and the date written out in words.
- **Below that, centered:** the weather icon, temperature and a short condition description.

---

## Features

| Area | Description |
| --- | --- |
| **Sensors** | CO2, TVOC, temperature and humidity (SCD41 + SGP40) on four tiles, updated live. |
| **History** | Tapping a tile opens the daily and weekly chart for that sensor (reused from the stock firmware). |
| **Clock & date** | 12/24-hour time and the date written out in words (e.g. "Wednesday, 2 July 2026"). |
| **Weather** | Current temperature, condition icon and description for the chosen city (Open-Meteo, no API key). |
| **Forecast** | 3-day screen: weekday + date, icon, high/low temperature, description. |
| **Wi-Fi** | Status icon with signal strength; network configuration. |
| **Time settings** | 12/24-hour format, NTP sync, a time zone picked from a list of named regions (automatic daylight-saving switching by date), manual date/time entry, NTP server address. |
| **Display** | Brightness slider (live preview) and an "always on" mode; when off, a picker for the backlight-off delay. |
| **Language** | English (default) or Polish, switchable in Settings; the whole UI, dates and weather descriptions follow the choice, which is saved on the device. |

---

## Screens

- **Home** — tiles + clock/date + weather, with the Wi-Fi and settings icons.
- **3-day forecast** — opened by tapping the weather.
- **Settings** — top to bottom: Language, Wi-Fi, Time settings, Weather (city search), Display.
- **Time settings** — 12/24 h and NTP toggles, a list of named time zones (with automatic DST), rollers for manual date/time, an NTP server field.
- **Sensor history** — a daily/weekly chart for the selected sensor.

City search: type a name and pick from the suggestions — the chosen city is saved and the weather is fetched right away. If you don't pick anything, the location is **detected automatically from your IP address** once connected to a network.

---

## How it works

**Architecture.** The Seeed firmware is built MVC-style around an `esp_event` loop. The model (sensors, Wi-Fi, time, display) publishes `VIEW_EVENT_*` events and the view layer receives them and draws. This overlay **does not change the model** — it provides its own LVGL screens that subscribe to the same events, plus a new weather module.

**Weather.** The `indicator_weather` module uses [Open-Meteo](https://open-meteo.com/) (free, no key):
- geocoding (city search),
- current weather (temperature + WMO weather code),
- daily forecast (high/low + WMO code),
- automatic city detection from IP geolocation when nothing is chosen.

**Graphics.** The weather icons come from the "Weather Icons" font converted to LVGL; Polish characters are provided by a custom Montserrat font generated with the Ą–Ż range. Both are generated from source files and compiled in.

**Building.** Rather than keeping the entire (~200 MB) Seeed firmware in the repo, the workflow does it for you:

```
GitHub Actions
   ├─ clones Seeed/sensecap_indicator_esp32 at a PINNED commit
   ├─ copies overlay/main/**  →  examples/indicator_basis/main/
   ├─ runs ci/apply_overlay.py  (a few deterministic edits to Seeed's code)
   ├─ idf.py build          (image espressif/idf:v5.1.4, ESP32-S3)
   └─ esptool merge_bin     →  merged.bin  (offsets straight from build/flash_args)
```

The patcher (`ci/apply_overlay.py`) is deterministic and **fails the build loudly** if any edit no longer matches the pinned upstream — so the build can never silently "succeed" on unmodified firmware.

---

## Flashing the device

`aurapanel-d1pro.bin` contains the bootloader + partition table + application, so flash it from offset **`0x0`**:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 write_flash 0x0 aurapanel-d1pro.bin
```

> Windows: use the right `COM` port. Espressif's web flasher works too (`aurapanel-d1pro.bin` @ `0x0`).

---

## Repository layout

```
.
├─ .github/workflows/build.yml     # CI: clone + patch + build + aurapanel-d1pro.bin
├─ ci/apply_overlay.py             # deterministic edits to Seeed's code
└─ overlay/main/
   ├─ model/
   │  └─ indicator_weather.c/.h    # Open-Meteo: geocoding, weather, forecast, NTP
   └─ ui/
      ├─ ui_home.c/.h              # home screen (tiles, clock, date, weather)
      ├─ ui_settings.c/.h          # settings (language, Wi-Fi, time, weather, display)
      ├─ ui_time.c/.h              # time settings (12/24h, NTP, named zones w/ auto-DST, manual)
      ├─ ui_forecast.c/.h          # 3-day forecast
      ├─ ui_i18n.c/.h              # translations (English/Polish) + date/weather localization
      ├─ ui_font_pl_16.c           # Montserrat 16 px with the Polish range
      ├─ ui_font_pl_18.c           # Montserrat 18 px with the Polish range
      ├─ ui_font_weather_34.c      # weather-icon font
      └─ ui_font_pl.h              # font declarations
```

---

## Customization

The main knobs:

| Where | Setting |
| --- | --- |
| `.github/workflows/build.yml` → `UPSTREAM_SHA` | pinned commit of Seeed's upstream (after changing it, verify the patcher still matches). |
| `.github/workflows/build.yml` → `PROJECT_DIR` | path of the example being built. |
| `overlay/main/model/indicator_weather.h` → `WEATHER_REFRESH_MIN` | weather refresh interval in minutes (default 30). |
| `overlay/main/ui/ui_time.c` → `YEAR_MIN`/`YEAR_MAX` | year range in the manual date entry. |
| `overlay/main/ui/ui_i18n.c` → `STR` table | UI strings; add a language by extending `ui_lang_t` and the tables. |

Want different weather icons or a wider character range? Fonts are generated from TTF files with `lv_font_conv`; just replace the `ui_font_*` file and its declaration.

---

## Notes and limitations

- **First boot:** until Wi-Fi is configured the device shows the network-setup screen; it returns home once connected.
- **Time zone & DST:** you pick a named region (e.g. "Poland / Central Europe") and daylight-saving switches automatically by date (POSIX rules). This works best in automatic mode (NTP provides UTC). In manual mode the entered time is set as-is. The chosen zone is re-applied after a reboot.
- **Language:** stored on the device (NVS); English is the default until changed.
- This is an overlay on the `indicator_basis` example, not on the factory (ODM) firmware, in line with Seeed's open-source repository.

---

## Credits and licenses

- **[SenseCAP Indicator ESP32](https://github.com/Seeed-Solution/sensecap_indicator_esp32)** — Seeed Studio (base: BSP, drivers, model). License as in the upstream repository.
- **[Open-Meteo](https://open-meteo.com/)** — free weather API (no key).
- **[Weather Icons](https://github.com/erikflowers/weather-icons)** — Erik Flowers, weather icons under SIL OFL 1.1.
- **[Montserrat](https://fonts.google.com/specimen/Montserrat)** — font under SIL OFL 1.1.
- **[LVGL](https://lvgl.io/)** — UI library (8.3).

This overlay's code: MIT. Font assets keep their OFL licenses.
