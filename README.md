<div align="center">

# SenseCAP Indicator D1 Pro — własny firmware

**Zegar · kalendarz · dane z czujników · pogoda i prognoza 3-dniowa**
zbudowane w całości na GitHub Actions do jednego pliku `merged.bin`.

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.1.4-red)
![Target](https://img.shields.io/badge/MCU-ESP32--S3-blue)
![LVGL](https://img.shields.io/badge/LVGL-8.3-green)
![Weather](https://img.shields.io/badge/pogoda-Open--Meteo%20(bez%20klucza)-lightgrey)
![License](https://img.shields.io/badge/licencja-MIT%20%2F%20OFL-informational)

<!-- Po wypchnięciu do własnego repo podmień USER/REPO poniżej, aby pokazać status buildu -->
<!-- ![build](https://github.com/USER/REPO/actions/workflows/build.yml/badge.svg) -->

</div>

---

## Spis treści

- [Czym to jest](#czym-to-jest)
- [Jak to wygląda](#jak-to-wygląda)
- [Funkcje](#funkcje)
- [Ekrany](#ekrany)
- [Jak to działa](#jak-to-działa)
- [Budowanie (GitHub Actions)](#budowanie-github-actions)
- [Wgranie na urządzenie](#wgranie-na-urządzenie)
- [Struktura repozytorium](#struktura-repozytorium)
- [Dostosowanie](#dostosowanie)
- [Uwagi i ograniczenia](#uwagi-i-ograniczenia)
- [Podziękowania i licencje](#podziękowania-i-licencje)

---

## Czym to jest

To **nakładka** na oficjalny przykład Seeed [`indicator_basis`](https://github.com/Seeed-Solution/sensecap_indicator_esp32) dla urządzenia **SenseCAP Indicator D1 Pro** (ekran dotykowy 4", ESP32-S3 + RP2040).

Zamiast pisać wszystko od zera, projekt **zachowuje sprawdzone fundamenty** oryginalnego firmware (bring-up płytki, sterownik wyświetlacza i dotyku, komunikacja z RP2040 dostarczająca dane z czujników, stos Wi-Fi, SNTP, zapis do NVS) i **wymienia całą warstwę interfejsu** na własną oraz dokłada **pogodę** z darmowego dostawcy Open-Meteo.

Najważniejsze: **nie musisz nic budować lokalnie**. Wrzucasz to repo na GitHub, a workflow sam klonuje oryginał Seeed (na przypiętej wersji), nakłada pliki z `overlay/`, nanosi kilka drobnych zmian w kodzie Seeed i kompiluje gotowy do wgrania `merged.bin`.

---

## Jak to wygląda

Ekran główny (480 × 480, ciemny motyw):

```
┌─────────────────────────────────────────────┐
│                              [((•))]   [⚙]   │   ← Wi-Fi (zasięg / przekreślone) + ustawienia
│                                               │
│      ┌───────────┐      ┌───────────┐         │
│      │ CO2       │      │ TVOC      │         │
│      │   612 ppm │      │   34 ppb  │         │   ← 4 kafelki: klik = historia (wykres)
│      └───────────┘      └───────────┘         │
│      ┌───────────┐      ┌───────────┐         │
│      │ Temp      │      │ Wilgotność│         │
│      │   22.4 °C │      │    47 %   │         │
│      └───────────┘      └───────────┘         │
│                                               │
│              12:34   Środa, 2 lipca 2026      │   ← godzina + data po polsku
│         Świdnik  ☀  23°C  Bezchmurnie         │   ← klik = prognoza 3-dniowa
└─────────────────────────────────────────────┘
```

- **4 wyśrodkowane kafelki** czujników w równych odstępach.
- **Prawy górny róg:** ikona Wi-Fi (słupki zasięgu, a przy braku sieci — przekreślona) oraz trybik wchodzący w ustawienia.
- **Dół, wyśrodkowany:** duża godzina oraz data słownie po polsku.
- **Pod spodem:** miejscowość, prawdziwa ikona pogody, temperatura i krótki opis.

---

## Funkcje

| Obszar | Opis |
| --- | --- |
| **Czujniki** | CO2, TVOC, temperatura i wilgotność (SCD41 + SGP40) na czterech kafelkach, aktualizacja na żywo. |
| **Historia** | Kliknięcie kafla otwiera wykres dzienny i tygodniowy danego czujnika (z oryginalnego firmware). |
| **Zegar i data** | Godzina 12/24 h oraz data słownie po polsku (np. „Środa, 2 lipca 2026"). |
| **Pogoda** | Bieżąca temperatura, ikona warunków i opis dla wybranej miejscowości (Open-Meteo, bez klucza API). |
| **Prognoza** | Ekran 3-dniowy: dzień tygodnia + data, ikona, temperatura maks./min., opis. |
| **Wi-Fi** | Ikona statusu z siłą sygnału; konfiguracja sieci. |
| **Ustawienia czasu** | Format 12/24 h, synchronizacja NTP, strefa czasowa, czas letni, ręczne ustawienie daty/godziny, adres serwera NTP. |
| **Wyświetlacz** | Suwak jasności (podgląd na żywo) oraz tryb „zawsze włączony"; po wyłączeniu — wybór czasu do wygaszenia podświetlenia. |
| **Język** | Pełna polska diakrytyka (własny font Montserrat z zakresem Ą–Ż). |

---

## Ekrany

- **Główny** — kafelki + zegar/data + pogoda, z ikonami Wi-Fi i ustawień.
- **Prognoza 3-dniowa** — otwierana kliknięciem w pogodę.
- **Ustawienia** — kolejno: Wi-Fi, Ustawienia czasu, Pogoda (wyszukiwarka miasta), Wyświetlacz.
- **Ustawienia czasu** — przełączniki, strefa, DST, rolki do ręcznego ustawienia, pole NTP.
- **Historia czujnika** — wykres dzień/tydzień dla wybranego czujnika.

Wyszukiwanie miasta: wpisz nazwę, wybierz z listy podpowiedzi — wybrana miejscowość zapisuje się i od razu pobierana jest pogoda. Jeśli nic nie wybierzesz, lokalizacja zostanie **wykryta automatycznie z adresu IP** po połączeniu z siecią.

---

## Jak to działa

**Architektura.** Firmware Seeed jest zbudowany w stylu MVC na pętli zdarzeń `esp_event`. Model (czujniki, Wi-Fi, czas, wyświetlacz) publikuje zdarzenia `VIEW_EVENT_*`, a warstwa widoku je odbiera i rysuje. Ta nakładka **nie zmienia modelu** — dostarcza własne ekrany LVGL, które nasłuchują tych samych zdarzeń, oraz nowy moduł pogody.

**Pogoda.** Moduł `indicator_weather` korzysta z [Open-Meteo](https://open-meteo.com/) (darmowe, bez klucza):
- geokodowanie (wyszukiwarka miast),
- pogoda bieżąca (temperatura + kod pogody WMO),
- prognoza dzienna (maks./min. + kod WMO),
- automatyczne wykrycie miasta z geolokalizacji IP, gdy nic nie wybrano.

**Grafika.** Ikony pogody pochodzą z fontu „Weather Icons" przekonwertowanego do LVGL; polskie znaki zapewnia własny font Montserrat wygenerowany z zakresem Ą–Ż. Oba są generowane z plików źródłowych i wkompilowane.

**Budowanie.** Zamiast trzymać w repo cały (~200 MB) firmware Seeed, workflow robi to za Ciebie:

```
GitHub Actions
   ├─ klonuje Seeed/sensecap_indicator_esp32 na PRZYPIĘTYM commicie
   ├─ kopiuje overlay/main/**  →  examples/indicator_basis/main/
   ├─ uruchamia ci/apply_overlay.py  (kilka deterministycznych zmian w kodzie Seeed)
   ├─ idf.py build          (obraz espressif/idf:v5.1.4, ESP32-S3)
   └─ esptool merge_bin     →  merged.bin  (offsety wprost z build/flash_args)
```

Patcher (`ci/apply_overlay.py`) jest deterministyczny i **głośno przerywa build**, gdy któraś zmiana nie pasuje do przypiętej wersji — dzięki temu build nigdy nie „uda się" po cichu na niezmienionym firmware.

---

## Budowanie (GitHub Actions)

1. Utwórz **puste** repozytorium na GitHubie i wgraj do niego zawartość tego folderu (z zachowaniem struktury katalogów).
2. Wejdź w zakładkę **Actions** — build ruszy sam po `push` (lub uruchom ręcznie: *Run workflow*).
3. Po zielonym buildzie otwórz dany run → sekcja **Artifacts** → pobierz **`sensecap-indicator-merged`** (w środku `merged.bin`).

Chcesz binarkę w **Releases**? Wypchnij tag:

```bash
git tag v1.0.0 && git push --tags
```

`merged.bin` dołączy się do Release automatycznie.

---

## Wgranie na urządzenie

`merged.bin` zawiera bootloader + tablicę partycji + aplikację, więc wgrywasz go od adresu **`0x0`**:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 write_flash 0x0 merged.bin
```

> Windows: użyj właściwego portu `COM`. Zadziała też web-flasher Espressif (`merged.bin` @ `0x0`).

---

## Struktura repozytorium

```
.
├─ .github/workflows/build.yml     # CI: klon + patch + build + merged.bin
├─ ci/apply_overlay.py             # deterministyczne zmiany w kodzie Seeed
└─ overlay/main/
   ├─ model/
   │  └─ indicator_weather.c/.h    # Open-Meteo: geokodowanie, pogoda, prognoza, NTP
   └─ ui/
      ├─ ui_home.c/.h              # ekran główny (kafelki, zegar, data, pogoda)
      ├─ ui_settings.c/.h          # ustawienia (WiFi, czas, pogoda, wyświetlacz)
      ├─ ui_time.c/.h              # ustawienia czasu (12/24h, NTP, strefa, DST, ręcznie)
      ├─ ui_forecast.c/.h          # prognoza 3-dniowa
      ├─ ui_font_pl_16.c           # Montserrat 16 px z polskim zakresem
      ├─ ui_font_pl_18.c           # Montserrat 18 px z polskim zakresem
      ├─ ui_font_weather_34.c      # font ikon pogody
      └─ ui_font_pl.h              # deklaracje fontów
```

---

## Dostosowanie

Najważniejsze pokrętła:

| Gdzie | Ustawienie |
| --- | --- |
| `.github/workflows/build.yml` → `UPSTREAM_SHA` | przypięty commit oryginału Seeed (przy zmianie sprawdź, czy patcher nadal trafia). |
| `.github/workflows/build.yml` → `PROJECT_DIR` | ścieżka budowanego przykładu. |
| `overlay/main/model/indicator_weather.h` → `WEATHER_REFRESH_MIN` | co ile minut odświeżać pogodę (domyślnie 30). |
| `overlay/main/ui/ui_time.c` → `YEAR_MIN`/`YEAR_MAX` | zakres lat w ręcznym ustawieniu daty. |

Chcesz inne ikony pogody lub większy zakres znaków? Fonty generuje się z plików TTF narzędziem `lv_font_conv`; wystarczy podmienić plik `ui_font_*` i deklarację.

---

## Uwagi i ograniczenia

- **Pierwsze uruchomienie:** dopóki nie skonfigurujesz Wi-Fi, urządzenie pokazuje ekran konfiguracji sieci; po połączeniu wraca na ekran główny.
- **Strefa i czas letni** działają w oparciu o model czasu urządzenia — najpewniej w trybie automatycznym (NTP). W trybie ręcznym wpisany czas ustawiany jest wprost.
- Projekt jest nakładką na przykład `indicator_basis`, a nie na fabryczny (ODM) firmware — zgodnie z otwartym repozytorium Seeed.

---

## Podziękowania i licencje

- **[SenseCAP Indicator ESP32](https://github.com/Seeed-Solution/sensecap_indicator_esp32)** — Seeed Studio (baza: BSP, sterowniki, model). Licencja jak w repozytorium źródłowym.
- **[Open-Meteo](https://open-meteo.com/)** — darmowe API pogodowe (bez klucza).
- **[Weather Icons](https://github.com/erikflowers/weather-icons)** — Erik Flowers, ikony pogody na licencji SIL OFL 1.1.
- **[Montserrat](https://fonts.google.com/specimen/Montserrat)** — font na licencji SIL OFL 1.1.
- **[LVGL](https://lvgl.io/)** — biblioteka UI (8.3).

Kod tej nakładki: MIT. Zasoby fontów zachowują swoje licencje OFL.
