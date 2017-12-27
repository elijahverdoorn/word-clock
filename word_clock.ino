#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
#include <Wire.h>
#include "RTClib.h"

RTC_DS3231 rtc;

// Pin assignments for LED rows
// Rows are counted top to bottom
#define ROW_1 13
#define ROW_2 12
#define ROW_3 11
#define ROW_4 10
#define ROW_5 9
#define ROW_6 8
#define ROW_7 7
#define ROW_8 6

// Pin assignments for controls
#define MODE_BUTTON 5
#define HOUR_BUTTON 4
#define MINUTE_BUTTON 3

// Number of LEDs in a strip
#define NUM_LED 13

// Brightness constant - should be in the range [0, 255]
// Define based on power availiability
#define BRIGHTNESS 32

// How frequently we should query the RTC for the time
#define RTC_QUERY_PERIOD_MILLIS 5000UL

// How often to check the switch state
#define SWITCH_QUERY_PERIOD_MILLIS 1000UL

// TIME MODE constants
#define TIME_AUTO_OFF 22 // 11pm
#define TIME_AUTO_ON 8 // 8am

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
Adafruit_NeoPixel strip_1 = Adafruit_NeoPixel(NUM_LED, ROW_1);
Adafruit_NeoPixel strip_2 = Adafruit_NeoPixel(NUM_LED, ROW_2);
Adafruit_NeoPixel strip_3 = Adafruit_NeoPixel(NUM_LED, ROW_3);
Adafruit_NeoPixel strip_4 = Adafruit_NeoPixel(NUM_LED, ROW_4);
Adafruit_NeoPixel strip_5 = Adafruit_NeoPixel(NUM_LED, ROW_5);
Adafruit_NeoPixel strip_6 = Adafruit_NeoPixel(NUM_LED, ROW_6);
Adafruit_NeoPixel strip_7 = Adafruit_NeoPixel(NUM_LED, ROW_7);
Adafruit_NeoPixel strip_8 = Adafruit_NeoPixel(NUM_LED, ROW_8);

// Colors
uint32_t white = strip_1.Color(255, 255, 255);
uint32_t off = strip_1.Color(0, 0, 0);
uint32_t red = strip_1.Color(255, 0, 0);
uint32_t green = strip_1.Color(0, 255, 0);
uint32_t blue = strip_1.Color(0, 0, 255);

// Global variables
static unsigned long lastRTCQueryTime = 0; // the last time we queried the clock for the time

static unsigned int currentHour = 0; // the hour that will be displayed on the clock
static unsigned int currentMinute = 0; // the minute that will be used to calculate what to show on the clock
static unsigned int currentHour24 = 0; // the hour (in 24 hour time) used for TIME MODE

// Globals for mode button
int modeButtonState = LOW;
int lastModeButtonState = LOW;
static unsigned int displayMode = 1; // 0 = off, 1 = on, 2 = time; default to on
unsigned long lastModeDebounceTime = 0;
unsigned long modeDebounceDelay = 100;
bool shouldDisplayTimeBool = false;

// Globals for hour button
static unsigned int hourOffset = 0; // This gets added to the hour taken from the clock, used for daylight savings and such. Taken mod 12
int hourButtonState = LOW;
int lastHourButtonState = LOW;
unsigned long lastHourDebounceTime = 0;
unsigned long hourDebounceDelay = 100;

// Globals for minute button
static unsigned int minuteOffset = 0; // Added to the minute taken from the clock, mod 60
int minuteButtonState = LOW;
int lastMinuteButtonState = LOW;
unsigned long lastMinuteDebounceTime = 0;
unsigned long minuteDebounceDelay = 100;

void setup() {
  // Init all 8 strands of LEDs
  // Use half brightness due to power concerns
  strip_1.begin();
  strip_1.setBrightness(BRIGHTNESS);
  strip_1.show();

  strip_2.begin();
  strip_2.setBrightness(BRIGHTNESS);
  strip_2.show();

  strip_3.begin();
  strip_3.setBrightness(BRIGHTNESS);
  strip_3.show();

  strip_4.begin();
  strip_4.setBrightness(BRIGHTNESS);
  strip_4.show();

  strip_5.begin();
  strip_5.setBrightness(BRIGHTNESS);
  strip_5.show();

  strip_6.begin();
  strip_6.setBrightness(BRIGHTNESS);
  strip_6.show();

  strip_7.begin();
  strip_7.setBrightness(BRIGHTNESS);
  strip_7.show();

  strip_8.begin();
  strip_8.setBrightness(BRIGHTNESS);
  strip_8.show();

  // Clock Setup
  Serial.begin(9600);

  delay(3000); // wait for console opening

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  // Control Setup
  pinMode(MODE_BUTTON, INPUT);

  clearAll();
  showAll();
}

void loop() {
  handleHourAdjust();
  handleMinuteAdjust();
  handleModeButton();
  
  unsigned long timeNow = millis();

  if ((timeNow - lastRTCQueryTime) > RTC_QUERY_PERIOD_MILLIS) {
    lastRTCQueryTime = timeNow;
    readTime();
  }

  if (shouldDisplayTimeBool) {
    // the user wants the time to be displayed
    clearAll(); // start with a clean slate
    showTime((currentHour + hourOffset) % 12, (currentMinute + minuteOffset) % 60);
    showAll();
  } else {
    clearAll();
    showAll();
  }

  // testMatrix();
  delay(1); // don't want to burn out the chip
}

// Handle input from the buttons to adjust the time
void handleHourAdjust() {
  // will update the hourOffset global based on user button press
  int reading = digitalRead(HOUR_BUTTON);
  
  if (reading != lastHourButtonState) {
    lastHourDebounceTime = millis();
  }

  if ((millis() - lastHourDebounceTime) > hourDebounceDelay) {
    if (reading != hourButtonState) {
      hourButtonState = reading;

      if (hourButtonState == HIGH) {
        hourOffset++;
        hourOffset = hourOffset % 12;
      }
    }
  }

  //Serial.println(currentHour + hourOffset);
  lastHourButtonState = reading;
}

void handleMinuteAdjust() {
  // will update the minuteOffset global based on user button press
  int reading = digitalRead(MINUTE_BUTTON);
  
  if (reading != lastMinuteButtonState) {
    lastMinuteDebounceTime = millis();
  }

  if ((millis() - lastMinuteDebounceTime) > minuteDebounceDelay) {
    if (reading != minuteButtonState) {
      minuteButtonState = reading;

      if (minuteButtonState == HIGH) {
        minuteOffset++;
        minuteOffset = minuteOffset % 60;
      }
    }
  }

  Serial.println((currentMinute + minuteOffset) % 60);
  lastMinuteButtonState = reading;
}

// Handle input from the light on/off/time button
bool handleModeButton() {
  int reading = digitalRead(MODE_BUTTON);

  // if (reading == HIGH) {
  //  Serial.println("high");
  // }
  
  if (reading != lastModeButtonState) {
    lastModeDebounceTime = millis();
  }

  if ((millis() - lastModeDebounceTime) > modeDebounceDelay) {
    if (reading != modeButtonState) {
      modeButtonState = reading;

      if (modeButtonState == HIGH) {
        displayMode++;
        displayMode = displayMode % 3;
        Serial.println(displayMode);
      }
    }
  }

  lastModeButtonState = reading;

  switch (displayMode) {
    case 0:
      shouldDisplayTimeBool = false;
      break;
    case 1:
      shouldDisplayTimeBool = true;
      break;
    case 2:
      if (currentHour24 > TIME_AUTO_OFF || currentHour24 < TIME_AUTO_ON) {
        shouldDisplayTimeBool = false;
      } else {
        shouldDisplayTimeBool = true;
      }
      break;
    default:
      break;
  }
}

void turnOffStrip(Adafruit_NeoPixel strip) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, off);
  }
  strip.show();
}

// Test the entire matrx by flashing all LEDs green, then red, then blue
void testMatrix() {
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < NUM_LED; i++) {
      switch (j) {
        case 0:
          strip_1.setPixelColor(i, green);
          strip_2.setPixelColor(i, green);
          strip_3.setPixelColor(i, green);
          strip_4.setPixelColor(i, green);
          strip_5.setPixelColor(i, green);
          strip_6.setPixelColor(i, green);
          strip_7.setPixelColor(i, green);
          strip_8.setPixelColor(i, green);
          break;
        case 1:
          strip_1.setPixelColor(i, red);
          strip_2.setPixelColor(i, red);
          strip_3.setPixelColor(i, red);
          strip_4.setPixelColor(i, red);
          strip_5.setPixelColor(i, red);
          strip_6.setPixelColor(i, red);
          strip_7.setPixelColor(i, red);
          strip_8.setPixelColor(i, red);
          break;
        case 2:
          strip_1.setPixelColor(i, blue);
          strip_2.setPixelColor(i, blue);
          strip_3.setPixelColor(i, blue);
          strip_4.setPixelColor(i, blue);
          strip_5.setPixelColor(i, blue);
          strip_6.setPixelColor(i, blue);
          strip_7.setPixelColor(i, blue);
          strip_8.setPixelColor(i, blue);
          break;
        default:
          break;
      }
    }

    strip_1.show();
    strip_2.show();
    strip_3.show();
    strip_4.show();
    strip_5.show();
    strip_6.show();
    strip_7.show();
    strip_8.show();

    delay(1000);
  }
}

void showTime(int hour, int minute) {
  lightItIs(red);

  if (minute < 5) {
    lightOClock(red);
  } else if (minute >= 5 && minute < 10) {
    lightFiveTop(red);
    lightMinutes(red);
  } else if (minute >= 10 && minute < 15) {
    lightMinutes(red);
    lightTenTop(red);
  } else if (minute >= 15 && minute < 20) {
    lightQuarter(red);
  } else if (minute >= 20 && minute < 25) {
    lightTwenty(red);
    lightMinutes(red);
  } else if (minute >= 25 && minute < 30) {
    lightTwenty(red);
    lightFiveTop(red);
    lightMinutes(red);
  } else if (minute >= 30 && minute < 35) {
    lightHalf(red);
  } else if (minute >= 35 && minute < 40) {
    lightTwenty(red);
    lightFiveTop(red);
    lightMinutes(red);
  } else if (minute >= 40 && minute < 45) {
    lightTwenty(red);
    lightMinutes(red);
  } else if (minute >= 45 && minute < 50) {
    lightQuarter(red);
  } else if (minute >= 50 && minute < 55) {
    lightTenTop(red);
    lightMinutes(red);
  } else if (minute >= 55) {
    lightFiveTop(red);
    lightMinutes(red);
  }

  // Handle the "hour" part of the time
  if (minute < 35) {
    if (minute >= 5) {
      lightPast(red);
    }

    switch (hour) {
      case 1:
        lightOne(red);
        break;
      case 2:
        lightTwo(red);
        break;
      case 3:
        lightThree(red);
        break;
      case 4:
        lightFour(red);
        break;
      case 5:
        lightFiveBottom(red);
        break;
      case 6:
        lightSix(red);
        break;
      case 7:
        lightSeven(red);
        break;
      case 8:
        lightEight(red);
        break;
      case 9:
        lightNine(red);
        break;
      case 10:
        lightTenBottom(red);
        break;
      case 11:
        lightEleven(red);
        break;
      case 12:
      case 0:
        lightTwelve(red);
        break;
    }
  } else {
    lightTo(red);

    switch ((hour + 1) % 12) {
      case 1:
        lightOne(red);
        break;
      case 2:
        lightTwo(red);
        break;
      case 3:
        lightThree(red);
        break;
      case 4:
        lightFour(red);
        break;
      case 5:
        lightFiveBottom(red);
        break;
      case 6:
        lightSix(red);
        break;
      case 7:
        lightSeven(red);
        break;
      case 8:
        lightEight(red);
        break;
      case 9:
        lightNine(red);
        break;
      case 10:
        lightTenBottom(red);
        break;
      case 11:
        lightEleven(red);
        break;
      case 12:
      case 0:
        lightTwelve(red);
        break;
    }
  }
}

/* Light the various words on the clock
   The grid used:

   IT_IS_TENHALF
   QUARTERTWENTY
   FIVE_MINUTES_
   PASTTO_ONETWO
   THREEFOURFIVE
   SIXSEVENEIGHT
   NINETENELEVEN
   TWELVE_OCLOCK

   _ = doesn't matter what the letter is, we're never going to light it
*/


void lightItIs(uint32_t color) {
  strip_1.setPixelColor(0, color);
  strip_1.setPixelColor(1, color);
  strip_1.setPixelColor(3, color);
  strip_1.setPixelColor(4, color);
}

void lightTenTop(uint32_t color) {
  strip_1.setPixelColor(6, color);
  strip_1.setPixelColor(7, color);
  strip_1.setPixelColor(8, color);
}

void lightHalf(uint32_t color) {
  strip_1.setPixelColor(9, color);
  strip_1.setPixelColor(10, color);
  strip_1.setPixelColor(11, color);
  strip_1.setPixelColor(12, color);
}

void lightQuarter(uint32_t color) {
  strip_2.setPixelColor(0, color);
  strip_2.setPixelColor(1, color);
  strip_2.setPixelColor(2, color);
  strip_2.setPixelColor(3, color);
  strip_2.setPixelColor(4, color);
  strip_2.setPixelColor(5, color);
  strip_2.setPixelColor(6, color);
}

void lightTwenty(uint32_t color) {
  strip_2.setPixelColor(7, color);
  strip_2.setPixelColor(8, color);
  strip_2.setPixelColor(9, color);
  strip_2.setPixelColor(10, color);
  strip_2.setPixelColor(11, color);
  strip_2.setPixelColor(12, color);
}

void lightFiveTop(uint32_t color) {
  strip_3.setPixelColor(0, color);
  strip_3.setPixelColor(1, color);
  strip_3.setPixelColor(2, color);
  strip_3.setPixelColor(3, color);
}

void lightMinutes(uint32_t color) {
  strip_3.setPixelColor(5, color);
  strip_3.setPixelColor(6, color);
  strip_3.setPixelColor(7, color);
  strip_3.setPixelColor(8, color);
  strip_3.setPixelColor(9, color);
  strip_3.setPixelColor(10, color);
  strip_3.setPixelColor(11, color);
}

void lightPast(uint32_t color) {
  strip_4.setPixelColor(0, color);
  strip_4.setPixelColor(1, color);
  strip_4.setPixelColor(2, color);
  strip_4.setPixelColor(3, color);
}

void lightTo(uint32_t color) {
  strip_4.setPixelColor(4, color);
  strip_4.setPixelColor(5, color);
}

void lightOne(uint32_t color) {
  strip_4.setPixelColor(7, color);
  strip_4.setPixelColor(8, color);
  strip_4.setPixelColor(9, color);
}

void lightTwo(uint32_t color) {
  strip_4.setPixelColor(10, color);
  strip_4.setPixelColor(11, color);
  strip_4.setPixelColor(12, color);
}

void lightThree(uint32_t color) {
  strip_5.setPixelColor(0, color);
  strip_5.setPixelColor(1, color);
  strip_5.setPixelColor(2, color);
  strip_5.setPixelColor(3, color);
  strip_5.setPixelColor(4, color);
}

void lightFour(uint32_t color) {
  strip_5.setPixelColor(5, color);
  strip_5.setPixelColor(6, color);
  strip_5.setPixelColor(7, color);
  strip_5.setPixelColor(8, color);
}

void lightFiveBottom(uint32_t color) {
  strip_5.setPixelColor(9, color);
  strip_5.setPixelColor(10, color);
  strip_5.setPixelColor(11, color);
  strip_5.setPixelColor(12, color);
}

void lightSix(uint32_t color) {
  strip_6.setPixelColor(0, color);
  strip_6.setPixelColor(1, color);
  strip_6.setPixelColor(2, color);
}

void lightSeven(uint32_t color) {
  strip_6.setPixelColor(3, color);
  strip_6.setPixelColor(4, color);
  strip_6.setPixelColor(5, color);
  strip_6.setPixelColor(6, color);
  strip_6.setPixelColor(7, color);
}

void lightEight(uint32_t color) {
  strip_6.setPixelColor(8, color);
  strip_6.setPixelColor(9, color);
  strip_6.setPixelColor(10, color);
  strip_6.setPixelColor(11, color);
  strip_6.setPixelColor(12, color);
}

void lightNine(uint32_t color) {
  strip_7.setPixelColor(0, color);
  strip_7.setPixelColor(1, color);
  strip_7.setPixelColor(2, color);
  strip_7.setPixelColor(3, color);
}

void lightTenBottom(uint32_t color) {
  strip_7.setPixelColor(4, color);
  strip_7.setPixelColor(5, color);
  strip_7.setPixelColor(6, color);
}

void lightEleven(uint32_t color) {
  strip_7.setPixelColor(7, color);
  strip_7.setPixelColor(8, color);
  strip_7.setPixelColor(9, color);
  strip_7.setPixelColor(10, color);
  strip_7.setPixelColor(11, color);
  strip_7.setPixelColor(12, color);
}

void lightTwelve(uint32_t color) {
  strip_8.setPixelColor(0, color);
  strip_8.setPixelColor(1, color);
  strip_8.setPixelColor(2, color);
  strip_8.setPixelColor(3, color);
  strip_8.setPixelColor(4, color);
  strip_8.setPixelColor(5, color);
}

void lightOClock(uint32_t color) {
  strip_8.setPixelColor(7, color);
  strip_8.setPixelColor(8, color);
  strip_8.setPixelColor(9, color);
  strip_8.setPixelColor(10, color);
  strip_8.setPixelColor(11, color);
  strip_8.setPixelColor(12, color);
}

// Tell all strips to display what we've told them to display
void showAll() {
  strip_1.show();
  strip_2.show();
  strip_3.show();
  strip_4.show();
  strip_5.show();
  strip_6.show();
  strip_7.show();
  strip_8.show();
}

// turn off all the LEDs
void clearAll() {
  for (int i = 0; i < NUM_LED; i++){
    strip_1.setPixelColor(i, off);
    strip_2.setPixelColor(i, off);
    strip_3.setPixelColor(i, off);
    strip_4.setPixelColor(i, off);
    strip_5.setPixelColor(i, off);
    strip_6.setPixelColor(i, off);
    strip_7.setPixelColor(i, off);
    strip_8.setPixelColor(i, off);
  }
}

void readTime() {
  DateTime now = rtc.now();

  currentHour = now.hour();
  currentHour24 = currentHour % 24; // for the TIME_MODE
  currentHour = currentHour % 12; // want 12 hour time

  currentMinute = now.minute();
  if (currentMinute >= 60) {
    // need to advance the hour
    currentHour++;
    currentHour = currentHour % 12;

    currentMinute = currentMinute % 60; // we've accounted for the hour now, so we need any remaining minutes
  }
}

