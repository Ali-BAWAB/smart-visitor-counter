#include <FastLED.h>
#include <Wire.h>
#include <DS3231.h>
#include <LiquidCrystal.h>

// ======== Sharp IR Pins ========
const int entrancePin = A0;
const int exitPin     = A1;

// ======== Thresholds ========
int entranceThreshold = 220;
int exitThreshold     = 220;
const int hysteresis  = 25;

// ======== Counters ========
int in_count = 0;
int out_count = 0;
int current_count = 0;
int last_count = -1;

// ======== STATE MACHINE ========
enum State {
  IDLE,
  ENTRY_START,
  ENTRY_MIDDLE,
  EXIT_START,
  EXIT_MIDDLE
};

State state = IDLE;

unsigned long stateTime = 0;
const unsigned long timeoutMs = 3000;

// ======== LED STRIP ========
#define NUM_LEDS 90
#define LED_PIN 6

CRGB leds[NUM_LEDS];

// ======== RELAY PIN ========
const int relayPin = 5;

// ======== BUZZER TIMING ========
unsigned long buzzerTimer = 0;

const unsigned long buzzerOnTime  = 3000;  // 3 seconds
const unsigned long buzzerOffTime = 5000;  // 5 seconds

bool buzzerState = false;
bool buzzerActive = false;

// ======== LED FADING ========
int fadeValue = 0;
int fadeDirection = 5;

unsigned long lastFadeMillis = 0;
const unsigned long fadeInterval = 20;

// ======== LED ZONES ========
enum LightZone {
  ZONE_OFF,
  ZONE_GREEN,
  ZONE_YELLOW,
  ZONE_RED
};

LightZone currentZone = ZONE_OFF;

// ======== RTC & LCD ========
DS3231 rtc;

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

// ======== DIRECTION FLAGS ========
bool entranceClearedFirst = false;
bool exitClearedFirst = false;


// ============================================================
// SENSOR FUNCTIONS
// ============================================================

int smoothRead(int pin) {

  long sum = 0;

  for (int i = 0; i < 6; i++) {

    sum += analogRead(pin);
    delayMicroseconds(300);
  }

  return sum / 6;
}


bool isBlocked(int v, int t) {

  return v > t + hysteresis;
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100);

  Wire.begin();
  rtc.begin();

  lcd.begin(16, 2);

  lcd.print("System Ready");

  delay(1000);

  lcd.clear();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // ==========================================================
  // READ SENSORS
  // ==========================================================

  int entranceVal = smoothRead(entrancePin);
  int exitVal     = smoothRead(exitPin);

  bool entranceBlocked = isBlocked(
    entranceVal,
    entranceThreshold
  );

  bool exitBlocked = isBlocked(
    exitVal,
    exitThreshold
  );


  // ==========================================================
  // DIRECTION DETECTION FLAGS
  // ==========================================================

  if (entranceBlocked && exitBlocked) {

    entranceClearedFirst = false;
    exitClearedFirst = false;
  }

  if (!entranceBlocked &&
      exitBlocked &&
      !entranceClearedFirst &&
      !exitClearedFirst) {

    entranceClearedFirst = true;
  }

  if (!exitBlocked &&
      entranceBlocked &&
      !entranceClearedFirst &&
      !exitClearedFirst) {

    exitClearedFirst = true;
  }


  // ==========================================================
  // STATE MACHINE
  // ==========================================================

  switch (state) {

    case IDLE:

      if (entranceBlocked && !exitBlocked) {

        state = ENTRY_START;
        stateTime = millis();
      }

      else if (exitBlocked && !entranceBlocked) {

        state = EXIT_START;
        stateTime = millis();
      }

      break;


    case ENTRY_START:

      if (entranceBlocked && exitBlocked) {

        state = ENTRY_MIDDLE;
      }

      else if (millis() - stateTime > timeoutMs) {

        state = IDLE;
      }

      break;


    case ENTRY_MIDDLE:

      if (!entranceBlocked && !exitBlocked) {

        if (entranceClearedFirst) {

          in_count++;
        }

        state = IDLE;
      }

      break;


    case EXIT_START:

      if (entranceBlocked && exitBlocked) {

        state = EXIT_MIDDLE;
      }

      else if (millis() - stateTime > timeoutMs) {

        state = IDLE;
      }

      break;


    case EXIT_MIDDLE:

      if (!entranceBlocked && !exitBlocked) {

        if (exitClearedFirst && out_count < in_count) {

          out_count++;
        }

        state = IDLE;
      }

      break;
  }


  // ==========================================================
  // CURRENT VISITOR COUNT
  // ==========================================================

  current_count = in_count - out_count;


  // ==========================================================
  // DETERMINE LIGHT ZONE
  // ==========================================================

  LightZone newZone;

  if (current_count == 0) {

    newZone = ZONE_OFF;
  }

  else if (current_count <= 3) {

    newZone = ZONE_GREEN;
  }

  else if (current_count <= 5) {

    newZone = ZONE_YELLOW;
  }

  else {

    newZone = ZONE_RED;
  }


  // ==========================================================
  // ZONE CHANGE DETECTION
  // ==========================================================

  if (newZone != currentZone) {

    currentZone = newZone;

    // Reset fade ONLY when changing color zone
    if (currentZone == ZONE_YELLOW ||
        currentZone == ZONE_RED) {

      fadeValue = 0;
      fadeDirection = 5;
      lastFadeMillis = millis();
    }
  }


  // ==========================================================
  // LCD UPDATE
  // ==========================================================

  if (current_count != last_count) {

    last_count = current_count;

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print("Visitors: ");
    lcd.print(current_count);
  }


  // ==========================================================
  // RTC / LCD TIME
  // ==========================================================

  RTCDateTime dt = rtc.getDateTime();

  lcd.setCursor(0, 1);

  lcd.print("Time ");

  if (dt.hour < 10) {
    lcd.print("0");
  }

  lcd.print(dt.hour);
  lcd.print(":");

  if (dt.minute < 10) {
    lcd.print("0");
  }

  lcd.print(dt.minute);

  lcd.print("   ");


  unsigned long now = millis();


  // ==========================================================
  // 0 VISITORS
  // ==========================================================

  if (currentZone == ZONE_OFF) {

    // Buzzer OFF
    digitalWrite(relayPin, LOW);

    buzzerState = false;
    buzzerActive = false;


    // White after 16:00
    // OFF before 16:00

    if (dt.hour >= 16) {

      fill_solid(
        leds,
        NUM_LEDS,
        CRGB::White
      );
    }

    else {

      fill_solid(
        leds,
        NUM_LEDS,
        CRGB::Black
      );
    }

    FastLED.show();
  }


  // ==========================================================
  // 1–3 VISITORS
  // GREEN ZONE
  // ==========================================================

  else if (currentZone == ZONE_GREEN) {

    // Buzzer OFF
    digitalWrite(relayPin, LOW);

    buzzerState = false;
    buzzerActive = false;


    // Solid green

    fill_solid(
      leds,
      NUM_LEDS,
      CRGB::Green
    );

    FastLED.show();
  }


  // ==========================================================
  // 4–5 VISITORS
  // YELLOW WARNING ZONE
  // ==========================================================

  else if (currentZone == ZONE_YELLOW) {

    // Buzzer OFF
    digitalWrite(relayPin, LOW);

    buzzerState = false;
    buzzerActive = false;


    // Continuous yellow fading
    // Fade is NOT reset when count changes
    // between 4 and 5.

    if (now - lastFadeMillis >= fadeInterval) {

      fadeValue += fadeDirection;


      if (fadeValue >= 255 ||
          fadeValue <= 0) {

        fadeDirection = -fadeDirection;
      }


      fadeValue = constrain(
        fadeValue,
        0,
        255
      );


      fill_solid(
        leds,
        NUM_LEDS,
        CRGB(
          255,
          255,
          0
        )
      );


      FastLED.show();

      lastFadeMillis = now;
    }
  }


  // ==========================================================
  // >5 VISITORS
  // RED WARNING / ALARM ZONE
  // ==========================================================

  else if (currentZone == ZONE_RED) {

    // ========================================================
    // BUZZER
    // 3 SECONDS ON / 5 SECONDS OFF
    // ========================================================

    if (!buzzerActive) {

      // Start buzzer cycle only once
      // when entering the red zone

      buzzerActive = true;

      buzzerState = true;

      buzzerTimer = now;

      digitalWrite(relayPin, HIGH);
    }

    else if (buzzerState) {

      // Buzzer currently ON

      if (now - buzzerTimer >= buzzerOnTime) {

        buzzerState = false;

        buzzerTimer = now;

        digitalWrite(relayPin, LOW);
      }
    }

    else {

      // Buzzer currently OFF

      if (now - buzzerTimer >= buzzerOffTime) {

        buzzerState = true;

        buzzerTimer = now;

        digitalWrite(relayPin, HIGH);
      }
    }


    // ========================================================
    // CONTINUOUS RED FADING
    // ========================================================

    if (now - lastFadeMillis >= fadeInterval) {

      fadeValue += fadeDirection;


      if (fadeValue >= 255 ||
          fadeValue <= 0) {

        fadeDirection = -fadeDirection;
      }


      fadeValue = constrain(
        fadeValue,
        0,
        255
      );


      fill_solid(
        leds,
        NUM_LEDS,
        CRGB(
          255,
          0,
          0
        )
      );


      FastLED.show();

      lastFadeMillis = now;
    }
  }
}