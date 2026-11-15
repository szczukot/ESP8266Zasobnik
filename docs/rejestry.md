# Rejestry sterownika TECH ST-402N PWM

Rejestry odczytywane z ramek na linii RJ12 (ESP8266Zasobnik). Instalacja: **schemat 1** (kolektor, pompa, jeden zbiornik, czujniki: kolektor + zbiornik).

Oznaczenia pewności:
- ✅ **pewne** — potwierdzone obserwacją lub zmianą nastawy,
- 🟡 **prawdopodobne** — pasuje do instrukcji i wartości, nie sprawdzone zmianą,
- ❔ **nieznane** — opis tego, co zaobserwowano.

Zapis: rejestry zapisu mają numer **o 0x1388 (5000) mniejszy** od rejestru odczytu (np. zegar: odczyt 0x1620 = 5664 → zapis 664 = 0x0298). Sterownik wysyła rejestry zapisu zawsze z wartością 0000.

## Temperatury i wyjścia

| Rejestr | Pewność | Znaczenie | Format / uwagi |
|---|---|---|---|
| 1701 | ✅ | temperatura kolektora | ×0,1 °C, ze znakiem |
| 1702 | ✅ | temperatura zbiornika | ×0,1 °C, ze znakiem |
| 1703 | 🟡 | czujnik 3 (dodatkowy) | 0xF830 = −2000 = brak czujnika |
| 1704 | 🟡 | czujnik 4 (dodatkowy) | 0xF830 = −2000 = brak czujnika |
| 1742 | ✅ | kopia 1701 (temp. kolektora) | zawsze równy 1701 |
| 1743 | ✅ | kopia 1702 (temp. zbiornika) | zawsze równy 1702 |
| 1744 | ✅ | pompa solarna włączona | 0/1 |
| 1757 | ✅ | moc (obroty) pompy solarnej | % |
| 1756, 1758, 1759, 175A | 🟡 | pozostałe wyjścia (pompa 2/zawór, peryferia) | w schemacie 1 zawsze 0 |
| 1705 | ✅ | tryb pracy | 0 auto, 1 odmrażanie, 2 urlopowy |
| 157C | ✅ | stan sterownika | 0x62 praca normalna, 0x1E praca ręczna, 0x68 przegrzanie kolektora |
| 1620 | ✅ | zegar sterownika | starszy bajt = godzina, młodszy = minuta (0x083A = 08:58); zapis przez 0x0298 |
| 17B9 | ✅ | data sterownika | bity 15–11 = dzień, 10–7 = miesiąc, 6–0 = rok − 2000 (0x7D9A = 15.11.2026, 0x759A = 14.11, 0x6D9A = 13.11); zmienia się o północy sterownika; po zaniku zasilania data śmieciowa; zapis prawdopodobnie przez 0x0431 (niesprawdzony) |
| 15A7 | 🟡 | typ urządzenia | 0x20 (wg projektu Tech_RS-mqtt-gateway: DEVICE_TYPE) |
| 16FF | 🟡 | model urządzenia | 0x22 (wg Tech_RS-mqtt-gateway: DEVICE_MODEL) |

## Uzyski

| Rejestr | Pewność | Znaczenie | Format / uwagi |
|---|---|---|---|
| 1B98 | ✅ | uzysk chwilowy | ×0,1 kW |
| 1B99 | ✅ | uzysk dzienny | ×0,1 kWh; zerowany ok. 00:59 |
| 1B9A | ✅ | uzysk tygodniowy | kWh; zmienia się raz na dobę ok. 00:59; prawdopodobnie suma z 7 dni (14→15.11 spadł 91→86); przetrwa zanik zasilania |
| 1B9B | ✅ | uzysk miesięczny | kWh; zmienia się raz na dobę ok. 00:59; reguła niejasna (+8, potem +31) — możliwy wpływ złej daty w sterowniku; przetrwa zanik zasilania |
| 1B9C | ✅ | uzysk roczny — **młodsze 16 bitów** | kWh; wyświetlacz 15.11.2026: 105470 kWh = 0x0001_9BFE, a 1B9C = 0x9BFE; starsze słowo (=1) nie znalezione w ramkach; wartość bezsensowna, prawdopodobnie przez złą datę w sterowniku |

## Nastawy — zliczanie energii (menu serwisowe)

| Rejestr | Pewność | Znaczenie | Wartość u nas |
|---|---|---|---|
| 1B89 | ✅ | przepływ | 15 = 1,5 l/min (×0,1) |
| 1B8A | ✅ | rodzaj medium | 2 = druga pozycja listy (glikol); numeracja pozostałych pozycji do sprawdzenia |
| 1B8B | ✅ | stężenie glikolu | 50 % |
| 1B8C | ✅ | kalibracja | 0 |

## Nastawy — inne

| Rejestr | Pewność | Znaczenie | Wartość u nas |
|---|---|---|---|
| 1C06 | 🟡 | kod serwisowy | 112 = „0112” (fabryczny wg instrukcji) |
| 1B6A | 🟡 | temperatura antyzamarzania kolektora | −10 °C (zakres −50…+10) |
| 1B5A…1B97 (poza opisanymi) | ❔ | pozostałe nastawy menu serwisowego (delty, histerezy, min/max pompy, współczynnik biegów, przegrzanie…) | do dopasowania z wartościami z menu |

## Dzienniki zdarzeń (menu Statystyki)

Po 10 wpisów, każde pole w osobnym bloku 10 rejestrów. Daty w formacie FAT: `rok = 1980 + (v >> 9)`, `miesiąc = (v >> 5) & 15`, `dzień = v & 31`.

### Zaniki zasilania — 🟡

| Rejestry | Pole | Format |
|---|---|---|
| 1BD7…1BE0 | data | FAT (0x5AC8 = 2025-06-08) |
| 1BE1…1BEA | godzina | FAT: `godz = v >> 11`, `min = (v >> 5) & 63`, `s = (v & 31) × 2` |
| 1BEB…1BF4 | ? | zawsze 0 (może starsze słowo czasu trwania) |
| 1BF5…1BFE | czas trwania | jednostka do ustalenia |

Zanik zasilania 15.11.2026 (~1 min, zegar sterownika się wyzerował) **nie dodał wpisu** — prawdopodobnie sterownik zapisuje zanik tylko, gdy podtrzymanie zegara działa.

### Przegrzewy kolektora — 🟡

| Rejestry | Pole | Format |
|---|---|---|
| 1BA5…1BAE | temperatura kolektora? | wszystkie 300 |
| 1BAF…1BB8 | data | FAT (0x5ABC = 2025-05-28) |
| 1BB9…1BC2 | godzina | prawdopodobnie sekundy od północy (51326 = 14:15:26) |
| 1BC3…1BCC | ? | zawsze 0 |
| 1BCD…1BD6 | czas trwania | sekundy (wpis 14:19:59 + 687 s = 14:31:26, następny zaczyna się 14:31:27) |

## Nieznane

| Rejestr | Obserwacje |
|---|---|
| 1C04 | 0xF25F, 15.11 ok. 9:00 zmiana na 0xF260 (+1), wkrótce po starcie pompy |
| 1BA1, 1BA2 | 122 i 142; 142 = temp. zbiornika 14.2 °C z 14.11 wieczorem — może dzienne maksima (kolektor/zbiornik ×0,1 °C)? do obserwacji |
| 1C07, 1B58, 1B9D…1BA4, 1BFF…1C03, 1C05 | stałe |

## Statystyki, których nie ma w ramkach

Wartości z wyświetlacza (Statystyki) 15.11.2026 i wynik szukania w 192 rejestrach:

| Statystyka | Wyświetlacz | W ramkach |
|---|---|---|
| całkowity uzysk | 12,21 MWh | brak (sprawdzone kWh, ×10, ×0,1, 32 bity) |
| całkowita maks. temp. kolektora | 2693 (bzdura) | brak (0x0A85) |
| całkowita maks. temp. zbiornika | 73 °C | brak (0x49 / 0x2DA) |
| całkowity czas pracy pompy | 4773 h 21 min | brak (godziny, minuty, sekundy) |
| roczny uzysk | 105470 kWh | ✅ 1B9C = młodsze słowo |
| roczna maks. temp. kolektora | 34 °C | ❔ 0x22 jest w 16FF i 1BD5 — raczej przypadek |
| roczna maks. temp. zbiornika | 26 °C | brak (0x1A / 0x104) |
| roczny czas pracy pompy | 28 h 56 min | brak (minuty 0x06C8, sekundy) |

Roczne statystyki wyglądają na zepsute przez złą datę w sterowniku (np. wpis z 2031 r. w dzienniku zaników).

Nastawy menu użytkownika **podświetlenie** i **kontrast** też nie są wysyłane (15.11.2026 zmiana 3→4 i 0→1 nie zmieniła żadnego rejestru).

## Źródła

- Instrukcja ST-402N PWM: https://sterownikitech.pl/img/products_files/st-402N-PWM.pdf
- Tech_RS-mqtt-gateway (sterowniki kotłowe TECH, m.in. 1620, 1621, 157C, 15A7, 16FF): https://github.com/agirilovich/Tech_RS-mqtt-gateway
