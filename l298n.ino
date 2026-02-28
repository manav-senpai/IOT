#include <Wire.h>
#include <Arduino.h>

// ==========================================
//        NAVISENSE: FULL SYSTEM V1.0
// ==========================================

// --- 1. PIN DEFINITIONS ---
// SHARINGAN (Ultrasonics)
#define TRIG_LEFT   13
#define ECHO_LEFT   12
#define TRIG_FRONT  14
#define ECHO_FRONT  27
#define TRIG_RIGHT  26
#define ECHO_RIGHT  25

// IMAYOMA (Inputs)
#define PIN_BTN_SOS    5    // Emergency Button
#define PIN_BTN_SLEEP  4    // Sleep/Reset Button
#define PIN_IR_CLIFF   23   // Cliff Sensor (IR)
#define SDA_PIN        21   // ADXL Data
#define SCL_PIN        22   // ADXL Clock

// SHIZUKU (Light Input)
#define PIN_LDR        34   // LDR Sensor

// OUTPUTS (Audio & Visual)
#define PIN_BUZZ_PAS   15   // Passive Buzzer (Siren)
#define PIN_BUZZ_ACT   18   // Active Buzzer (Beeps)
#define PIN_LEDS_MAIN  2    // White/Yellow Torch LEDs
#define PIN_LEDS_SOS   32   // Red Warning LEDs

// --- 2. SETTINGS ---
#define ADXL_ADDR      0x53

// Distances (cm)
const int DIST_FAR     = 150; // Start beeping
const int DIST_NEAR    = 70;  // Speed up
const int DIST_CRIT    = 30;  // Panic mode

// Fall Thresholds (ADXL345)
// < 60 usually means flat/horizontal. > 150 means upright.
const int FALL_THRESH    = 60;  
const int UPRIGHT_THRESH = 160; 
const int FALL_TIME      = 2000; // 2 seconds to confirm fall

// --- 3. VARIABLES ---
bool isSleepMode = false;
bool isSOSActive = false;
bool isFallDetected = false;
bool isCliffDetected = false;

unsigned long fallTimer = 0;
unsigned long lastDebounce = 0;

// Audio Timers
unsigned long prevNavMillis = 0;
unsigned long prevSirenMillis = 0;
unsigned long prevCliffMillis = 0;

// Siren State
int sirenFreq = 500;
int sirenStep = 50;
bool sosLedState = false;

// --- 4. HELPER FUNCTIONS ---

void initADXL() {
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(0x2D); Wire.write(8); Wire.endTransmission();
}

int readAccelX() {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(0x32); Wire.endTransmission();
  Wire.requestFrom(ADXL_ADDR, 2);
  if (Wire.available() >= 2) {
    int x0 = Wire.read(); int x1 = Wire.read();
    return abs((x1 << 8) | x0);
  }
  return 255; // Default safe value
}

long getDistance(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, 25000); // 25ms timeout
  if (duration == 0) return 999; // Far away
  return duration * 0.034 / 2;
}

// --- ALARM PATTERNS ---

void playSiren() {
  // "Vao Vao" + Strobing Red LEDs
  if (millis() - prevSirenMillis > 20) {
    prevSirenMillis = millis();
    
    // Sound
    ledcWriteTone(PIN_BUZZ_PAS, sirenFreq);
    sirenFreq += sirenStep;
    if (sirenFreq >= 2000 || sirenFreq <= 500) sirenStep = -sirenStep;

    // Light (Blink every 200ms approx)
    if (sirenFreq % 200 == 0) {
      sosLedState = !sosLedState;
      digitalWrite(PIN_LEDS_SOS, sosLedState ? HIGH : LOW);
    }
  }
}

void playCliffAlarm() {
  // "Tu-Ti-Tu-Ti" (Police) + Solid Red LEDs
  unsigned long now = millis();
  if (now - prevCliffMillis > 250) { // Switch every 250ms
    prevCliffMillis = now;
    if (sirenFreq == 2500) sirenFreq = 1500; else sirenFreq = 2500;
    ledcWriteTone(PIN_BUZZ_PAS, sirenFreq);
    
    // Toggle LED with sound
    sosLedState = !sosLedState;
    digitalWrite(PIN_LEDS_SOS, sosLedState ? HIGH : LOW);
  }
}

void playNavBeep(int interval) {
  // Simple "Click/Beep" on Active Buzzer
  if (millis() - prevNavMillis >= interval) {
    prevNavMillis = millis();
    digitalWrite(PIN_BUZZ_ACT, HIGH);
    delay(30); // Short tick
    digitalWrite(PIN_BUZZ_ACT, LOW);
  }
}

// --- MAIN SETUP ---

void setup() {
  Serial.begin(115200);
  
  // Setup Outputs
  pinMode(PIN_BUZZ_ACT, OUTPUT);
  pinMode(PIN_LEDS_MAIN, OUTPUT);
  pinMode(PIN_LEDS_SOS, OUTPUT);
  
  // Setup Inputs
  pinMode(PIN_BTN_SOS, INPUT_PULLUP);
  pinMode(PIN_BTN_SLEEP, INPUT_PULLUP);
  pinMode(PIN_IR_CLIFF, INPUT);      // Adjust to INPUT_PULLUP if needed
  pinMode(PIN_LDR, INPUT);

  // Setup Ultrasonics
  pinMode(TRIG_LEFT, OUTPUT); pinMode(ECHO_LEFT, INPUT);
  pinMode(TRIG_FRONT, OUTPUT); pinMode(ECHO_FRONT, INPUT);
  pinMode(TRIG_RIGHT, OUTPUT); pinMode(ECHO_RIGHT, INPUT);

  // Audio Channel
  ledcAttach(PIN_BUZZ_PAS, 2000, 8);

  initADXL();
  
  // Power On Sound
  Serial.println(">> NAVISENSE STARTING...");
  digitalWrite(PIN_BUZZ_ACT, HIGH); delay(100); digitalWrite(PIN_BUZZ_ACT, LOW);
  delay(100);
  digitalWrite(PIN_BUZZ_ACT, HIGH); delay(100); digitalWrite(PIN_BUZZ_ACT, LOW);
}

// --- MAIN LOOP ---

void loop() {
  unsigned long currentMillis = millis();

  // 1. BUTTONS (GLOBAL INTERRUPTS)
  if (digitalRead(PIN_BTN_SOS) == LOW) {
    isSOSActive = true; isSleepMode = false;
    Serial.println(">> MANUAL SOS TRIGGERED!");
  }

  if (digitalRead(PIN_BTN_SLEEP) == LOW) {
    if (currentMillis - lastDebounce > 500) {
      lastDebounce = currentMillis;
      
      if (isSOSActive || isFallDetected || isCliffDetected) {
        // RESET ALARMS
        isSOSActive = false; isFallDetected = false; isCliffDetected = false;
        ledcWriteTone(PIN_BUZZ_PAS, 0);
        digitalWrite(PIN_LEDS_SOS, LOW);
        Serial.println(">> ALARMS CLEARED.");
      } else {
        // TOGGLE SLEEP
        isSleepMode = !isSleepMode;
        if (isSleepMode) {
          Serial.println(">> SLEEPING...");
          digitalWrite(PIN_BUZZ_ACT, HIGH); delay(500); digitalWrite(PIN_BUZZ_ACT, LOW); // Long Beep
          digitalWrite(PIN_LEDS_MAIN, LOW); // Force Torch OFF
        } else {
          Serial.println(">> WAKING UP...");
          digitalWrite(PIN_BUZZ_ACT, HIGH); delay(100); digitalWrite(PIN_BUZZ_ACT, LOW); // Short Beep
        }
      }
    }
  }

  // IF SLEEPING -> STOP HERE
  if (isSleepMode) { delay(100); return; }


  // 2. CLIFF DETECTION (High Priority)
  int irState = digitalRead(PIN_IR_CLIFF);
  if (irState == HIGH) { // Assuming HIGH = NO REFLECTION (HOLE)
    isCliffDetected = true;
    Serial.println(">> CLIFF DETECTED!");
  } else {
    isCliffDetected = false;
  }

  if (isCliffDetected) {
    playCliffAlarm();
    return; // Stop everything else
  }


  // 3. FALL DETECTION (Medium Priority)
  int xVal = readAccelX();
  if (xVal < FALL_THRESH && !isSOSActive) {
    // Stick is Flat
    if (fallTimer == 0) fallTimer = currentMillis; // Start timer
    else if (currentMillis - fallTimer > FALL_TIME) {
      isFallDetected = true;
      Serial.println(">> FALL DETECTED!");
    }
  } else if (xVal > UPRIGHT_THRESH) {
    fallTimer = 0; // Reset timer if standing
  }


  // 4. ALARM EXECUTION
  if (isSOSActive || isFallDetected) {
    playSiren();
  } 
  else {
    // NO ALARMS -> NORMAL NAVIGATION
    ledcWriteTone(PIN_BUZZ_PAS, 0);
    digitalWrite(PIN_LEDS_SOS, LOW);

    // A. OBSTACLE AVOIDANCE
    long dist = getDistance(TRIG_FRONT, ECHO_FRONT);
    // Optional: Use Side sensors to modify logic (e.g., panic if all 3 are close)
    // long distL = getDistance(TRIG_LEFT, ECHO_LEFT);
    // long distR = getDistance(TRIG_RIGHT, ECHO_RIGHT);

    int beepSpeed = 0;
    if (dist < DIST_CRIT) beepSpeed = 100;       // Fast
    else if (dist < DIST_NEAR) beepSpeed = 400;  // Medium
    else if (dist < DIST_FAR) beepSpeed = 1000;  // Slow

    if (beepSpeed > 0) playNavBeep(beepSpeed);

    // B. AUTOMATIC TORCH (Shizuku)
    if (digitalRead(PIN_LDR) == HIGH) { // Dark
      digitalWrite(PIN_LEDS_MAIN, HIGH);
    } else {
      digitalWrite(PIN_LEDS_MAIN, LOW);
    }
  }
}