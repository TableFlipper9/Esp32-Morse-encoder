#include <Keypad.h>

const uint8_t BUZZER_PIN = 8;

uint8_t sensorPin = A0;
bool newMessage = true;

//button setup
const byte KP_ROWS = 4;
const byte KP_COLS = 4;

char keys[KP_ROWS][KP_COLS] = {
  {'1','2','3','4'},
  {'4','5','6','8'},
  {'7','8','9','0'},
  {'A','B','C','D'}
};

byte rowPins[KP_ROWS] = {9, 10, 11, 12};
byte colPins[KP_COLS] = {13, A2, A3, A4};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, KP_ROWS, KP_COLS);

// define certain base thrasholds; need to be the same with emmiter
const unsigned long SAMPLE_US = 2000;

float baseline = 0;
const float AMBIENT_ALPHA = 0.005f;

float noiseLevel = 5.0f;
const float NOISE_ALPHA = 0.02f;

const float TH_ON_MULT  = 3.0f;
const float TH_OFF_MULT = 1.5f;
const float TH_ON_ADD   = 2.0f;
const float TH_OFF_ADD  = 1.0f;

const float TH_ON_MIN  = 3.0f;
const float TH_OFF_MIN = 2.0f;

const int ON_SAMPLES  = 8;
const int OFF_SAMPLES = 8;
const unsigned long MIN_EVENT_US = 30000; 

// Adaptive dot time
unsigned long dotUs = 100000;
const unsigned long DOT_MIN_US = 60000;
const unsigned long DOT_MAX_US = 200000;
const float DOT_ALPHA = 0.12f;

struct Morse { char c; const char *code; };
Morse table[] = {
  {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
  {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."},
  {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."},
  {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."},
  {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
  {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"},
  {'Y', "-.--"}, {'Z', "--.."},
  {'0', "-----"},{'1', ".----"},{'2', "..---"},{'3', "...--"},
  {'4', "....-"},{'5', "....."},{'6', "-...."},{'7', "--..."},
  {'8', "---.."},{'9', "----."}
};

char decode(const char* code) {
  for (auto &m : table) if (!strcmp(m.code, code)) return m.c;
  return '?';
}

char buf[10];
int bufLen = 0;

void resetBuf() { bufLen = 0; buf[0] = '\0'; }
void pushSymbol(char s) {
  if (bufLen < (int)sizeof(buf) - 1) {
    buf[bufLen++] = s;
    buf[bufLen] = '\0';
  }
}
void flushChar() {
  if (bufLen == 0) return;
  char out = decode(buf);
  Serial.print(out);
  resetBuf();
}

// buzeer
bool buzzerEnabled = true;

void beepDot()  { if (buzzerEnabled) tone(BUZZER_PIN, 2000, 60); }
void beepDash() { if (buzzerEnabled) tone(BUZZER_PIN, 2000, 160); }

// laser state
bool laserOn = false;
unsigned long lastChangeUs = 0;

bool candidateState = false;
int candidateCount = 0;

bool computeRawState() {
  int x = analogRead(sensorPin);

  // Track ambient ONLY when currently OFF
  if (!laserOn) {
    baseline = (1.0f - AMBIENT_ALPHA) * baseline + AMBIENT_ALPHA * x;
  }

  float diff = x - baseline;
  if (diff < 0) diff = 0;

  if (!laserOn) {
    noiseLevel = (1.0f - NOISE_ALPHA) * noiseLevel + NOISE_ALPHA * diff;
  }

  float thOn  = max(TH_ON_MIN,  noiseLevel * TH_ON_MULT  + TH_ON_ADD);
  float thOff = max(TH_OFF_MIN, noiseLevel * TH_OFF_MULT + TH_OFF_ADD);

  if (!laserOn && diff > thOn)  return true;
  if ( laserOn && diff < thOff) return false;
  return laserOn;
}

bool readLaserState() {
  bool raw = computeRawState();

  if (raw == laserOn) {
    candidateCount = 0;
    return laserOn;
  }

  if (candidateCount == 0) {
    candidateState = raw;
    candidateCount = 1;
  } else if (raw == candidateState) {
    candidateCount++;
  } else {
    candidateState = raw;
    candidateCount = 1;
  }

  int needed = candidateState ? ON_SAMPLES : OFF_SAMPLES;
  if (candidateCount >= needed) {
    candidateCount = 0;
    return candidateState;
  }

  return laserOn;
}

// keypad inputs 
void handleKey(char k) {
  if (!k) return;

  //Serial.print("Pressed: ");
  //Serial.println(k);

  if (k == '1') {
    sensorPin = (sensorPin == A0) ? A1 : A0;

    // relearn baseline quickly on the new input
    long sum = 0;
    for (int i = 0; i < 200; i++) { sum += analogRead(sensorPin); delay(2); }
    baseline = sum / 200.0f;
    noiseLevel = 5.0f;

  } else if (k == '4') {
    buzzerEnabled = !buzzerEnabled;
    noTone(BUZZER_PIN);
  }
  else if (k == 'A'){
    Serial.println("\n");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // initial baseline
  long sum = 0;
  for (int i = 0; i < 200; i++) { sum += analogRead(sensorPin); delay(2); }
  baseline = sum / 200.0f;

  resetBuf();
  lastChangeUs = micros();

  Serial.println("Morse laser decoder ready \n");
  //Serial.println("Keypad: A=toggle A0/A1, B=buzzer on/off");
}

void loop() {
  // keypad
  for (int i = 0; i < 10; i++) { ///debugg    // read a few times each loop
    char k = keypad.getKey();
    if (!k) break;
    handleKey(k);
  }

  // laser state
  bool newState = readLaserState();
  unsigned long now = micros();

  if (newState != laserOn) {
    unsigned long dur = now - lastChangeUs;
    lastChangeUs = now;

    // ignore tiny glitches
    if (dur < MIN_EVENT_US) {
      laserOn = newState;
      delayMicroseconds(SAMPLE_US);
      return;
    }

    if (laserOn) {
      bool isDash = (dur > (unsigned long)(2.0f * dotUs));
      char sym = isDash ? '-' : '.';
      pushSymbol(sym);

      if (isDash) beepDash(); else beepDot();

      // update dot estimate
      if (!isDash) {
        unsigned long newDot = (unsigned long)((1.0f - DOT_ALPHA) * dotUs + DOT_ALPHA * dur);
        if (newDot < DOT_MIN_US) newDot = DOT_MIN_US;
        if (newDot > DOT_MAX_US) newDot = DOT_MAX_US;
        dotUs = newDot;
      }

    } else {
      if (dur > (unsigned long)(6.0f * dotUs)) {
        flushChar();
        Serial.print(' ');
      } else if (dur > (unsigned long)(2.5f * dotUs)) {
        flushChar();
      }
    }

    laserOn = newState;
  }

  if (!laserOn) {
    unsigned long offDur = now - lastChangeUs;
    if (bufLen > 0 && offDur > (unsigned long)(3.5f * dotUs)) {
      flushChar();
    }
  }

  delayMicroseconds(SAMPLE_US);
}
