#include <Time.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <Temboo.h>
#include <BlynkSimpleEsp8266.h>
#include "TembooAccount.h"
#include "WiFiCreds.h"
#include "GoogleCreds.h"
#include "BlynkCreds.h"

#define PIN_BUTTON 0
#define PIN_BUZZER 13
#define PIN_LED 4
#define PIN_REED 5

WiFiClient client;

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
volatile boolean buttonPressed = false;

boolean silent_alarm = true;
boolean prevDoorOpen = doorOpen;

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

  //Setup interrupts
  attachInterrupt(digitalPinToInterrupt(PIN_REED), onDoorChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonPress, RISING);

  //Setup Blynk
  Blynk.config(BLYNK_AUTH);

  onDoorChange(); //Take action based on beginning state
}

void loop() {
  Blynk.run();
  Blynk.virtualWrite(V3, alarm_enabled);

  reportEvents();

  if (alarmed) {
    // Cycle LED
    analogWrite(PIN_LED, 512);
    delay(500);
    reportEvents();
    analogWrite(PIN_LED, 0);
    delay(500);
  } else {
    delay(50);
  }
}

void reportEvents() {
  if(prevDoorOpen != doorOpen) {
    if(doorOpen){
      Blynk.virtualWrite(V0, "Open");
      logEvent("Door opened");
      if(alarmed) {
        Blynk.notify("Door Alarm Triggered!");
        startBuzzer();
      }
    } else {
      Blynk.virtualWrite(V0, "Closed");
      logEvent("Door closed");
    }
    prevDoorOpen = doorOpen;
  }

  if(buttonPressed) {
    buttonPressed = false;
    logEvent("Button pressed");
    stopAlarm();
  }
}

void onDoorChange() {
  if (digitalRead(PIN_REED) == HIGH) {
    doorOpen = true;
    if (alarm_enabled) {
      alarmed = true;
      // The LED will be cycled in the main loop
    }
  }
  else { //digitalRead(PIN_REED) == LOW
    doorOpen = false;
  }
}

void onButtonPress() {
  buttonPressed = true;
}

void startBuzzer() {
  if (!silent_alarm) {
    analogWrite(PIN_BUZZER, 512);
    analogWriteFreq(1000);
  }

  logEvent("Alarm sounding");
}

void stopAlarm() {
  if(alarmed) {
    alarmed = false;
    // Turn off the light
    analogWrite(PIN_LED, 0);

    // Stop the buzzer
    analogWrite(PIN_BUZZER, 0);
    
    logEvent("Alarm stopped");
  }
}

String getCurrentTime() {
  char timestr[] = "0000-00-00 00:00:00";
  sprintf(timestr, "%04i-%02i-%02i %02i:%02i:%02i", year(), month(), day(), hour(), minute(), second());
  return String(timestr);
}

void logEvent(String message) {
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
  AppendValuesChoreo.addInput("RefreshToken", G_REFRESH_TOKEN);
  AppendValuesChoreo.addInput("ClientSecret", G_CLIENT_SECRET);
  AppendValuesChoreo.addInput("Values", logString);
  AppendValuesChoreo.addInput("ClientID", G_CLIENT_ID);
  AppendValuesChoreo.addInput("SpreadsheetID", G_SPREADSHEET_ID);

  // Identify the Choreo to run
  AppendValuesChoreo.setChoreo("/Library/Google/Sheets/AppendValues");

  // Run the Choreo; when results are available, print them to serial
  AppendValuesChoreo.run();

  while(AppendValuesChoreo.available()) {
    char c = AppendValuesChoreo.read();
    Serial.print(c);
  }
  AppendValuesChoreo.close();
}

BLYNK_WRITE(V1) {
  logEvent("Calling police");
}

BLYNK_WRITE(V2) {
  stopAlarm();
}

BLYNK_WRITE(V3) {
  if(alarm_enabled) {
    alarm_enabled = false;
    alarmed = false;
    logEvent("System disabled");
  } else {
    alarm_enabled = true;
    logEvent("System enabled");
  }
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
