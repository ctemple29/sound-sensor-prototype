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

const int TRIG = 9;
const int ECHO = 10;

void setup() {
  Serial.begin(115200);          // sense-serial.js opens the port at this speed
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
}

void loop() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  // The echo time in microseconds; 0 means nothing came back within 30 ms.
  unsigned long us = pulseIn(ECHO, HIGH, 30000);
  float cm = us ? us / 58.0 : 400.0; // nothing in range reads as far away

  Serial.print("cm ");
  Serial.print(cm, 1);
  Serial.println();

  delay(50);                     // about 20 readings a second
}
