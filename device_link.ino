// ---------------------------------------------------------------------------
// Device link
// ---------------------------------------------------------------------------
// UART bridge to the ATmega64A terminal (RC522 + 16x2 LCD + keypad).
//
// The ESP32 stays the brain (state machine + GSM). The ATmega is a dumb
// terminal: it renders the two text lines we send, and it streams back the
// keypad presses and RFID card taps.
//
// Wire protocol (plain ASCII lines, '\n' terminated), 9600 8N1:
//
//   ESP32 -> ATmega
//     D|<line1>|<line2>   draw two 16-char lines on the LCD
//     B|k / B|s / B|r     buzzer: key / sms / ring tone
//     P|1 / P|0           LCD power on / off (backlight + clear)
//
//   ATmega -> ESP32
//     K<char>             one keypad press, e.g. K2  K#  K*
//     C                   RFID card tap (power toggle event)
//
// LINK is Serial1. Serial  = USB debug, Serial2 = SIM800L, so the ATmega link
// gets the third hardware UART on the freed keypad pins.

static String linkRxBuf = "";
static String linkKeyQueue = "";
static bool linkCardPending = false;

static String lcd16(const String &s) {
  if ((int)s.length() > 16)
    return s.substring(0, 16);
  return s;
}

void linkBegin() {
  LINK.begin(LINK_BAUD, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
}

void lcdShow(const String &line1, const String &line2) {
  LINK.print("D|");
  LINK.print(lcd16(line1));
  LINK.print('|');
  LINK.print(lcd16(line2));
  LINK.print('\n');
}

void lcdBeep(char code) {
  LINK.print("B|");
  LINK.print(code);
  LINK.print('\n');
}

void lcdPower(bool on) {
  LINK.print("P|");
  LINK.print(on ? '1' : '0');
  LINK.print('\n');
}

// Consume one buffered keypad char from the terminal, 0 if none.
char linkPopKey() {
  if (linkKeyQueue.length() == 0)
    return 0;
  char k = linkKeyQueue[0];
  linkKeyQueue.remove(0, 1);
  return k;
}

// True once per card tap; clears the flag.
bool consumeCardTap() {
  if (!linkCardPending)
    return false;
  linkCardPending = false;
  return true;
}

static void linkDispatch(const String &line) {
  if (line.length() == 0)
    return;
  char t = line[0];
  if (t == 'K' && line.length() >= 2) {
    linkKeyQueue += line[1];
  } else if (t == 'C') {
    linkCardPending = true;
  }
}

void linkPoll() {
  while (LINK.available()) {
    char c = LINK.read();
    if (c == '\n' || c == '\r') {
      linkDispatch(linkRxBuf);
      linkRxBuf = "";
    } else if (linkRxBuf.length() < 40) {
      linkRxBuf += c;
    }
  }
}

// ---------------------------------------------------------------------------
// 16x2 renderings of the phone screens (mirror of the OLED, LCD-shaped).
// ---------------------------------------------------------------------------
static String lcdSmsChunk(const String &text, int lineIndex) {
  int start = lineIndex * 16;
  if (start >= (int)text.length())
    return "";
  return text.substring(start, min(start + 16, (int)text.length()));
}

static String lcdTail(const String &s) {
  if ((int)s.length() > 16)
    return s.substring(s.length() - 16);
  return s;
}

void lcdRenderState() {
  String l1 = "";
  String l2 = "";

  switch (state) {
  case ST_HOME:
    if (!simReady)
      l1 = "No SIM";
    else if (!registeredOnNetwork())
      l1 = "No connection";
    else
      l1 = operatorName.length() ? operatorName : "Registered";
    if (signalCsq == 99)
      l2 = "Sig --  #=menu";
    else
      l2 = "Sig " + String(signalCsq) + "/31 #menu";
    break;

  case ST_MENU:
    l1 = "Menu";
    l2 = ">" + String(MAIN_MENU[mainMenuIndex]);
    break;

  case ST_MESSAGES:
    l1 = "Messages";
    l2 = ">" + String(MESSAGE_MENU[messageMenuIndex]);
    break;

  case ST_DIAL:
    l1 = "Dial: #call *del";
    l2 = lcdTail(dialNumber);
    break;

  case ST_CALLING:
    l1 = "Calling";
    l2 = lcdTail(dialNumber);
    break;

  case ST_RINGING_OUT:
    l1 = "Ringing";
    l2 = lcdTail(dialNumber);
    break;

  case ST_IN_CALL:
    l1 = "In call  *=end";
    l2 = lcdTail(dialNumber.length() ? dialNumber : incomingNumber);
    break;

  case ST_INCOMING_CALL:
    l1 = "Incoming #ans *no";
    l2 = lcdTail(incomingNumber.length() ? incomingNumber : "Unknown");
    break;

  case ST_INBOX:
    l1 = "Inbox " + String(inboxCount);
    if (inboxCount == 0)
      l2 = "No messages";
    else
      l2 = ">" + (inbox[inboxSelected].sender.length()
                      ? inbox[inboxSelected].sender
                      : String("Unknown"));
    break;

  case ST_READ_SMS:
    if (inboxCount == 0) {
      l1 = "Inbox 0";
      l2 = "No messages";
    } else {
      l1 = lcd16("F:" + inbox[inboxSelected].sender);
      l2 = lcdSmsChunk(inbox[inboxSelected].text, smsScrollLine);
    }
    break;

  case ST_SMS_TO:
    l1 = "SMS to: #ok *del";
    l2 = lcdTail(smsTo);
    break;

  case ST_SMS_BODY:
    l1 = "Msg: #send *del";
    l2 = lcdTail(smsBody);
    break;

  default:
    return;
  }

  lcdShow(l1, l2);
}
