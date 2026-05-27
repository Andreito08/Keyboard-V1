// ===== ARDUINO 1: TASTIERA ANALOGICA + LED (MASTER I2C) =====

#include <Wire.h>

const byte KEYBOARD_PIN = A0;

const byte LED_RED   = 4;
const byte LED_BLUE  = 5;
const byte LED_GREEN = 6;

const unsigned long MULTITAP_MS      = 1000;
const unsigned long MODE10_WAIT_MS   = 900;
const unsigned long DEBOUNCE_MS      = 60;
const unsigned long LED_PULSE_MS     = 120;

// ===== NUOVI VALORI CALCOLATI PER LE TUE RESISTENZE =====
const int expectedValues[10] = {
  990, 985, 969, 958, 945, 930, 913, 890, 867, 839
};

const char* keyLetters[9] = {
  "abc", "def", "ghi",
  "jkl", "mno", "pqr",
  "stu", "vwx", "yz"
};

enum Mode {
  MODE_ABC,
  MODE_123,
  MODE_SPC
};

enum PendingKind {
  PK_NONE,
  PK_LETTER,
  PK_NUMBER,
  PK_SPECIAL,
  PK_MODE10
};

struct Pending {
  bool active = false;
  byte key = 0;
  byte taps = 0;
  unsigned long lastTap = 0;
  PendingKind kind = PK_NONE;
  char value = 0;
};

Mode currentMode = MODE_ABC;
bool uppercaseNext = false;
Pending pending;

byte rawKey = 0;
byte stableKey = 0;
unsigned long lastKeyChange = 0;

int activeLed = -1;
unsigned long ledOffAt = 0;

// I2C slave address (display)
const uint8_t SLAVE_ADDR = 0x12;

int readAnalogSmooth() {
  long sum = 0;
  for (byte i = 0; i < 8; i++) {
    sum += analogRead(KEYBOARD_PIN);
    delayMicroseconds(250);
  }
  return sum / 8;
}

byte decodeKey(int value) {
  int bestDiff = 10000;
  int bestIdx = -1;

  for (byte i = 0; i < 10; i++) {
    int d = abs(value - expectedValues[i]);
    if (d < bestDiff) {
      bestDiff = d;
      bestIdx = i;
    }
  }

  if (bestDiff <= 12) return bestIdx + 1;
  return 0;
}

void allLedsOff() {
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_GREEN, LOW);
  activeLed = -1;
}

void pulseLed(byte pin) {
  allLedsOff();
  digitalWrite(pin, HIGH);
  activeLed = pin;
  ledOffAt = millis() + LED_PULSE_MS;
}

void updateLedPulse() {
  if (activeLed != -1 && millis() >= ledOffAt) {
    digitalWrite(activeLed, LOW);
    activeLed = -1;
  }
}

// Invia una stringa via I2C al slave (aggiunge '\n')
void sendI2C(const char* s) {
  Wire.beginTransmission(SLAVE_ADDR);
  Wire.write((const uint8_t*)s, strlen(s));
  Wire.write('\n');
  Wire.endTransmission();
  // piccolo delay per sicurezza bus (opzionale)
  delay(2);
}

void sendMode() {
  if (currentMode == MODE_ABC) sendI2C("MODE|abc");
  else if (currentMode == MODE_123) sendI2C("MODE|123");
  else sendI2C("MODE|+*-");
}

void sendCaps() {
  char buf[12];
  snprintf(buf, sizeof(buf), "CAPS|%c", uppercaseNext ? '1' : '0');
  sendI2C(buf);
}

void sendPreviewNone() {
  sendI2C("PREV|NONE");
}

void sendPreviewChar(char c) {
  char buf[16];
  snprintf(buf, sizeof(buf), "PREV|CHAR|%c", c);
  sendI2C(buf);
}

void sendPreviewText(const char* txt) {
  char buf[32];
  snprintf(buf, sizeof(buf), "PREV|TEXT|%s", txt);
  sendI2C(buf);
}

void sendOutChar(char c) {
  char buf[16];
  snprintf(buf, sizeof(buf), "OUT|CHAR|%c", c);
  sendI2C(buf);
}

void sendOutCmd(const char* cmd) {
  char buf[32];
  snprintf(buf, sizeof(buf), "OUT|CMD|%s", cmd);
  sendI2C(buf);
}

char applyCaps(char c) {
  if (uppercaseNext && c >= 'a' && c <= 'z') return c - 'a' + 'A';
  return c;
}

char getLetter(byte key, byte taps) {
  const char* grp = keyLetters[key - 1];
  byte len = strlen(grp);
  byte idx = (taps - 1) % len;
  return applyCaps(grp[idx]);
}

char specialFromKey(byte key) {
  switch (key) {
    case 1: return ' ';
    case 2: return '.';
    case 3: return ',';
    case 7: return '?';
    case 8: return '!';
    case 9: return '-';
    default: return 0;
  }
}

void clearPending() {
  pending.active = false;
  pending.key = 0;
  pending.taps = 0;
  pending.lastTap = 0;
  pending.kind = PK_NONE;
  pending.value = 0;
  sendPreviewNone();
}

void startPending(byte key) {
  pending.active = true;
  pending.key = key;
  pending.taps = 1;
  pending.lastTap = millis();

  if (key == 10) {
    pending.kind = PK_MODE10;

    if (currentMode == MODE_ABC) {
      sendPreviewText("123");
    } else if (currentMode == MODE_123) {
      sendPreviewChar('0');
    } else {
      sendPreviewText("abc");
    }
    return;
  }

  if (currentMode == MODE_ABC) {
    pending.kind = PK_LETTER;
    pending.value = getLetter(key, 1);
    sendPreviewChar(pending.value);
  }
  else if (currentMode == MODE_123) {
    pending.kind = PK_NUMBER;
    pending.value = char('0' + key);
    sendPreviewChar(pending.value);
  }
  else {
    pending.kind = PK_SPECIAL;

    if (key == 4) {
      pending.value = '\n';
      sendPreviewText("INV");
    } else if (key == 5) {
      pending.value = '^';
      sendPreviewText("MAI");
    } else if (key == 6) {
      pending.value = '\b';
      sendPreviewText("DEL");
    } else {
      pending.value = specialFromKey(key);
      if (pending.value == ' ') sendPreviewText("SPC");
      else sendPreviewChar(pending.value);
    }
  }
}

void commitMode10() {
  if (currentMode == MODE_ABC) {
    currentMode = MODE_123;
    sendMode();
    pulseLed(LED_BLUE);
  }
  else if (currentMode == MODE_123) {
    if (pending.taps >= 2) {
      currentMode = MODE_SPC;
      sendMode();
      pulseLed(LED_BLUE);
    } else {
      sendOutChar('0');
      pulseLed(LED_BLUE);
    }
  }
  else {
    currentMode = MODE_ABC;
    sendMode();
    pulseLed(LED_BLUE);
  }

  clearPending();
}

void commitPending() {
  if (!pending.active) return;

  if (pending.kind == PK_MODE10) {
    commitMode10();
    return;
  }

  if (pending.kind == PK_LETTER) {
    sendOutChar(pending.value);
    pulseLed(LED_GREEN);
    if (uppercaseNext) {
      uppercaseNext = false;
      sendCaps();
    }
  }
  else if (pending.kind == PK_NUMBER) {
    sendOutChar(pending.value);
    pulseLed(LED_BLUE);
  }
  else if (pending.kind == PK_SPECIAL) {
    if (pending.value == '\n') {
      sendOutCmd("ENTER");
      pulseLed(LED_BLUE);
    } else if (pending.value == '^') {
      uppercaseNext = !uppercaseNext;
      sendCaps();
      sendOutCmd("CAPS");
      pulseLed(LED_BLUE);
    } else if (pending.value == '\b') {
      sendOutCmd("BACK");
      pulseLed(LED_RED);
    } else {
      sendOutChar(pending.value);
      pulseLed(LED_BLUE);
    }
  }

  clearPending();
}

void advancePending() {
  pending.taps++;
  pending.lastTap = millis();

  if (pending.kind == PK_LETTER) {
    pending.value = getLetter(pending.key, pending.taps);
    sendPreviewChar(pending.value);
  }
  else if (pending.kind == PK_MODE10) {
    if (currentMode == MODE_ABC) {
      sendPreviewText("123");
      pending.taps = 1;
    }
    else if (currentMode == MODE_123) {
      if (pending.taps >= 2) {
        pending.taps = 2;
        sendPreviewText("+*-");
      }
    }
    else {
      sendPreviewText("abc");
      pending.taps = 1;
    }
  }
  else {
    commitPending();
  }
}

void handlePress(byte key) {
  if (!pending.active) {
    startPending(key);
    return;
  }

  if (key == pending.key && (millis() - pending.lastTap) <= MULTITAP_MS) {
    advancePending();
  } else {
    commitPending();
    startPending(key);
  }
}

void updateKeyboard() {
  int analogValue = readAnalogSmooth();
  byte keyNow = decodeKey(analogValue);

  if (keyNow != rawKey) {
    rawKey = keyNow;
    lastKeyChange = millis();
  }

  if ((millis() - lastKeyChange) >= DEBOUNCE_MS && stableKey != rawKey) {
    stableKey = rawKey;
    if (stableKey != 0) {
      handlePress(stableKey);
    }
  }
}

void updateTimeout() {
  if (!pending.active) return;

  unsigned long waitTime = (pending.kind == PK_MODE10) ? MODE10_WAIT_MS : MULTITAP_MS;

  if (millis() - pending.lastTap > waitTime) {
    commitPending();
  }
}

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  allLedsOff();

  Wire.begin(); // master
  delay(50); // lascia tempo allo slave di inizializzarsi

  sendMode();
  sendCaps();
  sendPreviewNone();
}

void loop() {
  updateKeyboard();
  updateTimeout();
  updateLedPulse();
}
