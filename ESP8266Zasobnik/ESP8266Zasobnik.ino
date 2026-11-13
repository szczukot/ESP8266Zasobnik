#include <WiFiClient.h>
#include <stddef.h>
#include <HashMap.h>
#include <time.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h> // biblioteka "PubSubClient" (Nick O'Leary)

// Tryby testowe (odkomentuj jeden lub oba):
// DEBUG    - dodatkowe komunikaty w MQTT na temat tech_solar/debug
//            (NIE na Serial - Serial jest podlaczony do sterownika TECH!)
// SIMULATE - zamiast czytac sterownik, co 5 s generuje sztuczna ramke z poprawnym CRC.
//            Mozna sprawdzic MQTT i Home Assistant bez podlaczania do TECH.
//#define DEBUG
//#define SIMULATE

void ProcessByte(int przeslanyBajt);
void SimulateTECH();
void DebugLog(String msg);

#ifdef DEBUG
#define DBG(msg) DebugLog(msg)
#else
#define DBG(msg)
#endif

// definicje sieci WiFi
#include "secrets.h" // hasla - plik poza repo, wzor w secrets.h.example
const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASSWORD;

// definicje TECH
#define TechTempSolar "1701"
#define TechTempTank "1702"
#define TechMode "1705" // 0 auto 1-odmrazanie 2-urlopowy
#define TechPumpOnOff "1744" // zasilanie pompy 
#define TechPumpPower "1757" // moc pompy
#define TechTime "1620" // czas na sterowniku
#define TechPower "1b98" // uzysk chwilowy
#define TechPowerDaily "1b99" // uzysk dzienny
#define TechEnergyWeek "1b9a" // uzysk tygodniowy, kWh bez dzielnika
#define TechEnergyMonth "1b9b" // uzysk miesieczny, kWh bez dzielnika
#define TechEnergy1B9C "1b9c" // prawdopodobnie uzysk roczny/calkowity - niepotwierdzone
#define TechState "157c" // stan sterownika: 0x1E reczna, 0x62 normalna (?), 0x68 przegrzanie kolektora

//#1B98 -> Uzysk chwilowy [0051=>8.1kWh]
//#1B99 -> Uzysk dzienny [0051=>8.1kWh]
//#1B9A -> Uzysk tygodniowy [00E2=>226kWh]
//#1B9B -> Uzysk miesięczny [07F5=>2037kWh]

//157c
//1c04
//17b9 ?? data ??

// definicje ESP8266
#define rx_pin 3
const int ledIn = 2;
const int ledGreen = 12;
const int ledBlue = 13;
const int ledRed = 15;

// dane dla serwera HTML
String contentHTML;
volatile int countErrorCRC = 0;
volatile int countErrorStart = 0;
volatile int countErrorClientTimeout = 0;
volatile int countReceivedFrame = 0;
volatile int countPublish = 0;
volatile int countErrorMqtt = 0;
String lastLog = "";

WiFiServer server(80);
WiFiClient client;

// MQTT do Home Assistant
#define MQTT_BASE "tech_solar"
WiFiClient mqttNet;
PubSubClient mqtt(mqttNet);
String deviceId;
unsigned long lastMqttTry = 0;
unsigned long lastDiag = 0;

// kolejka zapisu do sterownika
struct TxCommand
{
	bool pending;
	uint16_t addr;
	uint16_t reg;
	uint16_t val;
	uint8_t gapMs;
	unsigned long since;
};
TxCommand txCmd = { false, 0, 0, 0, 0, 0 };
String lastTx = "-";
int countTx = 0;

CreateHashMap(mypin, String, String, 255);


void setup(void)
{
	pinMode(ledGreen, OUTPUT);
	pinMode(ledRed, OUTPUT);
	pinMode(ledBlue, OUTPUT);
	pinMode(ledIn, OUTPUT);
	LEDOff(ledIn); // zgas led wbudowany

	pinMode(rx_pin, INPUT_PULLUP); // podcignij do VCC

	// wlacz czerwona diode - serwer startuje
	LEDOn(ledRed);

	// tylko client (bez AP)	
	WiFi.mode(WIFI_STA);
	WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  WiFi.setHostname("ESP8266Zasobnik");
	WiFi.begin(ssid, password);

	unsigned long timeout = millis();
	// Wait for connection
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		contentHTML += ".";

		if (millis() - timeout > 20000)
		{
			ESP.restart();
			return;
		}

	}

	contentHTML += "<br/>";
	contentHTML += "Connected to ";
	contentHTML += ssid;
	contentHTML += "<br/>";
	contentHTML += "IP Address ";
	contentHTML += WiFi.localIP().toString();
	contentHTML += "<br/>";

	server.begin();
	contentHTML += "HTTP server started<br/>";

	deviceId = "tech_solar_" + String(ESP.getChipId(), HEX);
	mqtt.setServer(SECRET_MQTT_HOST, SECRET_MQTT_PORT);
	mqtt.setBufferSize(1024); // konfiguracja autowykrywania HA nie miesci sie w domyslnych 256 B
	mqtt.setCallback(MqttCallback);

	// czas z NTP (do ustawiania zegara sterownika), strefa Polska z czasem letnim
	configTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");

	Serial.setRxBufferSize(1024); // HTTP blokuje petle - wiekszy bufor, zeby nie gubic bajtow
	Serial.begin(9600);
//	txSerial.begin(9600);


	// wszystko wystartowalo - wylacz czerwona diode
	LEDOff(ledRed);

	SendLog("Start");
}


void loop(void)
{
	HandleWWWClient();

	MqttLoop();

	TxLoop();

	ReadDataFromTECH();
}


String readLine = "";
unsigned long lastFrameEnd = 0; // koniec ostatniej ramki - do wyboru momentu nadawania
bool lastWasPoll = false;       // czy byla to pusta ramka zapytania 0226fff40218
String bajt2;
String key;
String value;
void ReadDataFromTECH()
{
#ifdef SIMULATE
	SimulateTECH();
#else
	if (Serial.available() > 0)
		ProcessByte(Serial.read());
#endif
}

void ProcessByte(int przeslanyBajt)
{
	{
		LEDOn(ledBlue);
		bajt2 = "";
		if (przeslanyBajt < 0)
		{
			SendLog("Bad_BYTE_" + readLine);
		}

		if (przeslanyBajt < 16)
		{
			bajt2 += "0"; //uzupelnienie
		}
		bajt2 += String(przeslanyBajt, HEX);
		readLine += bajt2;

		if (readLine.length() == 4 && readLine != "0226")
		{ // błędny początek ramki
			countErrorStart++; // bez logowania - nie blokujemy odczytu

			readLine = readLine.substring(2); // wytnij jeden bajt (2 znaki hex) i szukaj dalej poczatku ramki
		}

		if (readLine.length() >= 16 && readLine.substring(readLine.length() - 8, readLine.length() - 4) == "0218" && (readLine.length() % 8 == 0))
		{
			countReceivedFrame++;

			String frameCRC = readLine.substring(readLine.length() - 4, readLine.length());
			unsigned int testCRC = strtol(frameCRC.c_str(), NULL, 16);
			unsigned int calculatedCRC = crc16_mcrf4xx(readLine);

			if (testCRC == calculatedCRC)
		//	if (crc16_mcrf4xx(0xFFFF, (uint8_t*)readLine.c_str(), readLine.length()))
			{
				// i = 8: pomijamy naglowek 0226 + adres
				for (int i = 8; i < readLine.length() - 8; i = i + 8)
				{
					key = readLine.substring(i, i + 4);
					value = readLine.substring(i + 4, i + 8);

					// zapisujemy wszystkie rejestry; wysylamy tylko nowe lub zmienione
					if (mypin.indexOf(key) == -1 || mypin[key] != value)
					{
						LEDOff(ledBlue);
						SaveAndSendData(key, value);
					}
				}

			}
			else
			{
        LEDOff(ledBlue);

				SendLog("Bad_CRC_" + readLine);

				countErrorCRC++;
			}
			lastFrameEnd = millis();
			lastWasPoll = (readLine.length() == 16 && readLine.startsWith("0226fff4"));
			readLine = "";
		}

		// dla bezpieczestwa
		if (readLine.length() > 2048)
		{
      SendLog("Bad_LONGLINE_" + readLine.substring(0, 64));

			readLine = "";
		}

		LEDOff(ledBlue);
	}
}

void HandleWWWClient()
{
	client = server.available();
	if (client)
	{
		LEDOn(ledIn);

		unsigned long timeout = millis();
		while (client.available() == 0)
		{
			delay(1);
			if (millis() - timeout > 500)
			{
				countErrorClientTimeout++;
				client.stop();
				LEDOff(ledIn);
				return;
			}
		}

		// Read the first line of the request
		String req = client.readStringUntil('\r');
		DBG("WWW " + req);

		//   client.flush();

		String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>";
		s += contentHTML;

		{
      int rssi = WiFi.RSSI();

			// Prepare the response
      s += "<br/>";
			s += "RSSI: " + String(rssi) + "dBm";
      s += "<br/>";
			s += "Connect quality: " + String(dBmtoPercentage(rssi)) + " %";
			s += "<br/>";
			s += "Working time: " + SystemUptime();
			s += "<br/>";
			s += "Free memory: " + String(ESP.getFreeHeap());
			s += "<br/>";
			s += "Error client timeout: " + String(countErrorClientTimeout);
			s += "<br/>";
			s += "Error CRC: " + String(countErrorCRC);
			s += "<br/>";
			s += "Error start: " + String(countErrorStart);
			s += "<br/>";
			s += "Received frames: " + String(countReceivedFrame);
			s += "<br/>";
			s += "Last log: " + lastLog;
			s += "<br/>";
			if (mqtt.connected())
				s += "MQTT: polaczony";
			else
				s += "MQTT: brak polaczenia (stan " + String(mqtt.state()) + ")";
			s += "<br/>";
			s += "MQTT wyslane: " + String(countPublish) + "   bledy polaczenia: " + String(countErrorMqtt);
			s += "<br/>";
			s += "TX: " + String(countTx) + "   ostatnia ramka: " + lastTx + (txCmd.pending ? "   (czeka w kolejce)" : "");
			s += "<br/><br/>";
			s += ValuesHTML();
		}
		s += "</html>\n";

		client.flush();

		// Send the response to the client
		client.print(s);

		delay(1);

		LEDOff(ledIn);
	}
}

const int RSSI_MAX =-50;// define maximum strength of signal in dBm
const int RSSI_MIN =-100;// define minimum strength of signal in dBm
int dBmtoPercentage(int dBm)
{
  int quality;
    if(dBm <= RSSI_MIN)
    {
        quality = 0;
    }
    else if(dBm >= RSSI_MAX)
    {  
        quality = 100;
    }
    else
    {
        quality = 2 * (dBm + 100);
   }

     return quality;
}//dBmtoPercentage 

String SystemUptime()
{
	unsigned long ms = millis();
	unsigned long sec = (ms / 1000) % 60;
	int min = (ms / (1000 * 60)) % 60;
	int hour = (ms / (1000 * 60 * 60)) % 24;
	int day = (ms / (1000 * 60 * 60 * 24)) % 365;

  char buffer[25]; // Bufor na sformatowany string

  // Sformatuj poszczególne części czasu do stringów z dwoma cyframi
  sprintf(buffer, "%d days %02d:%02d:%02d", day, hour, min, sec);

  String formattedTime(buffer); // Konwertuj sformatowany string do Stringa

	return formattedTime;
}

void SaveAndSendData(String key, String value)
{
	mypin[key] = value;
	PublishRaw(key, value);   // kazdy rejestr na tech_solar/raw/<rejestr>
	PublishValue(key, value); // znane rejestry jako encje HA

	DBG("Zmiana " + key + " " + value);
}

// --- przeliczanie wartosci z TECH ---

bool IsNoSensor(const String& hex)
{
	return strtol(hex.c_str(), NULL, 16) == 0xF830; // brak czujnika
}

float HexToTemp(const String& hex)
{
	return (int16_t)strtol(hex.c_str(), NULL, 16) / 10.0; // ze znakiem - ujemne temperatury
}

long HexToLong(const String& hex)
{
	return strtol(hex.c_str(), NULL, 16);
}

String ModeName(long mode)
{
	if (mode == 0) return "auto";
	if (mode == 1) return "odmrażanie";
	if (mode == 2) return "urlopowy";
	return String(mode);
}

String StateName(long state)
{
	switch (state & 0xFF)
	{
		case 0x1E: return "praca ręczna";
		case 0x62: return "praca normalna";
		case 0x68: return "przegrzanie kolektora";
	}
	char buf[12];
	sprintf(buf, "kod 0x%02lX", state & 0xFF);
	return String(buf);
}

String ValuesHTML()
{
	String s;
	if (mypin.indexOf(TechTempSolar) != -1) s += "Temp. panel: " + String(HexToTemp(mypin[TechTempSolar]), 1) + " C<br/>";
	if (mypin.indexOf(TechTempTank) != -1) s += "Temp. zasobnik: " + String(HexToTemp(mypin[TechTempTank]), 1) + " C<br/>";
	if (mypin.indexOf(TechMode) != -1) s += "Tryb: " + ModeName(HexToLong(mypin[TechMode])) + "<br/>";
	if (mypin.indexOf(TechPumpOnOff) != -1) s += "Pompa: " + String(HexToLong(mypin[TechPumpOnOff]) ? "ON" : "OFF") + "<br/>";
	if (mypin.indexOf(TechPumpPower) != -1) s += "Moc pompy: " + String(HexToLong(mypin[TechPumpPower])) + " %<br/>";
	if (mypin.indexOf(TechPower) != -1) s += "Uzysk chwilowy: " + String(HexToLong(mypin[TechPower]) / 10.0, 1) + " kW<br/>";
	if (mypin.indexOf(TechPowerDaily) != -1) s += "Uzysk dzienny: " + String(HexToLong(mypin[TechPowerDaily]) / 10.0, 1) + " kWh<br/>";
	if (mypin.indexOf(TechEnergyWeek) != -1) s += "Uzysk tygodniowy: " + String(HexToLong(mypin[TechEnergyWeek])) + " kWh<br/>";
	if (mypin.indexOf(TechEnergyMonth) != -1) s += "Uzysk miesieczny: " + String(HexToLong(mypin[TechEnergyMonth])) + " kWh<br/>";
	if (mypin.indexOf(TechEnergy1B9C) != -1) s += "1B9C (?): " + String(HexToLong(mypin[TechEnergy1B9C])) + "<br/>";
	if (mypin.indexOf(TechState) != -1) s += "Stan: " + StateName(HexToLong(mypin[TechState])) + " (" + mypin[TechState] + ")<br/>";
	return s;
}

void SendLog(String msg)
{
	msg += "_free_" + String(ESP.getFreeHeap());
	lastLog = msg;
	if (mqtt.connected())
		mqtt.publish(MQTT_BASE "/log", msg.c_str());
}

// --- tryby testowe ---

void DebugLog(String msg)
{
	if (mqtt.connected())
		mqtt.publish(MQTT_BASE "/debug", msg.c_str());
}

#ifdef SIMULATE
unsigned long lastSim = 0;

// Buduje ramke jak ze sterownika: 0226 FFF4 [rejestr wartosc]... 0218 CRC
// i przepuszcza ja bajt po bajcie przez ten sam kod co prawdziwy odczyt.
void SimulateTECH()
{
	if (millis() - lastSim < 5000)
		return;
	lastSim = millis();

	unsigned long t = millis() / 5000; // krok symulacji
	int16_t tempSolar = -50 + (t * 37) % 900;   // -5,0 .. 84,9 C (sprawdza tez ujemne)
	int16_t tempTank = 300 + (t * 7) % 300;     // 30,0 .. 59,9 C
	uint16_t pumpOn = (tempSolar > tempTank + 50) ? 1 : 0;
	uint16_t pumpPower = pumpOn ? 30 + (t * 13) % 70 : 0;
	uint16_t power = pumpOn ? (t * 3) % 60 : 0;  // x0,1 kW
	uint16_t daily = (t / 4) % 300;              // x0,1 kWh

	uint16_t regs[][2] = {
		{ 0x1701, (uint16_t)tempSolar },
		{ 0x1702, (uint16_t)tempTank },
		{ 0x1705, 0 },  // tryb auto
		{ 0x1744, pumpOn },
		{ 0x1757, pumpPower },
		{ 0x1b98, power },
		{ 0x1b99, daily },
		{ 0x1b9a, (uint16_t)(20 + daily / 10) },     // tygodniowy kWh
		{ 0x1b9b, (uint16_t)(150 + daily / 10) },    // miesieczny kWh
		{ 0x1b9c, (uint16_t)(58164 + daily / 10) },  // 1B9C jak u sikor16
		{ 0x157c, (uint16_t)(tempSolar > 800 ? 0x68 : 0x62) }, // >80 C = przegrzanie
		{ 0x1ba1, (uint16_t)(t % 10) },              // nieznany - test raw
	};
	const int n = sizeof(regs) / sizeof(regs[0]);

	uint8_t frame[4 + n * 4 + 4];
	int len = 0;
	frame[len++] = 0x02; frame[len++] = 0x26;
	frame[len++] = 0xFF; frame[len++] = 0xF4;
	for (int i = 0; i < n; i++)
	{
		frame[len++] = regs[i][0] >> 8; frame[len++] = regs[i][0] & 0xFF;
		frame[len++] = regs[i][1] >> 8; frame[len++] = regs[i][1] & 0xFF;
	}

	// CRC-16/MCRF4XX z bajtow przed 0218, wysylane big-endian
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < len; i++)
	{
		crc ^= frame[i];
		for (int b = 0; b < 8; b++)
			crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
	}
	frame[len++] = 0x02; frame[len++] = 0x18;
	frame[len++] = crc >> 8; frame[len++] = crc & 0xFF;

	for (int i = 0; i < len; i++)
		ProcessByte(frame[i]);
}
#endif

// --- zapis do sterownika (sprzetowy TX = GPIO1 -> T1 -> RX sterownika) ---
//
// Ramka zapisu: 02 26 | adres | rejestr | wartosc | 02 18 | CRC
// Adres 0000 = do wszystkich (wg forum). Rejestr zapisu = numer parametru,
// a rejestr odczytu = 5000 + numer parametru (np. odczyt 1620 = 5664 -> zapis 664 = 0298).
// Wysylamy ok. 50 ms po pustym zapytaniu 0226FFF40218 (wtedy magistrala jest wolna).
// Jesli sterownik nie wysyla pustych zapytan, po 20 s wysylamy 50 ms po dowolnej ramce.


uint16_t Crc16Bytes(const uint8_t* data, int len)
{
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
			crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
	}
	return crc;
}

void QueueTx(uint16_t addr, uint16_t reg, uint16_t val, uint8_t gapMs)
{
	txCmd.addr = addr;
	txCmd.reg = reg;
	txCmd.val = val;
	txCmd.gapMs = gapMs;
	txCmd.since = millis();
	txCmd.pending = true;

	char buf[48];
	sprintf(buf, "TX_kolejka_%04x_%04x_%04x_gap%d", addr, reg, val, gapMs);
	SendLog(buf);
}

// "RRRRVVVV" albo "RRRR VVVV [ADRES] [ODSTEP_MS]", wszystko hex poza odstepem
void QueueRaw(String p)
{
	p.trim();
	p.toLowerCase();
	String tok[4];
	int n = 0;
	if (p.length() == 8 && p.indexOf(' ') == -1)
	{
		tok[0] = p.substring(0, 4);
		tok[1] = p.substring(4, 8);
		n = 2;
	}
	else
	{
		int start = 0;
		while (n < 4 && start < (int)p.length())
		{
			int sp = p.indexOf(' ', start);
			if (sp == -1) sp = p.length();
			if (sp > start) tok[n++] = p.substring(start, sp);
			start = sp + 1;
		}
	}

	if (n < 2 || tok[0].length() != 4 || tok[1].length() != 4)
	{
		SendLog("TX_zly_format_" + p);
		return;
	}

	uint16_t reg = strtol(tok[0].c_str(), NULL, 16);
	uint16_t val = strtol(tok[1].c_str(), NULL, 16);
	uint16_t addr = (n >= 3) ? strtol(tok[2].c_str(), NULL, 16) : 0x0000;
	uint8_t gap = (n >= 4) ? tok[3].toInt() : 0;
	QueueTx(addr, reg, val, gap);
}

void QueueClockSync()
{
	time_t now = time(nullptr);
	if (now < 1600000000) // NTP jeszcze nie ustawil czasu
	{
		SendLog("TX_brak_czasu_NTP");
		return;
	}
	struct tm* t = localtime(&now);
	// odczyt 1620 = 5664 -> zapis parametru 664 = 0x0298; format: bajt godzina, bajt minuty
	QueueTx(0x0000, 0x0298, (uint16_t)((t->tm_hour << 8) | t->tm_min), 0);
}

void SendFrame(uint16_t addr, uint16_t reg, uint16_t val, uint8_t gapMs)
{
	uint8_t f[12];
	f[0] = 0x02; f[1] = 0x26;
	f[2] = addr >> 8; f[3] = addr & 0xFF;
	f[4] = reg >> 8;  f[5] = reg & 0xFF;
	f[6] = val >> 8;  f[7] = val & 0xFF;
	f[8] = 0x02; f[9] = 0x18;
	uint16_t crc = Crc16Bytes(f, 8);
	f[10] = crc >> 8; f[11] = crc & 0xFF;

	LEDOn(ledGreen);
	for (int i = 0; i < 12; i++)
	{
		Serial.write(f[i]);
		if (gapMs)
		{
			Serial.flush();
			delay(gapMs);
		}
	}
	Serial.flush();
	LEDOff(ledGreen);
	countTx++;

	char hex[25];
	for (int i = 0; i < 12; i++)
		sprintf(hex + 2 * i, "%02x", f[i]);
	lastTx = String(hex);
	SendLog("TX_" + lastTx);
	if (mqtt.connected())
		mqtt.publish(MQTT_BASE "/tx", lastTx.c_str(), true);
}

void TxLoop()
{
	if (!txCmd.pending)
		return;
	if (readLine.length() != 0 || Serial.available() > 0)
		return; // trwa odbior ramki
	if (lastFrameEnd == 0)
		return; // jeszcze nie bylo zadnej ramki

	unsigned long sinceFrame = millis() - lastFrameEnd;
	bool waitedLong = millis() - txCmd.since > 20000;
	if (!lastWasPoll && !waitedLong)
		return; // czekamy na puste zapytanie
	if (sinceFrame < 50 || sinceFrame > 500)
		return; // tylko w oknie 50-500 ms po ramce

	txCmd.pending = false;
	SendFrame(txCmd.addr, txCmd.reg, txCmd.val, txCmd.gapMs);
}

void MqttCallback(char* topic, byte* payload, unsigned int length)
{
	String t(topic);
	String p;
	for (unsigned int i = 0; i < length; i++)
		p += (char)payload[i];

	if (t == MQTT_BASE "/cmd/sync_clock")
		QueueClockSync();
	else if (t == MQTT_BASE "/cmd/raw")
		QueueRaw(p);
}

// --- MQTT / Home Assistant ---

void MqttLoop()
{
	if (!mqtt.connected())
	{
		LEDOn(ledRed); // czerwona = brak polaczenia z MQTT
		if (millis() - lastMqttTry > 5000)
		{
			lastMqttTry = millis();
			MqttConnect();
		}
	}
	else
	{
		mqtt.loop();
		if (millis() - lastDiag > 60000)
		{
			lastDiag = millis();
			PublishDiag();
		}
	}
}

void MqttConnect()
{
	if (WiFi.status() != WL_CONNECTED)
		return;

	if (!mqtt.connect(deviceId.c_str(), SECRET_MQTT_USER, SECRET_MQTT_PASSWORD,
		MQTT_BASE "/status", 0, true, "offline"))
	{
		countErrorMqtt++;
		return;
	}

	LEDOff(ledRed);
	mqtt.publish(MQTT_BASE "/status", "online", true);
	mqtt.subscribe(MQTT_BASE "/cmd/#");
	PublishDiscovery();

	// po (ponownym) polaczeniu wyslij wszystkie znane wartosci
	const char* keys[] = { TechTempSolar, TechTempTank, TechMode, TechPumpOnOff, TechPumpPower, TechPower, TechPowerDaily,
		TechEnergyWeek, TechEnergyMonth, TechEnergy1B9C, TechState, TechTime };
	for (const char* k : keys)
	{
		if (mypin.indexOf(k) != -1)
			PublishValue(k, mypin[k]);
	}
	PublishDiag();
}

void PublishValue(String key, String value)
{
	if (!mqtt.connected())
		return;

	String obj;
	String payload;
	if (key == TechTempSolar || key == TechTempTank)
	{
		if (IsNoSensor(value))
			return;
		obj = (key == TechTempSolar) ? "temp_solar" : "temp_tank";
		payload = String(HexToTemp(value), 1);
	}
	else if (key == TechMode)
	{
		obj = "mode";
		payload = ModeName(HexToLong(value));
	}
	else if (key == TechPumpOnOff)
	{
		obj = "pump";
		payload = HexToLong(value) ? "ON" : "OFF";
	}
	else if (key == TechPumpPower)
	{
		obj = "pump_power";
		payload = String(HexToLong(value));
	}
	else if (key == TechPower)
	{
		obj = "power";
		payload = String(HexToLong(value) / 10.0, 1);
	}
	else if (key == TechPowerDaily)
	{
		obj = "energy_daily";
		payload = String(HexToLong(value) / 10.0, 1);
	}
	else if (key == TechEnergyWeek)
	{
		obj = "energy_week";
		payload = String(HexToLong(value));
	}
	else if (key == TechEnergyMonth)
	{
		obj = "energy_month";
		payload = String(HexToLong(value));
	}
	else if (key == TechEnergy1B9C)
	{
		obj = "reg_1b9c";
		payload = String(HexToLong(value));
	}
	else if (key == TechTime)
	{
		long v = HexToLong(value);
		char buf[8];
		sprintf(buf, "%02ld:%02ld", (v >> 8) & 0xFF, v & 0xFF);
		obj = "clock";
		payload = buf;
	}
	else if (key == TechState)
	{
		obj = "state";
		payload = StateName(HexToLong(value));
	}
	else
	{
		return;
	}

	LEDOn(ledGreen);
	String topic = String(MQTT_BASE "/") + obj;
	mqtt.publish(topic.c_str(), payload.c_str(), true);
	LEDOff(ledGreen);
	countPublish++;
}

void PublishRaw(String key, String value)
{
	if (!mqtt.connected())
		return;

	// surowy podglad do rozpracowywania rejestrow (bez encji w HA)
	String topic = String(MQTT_BASE "/raw/") + key;
	String payload = value + " = " + String(HexToLong(value)) + " / " + String((int16_t)HexToLong(value));
	mqtt.publish(topic.c_str(), payload.c_str(), true);
}

void PublishDiag()
{
	if (!mqtt.connected())
		return;

	String j = "{\"rssi\":" + String(WiFi.RSSI()) +
		",\"uptime\":" + String(millis() / 1000) +
		",\"heap\":" + String(ESP.getFreeHeap()) +
		",\"frames\":" + String(countReceivedFrame) +
		",\"crc_errors\":" + String(countErrorCRC) +
		",\"start_errors\":" + String(countErrorStart) + "}";
	mqtt.publish(MQTT_BASE "/diag", j.c_str(), true);
}

void PublishDiscoveryEntity(const char* component, const char* obj, const char* name, const char* stateTopic, const char* extra)
{
	String topic = String("homeassistant/") + component + "/" + deviceId + "/" + obj + "/config";

	String p = String("{\"name\":\"") + name + "\"";
	p += ",\"uniq_id\":\"" + deviceId + "_" + obj + "\"";
	if (stateTopic != nullptr)
		p += String(",\"stat_t\":\"" MQTT_BASE "/") + stateTopic + "\"";
	p += ",\"avty_t\":\"" MQTT_BASE "/status\"";
	p += extra;
	p += ",\"dev\":{\"ids\":[\"" + deviceId + "\"],\"name\":\"Sterownik TECH solar\",\"mf\":\"TECH\",\"mdl\":\"ST-401n\",\"sw\":\"ESP8266Zasobnik\"}";
	p += "}";

	mqtt.publish(topic.c_str(), p.c_str(), true);
}

void PublishDiscovery()
{
	PublishDiscoveryEntity("sensor", "temp_solar", "Temperatura panelu", "temp_solar",
		",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"°C\",\"stat_cla\":\"measurement\"");
	PublishDiscoveryEntity("sensor", "temp_tank", "Temperatura zasobnika", "temp_tank",
		",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"°C\",\"stat_cla\":\"measurement\"");
	PublishDiscoveryEntity("sensor", "mode", "Tryb pracy", "mode",
		",\"ic\":\"mdi:cog\"");
	PublishDiscoveryEntity("binary_sensor", "pump", "Pompa", "pump",
		",\"dev_cla\":\"running\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"");
	PublishDiscoveryEntity("sensor", "pump_power", "Moc pompy", "pump_power",
		",\"unit_of_meas\":\"%\",\"stat_cla\":\"measurement\",\"ic\":\"mdi:pump\"");
	PublishDiscoveryEntity("sensor", "power", "Uzysk chwilowy", "power",
		",\"dev_cla\":\"power\",\"unit_of_meas\":\"kW\",\"stat_cla\":\"measurement\"");
	PublishDiscoveryEntity("sensor", "energy_daily", "Uzysk dzienny", "energy_daily",
		",\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\"");
	PublishDiscoveryEntity("sensor", "energy_week", "Uzysk tygodniowy", "energy_week",
		",\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\"");
	PublishDiscoveryEntity("sensor", "energy_month", "Uzysk miesięczny", "energy_month",
		",\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\"");
	PublishDiscoveryEntity("sensor", "state", "Stan sterownika", "state",
		",\"ic\":\"mdi:solar-panel\"");

	// zapis do sterownika (testy)
	PublishDiscoveryEntity("button", "sync_clock", "Ustaw zegar sterownika", nullptr,
		",\"cmd_t\":\"" MQTT_BASE "/cmd/sync_clock\",\"pl_prs\":\"PRESS\",\"ent_cat\":\"config\",\"ic\":\"mdi:clock-edit-outline\"");
	PublishDiscoveryEntity("text", "raw_write", "Zapis rejestru (test)", "tx",
		",\"cmd_t\":\"" MQTT_BASE "/cmd/raw\",\"max\":40,\"ent_cat\":\"config\",\"ic\":\"mdi:send\"");

	// diagnostyka
	PublishDiscoveryEntity("sensor", "clock", "Zegar sterownika", "clock",
		",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:clock-outline\"");
	PublishDiscoveryEntity("sensor", "reg_1b9c", "Rejestr 1B9C (uzysk roczny?)", "reg_1b9c",
		",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\"");
	PublishDiscoveryEntity("sensor", "rssi", "WiFi RSSI", "diag",
		",\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\",\"val_tpl\":\"{{ value_json.rssi }}\"");
	PublishDiscoveryEntity("sensor", "uptime", "Czas pracy", "diag",
		",\"dev_cla\":\"duration\",\"unit_of_meas\":\"s\",\"ent_cat\":\"diagnostic\",\"val_tpl\":\"{{ value_json.uptime }}\"");
	PublishDiscoveryEntity("sensor", "frames", "Odebrane ramki", "diag",
		",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\",\"val_tpl\":\"{{ value_json.frames }}\"");
	PublishDiscoveryEntity("sensor", "crc_errors", "Bledy CRC", "diag",
		",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\",\"val_tpl\":\"{{ value_json.crc_errors }}\"");
	PublishDiscoveryEntity("sensor", "start_errors", "Bledy poczatku ramki", "diag",
		",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\",\"val_tpl\":\"{{ value_json.start_errors }}\"");
}


unsigned int crc16_mcrf4xx(String data)
{
	String frame = data.substring(0, data.length() - 8);

	unsigned int calculatedCRC = 0xFFFF;
	for (int i = 0; i < frame.length(); i = i + 2)
	{

		char du[3] = {0}; // strtol wymaga zera na koncu
		du[0] = frame[i];
		du[1] = frame[i + 1];
		calculatedCRC ^= strtol(du, NULL, 16);

		for (int j = 8; j != 0; j--)
		{
			if ((calculatedCRC & 0x0001) != 0)
			{
				calculatedCRC >>= 1;
				calculatedCRC ^= 0x8408;
			}
			else calculatedCRC >>= 1;
		}
	}

	DBG("Ramka " + data + " CRC obliczone: " + String(calculatedCRC, HEX));
	return calculatedCRC;
}

void LEDOn(int pin)
{
	if (pin == ledIn)
		digitalWrite(pin, 0);
	else
		digitalWrite(pin, 1);
}

void LEDOff(int pin)
{
	if (pin == ledIn)
		digitalWrite(pin, 1);
	else
		digitalWrite(pin, 0);
}

//uint16_t crc16_mcrf4xx(uint16_t crc, uint8_t *data, size_t len)
//{
//	if (!data || len < 0)
//		return crc;
//
//	char du[2];
//
//	len = len - 4;
//
//	while (len--) {
//
//		du[0] = *data++;
//		du[1] = *data++;
//		crc ^= strtol(du, NULL, 16);
//
//		for (int i = 0; i < 8; i++) {
//			if (crc & 1)  crc = (crc >> 1) ^ 0x8408;
//			else          crc = (crc >> 1);
//		}
//	}
//
//	*data++;
//	*data++;
//	
//	du[0] = *data++;
//	du[1] = *data++;
//
//	long frameCRC = strtol(du, NULL, 16);
//#ifdef DEBUG
//	Serial.println("frameCRC: " + String(frameCRC) + " Calculated CRC: " + String(crc, HEX));
//#endif
//	return crc;
//}
