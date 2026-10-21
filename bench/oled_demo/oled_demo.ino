// oled_demo.ino: bench.ino plus a 0.96in OLED readout, for the demo.
//
// Same serial output as bench.ino ("cm 42.5" lines at 115200), so
// sense-serial.js works unchanged. The screen is extra. On the two-colour
// panels the top 16 rows are yellow: that strip is the status (state badge,
// readings per second). The blue below is the data: the distance, a sonar ping
// whose blip sits at the measured range, and a scrolling trace of the history.
//
// Wiring, Arduino Nano:
//   HC-SR04  VCC -> 5V    GND -> GND    TRIG -> D9    ECHO -> D10
//   OLED     VCC -> 5V    GND -> GND    SDA  -> A4    SCL  -> A5
// The OLED is an SSD1306, 128 x 64, I2C at 0x3C. Needs the U8g2 library.

#include <U8g2lib.h>
#include <Wire.h>

// Page mode: draws the screen in eight strips with a 128-byte buffer, because
// the Nano has 2 KB of RAM and a full frame would take half of it.
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

const int TRIG = 9;
const int ECHO = 10;

const float NEAR_CM = 30.0;      // badge thresholds for the screen only;
const float HERE_CM = 120.0;     // the real State Table lives in the browser
const float MAX_CM = 200.0;      // top of the graph and the sonar range

const int GRAPH_H = 10;          // the trace, rows 53 to 63
uint8_t hist[128];               // one column per reading, 0..GRAPH_H
uint8_t head = 0;

float smooth = MAX_CM;
float sweep = 0;                 // sonar sweep angle, degrees
int sweepDir = 1;
uint8_t ping = 0;                // expanding ring radius
uint8_t rate = 0;                // readings in the last second, for the header
uint8_t count = 0;
unsigned long rateStart = 0;

float readCm() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long us = pulseIn(ECHO, HIGH, 30000);
  return us ? us / 58.0 : 400.0;
}

void splash() {
  for (uint8_t r = 2; r < 70; r += 6) {
    oled.firstPage();
    do {
      oled.drawCircle(64, 32, r);
      if (r > 8) oled.drawCircle(64, 32, r - 8);
      oled.setFont(u8g2_font_6x12_tr);
      oled.drawStr(28, 36, "SENSOR LAYER");
    } while (oled.nextPage());
  }
}

void drawHeader(float cm) {
  // Rows 0-15, the yellow strip: state badge left, readings per second right.
  const char *label = cm < NEAR_CM ? "NEAR" : cm < HERE_CM ? "HERE" : "EMPTY";
  oled.setFont(u8g2_font_6x12_tr);
  int w = oled.getStrWidth(label) + 8;
  if (cm < NEAR_CM) {              // near: solid badge
    oled.drawRBox(0, 1, w, 13, 3);
    oled.setDrawColor(0);
    oled.drawStr(4, 11, label);
    oled.setDrawColor(1);
  } else {
    oled.drawRFrame(0, 1, w, 13, 3);
    oled.drawStr(4, 11, label);
  }
  char buf[8];
  oled.setFont(u8g2_font_5x8_tr);
  itoa(rate, buf, 10);
  strcat(buf, "/s");
  int rw = oled.getStrWidth(buf);
  oled.drawStr(127 - rw, 10, buf);
  // a pulse that blinks once per reading, beside the rate
  if (head & 1) oled.drawDisc(127 - rw - 6, 7, 2);
  else oled.drawCircle(127 - rw - 6, 7, 2);
}

void drawNumber(float cm) {
  char buf[8];
  oled.setFont(u8g2_font_logisoso22_tn);
  // The smoothed value creeps toward 400 (no echo) but never reaches it.
  if (cm >= 399.0) {
    strcpy(buf, "---");
  } else {
    dtostrf(cm, 0, cm < 100 ? 1 : 0, buf);
  }
  oled.drawStr(0, 45, buf);
  int x = oled.getStrWidth(buf) + 2;
  oled.setFont(u8g2_font_5x8_tr);
  oled.drawStr(x, 45, "cm");
}

void drawSonar(float cm) {
  const int ox = 99, oy = 50, R = 28;
  // range rings and the frame of the fan
  oled.drawCircle(ox, oy, R / 3, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  oled.drawCircle(ox, oy, 2 * R / 3, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  oled.drawCircle(ox, oy, R, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  // the outgoing ping
  if (ping < R) oled.drawCircle(ox, oy, ping, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  // the sweep line
  float a = (sweep - 90) * DEG_TO_RAD;
  oled.drawLine(ox, oy, ox + R * cos(a), oy + R * sin(a));
  // the blip, at the measured range along the sweep
  if (cm < MAX_CM) {
    float r = cm / MAX_CM * R;
    int bx = ox + r * cos(a), by = oy + r * sin(a);
    oled.drawDisc(bx, by, cm < NEAR_CM ? 3 : 2);
  }
}

void drawGraph() {
  // A trace, oldest at the left: a line reads cleaner than solid bars, on the
  // panel and in a photo of it.
  const int y0 = 63;
  for (uint8_t x = 1; x < 128; x++) {
    uint8_t a = hist[(head + x - 1) % 128];
    uint8_t b = hist[(head + x) % 128];
    oled.drawLine(x - 1, y0 - a, x, y0 - b);
  }
}

void setup() {
  Serial.begin(115200);          // sense-serial.js opens the port at this speed
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  oled.begin();
  oled.setBusClock(400000);
  splash();
}

void loop() {
  float cm = readCm();

  Serial.print("cm ");
  Serial.print(cm, 1);
  Serial.println();

  smooth += (min(cm, 400.0f) - smooth) * 0.35;

  count++;
  if (millis() - rateStart >= 1000) {
    rate = count;
    count = 0;
    rateStart = millis();
  }

  // closer reads taller in the graph
  float c = constrain(smooth, 0, MAX_CM);
  hist[head] = (uint8_t)((1.0 - c / MAX_CM) * GRAPH_H);
  head = (head + 1) % 128;

  sweep += 6 * sweepDir;
  if (sweep > 60 || sweep < -60) sweepDir = -sweepDir;
  ping = (ping + 3) % 36;

  oled.firstPage();
  do {
    drawHeader(smooth);
    drawNumber(smooth);
    drawSonar(smooth);
    drawGraph();
  } while (oled.nextPage());
}
