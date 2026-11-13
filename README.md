# ESP8266Zasobnik

ESP8266 (Witty Cloud) czytający ramki ze sterownika solarnego TECH (ST-401n) po linii RJ12
i wysyłający wartości przez MQTT do Home Assistant (autowykrywanie).

## Konfiguracja

Skopiuj `ESP8266Zasobnik/secrets.h.example` jako `ESP8266Zasobnik/secrets.h` i uzupełnij
WiFi oraz dane serwera MQTT (np. dodatek Mosquitto w HA).

## Arduino IDE

- Płytka: **NodeMCU 1.0 (ESP-12E Module)**, rdzeń esp8266
- Biblioteki: **PubSubClient** (Nick O'Leary), **HashMap** (Alexander Brevig)

## MQTT

| Temat | Zawartość |
|---|---|
| `tech_solar/status` | `online` / `offline` |
| `tech_solar/temp_solar`, `tech_solar/temp_tank` | °C |
| `tech_solar/mode` | `auto` / `odmrażanie` / `urlopowy` |
| `tech_solar/pump` | `ON` / `OFF` |
| `tech_solar/pump_power` | % |
| `tech_solar/power` | kW (uzysk chwilowy) |
| `tech_solar/energy_daily` | kWh (uzysk dzienny) |
| `tech_solar/energy_week`, `tech_solar/energy_month` | kWh (uzysk tygodniowy, miesięczny) |
| `tech_solar/state` | stan sterownika (157C): praca ręczna / normalna / przegrzanie kolektora / kod |
| `tech_solar/reg_1b9c` | rejestr 1B9C – prawdopodobnie uzysk roczny, do weryfikacji |
| `tech_solar/raw/<rejestr>` | każdy rejestr surowo: `hex = bez znaku / ze znakiem` (bez encji w HA) |
| `tech_solar/diag` | JSON: RSSI, czas pracy, ramki, błędy |
| `tech_solar/log`, `tech_solar/debug` | logi |

## Tryby testowe

Na początku `ESP8266Zasobnik.ino`:

- `#define SIMULATE` – bez sterownika: co 5 s sztuczna ramka z poprawnym CRC,
  zmieniające się temperatury (także ujemne) i pompa. Sprawdza całą drogę aż do HA.
- `#define DEBUG` – dodatkowe komunikaty na `tech_solar/debug`.
  Nie używa `Serial`, bo `Serial` jest podłączony do sterownika.

## Diody (RGB na Witty)

- czerwona – start / brak połączenia z MQTT
- zielona – wysyłanie do MQTT
- niebieska – odbiór bajtów ze sterownika

## Zapis do sterownika (testy)

Nadawanie przez sprzętowy TX (GPIO1) → T1 → pin 1 RJ12, zwykły UART 9600 8N1 (T1 sam odwraca sygnał).
Ramka: `02 26 | adres | rejestr | wartość | 02 18 | CRC`, wysyłana ok. 50 ms po pustym zapytaniu
`0226FFF40218` (lub po dowolnej ramce, jeśli przez 20 s nie było zapytania).

Reguła z forum: rejestr odczytu = 5000 + numer parametru, rejestr zapisu = numer parametru
(np. odczyt `1620` = 5664 → zapis `0298` = 664).

- przycisk **Ustaw zegar sterownika** – czas z NTP na `0298` (bajt godzina, bajt minuty); efekt widać w encji **Zegar sterownika** (`1620`)
- pole **Zapis rejestru (test)** / temat `tech_solar/cmd/raw`: `RRRRVVVV` albo `RRRR VVVV [ADRES] [ODSTĘP_MS]`,
  np. `0298 1118` (17:24), `0298 1118 fff4`, `0298 1118 0000 2` (2 ms między bajtami)
- wysłana ramka: `tech_solar/tx`, logi `TX_...` na `tech_solar/log`
