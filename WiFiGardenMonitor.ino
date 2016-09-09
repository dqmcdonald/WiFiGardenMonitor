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


long rssi;
long wifi_strength;
char buff[128];

int relay_pin = D1;
int value = LOW;
WiFiServer server(80);
IPAddress ip(192, 168, 1, 99); // where xx is the desired IP Address
IPAddress gateway(192, 168, 1, 1); // set gateway to match your network


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
  String esid;
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



}

void loop() {

  ArduinoOTA.handle();

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
  client.print("<h3>Wifi Strength is: ");
  client.print(wifi_strength);
  client.print("% <br>");

  client.print("<canvas id=\"myCanvas\" width=\"300\" height=\"25\" style=\"border:1px solid #000000;\"></canvas><br><br>");

  client.print("<script>");
  client.print("var c = document.getElementById(\"myCanvas\");");
  client.print("var ctx = c.getContext(\"2d\");");
  client.print("ctx.fillStyle = \"#FF0000\";");
  wifi_strength = (int)(wifi_strength / 100.0 * 300);
  snprintf(buff, sizeof(buff), "ctx.fillRect(0,0,%d,25);", wifi_strength);
  client.print(buff);
  client.println("</script></H3>");

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
