#include <Time.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <Temboo.h>
#include "TembooAccount.h"
#include "WiFiCreds.h"

#define PIN_BUTTON 0
#define PIN_BUZZER 13
#define PIN_REED 2

WiFiClient client;

volatile boolean lock = false;

/* -------- LED Stuff -------- */
#define PIN_LED 15 
Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, PIN_LED, NEO_GRB + NEO_KHZ800);
/* -------- End LED Stuff -------- */

/* -------- NTP Stuff -------- */
WiFiUDP Udp;
unsigned int localPort = 8888;

// NTP Servers:
static const char ntpServerName[] = "us.pool.ntp.org";

//const int timeZone = -5;  // Eastern Standard Time (USA)
const int timeZone = -4;  // Eastern Daylight Time (USA)

time_t getNtpTime();
void sendNTPpacket(IPAddress &address);
/* -------- End NTP Stuff -------- */

// Alarm stuff
volatile boolean doorOpen = false;
volatile boolean alarmed = false; // Should the alarm be on?
volatile boolean alarm_enabled = true; // Has the alarm been enabled?
boolean silent_alarm = true;

// Our functions
String getCurrentTime();
void logEvent(String message);
void onDoorChange();
void onButtonPress();
void startBuzzer();
void stopAlarm();

void setup() {
  Serial.begin(115200);
  Serial.println();

  // Initialize WiFi connection
  WiFi.begin(ssid, pass); //connecting to the router
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  // Start UDP and NTP
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
  Serial.println("waiting for time sync");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  // Setup reed switch
  pinMode(PIN_REED, INPUT_PULLUP);

  pinMode(PIN_BUTTON, INPUT);

  //Configure pixel light
  pixel.begin(); // initialize LED 
  pixel.setBrightness(48); // lower brightess (default is 256)  
  pixel.show();

  //Setup interrupts
//  attachInterrupt(digitalPinToInterrupt(PIN_REED), onDoorChange, CHANGE);
//  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonPress, RISING);
  
//  onDoorChange(); //Take action based on beginning state
}

void loop() {
//  if (alarmed) {
//    // Cycle LED
//    pixel.setPixelColor(0, pixel.Color(255,0,0));
//    pixel.show();
//    delay(500);
//    pixel.setPixelColor(0, pixel.Color(0,0,0));
//    pixel.show();
//    delay(500);
//  } else {
//    delay(50);
//  }

  if(digitalRead(PIN_BUTTON) == LOW) {
    logEvent("Button pressed");
  }

//  logEvent("Test log");
//  delay(3000);
}

void onDoorChange() {
  logEvent("Door status changed");
  
  if (digitalRead(PIN_REED) == HIGH) {
    doorOpen = true;
    if (alarm_enabled) {
      alarmed = true;
      startBuzzer();
      // The LED will be cycled in the main loop
    }
  }
  else { //digitalRead(PIN_REED) == LOW
    doorOpen = false;
    stopAlarm();
  }
}

void onButtonPress() {
  logEvent("Button pressed");
//  stopAlarm();
}

void startBuzzer() {
  logEvent("Alarm sounding");
  
  if (!silent_alarm) {
    analogWrite(PIN_BUZZER, 512); 
    analogWriteFreq(1000);
  }
}

void stopAlarm() {
  if(alarmed) {
    logEvent("Alarm stopped");
    
    alarmed = false;
    // Turn off the light
    pixel.setPixelColor(0, pixel.Color(0,0,0));
    pixel.show();
  
    // Stop the buzzer
    analogWrite(PIN_BUZZER, 0);
  }
}

String getCurrentTime() {
  char timestr[] = "0000-00-00 00:00:00";
  sprintf(timestr, "%04i-%02i-%02i %02i:%02i:%02i", year(), month(), day(), hour(), minute(), second());
  return String(timestr);
}

void logEvent(String message) {
  while(lock) {
    delay(1000);
  }

  lock = true;
  
  String logString = "[[\"" + getCurrentTime() + "\", \"" + message + "\"]]";
  Serial.print("Log event: ");
  Serial.println(logString);
  
  TembooChoreo AppendValuesChoreo(client);

  // Invoke the Temboo client
  AppendValuesChoreo.begin();

  // Set Temboo account credentials
  AppendValuesChoreo.setAccountName(TEMBOO_ACCOUNT);
  AppendValuesChoreo.setAppKeyName(TEMBOO_APP_KEY_NAME);
  AppendValuesChoreo.setAppKey(TEMBOO_APP_KEY);
    
  // Set Choreo inputs
  AppendValuesChoreo.addInput("RefreshToken", "1/9sCCEcIq6kMSbTKsdNam5VbaSVTq-YfpOdTJd_GJm9kyL5BAjLLuduTQyhi7g8Em");
  AppendValuesChoreo.addInput("ClientSecret", "t_5ksvv_Pl1fP6SZE25flqqM");
  AppendValuesChoreo.addInput("Values", logString);
  AppendValuesChoreo.addInput("ClientID", "13405612906-l5adf3o93tfp304cp6rgct6ui9buigj5.apps.googleusercontent.com");
  AppendValuesChoreo.addInput("SpreadsheetID", "18G0_ZlwhWQdamD5wjMZKELFqDkA9PSVelmuWC0Hzsik");
  
  // Identify the Choreo to run
  AppendValuesChoreo.setChoreo("/Library/Google/Sheets/AppendValues");
  
  // Run the Choreo; when results are available, print them to serial
  AppendValuesChoreo.run();
  
  while(AppendValuesChoreo.available()) {
    char c = AppendValuesChoreo.read();
    Serial.print(c);
  }
  AppendValuesChoreo.close();

  lock = false;
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime() {
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
void sendNTPpacket(IPAddress &address) {
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
