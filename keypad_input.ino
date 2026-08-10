// ---------------------------------------------------------------------------
// Keypad
// ---------------------------------------------------------------------------
void setupKeypad() {
  for (byte r = 0; r < KEYPAD_ROWS; r++) {
    pinMode(KEYPAD_ROW_PINS[r], OUTPUT);
    digitalWrite(KEYPAD_ROW_PINS[r], HIGH);
  }

  for (byte c = 0; c < KEYPAD_COLS; c++)
    pinMode(KEYPAD_COL_PINS[c], INPUT_PULLUP);
}

char scanRawKeypad() {
  for (byte r = 0; r < KEYPAD_ROWS; r++) {
    digitalWrite(KEYPAD_ROW_PINS[r], LOW);
    delayMicroseconds(5);

    for (byte c = 0; c < KEYPAD_COLS; c++) {
      if (digitalRead(KEYPAD_COL_PINS[c]) == LOW) {
        digitalWrite(KEYPAD_ROW_PINS[r], HIGH);
        return KEYPAD_MAP[r][c];
      }
    }
    digitalWrite(KEYPAD_ROW_PINS[r], HIGH);
  }

  return 0;
}

char getKeyPress() {
  char raw = scanRawKeypad();
  unsigned long now = millis();

  if (raw != lastRawKey) {
    lastRawKey = raw;
    lastKeyChangeMs = now;
  }

  if (now - lastKeyChangeMs < KEY_DEBOUNCE_MS)
    return 0;

  if (raw != debouncedKey) {
    debouncedKey = raw;
    if (debouncedKey)
      return debouncedKey;
  }

  return 0;
}
