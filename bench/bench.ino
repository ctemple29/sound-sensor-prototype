   // put your setup code here, to run once:
// bench.ino: the Arduino half of a bench pair's sensor.
//
// It reads the sensor and prints one line per reading over USB, as name and
// number pairs separated by spaces:
//
//   cm 42.5
//   cm 41.9 light 612
//
// That is all it does. The meaning (the signals, the State Table, the piece) is
// built in the browser by bench/sense-serial.js, the same as the camera pairs.
//
// Wired for an HC-SR04 ultrasonic sensor on an Arduino Nano:
//   VCC -> 5V    GND -> GND    TRIG -> D9    ECHO -> D10
// To add a second sensor, read it in loop() and print it on the same line,
// before the println, e.g. a photoresistor divider on A0:
//   Serial.print(" light "); Serial.print(analogRead(A0));

// US-025A (trigger/echo mode) -> Arduino Nano -> JUMO analog input
// Output: 12-bit PWM on D9 (Timer1), filtered to 0-5 V, then scaled to 0-10 V externally.
// US-025A (trigger/echo) -> Arduino Nano -> 0.96" SSD1306 OLED (I2C)
// Serial output keeps the original "cm 42.5" format at 115200 baud.
// US-025A + SSD1306 OLED + analog microphone + red LED on Arduino Nano

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pins ----------
const int TRIG    = 9;
const int ECHO    = 10;
const int MIC_PIN = A0;
const int LED_PIN = 6;                    // red LED via 220 ohm resistor

// ---------- Settings ----------
const float RANGE_CM = 400.0;
const float SMOOTHING = 0.5;
const unsigned long PING_TIMEOUT_US = 25000UL;
const unsigned long PING_PERIOD_MS  = 30;
const unsigned long DRAW_PERIOD_MS  = 100;

const int  MIC_THRESHOLD        = 50;    // LED turns on above this (0-1023 scale)
const unsigned long MIC_WINDOW_MS   = 50;     // sampling window for peak-to-peak
const unsigned long LED_HOLD_MS     = 200;    // keep LED on briefly so flashes are visible
const unsigned long MIC_PRINT_MS    = 2000;   // serial report interval
const bool PRINT_DISTANCE = false;        // true = also print cm at ~33 Hz (floods the monitor)

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
bool oledOk = false;

float filtered = RANGE_CM;
bool haveReading = false;
uint8_t misses = 0;

// Microphone state
int micMin = 1023, micMax = 0;
int micLevel = 0;                 // latest peak-to-peak
int micPeak2s = 0;                // loudest peak-to-peak since last serial report
unsigned long micWindowStart = 0, ledOffAt = 0, lastMicPrint = 0;

unsigned long pingUs() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(15);
  digitalWrite(TRIG, LOW);
  return pulseIn(ECHO, HIGH, PING_TIMEOUT_US);
}

void drawScreen(bool valid, float cm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("DISTANCE"));

  display.setTextSize(3);
  display.setCursor(0, 16);
  if (valid) display.print(cm, 1);
  else       display.print(F("---"));

  display.setTextSize(2);
  display.setCursor(104, 24);
  display.print(F("cm"));

  display.drawRect(0, 50, SCREEN_W, 12, SSD1306_WHITE);
  if (valid) {
    int barW = (int)((cm / RANGE_CM) * (SCREEN_W - 4));
    display.fillRect(2, 52, constrain(barW, 0, SCREEN_W - 4), 8, SSD1306_WHITE);
  }
  display.display();
}

void updateMic(unsigned long now) {
  int s = analogRead(MIC_PIN);
  if (s < micMin) micMin = s;
  if (s > micMax) micMax = s;

  if (now - micWindowStart >= MIC_WINDOW_MS) {
    micLevel = micMax - micMin;
    if (micLevel > micPeak2s) micPeak2s = micLevel;
    if (micLevel > MIC_THRESHOLD) ledOffAt = now + LED_HOLD_MS;
    micMin = 1023; micMax = 0;
    micWindowStart = now;
  }

  digitalWrite(LED_PIN, (long)(ledOffAt - now) > 0 ? HIGH : LOW);

  if (now - lastMicPrint >= MIC_PRINT_MS) {
    lastMicPrint = now;
    Serial.print(F("mic "));
    Serial.println(micPeak2s);
    micPeak2s = 0;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(TRIG, LOW);
  digitalWrite(LED_PIN, LOW);
  Serial.println(F("boot"));

#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(3000, true);
#endif

  unsigned long t0 = millis();
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Serial.print(oledOk ? F("oled ok, init ms: ") : F("oled NOT found, ms: "));
  Serial.println(millis() - t0);
  if (oledOk) Wire.setClock(400000);

  micWindowStart = lastMicPrint = millis();
}

void loop() {
  static unsigned long lastPing = 0, lastDraw = 0;
  unsigned long now = millis();

  updateMic(now);

  if (now - lastPing >= PING_PERIOD_MS) {
    lastPing = now;
    unsigned long us = pingUs();

    if (us > 0) {
      float cm = constrain(us / 58.0, 0.0, RANGE_CM);
      filtered = haveReading ? filtered + SMOOTHING * (cm - filtered) : cm;
      haveReading = true;
      misses = 0;
      if (PRINT_DISTANCE) { Serial.print(F("cm ")); Serial.println(filtered, 1); }
    } else {
      if (misses < 255) misses++;
      if (PRINT_DISTANCE) Serial.println(F("cm 400.0   (no echo)"));
    }
  }

  if (oledOk && now - lastDraw >= DRAW_PERIOD_MS) {
    lastDraw = now;
    drawScreen(haveReading && misses < 5, filtered);
  }
}