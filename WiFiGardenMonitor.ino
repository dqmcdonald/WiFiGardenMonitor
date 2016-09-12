

/* A Garden monitor - to control irrigration and monitor
    moisture levels

    D Q McDonald
    September 2016
 *  */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <Time.h>
#include <TimeLib.h>
#include <RHReliableDatagram.h>
#include <RH_NRF24.h>
#include <SPI.h>

#define CLIENT_ADDRESS 1
#define SERVER_ADDRESS 2

#include "gauge.h"

// Singleton instance of the radio driver
RH_NRF24 driver(D2, D8);

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram manager(driver, SERVER_ADDRESS);

uint8_t data[] = "And hello back to you";
// Dont put this on the stack:
uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];

class SensorData {
  public:
    char station_name[16];
    uint16_t count1;
    uint16_t count2;
    uint32_t count3;
};


SensorData sd;


#define PUSH_DATA_FREQUENCY 1200 // Frequency of pushing data in seconds

char buff[128];
String esid;

//NTP stuff:
WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
// NTP Servers:
static const char ntpServerName[] = "nz.pool.ntp.org";
time_t getNtpTime();
void sendNTPpacket(IPAddress &address);
const int timeZone = -12;   //NZST

const int tempPin = 17; // ADC pin

int relay_pin = D1;
int value = LOW;
WiFiServer server(80);
IPAddress ip(192, 168, 1, 99); // where xx is the desired IP Address
IPAddress gateway(192, 168, 1, 1); // set gateway to match your network

int get_temperature();

const char* host = "data.sparkfun.com";
const char* publicKey = "n1Vg4mDx1nI2yAy07n0a";
const char* privateKey = "Mo9PMV0WomIzaNaA75AV";
long int next_push_time = 0;


void push_data();
void handle_webserver();


void setup() {
  EEPROM.begin(512);
  Serial.begin(9600);
  delay(10);


  pinMode(relay_pin, OUTPUT);
  digitalWrite(relay_pin, HIGH);


  Serial.print(F("Setting static ip to : "));
  Serial.println(ip);

  // read eeprom for ssid and pass
  Serial.println("Reading EEPROM ssid");

  for (int i = 0; i < 32; ++i)
  {
    esid += char(EEPROM.read(i));
  }
  Serial.print("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM pass");
  String epass = "";
  for (int i = 32; i < 96; ++i)
  {
    epass += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(epass);


  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(esid);
  IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(esid.c_str(), epass.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.print("Use this URL : ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  ArduinoOTA.setHostname("GardenMonitor");
  ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IPess: ");
  Serial.println(WiFi.localIP());
  pinMode(2, OUTPUT);

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
  Serial.println("waiting for sync");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);


  if (!manager.init())
    Serial.println("NRF204 init failed");

}

void loop() {

  ArduinoOTA.handle();

  // Wait for a message addressed to us from the client
  uint8_t len = sizeof(buf);
  uint8_t from;
  if (manager.recvfromAck(buf, &len, &from))
  {
    Serial.print("got request from : 0x");
    memcpy( &sd, buf, sizeof(sd));
    Serial.print(from, HEX);
    Serial.print(": ");
    Serial.println(sd.station_name);
    Serial.println(sd.count1);
    Serial.println(sd.count2);
    Serial.println(sd.count3);
    

    // Send a reply back to the originator client
    if (!manager.sendtoWait(data, sizeof(data), from))
      Serial.println("sendtoWait failed");
  }



  push_data();

  handle_webserver();


}


// Read temperature from LM35 sensor on pin 17
int get_temperature() {
  float temp;
  temp = analogRead(tempPin);
  temp = temp * 3.3 / 1024.0 * 100.0; // 10mV per degreee
  Serial.print("Temperature = ");
  Serial.println(temp);
  return round(temp);
}

void push_data()
{
  if ( next_push_time != 0 && next_push_time > millis() ) {
    return;
  }

  Serial.println("Pushing data");

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return;
  }

  // We now create a URI for the request
  String url = "/input/";
  url += publicKey;
  url += "?private_key=";
  url += privateKey;
  url += "&temp=";
  url += get_temperature();

  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  delay(10);

  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println();
  Serial.println("closing connection");
  next_push_time = millis() + PUSH_DATA_FREQUENCY * 1000;




}


void handle_webserver()
{

  long rssi;
  long wifi_strength;
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  rssi = WiFi.RSSI();  // eg. -63
  wifi_strength = (100 + rssi);


  // Wait until the client sends some data
  Serial.println("new client");
  while (!client.available()) {
    delay(1);
  }

  // Read the first line of the request
  String request = client.readStringUntil('\r');
  Serial.println(request);
  client.flush();

  // Match the request
  if (request.indexOf("/?but=on") != -1) {
    digitalWrite(relay_pin, LOW);
    value = HIGH;
  }
  if (request.indexOf("/?but=off") != -1) {
    digitalWrite(relay_pin, HIGH);
    value = LOW;
  }

  // Return the response
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println(""); //  do not forget this one
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");
  client.println("<meta http-equiv='refresh' content='10'>");

  client.print("<H1>Welcome to Q's D1 mini</H1>");
  client.print("<H3> Date: " );
  client.print(day());
  client.print(" ");
  client.print(monthStr(month()));
  client.print(" ");
  client.print(year());
  client.print("<br>Time: " );
  int h = hour();
  if ( h < 10 )
    client.print("0");
  client.print( h);
  client.print(":");
  int m = minute();
  if ( m < 10 )
    client.print("0");
  client.print(m);
  client.print(":");
  int s = second();
  if ( s < 10 )
    client.print("0");
  client.print(s);
  client.println("");


  client.print("<br>Wifi connected to ");
  client.print( esid  );
  client.print(" </H3> " );

  String g = gauge_str;
  String t = "";
  t += get_temperature();
  String w = "";
  w += wifi_strength;
  g.replace("|NAME1|", "Temperature");
  g.replace("|VAL1|", t);
  g.replace("|NAME2|", "WiFi %");
  g.replace("|VAL2|", w);

  client.print(g);

  client.print("<H2>Pump pin is now: ");

  if (value == HIGH) {
    client.print("On</H2>");
  } else {
    client.print("Off</H2>");
  }
  client.println("");

  client.println("<form method='get'> <button type=\"submit\" name=\"but\" value='on'><b>Turn Pump On</b></button>");
  client.println("<button type=\"submit\" name=\"but\" value='off'><b>Turn Pump Off</b></button></form>");


  client.println("<br><img src=\"https://usercontent2.hubstatic.com/7883439.jpg\" alt=\"Robbie\" style=\"width:304px;height:228px;\">");


  client.println("</html>");

  delay(1);
  Serial.println("Client disconnected");
  Serial.println("");


}
/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}


