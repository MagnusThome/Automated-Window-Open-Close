#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h> 
#include <TimeAlarms.h>
#include <EEPROM.h>


// -------------------------------------------------------------------
/*
 
  When boot up is complete and everything is working, including wifi 
  access, web page and clock sync, the unit will make TWO audible clicks.
  
  Audible SINGLE clicks during boot equals a failure to connect to an 
  NTP time server. Each time this click sounds a retry to connect 
  starts. Not until a succesful connection to an NTP server has been 
  made, so current time is known, can the boot up continue and then 
  issue a double click sound, when fully done. 

  If connecting to Wifi fails the boot up will timeout after 60 seconds
  and issue a quadruple click and reboot.

*/
// ---------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------



// EDIT YOUR INFO HERE


int timezone = 1;

const char* ssid = "xxxxxxxxx";
const char* password = "xxxxxxxxxxx";
const char* hostname = "sovrumsfonster";

const char* ota_path = "/firmware";
const char* ota_username = "xxxxxxxx";
const char* ota_password = "xxxxxxxx";

const char* mqttserver = "homeautomation";
const char* mqttpubtopic = "stan/fonster/sovrum"; 
const char* mqttsubtopic = "stan/fonster/sovrum/control";

const float windowAngleComp = 1.5;  // ADJUST SO THE angleOpen/centimeters SHOWN ON WEBPAGE/SERIAL/MQTT ARE CORRECT (DEPENDS ON HOW YOU MOUNTED THE MOTOR)

#define OPEN 12     // GPIO PIN TO DRIVE #1 RELAY TO OPEN (ACTIVE LOW)
#define CLOSE 13    // GPIO PIN TO DRIVE #2 RELAY TO CLOSE (ACTIVE LOW)
#define BUTTON 15   // GPIO PIN FOR PHYSICAL BUTTON (ACTIVE HIGH - PIN 15 HAS PULL DOWN RESISTOR)

//#define ENGLISH   // CHOOSE ONE LANGUAGE
#define SVENSKA     // CHOOSE ONE LANGUAGE






// ---------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------




WiFiClient client;
PubSubClient mqttclient(client);
WiFiUDP Udp;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;


#define NTPVERBOSE 0
IPAddress timeServerIP;
const char* ntpServerName = "time.nist.gov";
unsigned int localPort = 8888; 

AlarmId id;

boolean direction = false;
boolean running = false;

unsigned long startOpen = 0;
unsigned long startClose = 0;
unsigned long timeout = 0;
int openAngle = 0;

#define MQTTBUFFSIZE 200
char mqttbuff[MQTTBUFFSIZE];

#define EEPROMAUTOADDR 0
#define EEPROMDAYSAVEADDR 1
byte enableAutomation;
byte daylightSaving;

#ifdef SVENSKA
String languagestrings = "var webpagemessages = ['&Ouml;ppna/st&auml;ng f&ouml;nster', 'F&ouml;nstret &auml;r st&auml;ngt', 'F&ouml;nstret &auml;r &ouml;ppet ca ', '&Ouml;ppnar f&ouml;nstret, tryck igen f&ouml;r att stanna', 'St&auml;nger f&ouml;nstret, tryck igen f&ouml;r att stanna', 'Automation', 'Sommartid']";
String stringsonoroff = "var stronoroff = ['Av', 'P&aring;']";
#endif
#ifdef ENGLISH
String languagestrings = "var webpagemessages = ['Open/Close window', 'The window is closed', 'The window is open ca ', 'Opening window, click again to stop', 'Closing window, click again to stop', 'Automation', 'Daylight Savings']";
String stringsonoroff = "var stronoroff = ['Off', 'On']";
#endif






// ---------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------

// EDIT YOUR SCHEDULES HERE

void mySchedules(void) {
  Alarm.alarmRepeat(6,10,0, morningClose);
  Alarm.alarmRepeat(22,00,0, eveningOpen);
}



void morningClose(void){
  if(enableAutomation) {
    //  windowOpenTocm(2); 
    windowClose();
  }
}

void eveningOpen(void){
  if(enableAutomation) {
    windowOpenTocm(13);
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------------------






// -------------------------------------------------------------------
// SETUP

void setup(void) {  
  
  Serial.begin(115200);
  delay(250);
  Serial.println("\nBooting...");
  
  pinMode(OPEN, OUTPUT);
  digitalWrite(OPEN, HIGH);
  pinMode(CLOSE, OUTPUT);
  digitalWrite(CLOSE, HIGH);
  
  pinMode(BUTTON, INPUT);
  digitalWrite(BUTTON, LOW);

  Serial.print("Sensor \"" + String(hostname) + "\" is connecting to wifi \"" + String(ssid) + "\"...");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis()>60000) { // NO CONNECTION AFTER 60 SECONDS, REBOOT AND RETRY
      clickSound(4);
      ESP.restart();
    }
  }
  Serial.println(" done! Signal strength: " + String(WiFi.RSSI()));
  Serial.print("Webserver started at http://");
  Serial.println(WiFi.localIP());

  EEPROM.begin(512);
  enableAutomation = EEPROM.read(EEPROMAUTOADDR);
  daylightSaving = EEPROM.read(EEPROMDAYSAVEADDR);
  if (EEPROM.read(EEPROMAUTOADDR) > 1) {  // IF EEPROM NEVER INITILZED
    enableAutomation = 0;
  }
  if (EEPROM.read(EEPROMDAYSAVEADDR) > 1) {
    daylightSaving = 0;
  }

  server.on("/", handleRoot);
  server.on("/button", handleRESTbutton);
  server.on("/toggleauto", handleRESTtoggleauto);
  server.on("/daysave", handleRESTdaysave);
  server.begin();
  httpUpdater.setup(&server, ota_path, ota_username, ota_password);

  mqttclient.setServer(mqttserver, 1883);
  mqttclient.setCallback(mqttcallback);
  mqttConnect(); 

  Serial.print("Connecting to time server... ");
  Udp.begin(localPort);
  setSyncProvider(getNtpTime);
  while (timeStatus() != timeSet) {
    Serial.print("retrying... ");
    clickSound(1);
    setSyncProvider(getNtpTime);
    delay(8000);
  }
  Serial.print("done: ");
  digitalClockDisplay();
  
  
  mySchedules();
  
  clickSound(2);
}




// -------------------------------------------------------------------
// LOOP

void loop() {

  unsigned long now = millis();


  static bool prevbutton = false;
  bool button = digitalRead(BUTTON);
  
  static unsigned long timer1 = 100;
  if (now - timer1 >= 100) {
    timer1 = now;
    if (button && !prevbutton) {
      buttonAction();
    }
    prevbutton = button;
  }


  if (!mqttclient.connected()) {
    mqttConnect();
  }
  mqttclient.loop();

  server.handleClient();

  Alarm.delay(0);

  
  // RUNNING WITHOUT A STOP COMMAND FOR TOO LONG, ISSUE STOP
  if ( (timeout>0) && (millis()-timeout >= 30*1000) ) {
    Serial.print("TIMEOUT");
    windowStop();
  }
}





// -------------------------------------------------------------------
// WEB PAGE REST --OR-- PHYSICAL BUTTON CLICKED

int buttonAction (void) {

  if (!running) {
    if(!direction) {
      windowOpen();
      return (int(openAngle*windowAngleComp)*10) + 1;  // REST return data (1 is code for windows run/direction status)
    }
    else {
      windowClose();
      return (int(openAngle*windowAngleComp)*10) + 2;  // REST return data (2 is code for windows run/direction status)
    }
  }
  else {
    windowStop();
    return (int(openAngle*windowAngleComp)*10);        // REST return data (0 is code for windows run/direction status)
  }
}




// -------------------------------------------------------------------
// MOTOR CONTROL

void windowOpen (void) {
  direction = true;
  running = true;
  if (startClose) {
    openAngle -= (millis()- startClose);
    startClose = 0;
  }
  
  startOpen = millis();
  timeout = millis();
  digitalWrite(OPEN, LOW);
  digitalWrite(CLOSE, HIGH);

  snprintf( mqttbuff, MQTTBUFFSIZE, "{\"host\":\"%s\",\"type\":\"window\",\"data\":\"opening\",\"openAngle\":\"%d\"}", hostname, int(openAngle*windowAngleComp) ); 
  mqttclient.publish(mqttpubtopic, mqttbuff);
  Serial.print("\tOpening from\t");
  Serial.print(int(openAngle*windowAngleComp));
  Serial.print("\t");
  digitalClockDisplay();
}



void windowClose (void) {
  direction = false;
  running = true;
  if (startOpen) {
    openAngle += (millis()- startOpen);
    startOpen = 0;
  }

  startClose = millis();
  timeout = millis();
  digitalWrite(OPEN, HIGH);
  digitalWrite(CLOSE, LOW);

  snprintf( mqttbuff, MQTTBUFFSIZE, "{\"host\":\"%s\",\"type\":\"window\",\"data\":\"closing\",\"openAngle\":\"%d\"}", hostname, int(openAngle*windowAngleComp) ); 
  mqttclient.publish(mqttpubtopic, mqttbuff);
  Serial.print("\tClosing from\t");
  Serial.print(int(openAngle*windowAngleComp));
  Serial.print("\t");
  digitalClockDisplay();
}



void windowStop (void) {
  running = false;
  if (startClose) {
    openAngle -= (millis()- startClose);
    startClose = 0;
    
    // FORCE TO ALWAYS OPENING NEXT PRESS IF FULLY CLOSED
    if (openAngle < 0) {
      openAngle = 0;
      direction = false;   
    }
  }
  if (startOpen) {
    openAngle += (millis()- startOpen);
    startOpen = 0;
  }

  timeout = 0;
  digitalWrite(OPEN, HIGH);
  digitalWrite(CLOSE, HIGH);

  snprintf( mqttbuff, MQTTBUFFSIZE, "{\"host\":\"%s\",\"type\":\"window\",\"data\":\"stopped\",\"openAngle\":\"%d\"}", hostname, int(openAngle*windowAngleComp) ); 
  mqttclient.publish(mqttpubtopic, mqttbuff);
  Serial.print("\tStopped at  \t");
  Serial.print(int(openAngle*windowAngleComp));
  Serial.print("\t");
  digitalClockDisplay();
}







// -------------------------------------------------------------------
// OPEN WINDOW TO SPECIFIED CENTIMETERS

void windowOpenTocm (int centimeters) {
  int delayCalc = (centimeters*1000)-openAngle;
  if (delayCalc<0) {
    delayCalc = -(delayCalc);
    windowClose();
  }
  else {
    windowOpen(); 
  }
  delay(delayCalc/windowAngleComp);             // NEEDS TO BE MOVED TO MAKE UNBLOCKING
  windowStop();
}




// -------------------------------------------------------------------
// HANDLE ROOT WEB PAGE WITH A BUTTON TO CLICK ON
// THE BUTTON'S TEXT WILL SHOW STATUS OF WINDOW

void handleRoot() {
  
  String padding = "";
  if (minute()<10) {
    padding = "0";
  }
  

  String webpage = "<html><head><title>" + String(hostname) + "</title></head>\n \
    <script>" + languagestrings + "</script>\n \ 
    <script>" + stringsonoroff + "</script>\n \ 
    <script src='https://code.jquery.com/jquery-2.1.3.min.js'></script>\n \
    <script type='text/javascript' src='https://cdn.rawgit.com/Foliotek/AjaxQ/master/ajaxq.js'></script>\n \
    <style>body {width:700px;margin:20px;font-family:sans-serif;font-size:18px;font-weight:400;color:#444;}</style>\n \
    <style>button {width:100%;padding:10px;border-radius:6px;font-size:18px;font-weight:400;color:#fff;background-color:#337ab7;border:1px solid transparent;border-color:#2e6da4;text-align:center;touch-action:manipulation;cursor:pointer;box-shadow: 2px 2px 7px #888;}</style>\n \
    <body>\n \
    <button class='btn btn-block btn-lg btn-primary' id='winbtn'></button><br><br>\n \
    <div style='font-size:26px;width:100%;padding:20px;text-align:center;'>" + hour() + ":" + padding + minute() + "</div><br>\n \
    <button class='btn btn-block btn-lg btn-primary' id='autobtn'></button><br><br>\n \
    <button class='btn btn-block btn-lg btn-primary' id='daysavebtn'></button><br><br>\n \
    <div style='font-size:14px;width:100%;text-align:center;'><br>" + hostname + " (wifi " + String(WiFi.RSSI()) + "dB)</div>\n \
    <script>\n \
      $(document).ready(function() {\n \
        $('button#winbtn').html(webpagemessages[0]);\n \
        $('button#autobtn').html(webpagemessages[5] + ' ' + stronoroff[" +  String(enableAutomation) + "]);\n \
        $('button#daysavebtn').html(webpagemessages[6] + ' ' + stronoroff[" +  String(daylightSaving) + "]);\n \
        $('#winbtn').click(function() {$.getq('queue', 'http://" + (WiFi.localIP()).toString() + ":80/button', onsuccess);\n \
          function onsuccess (result) {\n \
            message = result.return_value;\n \
            if (result.return_value%10 == 0) {\n \
              if (result.return_value/10 == 0) { message = webpagemessages[1];}\n \
              else {message = webpagemessages[2] + Math.round(((result.return_value)/10000)+0.5) + \" cm\";}\n \
            }\n \
            if (result.return_value%10 == 1) { message = webpagemessages[3];}\n \
            if (result.return_value%10 == 2) { message = webpagemessages[4];}\n \
            $('button#winbtn').html(message);\n \
        }});\n \
        $('#autobtn').click(function() {$.getq('queue', 'http://" + (WiFi.localIP()).toString() + ":80/toggleauto', onsuccess);\n \
          function onsuccess (result) {\n \
            $('button#autobtn').html(webpagemessages[5] + ' ' + stronoroff[result.return_value]);\n \
          }\n \
        });\n \
        $('#daysavebtn').click(function() {$.getq('queue', 'http://" + (WiFi.localIP()).toString() + ":80/daysave', onsuccess);\n \
          function onsuccess (result) {\n \
            $('button#daysavebtn').html(webpagemessages[6] + ' ' + stronoroff[result.return_value]);\n \
          }\n \
        });\n \
      });\n \
    </script>\n \
    </body></html>";
  server.send(200, "text/html", webpage);
}




// -------------------------------------------------------------------
// HANDLE WEB REST CALLS

void handleRESTbutton() {
  Serial.println("handleRESTbutton");
  int restdata = buttonAction();
  String reststring = "{\"return_value\": " + String(restdata) + ", \"name\": \"" + String(hostname) + "\"}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", reststring);
  Serial.println(reststring);
}


void handleRESTtoggleauto() {
  Serial.print("handleRESTtoggleauto. Value saved in EEPROM: ");
  if (enableAutomation==0) { enableAutomation = 1; }
  else { enableAutomation = 0; }
  EEPROM.write(EEPROMAUTOADDR, enableAutomation);
  EEPROM.commit();
  Serial.println(EEPROM.read(EEPROMAUTOADDR));
  String reststring = "{\"return_value\": " + String(enableAutomation) + ", \"name\": \"" + String(hostname) + "\"}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", reststring);
  Serial.println(reststring);
}


void handleRESTdaysave() {
  Serial.print("handleRESTdaysave. Value saved in EEPROM: ");
  if (daylightSaving==0) { daylightSaving = 1; }
  else { daylightSaving = 0; }
  EEPROM.write(EEPROMDAYSAVEADDR, daylightSaving);
  EEPROM.commit();
  Serial.println(EEPROM.read(EEPROMDAYSAVEADDR));
  String reststring = "{\"return_value\": " + String(daylightSaving) + ", \"name\": \"" + String(hostname) + "\"}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", reststring);
  Serial.println(reststring);

  Serial.print("Setting NTP... ");
  setSyncProvider(getNtpTime);
  getNtpTime();
  Serial.println("done!");
}


// -------------------------------------------------------------------
// MQTT INCOMING SUBSCRIPTION MESSAGES

void mqttcallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (payload[0]=='o') {
    windowOpen();
  }
  if (payload[0]=='c') {
    windowClose();
  }
  if (payload[0]=='s') {
    windowStop();
  }
}



//------------------------------------------
// CONNECT TO MQTT

void mqttConnect() {
  if (!mqttclient.connected()) {
    Serial.print("Connecting to mqtt server \"" + String(mqttserver) + "\"...");
    if (mqttclient.connect(hostname)) {
      Serial.println(" done!");
      snprintf( mqttbuff, MQTTBUFFSIZE, "{\"host\":\"%s\",\"type\":\"wifi\",\"data\":\"%d\"}", hostname, WiFi.RSSI() ); 
      mqttclient.publish(mqttpubtopic, mqttbuff);
      mqttclient.subscribe(mqttsubtopic);
    } 
    else {
      Serial.print("failed, rc=");
      Serial.print(mqttclient.state());
      Serial.println(" failed!");
    }
  }
}



// -------------------------------------------------------------------
void digitalClockDisplay(){

  Serial.print(year()); 
  Serial.print("-"); 
  Serial.print(month());
  Serial.print("-"); 
  Serial.print(day());
  Serial.print(" ");
  printDigits(hour());
  Serial.print(":");
  printDigits(minute());
  Serial.print(":");
  printDigits(second());
  Serial.println();
}


void printDigits(int digits){
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}




// -------------------------------------------------------------------
// AUDIBLE CLICKING OF RELAYS JUST FOR ALERTING USER

// SINCE BOTH RELAYS ARE ACTIVATED THE MOTOR DOES _NOT_ START, 
// NOT EVEN DURING THE SHORT PULSES.

void clickSound(int clicks) {
    for (int i=0;i<clicks;i++) {
      digitalWrite(OPEN, LOW);      
      digitalWrite(CLOSE, LOW);
      delay(6);
      digitalWrite(OPEN, HIGH);
      digitalWrite(CLOSE, HIGH);
      delay(170);
    }
}




// -------------------------------------------------------------------
// GET TIME FROM NTP SERVERS 

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  if (NTPVERBOSE) {
    Serial.print("Transmit NTP Request ");
    Serial.print(timeServerIP);
    Serial.print("... ");
  }
  WiFi.hostByName(ntpServerName, timeServerIP);
  sendNTPpacket(timeServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 8000) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      if (NTPVERBOSE) {
        Serial.println("ok!");
      }
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + ((daylightSaving+timezone) * SECS_PER_HOUR);
    }
  }
  if (NTPVERBOSE) {
    Serial.println("No NTP Response!");
  }
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
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}






// -------------------------------------------------------------------
