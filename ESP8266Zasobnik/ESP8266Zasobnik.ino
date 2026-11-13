#include <WiFiClient.h>
#include <stddef.h>
#include <HashMap.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h> // biblioteka "PubSubClient" (Nick O'Leary)

//#define DEBUG

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

//#1B98 -> Uzysk chwilowy [0051=>8.1kWh]
//#1B99 -> Uzysk dzienny [0051=>8.1kWh]
//#1B9A -> Uzysk tygodniowy [00E2=>226kWh]
//#1B9B -> Uzysk miesięczny [07F5=>2037kWh]

//157c
//1c04
//17b9 ?? data ??

// definicje ESP8266
#define rx_pin 3
#define tx_pin 14
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

CreateHashMap(mypin, String, String, 255);

//SoftwareSerial txSerial(SW_SERIAL_UNUSED_PIN, tx_pin); // RX, TX
unsigned long bitTime;

void setup(void)
{
#ifdef DEBUG
	Serial.begin(9600);
	Serial.println("start");
#endif

	pinMode(ledGreen, OUTPUT);
	pinMode(ledRed, OUTPUT);
	pinMode(ledBlue, OUTPUT);
	pinMode(ledIn, OUTPUT);
	LEDOff(ledIn); // zgas led wbudowany

	pinMode(rx_pin, INPUT_PULLUP); // podcignij do VCC
//	pinMode(tx_pin, OUTPUT);

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
#ifdef DEBUG
		Serial.println(WiFi.status());
#endif
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

	Serial.setRxBufferSize(1024); // HTTP blokuje petle - wiekszy bufor, zeby nie gubic bajtow
	Serial.begin(9600);
//	txSerial.begin(9600);

	pinMode(tx_pin, OUTPUT);
	digitalWrite(tx_pin, HIGH);
	bitTime = ESP.getCpuFreqMHz() * 1000000 / 9600; //80 * 1 000 000 / 9600 = 8333;

	// wszystko wystartowalo - wylacz czerwona diode
	LEDOff(ledRed);

	SendLog("Start");
}


void loop(void)
{
	HandleWWWClient();

	MqttLoop();

	ReadDataFromTECH();
}


String readLine = "";
int przeslanyBajt = 0;
String bajt2;
String key;
String value;
void ReadDataFromTECH()
{

#ifdef DEBUG
	if (millis() % 1000 == 0)
	{
		readLine = "0226fff41bdc59eb1be64c141bf000001bfa0053021809d1";
		Serial.println("linia");
		Serial.println(ESP.getFreeHeap());
		Serial.println(millis());
		LEDOn(ledBlue);
	}
	else
	{
		readLine = "";
	}
	{
#else
	if (Serial.available() > 0)
	{
		LEDOn(ledBlue);
		bajt2 = "";
		przeslanyBajt = Serial.read();
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
      countErrorStart++; // bez SendLog - kazdy GET blokuje odczyt

      readLine = readLine.substring(2); // wytnijmy jeden znak, aż trafimy na dobry ciąg. Tylko jeden znak, bo czasami coś sie dzieje, że string ma nieparzystą ilośc znaków. I wtedy długo by szukał nowej ramki. Jakby to nie był jednak prawdziwy początek ramki, to potem wywali CRC i będzie ok
    }

#endif 
		if (readLine.length() >= 16 && readLine.substring(readLine.length() - 8, readLine.length() - 4) == "0218" && (readLine.length() % 8 == 0))
		{
			countReceivedFrame++;

			String frameCRC = readLine.substring(readLine.length() - 4, readLine.length());
			unsigned int testCRC = strtol(frameCRC.c_str(), NULL, 16);
			unsigned int calculatedCRC = crc16_mcrf4xx(readLine);

			if (testCRC == calculatedCRC)
		//	if (crc16_mcrf4xx(0xFFFF, (uint8_t*)readLine.c_str(), readLine.length()))
			{
        for (int i = 0; i < readLine.length() - 8; i = i + 8)
				{
					key = readLine.substring(i, i + 4);
					value = readLine.substring(i + 4, i + 8);

					if (mypin.indexOf(key) == -1) // nie ma takiego
					{
						if ((key == TechTempSolar) ||
							(key == TechTempTank) ||
							(key == TechPumpPower) ||
							(key == TechPumpOnOff) ||
							(key == TechMode) ||
              (key == TechPower) ||
              (key == TechPowerDaily))
						{ // na start zapisujemy tylko te ktore uzywamy
              LEDOff(ledBlue);

							SaveAndSendData(key, value);
						}
						else
						{
						//	mypin[key] = value; nie logujmy innych na razie
						}
					}
					else if (mypin[key] != value) // juz taki jest i sie zmienil
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
#ifdef DEBUG
				Serial.println("CRC Error !");
#endif
			}
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

#define WAIT { while (ESP.getCycleCount()-start < wait); wait += bitTime; }

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
#ifdef DEBUG
		Serial.println(req);
#endif

		//   client.flush();

		String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>";
		s += contentHTML;

		// Match the request
		//if (req.indexOf("/set") != -1)
		//{

		//	char* ass = "0226FFF4170500010218BF85";
		//	//char* ass = "022600001B66008002181885";
		//	//char* ass = "0226FFF41B66008002185719";
		//	uint8_t aa[12];
		//	aa[0] = 2;
		//	aa[1] = 38;
		//	aa[2] = 255;
		//	aa[3] = 244;
		//	aa[4] = 23;
		//	aa[5] = 05;
		//	aa[6] = 0;
		//	aa[7] = 1;
		//	aa[8] = 2;
		//	aa[9] = 24;
		//	aa[10] = 191;
		//	aa[11] = 133;

		//	unsigned long wait = bitTime;
		//	unsigned long start = ESP.getCycleCount();
		//	digitalWrite(tx_pin, LOW);
		//	WAIT;

		//	for (int i = 0; i < 12; i++)
		//	{
		//		size_t ret = writeByte(aa[i]);
		//	}

		//	digitalWrite(tx_pin, HIGH);


		//	s += "<br/>";
		//	s += "SET !!! ";

		//}
		//else
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

size_t writeByte(uint8_t b) 
{
	// Disable interrupts in order to get a clean transmit
	cli();
	unsigned long wait = bitTime;
//	digitalWrite(tx_pin, HIGH);
	unsigned long start = ESP.getCycleCount();
	// Start bit;
	digitalWrite(tx_pin, LOW);
	WAIT;
	digitalWrite(tx_pin, HIGH);
	WAIT;
//	delay(2);
	for (int i = 0; i < 8; i++) {
		digitalWrite(tx_pin, (b & 1) ? LOW : HIGH);
		WAIT;
		b >>= 1;
	}
	// Stop bit
	digitalWrite(tx_pin, LOW);
	WAIT;
	sei();
	return 1;
}


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
	if ((key != TechTempSolar) && (key != TechTempTank) && (key != TechMode)
		&& (key != TechPumpOnOff) && (key != TechPumpPower)
		&& (key != TechPower) && (key != TechPowerDaily)
		&& (key != TechTime)  // pomijamy zmiane godziny
		&& (key != "1742")  // to samo co 1701 (moe jakis offset mozna ustawic na regulatorze)
		&& (key != "1743")) // to samo co 1702 (moe jakis offset mozna ustawic na regulatorze)
	{
		String log = "";
		log += key;
		log += "_";
		log += mypin[key];
		log += "_";
		log += value;

		SendLog(log);
	}
	mypin[key] = value;
	PublishValue(key, value);

  #ifdef DEBUG
    Serial.println(key + " " + value);
  #endif
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
	return s;
}

void SendLog(String msg)
{
	msg += "_free_" + String(ESP.getFreeHeap());
	lastLog = msg;
	if (mqtt.connected())
		mqtt.publish(MQTT_BASE "/log", msg.c_str());
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
	PublishDiscovery();

	// po (ponownym) polaczeniu wyslij wszystkie znane wartosci
	const char* keys[] = { TechTempSolar, TechTempTank, TechMode, TechPumpOnOff, TechPumpPower, TechPower, TechPowerDaily };
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

	// diagnostyka
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

#ifdef DEBUG
	Serial.println("frameCRC: " + data + " Calculated CRC: " + String(calculatedCRC, HEX));
#endif
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
