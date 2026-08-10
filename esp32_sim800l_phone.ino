/*
 * ESP32 GSM Phone - keypad phone UI
 * ---------------------------------
 * Hardware: ESP32 DevKitV1 + SIM800L + SSD1306 128x64 OLED + 4x3 keypad.
 *
 * This sketch turns the first serial-only dialer into a small old-phone style
 * UI: soft power button, boot/shutdown screens, home screen with operator and
 * signal, keypad menu, outgoing/incoming calls, basic SMS send/read, and simple
 * buzzer sounds.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
static const int PIN_SIM_RX = 16; // D16 / RX2 <- SIM800L TXD
static const int PIN_SIM_TX = 17; // D17 / TX2 -> SIM800L RXD
static const int PIN_SDA = 21;    // D21 / SDA -> OLED SDA
static const int PIN_SCL = 22;    // D22 / SCL -> OLED SCL

static const int PIN_POWER_BUTTON = 18; // D18 -> button to GND
static const int PIN_BUZZER = 23;       // D23 -> buzzer positive

// ATmega64A terminal link (Serial1) on freed keypad pins.
static const int PIN_LINK_RX = 26; // D26 <- ATmega TX (5V -> 3.3V, level shift)
static const int PIN_LINK_TX = 27; // D27 -> ATmega RX
static const uint32_t LINK_BAUD = 9600;
#define LINK Serial1

static const byte KEYPAD_ROWS = 4;
static const byte KEYPAD_COLS = 3;
static const int KEYPAD_ROW_PINS[KEYPAD_ROWS] = {13, 14, 27, 26};
static const int KEYPAD_COL_PINS[KEYPAD_COLS] = {25, 33, 32};
static const char KEYPAD_MAP[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'},
};

// ---------------------------------------------------------------------------
// Modem / OLED
// ---------------------------------------------------------------------------
#define SIM Serial2

static const uint32_t SIM_BAUD_CANDIDATES[] = {115200, 9600, 57600, 38400,
                                               19200};
static const int SIM_BAUD_COUNT = 5;
uint32_t simBaud = 115200;

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_RESET -1
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool displayReady = false;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum PhoneState {
  ST_OFF,
  ST_BOOTING,
  ST_HOME,
  ST_MENU,
  ST_DIAL,
  ST_CALLING,
  ST_RINGING_OUT,
  ST_IN_CALL,
  ST_INCOMING_CALL,
  ST_CALL_ENDED,
  ST_MESSAGES,
  ST_INBOX,
  ST_READ_SMS,
  ST_SMS_TO,
  ST_SMS_BODY,
  ST_SENDING_SMS,
  ST_POWERING_OFF
};

PhoneState state = ST_OFF;
unsigned long stateEnteredMs = 0;

bool simPresent = false;
bool simReady = false;
int netReg = -1;
int signalCsq = 99;
String operatorName = "";

String dialNumber = "";
String incomingNumber = "";
String callEndReason = "";

static const int MAX_SMS = 5;
struct SmsMessage {
  String sender;
  String text;
};
SmsMessage inbox[MAX_SMS];
int inboxCount = 0;
int inboxSelected = 0;
int smsScrollLine = 0;
bool unreadSms = false;
String smsTo = "";
String smsBody = "";

int mainMenuIndex = 0;
int messageMenuIndex = 0;
const char *MAIN_MENU[] = {"Call", "Messages"};
const int MAIN_MENU_COUNT = 2;
const char *MESSAGE_MENU[] = {"Inbox", "Write SMS"};
const int MESSAGE_MENU_COUNT = 2;

unsigned long lastClccPollMs = 0;
unsigned long lastStatusPollMs = 0;
const unsigned long CLCC_POLL_INTERVAL = 900;
const unsigned long STATUS_POLL_INTERVAL = 7000;

char lastRawKey = 0;
char debouncedKey = 0;
unsigned long lastKeyChangeMs = 0;
const unsigned long KEY_DEBOUNCE_MS = 45;

bool powerWasDown = false;
unsigned long powerDownMs = 0;
const unsigned long POWER_LONG_PRESS_MS = 1200;

char lastTextKey = 0;
int lastTextIndex = 0;
unsigned long lastTextPressMs = 0;
const unsigned long TEXT_CYCLE_MS = 850;

// Functions implemented in the other Arduino tabs.
void setupKeypad();
void showCentered(const String &top, const String &middle,
                  const String &bottom);
void oledOff();
void handlePowerButton();
void pollSerialDebug();
void maybeFinishTextCycle();
char getKeyPress();
void handleKey(char key);
String simReadFor(unsigned long timeoutMs);
String simCommand(const String &cmd, unsigned long timeoutMs = 1000);
void handleModemChunk(const String &chunk);
void handleClcc(const String &resp);
void startCall(const String &number);
void hangUp(const String &reason = "Ended");
void answerCall();
bool sendSms(const String &number, const String &text);
void ringBeep();
void enterState(PhoneState next);
void redrawState();
void updateNetworkStatus();
void linkBegin();
void linkPoll();
void lcdShow(const String &line1, const String &line2);
void lcdBeep(char code);
void lcdPower(bool on);
void lcdRenderState();
char linkPopKey();
bool consumeCardTap();
void bootPhone();
void shutdownPhone();

// Arduino
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== ESP32 GSM keypad phone ==="));

  pinMode(PIN_POWER_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  setupKeypad();
  linkBegin();

  Wire.begin(PIN_SDA, PIN_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!displayReady)
    Serial.println(F("[BOOT] OLED failed. Check 0x3C/0x3D and wiring."));

  SIM.begin(SIM_BAUD_CANDIDATES[0], SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
  delay(200);

  showCentered("ESP32 GSM", "OFF", "Press power");
  delay(900);
  oledOff();
}

void loop() {
  handlePowerButton();
  pollSerialDebug();

  linkPoll();
  if (consumeCardTap()) {
    if (state == ST_OFF)
      bootPhone();
    else
      shutdownPhone();
  }

  if (state == ST_OFF || state == ST_BOOTING || state == ST_POWERING_OFF)
    return;

  maybeFinishTextCycle();

  char key = getKeyPress();
  handleKey(key);

  if (SIM.available()) {
    String chunk = simReadFor(80);
    Serial.println(chunk);
    handleModemChunk(chunk);
  }

  unsigned long now = millis();
  if (state == ST_CALLING || state == ST_RINGING_OUT || state == ST_IN_CALL) {
    if (now - lastClccPollMs >= CLCC_POLL_INTERVAL) {
      lastClccPollMs = now;
      String r = simCommand("AT+CLCC", 700);
      handleClcc(r);
      handleModemChunk(r);
    }
  }

  if (state == ST_INCOMING_CALL && now - stateEnteredMs > 900) {
    ringBeep();
    stateEnteredMs = now;
  }

  if (state == ST_CALL_ENDED && now - stateEnteredMs > 2200) {
    dialNumber = "";
    incomingNumber = "";
    enterState(ST_HOME);
    redrawState();
  }

  if (state == ST_HOME && now - lastStatusPollMs >= STATUS_POLL_INTERVAL) {
    lastStatusPollMs = now;
    updateNetworkStatus();
    redrawState();
  }
}
