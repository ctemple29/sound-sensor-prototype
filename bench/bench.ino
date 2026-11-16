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
// US-025A + SSD1306 OLED + analog microphone + touch sensor + red LED
// Arduino Nano
//
// OLED states (each stays on screen STATE_HOLD_MS after it was last triggered):
//   Gone         no echo or object beyond 200 cm (distance reading hidden)
//   Passerby     object within 200 cm (distance shown)
//   Near         object closer than 15 cm (distance shown)
//   Interested   touch sensor touched (shown once for TOUCH_HOLD_MS, then locked out briefly)
//   Disturbance  mic level above MIC_THRESHOLD (distance shown)
// Priority: Interested > Disturbance > Near > Passerby > Gone
// Displayed distance = average of the last 2 seconds of readings.
// Readings taken during a loud mic window are discarded, so sound does not affect distance.
// Red LED: lights when the mic is loud, and blinks 5 times on a new touch.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pins ----------
const int TRIG      = 9;
const int ECHO      = 10;
const int MIC_PIN   = A0;
const int LED_PIN   = 6;      // red LED via 220 ohm resistor
const int TOUCH_PIN = 4;      // touch sensor IO

// ---------- Settings ----------
const float RANGE_CM = 400.0;
const float NEAR_CM  = 200.0;      // beyond this = "Gone"
const float NEAR_HYST_CM = 5.0;    // must come back inside 195 cm to count as in range again
const float CLOSE_CM = 15.0;       // closer than this = "Near"
const float CLOSE_HYST_CM = 2.0;   // must move beyond 17 cm to leave "Near" (stops flicker)
const float SMOOTHING = 0.5;       // live filter, used for the state decisions
const unsigned long PING_TIMEOUT_US = 25000UL;
const unsigned long PING_PERIOD_MS  = 30;

const unsigned long DRAW_PERIOD_MS  = 250;     // OLED refresh while not "Gone" (also redraws on state change)
const unsigned long REINIT_MS       = 10000;   // re-initialize the OLED this often to clear glitches (0 = off)
#define I2C_CLOCK 400000UL                     // try 100000UL if the display still glitches

const unsigned long AVG_WINDOW_MS   = 2000;    // distance shown is the average over this time
const unsigned long STATE_HOLD_MS   = 3000;    // Passerby / Near / Disturbance persist this long after triggering

const unsigned long TOUCH_HOLD_MS    = 2000;   // "Interested" lasts exactly this long, no extension
const unsigned long TOUCH_LOCKOUT_MS = 1000;   // after it ends, touches are ignored for this long

const int  MIC_THRESHOLD            = 50;      // sensitivity (lower = more sensitive)
const unsigned long MIC_WINDOW_MS   = 50;
const unsigned long LED_HOLD_MS     = 200;     // red LED stays on this long after a loud sound
const unsigned long MIC_PRINT_MS    = 250;     // serial mic report interval (loudest level in that time)
const bool PRINT_DISTANCE           = true;    // print cm (about 20 lines/s)

const bool TOUCH_ACTIVE_HIGH        = true;    // set false if the sensor reads inverted
const uint8_t BLINK_COUNT           = 5;
const unsigned long BLINK_MS        = 60;

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1, I2C_CLOCK, I2C_CLOCK);
bool oledOk = false;

enum State { GONE, PASSERBY, NEAR, INTERESTED, DISTURBANCE };
State lastState = GONE;
bool forceDraw = true;

// Distance state
float filtered = RANGE_CM;
bool haveReading = false;
uint8_t misses = 0;
bool farAway = true;               // true = "Gone" range
bool isClose = false;              // true = "Near" range
float hist[3];                     // last 3 pings, for the median
uint8_t histN = 0;
float pendSum = 0;                 // readings waiting for the current mic window to end
uint8_t pendCnt = 0;
float lastAvg = -1;

// 2-second rolling average, stored as 100 ms buckets (20 buckets)
const unsigned long BUCKET_MS = 100;
const uint8_t NUM_BUCKETS = AVG_WINDOW_MS / BUCKET_MS;
float bSum[NUM_BUCKETS];
uint8_t bCnt[NUM_BUCKETS];
unsigned long bId[NUM_BUCKETS];

// State hold timers
unsigned long touchUntil = 0, touchReadyAt = 0, micShowUntil = 0, passUntil = 0, nearUntil = 0;

// Microphone state
int micMin = 1023, micMax = 0;
int micLevel = 0, micPeak = 0;
unsigned long micWindowStart = 0, ledOffAt = 0, lastMicPrint = 0;

// Touch / blink state
bool lastTouched = false;
bool blinking = false;
uint8_t blinkToggles = 0;
unsigned long nextBlinkAt = 0;

unsigned long pingUs() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(15);
  digitalWrite(TRIG, LOW);
  return pulseIn(ECHO, HIGH, PING_TIMEOUT_US);
}

float med3(float a, float b, float c) {
  float lo = (a < b) ? a : b;
  float hi = (a < b) ? b : a;
  float m  = (hi < c) ? hi : c;
  return (lo > m) ? lo : m;
}

void addToAverage(unsigned long now, float sum, uint8_t cnt) {
  unsigned long id = now / BUCKET_MS;
  uint8_t idx = id % NUM_BUCKETS;
  if (bId[idx] != id) {            // bucket holds old data: reset it
    bId[idx] = id;
    bSum[idx] = 0;
    bCnt[idx] = 0;
  }
  bSum[idx] += sum;
  bCnt[idx] += cnt;
}

// Returns the average of the last 2 s of valid readings, or -1 if there are none
float averageCm(unsigned long now) {
  unsigned long id = now / BUCKET_MS;
  float sum = 0;
  uint16_t cnt = 0;
  for (uint8_t i = 0; i < NUM_BUCKETS; i++) {
    if (bCnt[i] > 0 && (id - bId[i]) < NUM_BUCKETS) {
      sum += bSum[i];
      cnt += bCnt[i];
    }
  }
  return cnt ? sum / cnt : -1.0;
}

// Accept the readings gathered during a quiet mic window
void commitPending(unsigned long now) {
  if (pendCnt == 0) return;
  float m = pendSum / pendCnt;
  filtered = haveReading ? filtered + SMOOTHING * (m - filtered) : m;
  haveReading = true;
  addToAverage(now, pendSum, pendCnt);
  if (PRINT_DISTANCE) { Serial.print(F("cm ")); Serial.println(filtered, 1); }
  pendSum = 0;
  pendCnt = 0;
}

void drawScreen(State state, bool valid, float cm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (state == GONE) {
    // Distance reading is off: just the word, centered
    display.setTextSize(3);
    display.setCursor((SCREEN_W - 4 * 18) / 2, 20);
    display.print(F("Gone"));
    display.display();
    return;
  }

  // Label across the top (drops to size 1 if it would not fit at size 2)
  const char* label = (state == PASSERBY)   ? "Passerby"
                    : (state == NEAR)       ? "Near"
                    : (state == INTERESTED) ? "Interested"
                                            : "Disturbance";
  display.setTextSize((strlen(label) * 12 <= SCREEN_W) ? 2 : 1);
  display.setCursor(0, 0);
  display.print(label);

  // Distance reading (2-second average)
  display.setTextSize(3);
  display.setCursor(0, 24);
  if (valid) display.print(cm, 1);
  else       display.print(F("---"));

  // Small "cm" label, placed right after the number, on the number's baseline
  int x = display.getCursorX() + 4;
  display.setTextSize(1);
  display.setCursor(x, 40);
  display.print(F("cm"));

  // Bar graph
  display.drawRect(0, 52, SCREEN_W, 12, SSD1306_WHITE);
  if (valid) {
    int barW = (int)((cm / RANGE_CM) * (SCREEN_W - 4));
    display.fillRect(2, 54, constrain(barW, 0, SCREEN_W - 4), 8, SSD1306_WHITE);
  }
  display.display();
}

void updateMic(unsigned long now) {
  int s = analogRead(MIC_PIN);
  if (s < micMin) micMin = s;
  if (s > micMax) micMax = s;

  if (now - micWindowStart >= MIC_WINDOW_MS) {
    micLevel = micMax - micMin;
    if (micLevel > micPeak) micPeak = micLevel;

    if (micLevel > MIC_THRESHOLD) {
      ledOffAt = now + LED_HOLD_MS;
      micShowUntil = now + STATE_HOLD_MS;
      pendSum = 0;                 // loud window: throw away its distance readings
      pendCnt = 0;
    } else {
      commitPending(now);          // quiet window: keep them
    }

    micMin = 1023; micMax = 0;
    micWindowStart = now;
  }

  // The blink sequence has priority over the mic while it is running
  if (!blinking) {
    digitalWrite(LED_PIN, (long)(ledOffAt - now) > 0 ? HIGH : LOW);
  }

  if (now - lastMicPrint >= MIC_PRINT_MS) {
    lastMicPrint = now;
    Serial.print(F("mic "));
    Serial.println(micPeak);
    micPeak = 0;
  }
}

void updateTouch(unsigned long now) {
  bool touched = (digitalRead(TOUCH_PIN) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW));

  if (touched != lastTouched) {
    Serial.print(F("touch "));
    Serial.println(touched ? 1 : 0);
  }

  // A new touch (rising edge) activates once, if the lockout from the last one is over.
  // Holding the sensor does NOT extend or restart anything.
  if (touched && !lastTouched && (long)(now - touchReadyAt) >= 0) {
    touchUntil   = now + TOUCH_HOLD_MS;
    touchReadyAt = touchUntil + TOUCH_LOCKOUT_MS;

    if (!blinking) {
      blinking = true;
      blinkToggles = 1;
      digitalWrite(LED_PIN, HIGH);
      nextBlinkAt = now + BLINK_MS;
    }
  }
  lastTouched = touched;

  // Advance the sequence: odd toggles = on, even toggles = off
  if (blinking && (long)(now - nextBlinkAt) >= 0) {
    blinkToggles++;
    digitalWrite(LED_PIN, (blinkToggles % 2 == 1) ? HIGH : LOW);
    nextBlinkAt += BLINK_MS;
    if (blinkToggles >= BLINK_COUNT * 2) {
      blinking = false;
      digitalWrite(LED_PIN, LOW);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  digitalWrite(TRIG, LOW);
  digitalWrite(LED_PIN, LOW);
  Serial.println(F("boot"));

  for (uint8_t i = 0; i < NUM_BUCKETS; i++) { bSum[i] = 0; bCnt[i] = 0; bId[i] = 0; }

#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(3000, true);
#endif

  unsigned long t0 = millis();
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Serial.print(oledOk ? F("oled ok, init ms: ") : F("oled NOT found, ms: "));
  Serial.println(millis() - t0);

  micWindowStart = lastMicPrint = millis();
}

void loop() {
  static unsigned long lastPing = 0, lastDraw = 0, lastInit = 0;
  unsigned long now = millis();

  updateMic(now);
  updateTouch(now);

  if (now - lastPing >= PING_PERIOD_MS) {
    lastPing = now;
    Serial.flush();                // let serial finish so its interrupts don't disturb the echo timing
    unsigned long us = pingUs();

    if (us > 0) {
      float raw = constrain(us / 58.0, 0.0, RANGE_CM);
      if (histN < 3) hist[histN++] = raw;
      else { hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = raw; }
      float cm = (histN == 3) ? med3(hist[0], hist[1], hist[2]) : raw;

      pendSum += cm;               // committed or discarded when the mic window ends
      if (pendCnt < 255) pendCnt++;
      misses = 0;
    } else {
      if (misses < 255) misses++;
      histN = 0;
      if (PRINT_DISTANCE) Serial.println(F("cm 400.0   (no echo)"));
    }
  }

  // ---- Decide the state (cheap, runs every pass) ----
  bool valid = haveReading && misses < 5;

  if (!valid || filtered > NEAR_CM)              farAway = true;
  else if (filtered < NEAR_CM - NEAR_HYST_CM)    farAway = false;
  if (!farAway) passUntil = now + STATE_HOLD_MS;

  if (!valid || filtered > CLOSE_CM + CLOSE_HYST_CM)   isClose = false;
  else if (filtered < CLOSE_CM)                        isClose = true;
  if (isClose) nearUntil = now + STATE_HOLD_MS;

  State state;
  if ((long)(touchUntil - now) > 0)          state = INTERESTED;
  else if ((long)(micShowUntil - now) > 0)   state = DISTURBANCE;
  else if ((long)(nearUntil - now) > 0)      state = NEAR;
  else if ((long)(passUntil - now) > 0)      state = PASSERBY;
  else                                       state = GONE;

  // ---- Draw only when something changed, or periodically ----
  if (oledOk) {
    if (REINIT_MS && now - lastInit >= REINIT_MS) {
      lastInit = now;
      display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);   // clears any glitched controller state
      forceDraw = true;
    }

    bool changed = (state != lastState) || forceDraw;
    bool due = (state != GONE) && (now - lastDraw >= DRAW_PERIOD_MS);

    if (changed || due) {
      lastDraw = now;
      lastState = state;
      forceDraw = false;

      float avg = averageCm(now);
      if (avg >= 0) lastAvg = avg;
      else if (misses == 0 && lastAvg >= 0) avg = lastAvg;   // pings valid but held back by loud sound
      drawScreen(state, avg >= 0, avg);
    }
  }
}