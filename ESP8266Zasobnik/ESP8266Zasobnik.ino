#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <stddef.h>
#include <HashMap.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include "base64.h"

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

// definicje domoticz
#define IdxTempSolar "164"
#define IdxTempTank "163"
#define IdxLogs "165"
#define IdxPumpPower "166"
#define IdxPumpOnOff "167"

#define IdxPower "716"
#define IdxPowerDaily "2037"

//String DomoticzUrl = "http://zg.tits.vot.pl:16010/json.htm?";
String DomoticzUrl = "http://192.168.1.2:8080/json.htm?";

// definicje ESP8266
#define rx_pin 3
#define tx_pin 14
const int ledIn = 2;
const int ledGreen = 12;
const int ledBlue = 13;
const int ledRed = 15;

// dane dla serwera HTML
String contentHTML;
volatile int lastError = 0;
volatile int countErrorCRC = 0;
volatile int countErrorStart = 0;
volatile int countErrorClientTimeout = 0;
volatile int countErrorSendGET = 0;
volatile int countSendGET = 0;
volatile int countReceivedFrame = 0;

WiFiServer server(80);
WiFiClient client;

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
						{ // na start wysylamy tylko te ktore uzywamy w domoticzu
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
			s += "Error GET: " + String(countErrorSendGET) + "   Last error: " + String(lastError);
			s += "<br/>";
			s += "Send GET: " + String(countSendGET);
			s += "<br/>";
			s += "Received frames: " + String(countReceivedFrame);
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
	if (key == TechTempSolar) // temperatura na panelu
	{
		SendTemp(value, IdxTempSolar);
	}
	else if (key == TechTempTank) // temperatura w zasobniku
	{
		SendTemp(value, IdxTempTank);
	}
	else if (key == TechPumpOnOff) // zalaczenie pompy
	{
		SendSwitch(value != "0000", IdxPumpOnOff);
	}
	else if (key == TechPumpPower) // PWM pompy
	{
		SendLevel(value, IdxPumpPower);
	}
	else if (key == TechPower) // aktualna moc
	{
		SendPower(value, IdxPower);
	}
  else if (key == TechPowerDaily) // dzienny zysk
	{
		SendPower(value, IdxPowerDaily);
	}
	else if ((key != TechTime)  // pomijamy zmiane godziny
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

  #ifdef DEBUG
    Serial.println(key + " " + value);
  #endif
}

HTTPClient http;  //Declare an object of class HTTPClient
WiFiClient httpClient; // osobny klient dla HTTP (nie ten od serwera WWW)

void SendLog(String msg)
{
	msg += "_free_" + String(ESP.getFreeHeap());
	SendGET(DomoticzUrl + "type=command&param=udevice&idx=165&nvalue=0&svalue=" + msg); //Specify request destination
}

void SendTemp(String temp, String idx)
{
	int16_t dec = (int16_t)strtol(temp.c_str(), NULL, 16); // ze znakiem - ujemne temperatury
	if ((uint16_t)dec == 0xF830) return; // brak czujnika
	float flo = dec / 10.0;
	String str = String(flo, 1);
//	SendLog("Temp_" + str);
	SendGET(DomoticzUrl + "type=command&param=udevice&idx=" + idx + "&nvalue=0&svalue=" + str); //Specify request destination
}

void SendLevel(String level, String idx)
{
	long dec = strtol(level.c_str(), NULL, 16);
	String str = String(dec);
//	SendLog("Level_" + str);
	SendGET(DomoticzUrl + "type=command&param=switchlight&idx=" + idx + "&switchcmd=Set%20Level&level=" + str); //Specify request destination
}

void SendSwitch(bool on, String idx)
{
	String str = "Off";
	if (on)
	{
		str = "On";
	}
//	SendLog("Switch_" + str);
	SendGET(DomoticzUrl + "type=command&param=switchlight&idx=" + idx + "&switchcmd=" + str); //Specify request destination
}

void SendPower(String temp, String idx)
{
	long dec = strtol(temp.c_str(), NULL, 16);
	float flo = (dec / 10.0) * 1000.0;
	String str = String(flo, 1);
//	SendLog("Temp_" + str);
	SendGET(DomoticzUrl + "type=command&param=udevice&idx=" + idx + "&nvalue=0&svalue=" + str + ";0"); //Specify request destination
}


void SendGET(String get)
{
  if (WiFi.status() != WL_CONNECTED)
	{ //Check WiFi connection status
    WiFi.reconnect();
  }

	if (WiFi.status() == WL_CONNECTED)
	{ //Check again WiFi connection status
#ifdef DEBUG
		Serial.println("Send GET");
#endif
		LEDOn(ledGreen);

    int rssi = WiFi.RSSI();
    String getWithRSSI = get + "&rssi=" + String(round(dBmtoPercentage(rssi) / 10.0));

		http.begin(httpClient, getWithRSSI);

    //String auth = base64::encode("login:haslo");
    http.addHeader("Authorization", "Basic " SECRET_DOMOTICZ_AUTH_BASE64);

		int httpCode = http.GET(); //Send the request

		if (httpCode == 200)
		{ //Check the returning code

#ifdef DEBUG
			String payload = http.getString();  //Get the request response payload
			Serial.println("Receive: (" + String(httpCode) + ") " + payload);
#endif
			countSendGET++;

      LEDOff(ledGreen);
		}
		else
		{
      lastError = httpCode;
#ifdef DEBUG
			Serial.println("GET Error: " + String(httpCode));
#endif
			countErrorSendGET++;

      LEDOff(ledGreen);

      int errorType = 3;
      if (httpCode < 0)
      {
        errorType = 5;
      }

      // mrugnij czerwoną diodą że błąd
      for (int i = 0; i < errorType; i++)
      {
        delay(200);
        LEDOn(ledRed);
        delay(200);
        LEDOff(ledRed);
      }  
		}

		http.end();  //Close connection
	}
  else
  { // brak połączenia z WIFI
      for (int i = 0; i < 2; i++)
      {
        delay(500);
        LEDOn(ledRed);
        delay(500);
        LEDOff(ledRed);
      }
  }
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


//const char* host = "zg.tits.vot.pl";
//const char* streamId = "....................";
//const char* privateKey = "....................";
//
//void SendData2()
//{
//	// Use WiFiClient class to create TCP connections
//	WiFiClient client;
//	const int httpPort = 16010;
//	if (!client.connect(host, httpPort))
//	{
//		//  Serial.println("connection failed");
//		return;
//	}
//
//	// We now create a URI for the request
//	String url = "/input/";
//	url += streamId;
//	url += "?private_key=";
//	url += privateKey;
//
//
//	// This will send the request to the server
//	client.print(String("GET ") + url + " HTTP/1.1\r\n" +
//		"Host: " + host + "\r\n" +
//		"Connection: close\r\n\r\n");
//	unsigned long timeout = millis();
//	while (client.available() == 0) {
//		if (millis() - timeout > 5000) {
//			//    Serial.println(">>> Client Timeout !");
//			client.stop();
//			return;
//		}
//	}
//
//	// Read all the lines of the reply from server and print them to Serial
//	while (client.available()) {
//		//  String line = client.readStringUntil('\r');
//		//  Serial.print(line);
//	}
//
//	client.stop();
//}
