# SenseCAP Indicator D1 Pro — auto-build (wrzuć i gotowe)

Repo w stylu **„wrzucam do GitHuba i samo się buduje"**. Zawiera tylko:

```
.github/workflows/build.yml     <- CI: klon oryginału + patch + build + merged.bin
overlay/main/model/indicator_weather.{h,c}   <- pogoda (Open-Meteo, bez klucza)
overlay/main/ui/ui_home.{h,c}                 <- nowy ekran główny
ci/apply_overlay.py             <- automatyczne, deterministyczne edycje oryginału
```

GitHub Actions **sam** klonuje oficjalny `Seeed-Solution/sensecap_indicator_esp32`
(na przypiętym commicie), nakłada powyższe pliki, nanosi 4 drobne edycje i buduje
**pojedynczy `merged.bin`** do wgrania od adresu `0x0`. Nic nie edytujesz ręcznie.

## Jak użyć (3 kroki)

1. Utwórz **nowe, puste** repozytorium na GitHubie i wrzuć do niego całą zawartość
   tego folderu (zachowując strukturę katalogów). Wystarczy przez WWW: *Add file →
   Upload files* albo `git add . && git commit -m init && git push`.
2. Wejdź w zakładkę **Actions** — build ruszy sam po pushu (albo *Run workflow*).
3. Po zielonym buildzie pobierz artefakt **`sensecap-indicator-merged`** →
   w środku `merged.bin`.

Chcesz binarkę w sekcji **Releases**? Wypchnij tag:
```
git tag v1.0.0 && git push --tags
```
`merged.bin` doklei się do Release automatycznie.

## Wgranie na urządzenie

`merged.bin` zawiera bootloader + tablicę partycji + aplikację, wgrywasz go od `0x0`:
```
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 write_flash 0x0 merged.bin
```
(Windows: właściwy port COM. Web-flasher Espressif też przyjmie `merged.bin` @ 0x0.)

## Co robi firmware

Bazuje na oficjalnym przykładzie `indicator_basis` (czujniki D1 Pro: CO2, TVOC,
temperatura, wilgotność) i zmienia ekran główny na Twój układ:

- **4 wyśrodkowane kafelki** w równych odstępach (CO2 / TVOC / Temp / Wilgotność).
- **Na dole, po środku:** godzina, a pod nią ikonka pogody + temperatura + miejscowość.
- **Prawy górny róg:** WiFi (paski zasięgu / przekreślona) oraz trybik → ustawienia.
- **Ustawienia** (ze stocku): WiFi oraz czas (24h, auto-sync, ręczna data/godzina,
  **strefa czasowa**, DST). Powrót gestem wraca na ekran główny.
- **Pogoda:** darmowe **Open-Meteo** (bez klucza). Miasto ustala się **automatycznie
  z geolokalizacji IP** — działa bez żadnej konfiguracji zaraz po połączeniu z WiFi.

## Ważne (uczciwie)

- **Auto-lokalizacja z IP** daje pogodę od ręki. Ręczny wybór miasta i pole serwera
  NTP to opcjonalne rozszerzenia UI ustawień (logika w module pogody jest gotowa —
  wystarczy wysłać event `VIEW_EVENT_CITY_SEARCH_REQ`/`VIEW_EVENT_CITY_SELECT`).
- Warstwa UI to ręczny kod LVGL, którego nie dało się skompilować bez sprzętowego
  toolchaina — **załóż ewentualnie jedną iterację**: jeśli build w Actions pokaże błąd
  kompilacji UI, poprawka będzie widoczna w logu (zwykle drobiazg typu nazwa fontu/ikony).
  CI, patcher i moduł pogody są przetestowane pod kątem spójności.
- Build używa obrazu `espressif/idf:v5.1.4` (repo wymaga v5.1.x). Scalanie robi
  `esptool merge_bin @flash_args` w tym samym kontenerze, więc offsety flash są
  dokładnie takie, jakie wybrało IDF.

## Aktualizacja przypiętej wersji oryginału

W `.github/workflows/build.yml` zmienna `UPSTREAM_SHA` przypina commit Seeeda.
Jeśli podbijesz ją na nowszy, upewnij się, że kotwice w `ci/apply_overlay.py` nadal
pasują (patcher głośno zgłosi błąd, jeśli nie — build się wtedy nie „uda" po cichu).
