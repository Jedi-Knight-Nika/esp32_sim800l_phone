// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------
void enterState(PhoneState next) {
  state = next;
  stateEnteredMs = millis();
}

bool registeredOnNetwork() { return netReg == 1 || netReg == 5; }

void beep(int frequency, int durationMs) {
  if (frequency <= 0) {
    delay(durationMs);
    return;
  }
  tone(PIN_BUZZER, frequency, durationMs);
}

void keyBeep() {
  lcdBeep('k');
  beep(2400, 25);
}

void smsBeep() {
  lcdBeep('s');
  beep(1800, 80);
  delay(80);
  beep(2200, 80);
}

void ringBeep() {
  lcdBeep('r');
  beep(1200, 180);
  delay(80);
  beep(1500, 180);
}

String cleanPrintable(String s) {
  s.replace("\r", "");
  s.trim();
  return s;
}
