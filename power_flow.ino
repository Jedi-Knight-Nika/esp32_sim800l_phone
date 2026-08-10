// ---------------------------------------------------------------------------
// Power / boot
// ---------------------------------------------------------------------------
void bootPhone() {
  oledOn();
  enterState(ST_BOOTING);
  showCentered("ESP32 GSM", "BOOT", "Starting modem");
  beep(1800, 80);

  simPresent = simAutoBaud();
  if (!simPresent) {
    showCentered("SIM800L", "FAIL", "Check wiring");
    delay(2500);
    enterState(ST_HOME);
    redrawState();
    return;
  }

  showCentered("SIM800L", "OK", "Finding network");
  configureModem();

  unsigned long start = millis();
  do {
    updateNetworkStatus();
    showCentered("Network", registeredOnNetwork() ? "OK" : "WAIT",
                 operatorName.length() ? operatorName : "Registering");
    if (registeredOnNetwork())
      break;
    delay(1200);
  } while (millis() - start < 30000);

  enterState(ST_HOME);
  redrawState();
}

void shutdownPhone() {
  enterState(ST_POWERING_OFF);
  showCentered("ESP32 GSM", "OFF", "Shutting down");
  beep(900, 90);
  delay(600);
  if (simPresent)
    simCommand("AT+CFUN=0", 1000);
  enterState(ST_OFF);
  oledOff();
}

void handlePowerButton() {
  bool down = digitalRead(PIN_POWER_BUTTON) == LOW;
  unsigned long now = millis();

  if (down && !powerWasDown)
    powerDownMs = now;

  if (!down && powerWasDown) {
    unsigned long held = now - powerDownMs;
    if (state == ST_OFF) {
      bootPhone();
    } else if (held >= POWER_LONG_PRESS_MS) {
      shutdownPhone();
    }
  }

  powerWasDown = down;
}
