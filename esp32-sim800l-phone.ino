/*
 * ESP32 GSM Phone — Phase 1 (Serial-triggered outgoing voice calls)
 * -----------------------------------------------------------------
 * Hardware: ESP32 DevKitV1 (WROOM-32) + SIM800L + SSD1306 128x64 OLED
 *
 * What it does:
 *   - Reads a phone number typed into the Serial Monitor.
 *   - Dials via SIM800L (ATD<number>;) over UART2 (Serial2).
 *   - Tracks call state with a state machine using AT+CLCC polling + URCs.
 *   - Mirrors state on the OLED in real time.
 *   - On boot, runs an init sequence: power detect, AT handshake,
 *     network registration (AT+CREG?), signal quality (AT+CSQ).
 *
 * Libraries required (install via Library Manager):
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - (Adafruit BusIO — pulled in automatically as a dependency)
 *
 * Wiring: see the wiring table in the accompanying notes / README.
 *   ESP32 GPIO17 (TX2) -> SIM800L RXD   (via level shift / divider, see notes)
 *   ESP32 GPIO16 (RX2) <- SIM800L TXD
 *   ESP32 GPIO21 (SDA) <-> OLED SDA
 *   ESP32 GPIO22 (SCL) <-> OLED SCL
 *   SIM800L powered from SEPARATE 3.7–4.2V supply able to source ~2A spikes.
 *   Common GND between ESP32, SIM800L, OLED supply.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// Pin / peripheral configuration
// ---------------------------------------------------------------------------
static const int PIN_SIM_RX = 16; // ESP32 RX2  <- SIM800L TXD
static const int PIN_SIM_TX = 17; // ESP32 TX2  -> SIM800L RXD (level-shift!)
static const int PIN_SDA = 21;    // OLED I2C SDA
static const int PIN_SCL = 22;    // OLED I2C SCL

// SIM800L UART. Clones ship at different fixed bauds (or autobaud). We probe a
// list at boot and lock whichever one answers, so you don't have to guess.
static const uint32_t SIM_BAUD_CANDIDATES[] = {115200, 9600, 57600, 38400,
                                               19200};
static const int SIM_BAUD_COUNT = 5;
uint32_t simBaud = 115200; // filled in once a baud is found

// OLED
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C // some boards are 0x3D
#define OLED_RESET -1  // no dedicated reset pin
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// Convenience alias for the modem UART.
#define SIM Serial2

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum PhoneState {
  ST_BOOT,      // running init sequence
  ST_IDLE,      // registered, waiting for a number
  ST_DIALING,   // ATD sent, call setup in progress
  ST_RINGING,   // remote end alerting (ringback)
  ST_CONNECTED, // call answered / active
  ST_ENDED      // call finished/rejected/busy/no-answer, transient
};

PhoneState state = ST_BOOT;
unsigned long stateEnteredMs = 0;

// Call context
String dialNumber = "";   // number currently being dialed
String serialBuffer = ""; // accumulates typed number from Serial
String endReason = "";    // why the last call ended (for ENDED screen)

// SIM800L status snapshot
bool simPresent = false;
int netReg = -1;    // AT+CREG stat: 1=home,5=roaming registered
int signalCsq = 99; // AT+CSQ rssi: 0..31, 99=unknown

// Polling timers
unsigned long lastClccPollMs = 0;
unsigned long lastCsqPollMs = 0;
const unsigned long CLCC_POLL_INTERVAL = 800; // ms, while in a call
const unsigned long CSQ_POLL_INTERVAL = 5000; // ms, while idle

// ---------------------------------------------------------------------------
// Low-level SIM helpers
// ---------------------------------------------------------------------------

// Drain and return everything the modem sent within `timeoutMs`.
String simReadFor(unsigned long timeoutMs) {
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (SIM.available()) {
      char c = (char)SIM.read();
      resp += c;
      start = millis(); // extend window while bytes keep arriving
    }
  }
  return resp;
}

// Send an AT command and return the full response text.
String simCommand(const String &cmd, unsigned long timeoutMs = 1000) {
  while (SIM.available())
    SIM.read(); // flush stale bytes
  SIM.print(cmd);
  SIM.print("\r\n");
  return simReadFor(timeoutMs);
}

// Play a test tone out the SIM800L SPK line to verify speaker wiring + audio.
//   AT+STTONE=<play>,<toneID>,<duration_ms>  (play=1 start, 0 stop)
// toneID 1..20 are built-in tones; duration in ms. Requires the speaker on
// SPK_P/SPK_N. This is the audio-path check requested for bring-up.
void simSpeakerTest(int toneId = 1, int durationMs = 1500) {
  simCommand("AT+CLVL=60", 400); // moderate (PAM amp boosts)
  simCommand("AT+CRSL=60", 400); // moderate ring/tone level
  String cmd = "AT+STTONE=1," + String(toneId) + "," + String(durationMs);
  Serial.print(F("[AT>] "));
  Serial.println(cmd);
  String r = simCommand(cmd, 400);
  Serial.print(F("[AT<] "));
  Serial.println(r);
}

// Try AT a few times at the current baud. Also nudges autobaud modules to lock.
bool simHandshake(int attempts = 5) {
  for (int i = 0; i < attempts; i++) {
    String r = simCommand("AT", 400);

    if (r.indexOf("OK") >= 0)
      return true;

    delay(300);
  }

  return false;
}

// Probe each candidate baud, re-opening Serial2, until the modem answers AT.
// Returns true and leaves Serial2 open at the working baud (stored in simBaud).
bool simAutoBaud() {
  for (int i = 0; i < SIM_BAUD_COUNT; i++) {
    uint32_t b = SIM_BAUD_CANDIDATES[i];
    Serial.print(F("[BOOT] Trying baud "));
    Serial.println(b);
    SIM.end();

    delay(50);
    SIM.begin(b, SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
    delay(200);

    if (simHandshake(4)) {
      simBaud = b;
      Serial.print(F("[BOOT] Locked baud "));
      Serial.println(b);
      return true;
    }
  }
  return false;
}

// Parse "+CSQ: <rssi>,<ber>" -> rssi (0..31, or 99).
int parseCsq(const String &r) {
  int idx = r.indexOf("+CSQ:");

  if (idx < 0)
    return 99;
  int rssi = r.substring(idx + 5).toInt();

  return rssi;
}

// Parse "+CREG: <n>,<stat>" -> stat.
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

// Convert CSQ rssi (0..31) to approx dBm for display.
int csqToDbm(int rssi) {
  if (rssi == 99)
    return 0;
  return -113 + 2 * rssi; // per 3GPP 27.007 mapping
}

// ---------------------------------------------------------------------------
// OLED rendering
// ---------------------------------------------------------------------------
void drawHeader() {
  display.setCursor(0, 0);
  display.print("GSM Phone");
  // signal bars area (right side)
  display.setCursor(84, 0);

  if (netReg == 1 || netReg == 5) {
    display.print(netReg == 5 ? "R " : "  ");
    display.print(csqToDbm(signalCsq));
  } else {
    display.print("no net");
  }

  display.drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
}

void showScreen(const String &line1, const String &line2 = "",
                const String &line3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  drawHeader();
  display.setCursor(0, 16);
  display.print(line1);

  if (line2.length()) {
    display.setCursor(0, 30);
    display.print(line2);
  }

  if (line3.length()) {
    display.setCursor(0, 44);
    display.print(line3);
  }

  display.display();
}

// Big, centered status word for call states.
void showBigStatus(const String &word, const String &sub = "") {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawHeader();
  display.setTextSize(2);
  display.setCursor(0, 22);
  display.print(word);

  if (sub.length()) {
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print(sub);
  }

  display.display();
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------
void enterState(PhoneState s) {
  state = s;
  stateEnteredMs = millis();

  switch (s) {
  case ST_IDLE:
    Serial.println(F("[STATE] IDLE — type a number + Enter to dial."));
    showScreen("Ready.", "Type number in", "Serial Monitor");
    break;
  case ST_DIALING:
    Serial.print(F("[STATE] DIALING "));
    Serial.println(dialNumber);
    showBigStatus("Calling", dialNumber);
    break;
  case ST_RINGING:
    Serial.println(F("[STATE] RINGING"));
    showBigStatus("Ringing", dialNumber);
    break;
  case ST_CONNECTED:
    Serial.println(F("[STATE] CONNECTED"));
    showBigStatus("Connected", dialNumber);
    break;
  case ST_ENDED:
    Serial.print(F("[STATE] CALL ENDED — "));
    Serial.println(endReason);
    // Show the reason so you can tell answered vs busy vs no-answer etc.
    showBigStatus("Ended", endReason.length() ? endReason : dialNumber);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------------
// Call control
// ---------------------------------------------------------------------------
void startCall(const String &number) {
  dialNumber = number;
  endReason = ""; // clear previous call's outcome
  // ATD<number>; -> the trailing ';' selects VOICE call (not data).
  String cmd = "ATD" + number + ";";
  Serial.print(F("[AT>] "));
  Serial.println(cmd);
  String r = simCommand(cmd, 1500);
  Serial.print(F("[AT<] "));
  Serial.println(r);
  enterState(ST_DIALING);
  lastClccPollMs = 0; // force immediate CLCC poll
}

void hangUp() {
  Serial.println(F("[AT>] ATH"));

  String r = simCommand("ATH", 1000);

  Serial.print(F("[AT<] "));

  Serial.println(r);

  endReason = "Hung up";

  enterState(ST_ENDED);
}

// Map AT+CLCC <stat> field to our state machine.
//   +CLCC: <id>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>]
//   stat: 0=active 1=held 2=dialing 3=alerting 4=incoming 5=waiting
void handleClcc(const String &resp) {
  int idx = resp.indexOf("+CLCC:");

  if (idx < 0) {
    // No active call line reported. If we thought we were in a call, it ended.
    if (state == ST_CONNECTED) {
      endReason = "Call ended";
      enterState(ST_ENDED);
    } else if (state == ST_DIALING || state == ST_RINGING) {
      // Cleared before answer, no explicit URC seen.
      if (endReason.length() == 0)
        endReason = "No answer";
      enterState(ST_ENDED);
    }
    return;
  }

  // Field 3 = stat. Walk past 2 commas.
  String tail = resp.substring(idx + 6);
  int c1 = tail.indexOf(',');
  int c2 = tail.indexOf(',', c1 + 1);
  int c3 = tail.indexOf(',', c2 + 1);

  if (c1 < 0 || c2 < 0)
    return;
  int stat = tail.substring(c2 + 1, c3 < 0 ? tail.length() : c3).toInt();

  switch (stat) {
  case 0:
    if (state != ST_CONNECTED)
      enterState(ST_CONNECTED);
    break; // active
  case 2:
    if (state != ST_DIALING)
      enterState(ST_DIALING);
    break; // dialing
  case 3:
    if (state != ST_RINGING)
      enterState(ST_RINGING);
    break; // alerting
  default:
    break; // held/incoming/waiting not handled in phase 1
  }
}

// Watch for unsolicited call-end / failure result codes on the modem line.
void scanUrc(const String &chunk) {
  if (chunk.length() == 0)
    return;
  if (state != ST_DIALING && state != ST_RINGING && state != ST_CONNECTED)
    return;

  // Each result code maps to a human reason shown on the OLED.
  String reason = "";
  if (chunk.indexOf("BUSY") >= 0)
    reason = "Busy"; // callee busy
  else if (chunk.indexOf("NO ANSWER") >= 0)
    reason = "No answer"; // rang, no pickup
  else if (chunk.indexOf("NO DIALTONE") >= 0)
    reason = "No dialtone";                  // line problem
  else if (chunk.indexOf("NO CARRIER") >= 0) // hung up / rejected
    reason = (state == ST_CONNECTED) ? "Call ended" : "Rejected/ended";
  else if (chunk.indexOf("ERROR") >= 0)
    reason = "Call failed"; // bad number/cmd

  if (reason.length()) {
    Serial.print(F("[URC] "));
    Serial.println(chunk);
    endReason = reason;
    enterState(ST_ENDED);
  }
}

// ---------------------------------------------------------------------------
// Boot init sequence (with OLED progress)
// ---------------------------------------------------------------------------
void bootSequence() {
  showScreen("Booting...", "SIM800L detect");

  showScreen("Booting...", "Finding baud");
  simPresent = simAutoBaud();
  if (!simPresent) {
    showScreen("SIM800L: FAIL", "No AT @ any baud", "Swap TX/RX? GND?");
    Serial.println(F("[BOOT] No AT response at any baud. Check TX<->RX, GND."));

    while (true) {
      delay(1000);
    }
  }
  showScreen("SIM800L: OK", String("baud ") + simBaud, "AT handshake ok");
  Serial.print(F("[BOOT] SIM800L handshake OK @ "));
  Serial.println(simBaud);

  simCommand("ATE0", 500);       // echo off -> cleaner parsing
  simCommand("AT+CLIP=1", 500);  // caller ID for future incoming calls
  simCommand("AT+CMGF=1", 500);  // SMS text mode (future use)
  simCommand("AT+CHFA=0", 500);  // audio channel 0 = main (speaker path)
  simCommand("AT+CLVL=60", 500); // moderate volume (PAM amp boosts)
  simCommand("AT+SIDET=0", 500); // sidetone off

  showScreen("Speaker test", "Listen for BEEP");
  Serial.println(F("[BOOT] Speaker test — expect audible BEEP"));
  simSpeakerTest(1, 4000);        // toneId 1, 4s, louder/longer
  delay(4200);                    // let it finish before next AT traffic
  simCommand("AT+STTONE=0", 400); // ensure tone stopped

  showScreen("Network...", "Registering");
  unsigned long start = millis();
  netReg = -1;

  while (millis() - start < 30000) {
    String r = simCommand("AT+CREG?", 800);
    netReg = parseCreg(r);
    if (netReg == 1 || netReg == 5)
      break;
    showScreen("Network...", String("CREG stat=") + netReg, "waiting");
    delay(1500);
  }
  if (netReg == 1 || netReg == 5) {
    Serial.println(F("[BOOT] Registered on network"));
    showScreen("Network: OK", netReg == 5 ? "Roaming" : "Home network");
  } else {
    Serial.println(F("[BOOT] NOT registered (continuing anyway)"));
    showScreen("Network: none", "Not registered", "Check SIM/antenna");
  }
  delay(1200);

  String r = simCommand("AT+CSQ", 800);
  signalCsq = parseCsq(r);
  Serial.print(F("[BOOT] CSQ rssi="));
  Serial.print(signalCsq);
  Serial.print(F(" (~"));
  Serial.print(csqToDbm(signalCsq));
  Serial.println(F(" dBm)"));
  showScreen("Signal:", String("rssi ") + signalCsq + "/31",
             String(csqToDbm(signalCsq)) + " dBm");
  delay(1500);

  enterState(ST_IDLE);
}

// ---------------------------------------------------------------------------
// Diagnostics — type 'd' + Enter in Serial Monitor to dump SIM/network state.
// ---------------------------------------------------------------------------
void diagOne(const char *label, const char *cmd,
             unsigned long timeoutMs = 1500) {
  Serial.print(F("--- "));
  Serial.print(label);
  Serial.print(F("  ("));
  Serial.print(cmd);
  Serial.println(F(") ---"));
  String r = simCommand(cmd, timeoutMs);
  r.trim();
  Serial.println(r.length() ? r : "(no reply)");
  Serial.println();
}

void runDiagnostics() {
  Serial.println(F("\n========== SIM DIAGNOSTICS =========="));
  diagOne("SIM detected/unlocked", "AT+CPIN?"); // want: +CPIN: READY
  diagOne("Signal quality", "AT+CSQ");          // want: rssi > 5, not 99
  diagOne("Registration", "AT+CREG?");          // want: +CREG: n,1 or n,5
  diagOne("Current operator", "AT+COPS?");      // want: a carrier name
  diagOne("Last error reason", "AT+CEER");      // why last attempt failed
  // NOTE: AT+COPS=? full scan removed — it blocks ~60s and browns out the
  // module. You're already registered ("Call Ready"); no need to scan.
  Serial.println(F("========== END DIAGNOSTICS ==========\n"));
}

// ---------------------------------------------------------------------------
// Serial input (phone number entry)  [TODO: replace/augment with keypad]
// ---------------------------------------------------------------------------
void pollSerialInput() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) {
        if (state == ST_IDLE) {
          startCall(serialBuffer);
        } else if (state == ST_DIALING || state == ST_RINGING ||
                   state == ST_CONNECTED) {
          // Typing anything during a call hangs up (dev convenience).
          Serial.println(F("[IN] input during call -> hang up"));
          hangUp();
        }
      }
      serialBuffer = "";
    } else if (c == 't' || c == 'T') {
      // Dev shortcut: replay speaker test tone on demand while idle.
      if (state == ST_IDLE) {
        Serial.println(F("[IN] speaker test tone"));
        simSpeakerTest(1, 1500);
      }
    } else if (c == 'd' || c == 'D') {
      // Dev shortcut: dump SIM/network diagnostics.
      runDiagnostics();
    } else if (c == 'r' || c == 'R') {
      // Dev shortcut: force automatic network (re)registration.
      Serial.println(F("[IN] AT+COPS=0 (auto register)"));
      Serial.println(simCommand("AT+COPS=0", 5000));
    } else if (isdigit(c) || c == '+' || c == '*' || c == '#') {
      serialBuffer += c; // only keep dialable chars
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== ESP32 GSM Phone (phase 1) ==="));

  // OLED on default I2C pins.
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("[BOOT] SSD1306 init failed (check addr 0x3C/0x3D)"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("ESP32 GSM Phone");
    display.display();
  }

  // Modem UART — opened here; bootSequence() re-probes baud via simAutoBaud().
  SIM.begin(SIM_BAUD_CANDIDATES[0], SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
  delay(300);

  bootSequence();
}

void loop() {
  pollSerialInput();

  if (SIM.available()) {
    String chunk = simReadFor(60);
    scanUrc(chunk);

    if (chunk.indexOf("+CLCC:") >= 0)
      handleClcc(chunk);
  }

  unsigned long now = millis();

  if (state == ST_DIALING || state == ST_RINGING || state == ST_CONNECTED) {
    if (now - lastClccPollMs >= CLCC_POLL_INTERVAL) {
      lastClccPollMs = now;

      String r = simCommand("AT+CLCC", 600);

      handleClcc(r);

      scanUrc(r);
    }
  }

  if (state == ST_ENDED && (now - stateEnteredMs > 2500)) {
    dialNumber = "";
    enterState(ST_IDLE);
  }

  if (state == ST_IDLE && (now - lastCsqPollMs >= CSQ_POLL_INTERVAL)) {
    lastCsqPollMs = now;

    String r = simCommand("AT+CSQ", 500);
    signalCsq = parseCsq(r);

    String c = simCommand("AT+CREG?", 500);
    int reg = parseCreg(c);

    if (reg >= 0)
      netReg = reg;

    showScreen("Ready.", "Type number in", "Serial Monitor");
  }

  // -------------------------------------------------------------------------
  // TODO (phase 2+): physical keypad matrix input -> build dialNumber,
  //   replacing/augmenting pollSerialInput().
  // TODO: incoming call handling — enable "AT+CLCC" URC or RING/+CLIP parse,
  //   add ST_INCOMING state, answer with ATA on a button.
  // TODO: dedicated hang-up button (GPIO) -> hangUp().
  // TODO: ringback/busy tone — confirm SIM800L passes it through SPK line
  //   during ST_RINGING; if silent, generate fallback tone via ledc/PWM on a
  //   piezo pin here while state == ST_RINGING.
  // TODO: low-battery monitor via ADC on battery divider -> OLED warning.
  // -------------------------------------------------------------------------
}
