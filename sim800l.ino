// ---------------------------------------------------------------------------
// SIM helpers
// ---------------------------------------------------------------------------
String simReadFor(unsigned long timeoutMs) {
  String resp = "";
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    while (SIM.available()) {
      char c = (char)SIM.read();
      resp += c;
      start = millis();
    }
  }

  return resp;
}

String simCommand(const String &cmd, unsigned long timeoutMs) {
  while (SIM.available())
    SIM.read();

  SIM.print(cmd);
  SIM.print("\r\n");

  return simReadFor(timeoutMs);
}

bool simHandshake(int attempts = 5) {
  for (int i = 0; i < attempts; i++) {
    String r = simCommand("AT", 400);

    if (r.indexOf("OK") >= 0)
      return true;

    delay(300);
  }

  return false;
}

bool simAutoBaud() {
  for (int i = 0; i < SIM_BAUD_COUNT; i++) {
    uint32_t baud = SIM_BAUD_CANDIDATES[i];
    Serial.print(F("[BOOT] Trying SIM baud "));
    Serial.println(baud);
    SIM.end();
    delay(50);
    SIM.begin(baud, SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
    delay(250);

    if (simHandshake(4)) {
      simBaud = baud;
      Serial.print(F("[BOOT] SIM baud locked at "));
      Serial.println(baud);

      return true;
    }
  }

  return false;
}

int parseCsq(const String &r) {
  int idx = r.indexOf("+CSQ:");

  if (idx < 0)
    return 99;

  return r.substring(idx + 5).toInt();
}

int parseCreg(const String &r) {
  int idx = r.indexOf("+CREG:");

  if (idx < 0)
    return -1;

  String tail = r.substring(idx + 6);
  int comma = tail.indexOf(',');

  if (comma < 0)
    return -1;

  return tail.substring(comma + 1).toInt();
}

String parseOperator(const String &r) {
  int idx = r.indexOf("+COPS:");

  if (idx < 0)
    return "";

  int firstQuote = r.indexOf('"', idx);
  int secondQuote = r.indexOf('"', firstQuote + 1);

  if (firstQuote >= 0 && secondQuote > firstQuote)
    return r.substring(firstQuote + 1, secondQuote);

  return "";
}

int csqToBars(int rssi) {
  if (rssi == 99 || rssi < 2)
    return 0;
  if (rssi < 8)
    return 1;
  if (rssi < 14)
    return 2;
  if (rssi < 20)
    return 3;

  return 4;
}

void updateNetworkStatus() {
  String cpin = simCommand("AT+CPIN?", 700);
  simReady = cpin.indexOf("READY") >= 0;

  String creg = simCommand("AT+CREG?", 700);
  int parsedReg = parseCreg(creg);
  if (parsedReg >= 0)
    netReg = parsedReg;

  String csq = simCommand("AT+CSQ", 700);
  signalCsq = parseCsq(csq);

  String cops = simCommand("AT+COPS?", 1000);
  String parsedOperator = parseOperator(cops);

  if (parsedOperator.length())
    operatorName = parsedOperator;
}

void configureModem() {
  simCommand("AT+CFUN=1", 2000);
  simCommand("ATE0", 500);
  simCommand("AT+CLIP=1", 500);
  simCommand("AT+CMGF=1", 500);
  simCommand("AT+CNMI=2,1,0,0,0", 500); // SMS storage notification
  simCommand("AT+CHFA=0", 500);
  simCommand("AT+CLVL=60", 500);
  simCommand("AT+CRSL=60", 500);
}
