# Rejestry sterownika TECH ST-402N PWM

Rejestry odczytywane z ramek na linii RJ12 (ESP8266Zasobnik). Instalacja: **schemat 1** (kolektor, pompa, jeden zbiornik, czujniki: kolektor + zbiornik).

Oznaczenia pewności:
- ✅ **pewne** — potwierdzone obserwacją lub zmianą nastawy,
- 🟡 **prawdopodobne** — pasuje do instrukcji i wartości, nie sprawdzone zmianą,
- ❔ **nieznane** — opis tego, co zaobserwowano.

Ramka: `0226` (początek), adresat (`0000` wszyscy, `FFF4` moduł ethernet, `FFFA` regulator pokojowy, `FFF8` moduł GSM), pary rejestr–wartość, `0218` (koniec) + CRC. Puste zapytanie `0226 FFF4 0218` to sterownik odpytujący moduł ethernet — w tej roli występuje ESP.

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
| 157C | ✅ | stan sterownika | 0x62 praca normalna, 0x1E praca ręczna, 0x68 przegrzanie kolektora; wg słownika dzien.net (jako 01F4): 0xD4–0xD7 uszkodzony czujnik solara 1–4 |
| 1620 | ✅ | zegar sterownika | starszy bajt = godzina, młodszy = minuta (0x083A = 08:58); zapis przez 0x0298 |
| 17B9 | ✅ | data sterownika | bity 15–11 = dzień, 10–7 = miesiąc, 6–0 = rok; **odczyt: rok − 2000** (0x7D9A = 15.11.2026 po ręcznym ustawieniu); zmienia się o północy; po zaniku zasilania śmieć (np. 0x0962). **Zapis przez 0x0431: rok − 2008** (0x7D92 → wyświetlacz 15/11/26; 0x7D99 → 15/11/33; rok ≥ 2034 → śmieciowa data, np. 0x7D9A → 13/03/04; 0x0001 odrzucone). Po zapisie 17B9 przez chwilę pokazuje poprzednią datę, potem zapisaną wartość |
| 15A7 | ✅ | typ sterownika | 0x20 = **ST-402** (słownik dzien.net) |
| 16FF | 🟡 | typ/model sterownika (drugi rejestr) | 0x22; w słowniku dzien.net opisany ze znakami zapytania, kodu 0x22 tam nie ma |

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

## Nastawy — zbiornik (menu serwisowe)

| Rejestr | Pewność | Znaczenie | Wartość u nas |
|---|---|---|---|
| 1B5A | ✅ | temperatura zadana zbiornika | 75 °C |
| 1B5B | ✅ | temperatura maksymalna zbiornika 1 | 80 °C |
| 1B5C | ✅ | histereza zbiornika | 3 °C |
| 1B5D | ✅ | temperatura minimalna zbiornika 1 | 10 °C |
| 1B5E | ✅ | wychładzanie do zadanej | 1 = włączone |
| 1B5F | ✅ | delta urlop | 35 °C |

Wartości spisane z menu serwisowego 15.11.2026 i zgodne z kolejnymi rejestrami (75, 80, 3, 10, 1, 35) — dopasowanie po wartościach, bez zmiany nastawy.

## Nastawy — inne

| Rejestr | Pewność | Znaczenie | Wartość u nas |
|---|---|---|---|
| 1C06 | 🟡 | kod serwisowy | 112 = „0112” (fabryczny wg instrukcji) |
| 1B6A | 🟡 | temperatura antyzamarzania kolektora | −10 °C (zakres −50…+10) |
| 1B66 | ✅ | maksymalna temperatura pracy kolektorów | 133 °C (słownik dzien.net) |
| 1B60…1B97 (poza opisanymi) | ❔ | pozostałe nastawy menu serwisowego (delty, histerezy, min/max pompy, współczynnik biegów, przegrzanie…) | do dopasowania z wartościami z menu |

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
| 1BA0, 1BA1, 1BA2 | ✅ (19.11) 1BA0 = starsze słowo uzysku rocznego (rok = 1BA0·65536 + 1B9C = 105 470); uzysk całkowity = 1BA1·100 kWh + 1BA2·0,1 kWh (122 i 172 → 12 217,2 kWh = 12,22 MWh na wyświetlaczu) |
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

- Słownik rejestrów TECH: https://dzien.net/tech/index.php?baza=slownik

- Instrukcja ST-402N PWM: https://sterownikitech.pl/img/products_files/st-402N-PWM.pdf
- Tech_RS-mqtt-gateway (sterowniki kotłowe TECH, m.in. 1620, 1621, 157C, 15A7, 16FF): https://github.com/agirilovich/Tech_RS-mqtt-gateway

## Testy zapisu daty 0x0431 (15.11.2026)

Dzień 15, miesiąc 11, zmieniane tylko pole roku (bity 6–0). Odczyt z wyświetlacza sterownika (d/m/r).

| Zapis | Pole roku | Wyświetlacz | Uwagi |
|---|---|---|---|
| 7D80 | 0 | 14/03/78 | poza zakresem; 2008-11-15 − 11 204 dni |
| 7D81 | 1 | 15/11/09 | ✅ 2009 — dolna granica |
| 7D92 | 18 | 15/11/26 | ✅ 2026 |
| 7D99 | 25 | 15/11/33 | ✅ 2033 — górna granica |
| 7D9A | 26 | 13/03/04 | poza zakresem; 2034-11-15 − 11 204 dni |
| 759A | 26 (dzień 14) | 12/03/04 | jw. |
| 7DF9 | 121 | 22/04/79 | poza zakresem |
| 0001 | 1 (dzień 0, mies. 0) | bez zmian | odrzucone |

Wniosek (poprawiony po dalszych testach): część „śmieciowych” dat **nie pochodziła z zapisu daty, tylko z późniejszego zapisu godziny** (0x0298), który ESP robił automatycznie albo przyciskiem z HA.

- Zapis godziny przy dacie ustawionej **ręcznie** (17B9 = 0x7D9A, rok od 2000) — data zostaje (15.11.2026, test 11:06).
- Zapis godziny przy dacie ustawionej **przez ESP** (17B9 = 0x7D92, rok od 2008) — data cofa się o ok. 11 204 dni (15.11.2026 → 13.03.1996, testy 11:00 i 11:03).
- Sterownik przy zapisie godziny najpewniej odbudowuje datę z 17B9 (z rokiem od 2000), więc wartość zapisana przez ESP daje złą datę.
- Zapis daty: rok = 2008 + pole (pewnie sprawdzone dla 2009, 2026, 2033). Wyniki dla 2008 i 2034 mogły być zepsute zapisem godziny — do ponownego sprawdzenia.
- Do sprawdzenia: czy o północy sterownik sam przepisuje 17B9 do postaci z rokiem od 2000 (test 15→16.11.2026).

## Odczyt 17B9 przy ręcznym ustawianiu daty (15.11.2026)

| Data ustawiona w menu | 17B9 |
|---|---|
| 15.11.2026 | 7D9A |
| 16.11.2026 | 859A |
| 15.12.2026 | 7E1A |
| 15.11.2027 | 7D9B |
| 01.01.2009 | 0889 |
| 31.12.2033 | FE21 |
| 01.01.2000 (rok „00”) | 0880 |
| 31.12.2099 (rok „99”) | FE63 |
| 01.01.1970 (po zaniku zasilania, bez ustawiania) | 0962 |

- Menu: rok dwucyfrowy 00–99 = 2000–2099, przewija się w kółko; po zaniku zasilania data domyślna 01.01.1970 (wybór roku czterocyfrowy).
- 17B9 = dzień·2048 + miesiąc·128 + (rok − 2000); dla lat sprzed 2000 (rok ujemny) wychodzi dodatkowo +0x100 (0962 = 01.01.1970, 727C = 14.03.1996, B2EB = 22.04.1979).
- Zmiana daty w menu nie zmienia żadnego innego rejestru. Po ustawieniu daty sterownik przez ok. 1–2 min nie wysyła 1620 (zegara).
- Zapis daty przez 0x0431 działa inaczej niż menu (rok 2026 trzeba wysłać jako 18) i ma skutki uboczne (patrz wyżej) — **nie używać** do czasu wyjaśnienia.

## Testy zapisu daty 0x0431 — popołudnie 15.11.2026 (z wyłączoną korektą zegara)

Po zapisie wyświetlacz (i 17B9) przechodzi przez kilka stanów, zanim się ustabilizuje (ok. 2,5 min, w tym czasie sterownik nie wysyła 1620):
zapisana data → 00/00/00 (17B9 = 0x0030) → … → data końcowa.

| Start (wyświetlacz) | Zapis | Zaraz po zapisie | Po drodze | Końcowo (17B9) |
|---|---|---|---|---|
| 14.03.1996 | 7D92 (15.11, pole roku 18) | 15/11/26 | 00/00/00, 24/12/06 | 14/03/96 (727C) |
| 14.03.1996 | 8592 (16.11, pole roku 18) | ? | 00/00/00 | 15/03/96 (79E0) |
| 14.03.1996 | 9BB1 (19.07, pole roku 49) | 19/07/01 | 00/00/00, 24/12/06 | 24/12/06 (C6A2) |

Wnioski:
- zaraz po zapisie dzień i miesiąc są zawsze takie, jak wysłane; rok = pole + 8 (dla pola 49 wyświetla się „01”),
- po ok. 2,5 min sterownik datę „przelicza” i wychodzi inna — wcześniejsze „udane” zapisy z rana były najpewniej odczytami sprzed tego przeliczenia,
- 17B9 po ustabilizowaniu pokazuje datę z wyświetlacza (dzień, miesiąc zgodne), ale pole roku dla lat sprzed 2000 / ostatnich zapisów jest kodowane niejednolicie (1996 jako 96 albo 124+przeniesienie, 2006 jako 162),
- reguły przeliczania nie udało się ustalić — **zapisu 0x0431 nie używać**; datę ustawiać ręcznie, ESP tylko ostrzega (PR #40).

## Zapis daty 0x0431 — rozwiązanie (15.11.2026, wieczór)

Sesje z resetem sterownika przed każdą (A → X → E, odczyt po ~1/2/3 min, bez ustawiania zegara) pokazały, że przeliczenie jest **w pełni deterministyczne**. Zapisana data W (rok = 2008 + pole) po ~2,5 min zamienia się na:

| Zapis (rok W) | Wynik | Format 17B9 |
|---|---|---|
| 2008 – 31.12.2055 | W − 11 204 dni (2045→2015, 2054→2024, 31.12.2055→28.04.2025) | A albo B |
| od 15.01.2056 | W − 18 470 / 18 471 dni (2056→2005, 2077→2026) | B |

- 18 470 przy zegarze sterownika wieczorem (~17:40), 18 471 ok. północy (sesja 1 przy 15:00 też +1 dzień) — zależy od godziny na zegarze.
- Jeden bit = dokładnie rok albo miesiąc kalendarzowy; poprzedni stan daty nie ma wpływu.
- **Zapis `5345` (10.06.2077) daje 15/11/26** (17B9 = 7E36). Zapis godziny (0x0298) potem daty nie psuje (sprawdzone).
- Zapis daty i godziny w jednej ramce nic nie zmienia.

Formaty odczytu 17B9 (dzień zawsze w bitach 15–11):

- **A** (data z menu): `d·2048 + m·128 + (rok − 2000)` — 7D9A = 15.11.2026,
- **B** (po zaniku zasilania i po zapisie 0x0431): `d·2048 + m·128 + rr + 156`, rr = rok dwucyfrowy — 0962 = 01.01.70, 727C = 14.03.96, BEA2 = 23.12.06, 7E36 = 15.11.26.

Stulecia sterownik nie pokazuje (rok dwucyfrowy), a lata przestępne 1901–2099 wypadają tak samo, więc format B wystarcza. Gdy wartość pasuje do obu formatów, ESP wybiera datę bliższą dzisiejszej. Do sprawdzenia: czy dzień tygodnia (uzysk tygodniowy) liczy się w formacie B poprawnie.

ESP (FW 2026.11.15-5, PR #42) zapisuje „dziś + 18 470 dni” i po każdej próbie koryguje przesunięcie o różnicę w dniach.

## Statystyki z menu sterownika (19.11.2026, ok. 15:10)

| Okres | Uzysk | Maks. kolektor | Maks. zbiornik | Czas pracy pompy | Rejestr |
|---|---|---|---|---|---|
| dzienny | 0 (nie wyświetla) | 13 °C | 10 °C | – | 1B99 = 0 ✅ |
| tygodniowy | 105 kWh | 20 °C | 10 °C | 10 h 20 min | 1B9A = 105 ✅ |
| miesięczny | 464 kWh | 20 °C | 10 °C | 66 h 28 min | 1B9B = 464 ✅ |
| roczny | 105 470 kWh | 34 °C | 10 °C | 28 h 56 min | 1B9C = 0x9BFE (młodsze słowo) ✅ |
| całkowity | 12,22 MWh | 2693 °C (?) | 73 °C | 4777 h 20 min | 1BA1 + 1BA2 ✅ |

- **Uzysk całkowity = 1BA1·100 + 1BA2·0,1 kWh**: 1BA2 rosło 142 (14.11) → 170 (17.11) → 172 (18.11) dokładnie o uzyski dzienne (2,7 + 0,1 kWh, potem 0,2 kWh); 12 200 + 17,2 = 12 217,2 kWh → 12,22 MWh (15.11 rano 12 214,2 → 12,21). Do potwierdzenia przy przejściu 1BA2 przez 999 (1BA1 → 123).
- **Uzysk roczny = 1BA0·65 536 + 1B9C** (1 i 0x9BFE → 105 470 kWh).
- Maks. kolektor 2693 °C to najpewniej odczyt przy uszkodzonym/odłączonym czujniku kiedyś w przeszłości; maks. zbiornik 73 °C wygląda realnie.
- Uzyski w rejestrach zgadzają się z menu, więc ESP czyta je dobrze; dziwne zmiany tygodniowego/miesięcznego to obliczenia sterownika.
- **Maksymalnych temperatur i czasów pracy pompy nie ma w ramkach.** Przeglądanie menu nie dodało żadnego nowego rejestru (dalej 192). Nie pasuje też żadna wartość (szukane 13/130, 620, 3988, 1736 min).
- 1BA2 nie jest dziennym maksimum temperatury (patrz wyżej — to końcówka uzysku całkowitego).
- Statystyki roczne są niespójne: pompa 28 h 56 min w roku przy 66 h 28 min w miesiącu, maks. kolektor 34 °C w roku przy 83,9 °C 13.11. Najpewniej zepsuły je testy daty (sterownik zmieniał rok 1970/1996/2003/1906…). Uzysk roczny od 15.11 stoi na 105 470.
