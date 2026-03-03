#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "HX711.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_MPU6050.h>

// Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Pins
#define DHTPIN 10
#define DHTTYPE DHT22
#define PIR_PIN 3
#define BUZZER 5
#define BUTTON 4
#define GAS_PIN 2
#define LDR_PIN 1
#define TRIG 11
#define ECHO 12
#define HX_DT 6
#define HX_SCK 7

// Sensors
DHT dht(DHTPIN, DHTTYPE);
HX711 scale;
Adafruit_MPU6050 mpu;

float baseWeight = 0;
// >>> FIX: Track MPU6050 status with a flag instead of calling begin() every loop
bool mpuReady = false;

// Buzzer control
unsigned long lastBuzzerTime = 0;
const int beepInterval = 500;

// Ultrasonic helper
long readUltrasonic() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long duration = pulseIn(ECHO, HIGH, 30000);

  if (duration == 0) {
    duration = 20000;
  }

  return duration * 0.034 / 2;
}

// Risk calculation
int calculateRisk(float temp, int gas, int motion, float weightChange,
                  long distance, int light, float tilt) {
  int risk = 0;
  if (gas > 2000) risk += 30;
  else if (gas > 1000) risk += 15;

  if (temp > 40) risk += 10;
  if (motion) risk += 15;
  if (weightChange > 50) risk += 15;
  if (distance < 20) risk += 10;
  if (light < 1000 && motion) risk += 10;
  if (tilt > 20) risk += 10;

  return risk;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("ESP32 booted, starting sensors...");

  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  Wire.begin(8, 9);

  // I2C Scanner
  byte error, address;
  Serial.println("Scanning I2C...");
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at 0x");
      Serial.println(address, HEX);
    }
  }

  // DHT
  dht.begin();

  // HX711
  scale.begin(HX_DT, HX_SCK);
  unsigned long startTime = millis();
  while (!scale.is_ready() && (millis() - startTime < 3000)) {
    delay(10);
  }
  if (!scale.is_ready()) {
    Serial.println("HX711 not detected! Using default weight.");
    baseWeight = 100;
  } else {
    scale.set_scale();
    scale.tare();
    baseWeight = scale.get_units();
    Serial.print("Base weight: "); Serial.println(baseWeight);
  }

  // >>> FIX: Only call mpu.begin() ONCE here, save result to flag
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    mpuReady = false;
  } else {
    Serial.println("MPU6050 ready!");
    mpuReady = true;
  }

  // Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not at 0x3C! Trying 0x3D...");
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("SSD1306 allocation failed!");
    }
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  if (isnan(temp)) temp = 0;
  if (isnan(humidity)) humidity = 0;   // >>> FIX: Also guard humidity

  int gas = analogRead(GAS_PIN);
  int light = analogRead(LDR_PIN);
  int motion = digitalRead(PIR_PIN);
  long distance = readUltrasonic();

  float weight = scale.is_ready() ? scale.get_units(5) : baseWeight;
  // >>> FIX: Use fabs() for float instead of abs()
  float weightChange = fabs(weight - baseWeight);

  // >>> FIX: Use the flag, do NOT call mpu.begin() again
  float tilt = 0;
  if (mpuReady) {
    sensors_event_t a, g, tempEvent;
    mpu.getEvent(&a, &g, &tempEvent);
    tilt = sqrt(a.acceleration.x * a.acceleration.x +
                a.acceleration.y * a.acceleration.y +
                a.acceleration.z * a.acceleration.z);
  }

  int risk = calculateRisk(temp, gas, motion, weightChange, distance, light, tilt);

  String status = "SAFE";
  if (risk > 75) status = "CRITICAL";
  else if (risk > 50) status = "MEDIUM";
  else if (risk > 25) status = "LOW";

  // Buzzer
  if (risk > 75) {
    if (millis() - lastBuzzerTime > beepInterval) {
      digitalWrite(BUZZER, !digitalRead(BUZZER));
      lastBuzzerTime = millis();
    }
  } else {
    digitalWrite(BUZZER, LOW);
  }

  // Serial
  Serial.print("Temp: "); Serial.print(temp);
  Serial.print(" | Gas: "); Serial.print(gas);
  Serial.print(" | LDR: "); Serial.print(light);
  Serial.print(" | Dist: "); Serial.print(distance);
  Serial.print(" | Risk: "); Serial.println(risk);

  // Display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Temp: "); display.println(temp);
  display.print("Hum: "); display.println(humidity);
  display.print("Gas: "); display.println(gas);
  display.print("LDR: "); display.println(light);
  display.print("Dist: "); display.println(distance);
  display.print("Risk: "); display.println(risk);
  display.print("Status: "); display.println(status);
  display.display();

  delay(1000);
}
