#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <time.h>

#define LED_PIN     D4
#define NUM_DIGITS  4
#define NUM_SEGMENTS 7
#define LEDS_PER_SEG 2
#define NUM_LEDS 56

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(80);

String ssid = "";
String password = "";
String apiKey = "";
String rbl = "";

String lastDisplayValue = "----";
String apiLog = "";
unsigned long lastUpdate = 0;
unsigned long lastSecTick = 0;
int secondsToBus = 0;
bool blinkState = false;

const uint8_t digitSegmentMap[NUM_DIGITS][14] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13 },
  {14,15,16,17,18,19,20,21,22,23,24,25,26,27 },
  {28,29,30,31,32,33,34,35,36,37,38,39,40,41 },
  {42,43,44,45,46,47,48,49,50,51,52,53,54,55 }
};

const uint8_t digitPatterns[10][7] = {
  {1,1,1,1,1,1,0}, // 0
  {0,1,1,0,0,0,0}, // 1
  {1,1,0,1,1,0,1}, // 2
  {1,1,1,1,0,0,1}, // 3
  {0,1,1,0,0,1,1}, // 4
  {1,0,1,1,0,1,1}, // 5
  {1,0,1,1,1,1,1}, // 6
  {1,1,1,0,0,0,0}, // 7
  {1,1,1,1,1,1,1}, // 8
  {1,1,1,1,0,1,1}  // 9
};
// Minus: Segment 6 = Mitte unten (counted from 0)
const uint8_t minusSeg[14] = {6, 20}; // Segment 6 in Digit 0 und 1

// ---------- LittleFS Konfig speichern/lesen ----------
void saveConfig() {
  File f = LittleFS.open("/config.txt", "w");
  if(!f){
    Serial.println("Fehler beim Öffnen von /config.txt zum Schreiben!");
    return;
  }
  f.println(ssid);
  f.println(password);
  f.println(apiKey);
  f.println(rbl);
  f.close();
  Serial.println("---LittleFS gespeichert---");
  Serial.println("SSID: " + ssid);
  Serial.println("Passwort: " + password);
  Serial.println("API-Key: " + apiKey);
  Serial.println("RBL: " + rbl);
}

void loadConfig() {
  if(!LittleFS.exists("/config.txt")){
    Serial.println("---LittleFS: Noch keine Konfiguration gespeichert.---");
    return;
  }
  File f = LittleFS.open("/config.txt", "r");
  if(!f){
    Serial.println("Fehler beim Öffnen von /config.txt zum Lesen!");
    return;
  }
  ssid = f.readStringUntil('\n'); ssid.trim();
  password = f.readStringUntil('\n'); password.trim();
  apiKey = f.readStringUntil('\n'); apiKey.trim();
  rbl = f.readStringUntil('\n'); rbl.trim();
  f.close();
  Serial.println("---LittleFS Konfig geladen---");
  Serial.println("SSID: " + ssid);
  Serial.println("Passwort: " + password);
  Serial.println("API-Key: " + apiKey);
  Serial.println("RBL: " + rbl);
}

// Hilfsfunktion: ISO nach Unixzeit, keine Offsetkorrektur!
time_t parseISOTimestamp(String iso){
  int year   = iso.substring(0,4).toInt();
  int month  = iso.substring(5,7).toInt();
  int day    = iso.substring(8,10).toInt();
  int hour   = iso.substring(11,13).toInt();
  int minute = iso.substring(14,16).toInt();
  int second = iso.substring(17,19).toInt();
  struct tm t;
  t.tm_year = year-1900;
  t.tm_mon  = month-1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min  = minute;
  t.tm_sec = second;
  t.tm_isdst = -1;
  return mktime(&t); // keine Offsetkorrektur!
}

// Webserver Seiten
void handleRoot() {
  String html = "<h2>Wemos D1 Mini Anzeigen-Konfiguration</h2>";
  html += "<form action='/save' method='POST'>"
      "SSID: <input name='ssid' value='" + ssid + "'><br>"
      "WLAN-Passwort: <input name='password' type='password' value='" + password + "'><br>"
      "API-Key: <input name='apikey' value='" + apiKey + "'><br>"
      "RBL: <input name='rbl' value='" + rbl + "'><br>"
      "<input type='submit' value='Speichern'></form>";
  html += "<hr><a href='/status'>Status & API-Log</a>";
  server.send(200, "text/html", html);
}

void handleSave() {
  ssid = server.arg("ssid");
  password = server.arg("password");
  apiKey = server.arg("apikey");
  rbl = server.arg("rbl");

  Serial.println("---Werte aus Webformular übernommen---");
  Serial.println("SSID: " + ssid);
  Serial.println("Passwort: " + password);
  Serial.println("API-Key: " + apiKey);
  Serial.println("RBL: " + rbl);

  saveConfig();
  server.send(200, "text/html", "Daten gespeichert. Neustart...");
  delay(500);
  ESP.restart();
}

void handleStatus() {
  String html = "<h2>Aktuelle LED-Anzeige:</h2>";
  html += "<div id='countdown' style='color:green;font-size:2em;font-weight:bold'>";
  html += lastDisplayValue;
  html += "</div>";
  html +=
    "<script>var sec="+String(secondsToBus)+";\
     var el=document.getElementById('countdown');\
     function update(){\
       if(sec>0){\
         var min=Math.floor(sec/60);\
         var s=sec%60;\
         el.innerHTML=('0'+min).slice(-2)+\":\"+('0'+s).slice(-2);\
         sec--;\
       }else{el.innerHTML='--';}\
     }\
     setInterval(update,1000);\
     update();\
    </script>";
  html += "<h3>API Log:</h3>";
  html += "<textarea rows='20' cols='75' readonly>" + apiLog + "</textarea>";
  html += "<br><a href='/'>Zurück zur Konfiguration</a>";
  server.send(200, "text/html", html);
}


void connectWifi() {
  Serial.println("Verbinde mit WLAN...");
  WiFi.begin(ssid.c_str(), password.c_str());
  int tries=0;
  while (WiFi.status() != WL_CONNECTED && tries<30){
    delay(333); Serial.print("."); tries++;
  }
  if (WiFi.status()==WL_CONNECTED) {
    Serial.println("\nVerbunden! IP: " + WiFi.localIP().toString());
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Europa/Wien Sommer/Winter automatisch
    tzset();
    time_t now = time(nullptr);
    Serial.println(ctime(&now));
  } else {
    Serial.println("\nWLAN Fehler!");
  }
}

int fetchBusCountdown(String rbl, String key) {
  WiFiClientSecure client;
  client.setInsecure();
  String url = "/ogd_realtime/monitor?rbl=" + rbl +
               "&activateTrafficInfo=stoerungkurz&sender=" + key;

  time_t now = time(nullptr);
  struct tm *tm_ = localtime(&now); 
  char tbuf[10];
  sprintf(tbuf, "%02d:%02d:%02d", tm_->tm_hour, tm_->tm_min, tm_->tm_sec);

  if (!client.connect("www.wienerlinien.at", 443)) {
    apiLog += "[" + String(tbuf) + "] Fehler: keine Verbindung\n";
    Serial.printf("[%s] Fehler: keine Verbindung\n", tbuf);
    return -1;
  }
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: www.wienerlinien.at\r\n" +
               "Connection: close\r\n\r\n");

  String payload = "";
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }
  while (client.available()) payload += client.readString();
  client.stop();

  apiLog += "[" + String(tbuf) + "] API Payload:\n";
  apiLog += payload + "\n";

  int idxReal = payload.indexOf("\"timeReal\":\"");
  String real = "";
  if (idxReal > 0) {
    real = payload.substring(idxReal + 12, idxReal + 12 + 24);
  }

  time_t tReal = parseISOTimestamp(real);     
  time_t nowTime = time(nullptr);             
  long countSeconds = tReal - nowTime;
  if (countSeconds < 0) countSeconds = 0;

  apiLog += "--- Sekundengenauer Countdown ab jetzt: " + String(countSeconds) + " ---\n\n";
  if (apiLog.length() > 6000) apiLog = apiLog.substring(apiLog.length()-6000);

  return countSeconds;
}

// Neue Funktion: LED zeigt zwei blinkende Minus!
void displayBlinkMinus(bool state) {
  strip.clear();
  if (state) {
    // Minus links (Digit 0, Segment 6)
    for(int l=0; l<LEDS_PER_SEG; l++) 
      strip.setPixelColor(digitSegmentMap[0][6*LEDS_PER_SEG+l], strip.Color(0,0,255));
  } else {
    // Minus rechts (Digit 1, Segment 6)
    for(int l=0; l<LEDS_PER_SEG; l++) 
      strip.setPixelColor(digitSegmentMap[1][6*LEDS_PER_SEG+l], strip.Color(0,0,255));
  }
  strip.show();
  lastDisplayValue = "--";
}

void displayCountdown(int seconds){
  int min = seconds / 60;
  int sec = seconds % 60;
  if (min < 0) min = 0;
  if (sec < 0) sec = 0;
  char buf[6];
  sprintf(buf, "%02d:%02d", min, sec);
  lastDisplayValue = String(buf);

  int digits[4] = { min/10, min%10, sec/10, sec%10 };
  strip.clear();
  for(int d=0; d<NUM_DIGITS; d++){
    for(int s=0; s<NUM_SEGMENTS; s++){
      for(int l=0; l<LEDS_PER_SEG; l++){
        int ledIndex = digitSegmentMap[d][s*LEDS_PER_SEG+l];
        if(digits[d] >= 0 && digits[d] <= 9 && digitPatterns[digits[d]][s])
          strip.setPixelColor(ledIndex, strip.Color(0,255,0));
      }
    }
  }
  strip.show();
}

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show();

  LittleFS.begin();
  loadConfig();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  if(ssid.equals("") || password.equals("")) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("WienerLinienDisplaySetup", "12345678");
    Serial.println("Access Point: WienerLinienDisplaySetup / Passwort: 12345678");
    Serial.println("Web-Konfiguration auf http://192.168.4.1/");
  }
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", handleStatus);
  server.begin();

  if(!(ssid.equals("") || password.equals("")))
    connectWifi();
}

void loop() {
  server.handleClient();

  static int secCounter = 0;
  // API aktualisieren alle 20s
  if(millis()-lastUpdate > 20000) {
    secondsToBus = fetchBusCountdown(rbl, apiKey);
    lastUpdate = millis();
    secCounter = 0;
  }
  // Sekundenzähler anzeigen
  if(secondsToBus > 0) {
    if(millis()-lastSecTick > 1000) {
      secondsToBus--;
      displayCountdown(secondsToBus);
      lastSecTick = millis();
    }
  } else {
    // Abwechselndes Minus blinken, bei 0 angekommen
    if(millis()-lastSecTick > 666) {
      blinkState = !blinkState;
      displayBlinkMinus(blinkState);
      lastSecTick = millis();
    }
  }
}
