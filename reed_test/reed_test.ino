#include <Adafruit_NeoPixel.h>

#define PIN_BUTTON 0
#define PIN_BUZZER 13
#define PIN_LED 4
#define PIN_REED 5

volatile boolean doorOpen = false;
volatile boolean alarmed = false; // Should the alarm be on?
volatile boolean alarm_enabled = true; // Has the alarm been enabled?
boolean silent_alarm = true;

void setup() {
  //Serial.begin(9600);
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  //Setup interrupts
  attachInterrupt(digitalPinToInterrupt(PIN_REED), onDoorChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonPress, RISING);
  
  onDoorChange(); //Take action based on beginning state
}

void loop() {
  if (alarmed) {
    // Cycle LED
    analogWrite(PIN_LED, 512);
    delay(500);
    analogWrite(PIN_LED, 0);
    delay(500);
  } else {
    delay(50);
  }
}

void onDoorChange() {
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
  stopAlarm();
}

void startBuzzer() {
  if (!silent_alarm) {
    analogWrite(PIN_BUZZER, 512); 
    analogWriteFreq(1000);
  }
}

void stopAlarm() {
  alarmed = false;
  // Turn off the light
  analogWrite(PIN_LED, 0);

  // Stop the buzzer
  analogWrite(PIN_BUZZER, 0);
}

