// ===== ARDUINO 2: LCD 16x2 + BUZZER (SLAVE I2C) =====

#include <Wire.h>
#include <LiquidCrystal.h>

const byte LCD_RS = 7;
const byte LCD_EN = 6;
const byte LCD_D4 = 5;
const byte LCD_D5 = 4;
const byte LCD_D6 = 3;
const byte LCD_D7 = 2;
const byte BUZZER_PIN = 8;

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

const uint8_t SLAVE_ADDR = 0x12;

char textBuf[2][16];
byte cursorRow = 0;
byte cursorCol = 0;

bool hasPreview = false;
bool previewIsChar = false;
char previewChar = 0;
char previewText[4] = "";

char modeLabel[4] = "abc";
bool capsNext = false;

// I2C receive buffer / flag (modificati in ISR -> volatile)
volatile char rxLine[32];
volatile byte rxPos = 0;
volatile bool rxReady = false;

bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigitChar(char c) {
  return c >= '0' && c <= '9';
}

void beepLetter() { tone(BUZZER_PIN, 1600, 60); }
void beepNumber() { tone(BUZZER_PIN, 1100, 70); }
void beepSpecial() { tone(BUZZER_PIN, 750, 80); }
void beepDelete() { tone(BUZZER_PIN, 320, 120); }

void clearTextBuf() {
  for (byte r = 0; r < 2; r++) {
    for (byte c = 0; c < 16; c++) textBuf[r][c] = ' ';
  }
}

void scrollUp() {
  for (byte c = 0; c < 16; c++) {
    textBuf[0][c] = textBuf[1][c];
    textBuf[1][c] = ' ';
  }
}

void advanceCursor() {
  cursorCol++;
  if (cursorCol >= 16) {
    cursorCol = 0;
    if (cursorRow == 0) cursorRow = 1;
    else {
      scrollUp();
      cursorRow = 1;
    }
  }
}

void putChar(char c) {
  if (cursorRow == 1 && cursorCol >= 13) {
    cursorRow = 1;
    cursorCol = 12;
  }

  textBuf[cursorRow][cursorCol] = c;
  advanceCursor();

  if (cursorRow == 1 && cursorCol >= 13) {
    cursorRow = 1;
    cursorCol = 12;
  }
}

void doEnter() {
  if (cursorRow == 0) {
    cursorRow = 1;
    cursorCol = 0;
  } else {
    scrollUp();
    cursorRow = 1;
    cursorCol = 0;
  }
}

void doBack() {
  if (cursorRow == 0 && cursorCol == 0) return;

  if (cursorCol > 0) cursorCol--;
  else {
    cursorRow = 0;
    cursorCol = 15;
  }

  if (cursorRow == 1 && cursorCol >= 13) cursorCol = 12;
  textBuf[cursorRow][cursorCol] = ' ';
}

void clearPreview() {
  hasPreview = false;
  previewIsChar = false;
  previewChar = 0;
  previewText[0] = '\0';
}

void setPreviewChar(char c) {
  hasPreview = true;
  previewIsChar = true;
  previewChar = c;
  previewText[0] = '\0';
}

void setPreviewText(const char* t) {
  hasPreview = true;
  previewIsChar = false;
  strncpy(previewText, t, 3);
  previewText[3] = '\0';
}

void render() {
  char temp[2][16];
  for (byte r = 0; r < 2; r++) {
    for (byte c = 0; c < 16; c++) temp[r][c] = textBuf[r][c];
  }

  temp[1][13] = modeLabel[0];
  temp[1][14] = modeLabel[1];
  temp[1][15] = modeLabel[2];

  byte blinkRow = cursorRow;
  byte blinkCol = cursorCol;

  if (blinkRow == 1 && blinkCol >= 13) blinkCol = 12;

  if (hasPreview) {
    if (previewIsChar) {
      temp[blinkRow][blinkCol] = previewChar;
    } else {
      temp[1][10] = ' ';
      temp[1][11] = ' ';
      temp[1][12] = ' ';
      for (byte i = 0; i < 3 && previewText[i] != '\0'; i++) {
        temp[1][10 + i] = previewText[i];
      }
    }
  } else {
    temp[1][10] = ' ';
    temp[1][11] = capsNext ? '^' : ' ';
    temp[1][12] = ' ';
  }

  lcd.setCursor(0, 0);
  for (byte c = 0; c < 16; c++) lcd.write(temp[0][c]);

  lcd.setCursor(0, 1);
  for (byte c = 0; c < 16; c++) lcd.write(temp[1][c]);

  lcd.setCursor(blinkCol, blinkRow);
  lcd.cursor();
  lcd.blink();
}

void processLine(char* s) {
  if (strncmp(s, "MODE|", 5) == 0) {
    strncpy(modeLabel, s + 5, 3);
    modeLabel[3] = '\0';
    clearPreview();
    beepSpecial();
  }
  else if (strncmp(s, "CAPS|", 5) == 0) {
    capsNext = (s[5] == '1');
    beepSpecial();
  }
  else if (strcmp(s, "PREV|NONE") == 0) {
    clearPreview();
  }
  else if (strncmp(s, "PREV|CHAR|", 10) == 0) {
    setPreviewChar(s[10]);
  }
  else if (strncmp(s, "PREV|TEXT|", 10) == 0) {
    setPreviewText(s + 10);
  }
  else if (strncmp(s, "OUT|CHAR|", 9) == 0) {
    char c = s[9];
    putChar(c);
    clearPreview();

    if (isLetter(c)) beepLetter();
    else if (isDigitChar(c)) beepNumber();
    else beepSpecial();
  }
  else if (strncmp(s, "OUT|CMD|", 8) == 0) {
    const char* cmd = s + 8;

    if (strcmp(cmd, "ENTER") == 0) {
      doEnter();
      beepSpecial();
    } else if (strcmp(cmd, "BACK") == 0) {
      doBack();
      beepDelete();
    } else if (strcmp(cmd, "CAPS") == 0) {
      beepSpecial();
    }

    clearPreview();
  }

  render();
}

// ISR chiamata quando arrivano dati I2C
void receiveEvent(int howMany) {
  while (Wire.available()) {
    char ch = Wire.read();

    if (ch == '\r') continue;

    if (ch == '\n') {
      rxLine[rxPos] = '\0';
      rxReady = true;
      rxPos = 0;
    } else if (rxPos < sizeof(rxLine) - 1) {
      rxLine[rxPos++] = ch;
    }
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.begin(16, 2);
  clearTextBuf();
  clearPreview();
  render();

  Wire.begin(SLAVE_ADDR);
  Wire.onReceive(receiveEvent);
}

void loop() {
  // Processa messaggi ricevuti via I2C
  if (rxReady) {
    char localLine[32];
    
    // Copia il buffer volatile in una variabile locale
    noInterrupts();
    strcpy(localLine, (const char*)rxLine);
    rxReady = false;
    interrupts();

    processLine(localLine);
  }
}
