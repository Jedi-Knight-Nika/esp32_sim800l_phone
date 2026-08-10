// ---------------------------------------------------------------------------
// OLED UI
// ---------------------------------------------------------------------------
void oledOn() {
  if (displayReady)
    display.ssd1306_command(SSD1306_DISPLAYON);
}

void oledOff() {
  if (displayReady) {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }
}

void drawSignalBars(int x, int y, int bars) {
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2;
    int bx = x + i * 5;
    int by = y + (9 - h);

    if (i < bars)
      display.fillRect(bx, by, 3, h, SSD1306_WHITE);
    else
      display.drawRect(bx, by, 3, h, SSD1306_WHITE);
  }
}

void drawHeader() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("GSM");

  if (unreadSms)
    display.print(" MSG");

  if (!simReady) {
    display.setCursor(78, 0);
    display.print("NO SIM");
  } else if (!registeredOnNetwork()) {
    display.setCursor(76, 0);
    display.print("NO NET");
  } else {
    drawSignalBars(106, 0, csqToBars(signalCsq));
  }

  display.drawFastHLine(0, 11, OLED_WIDTH, SSD1306_WHITE);
}

void clearUi() {
  if (!displayReady)
    return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void showCentered(const String &top, const String &middle,
                  const String &bottom) {
  clearUi();
  display.setTextSize(1);
  display.setCursor(0, 6);
  display.print(top);
  display.setTextSize(2);
  display.setCursor(0, 24);
  display.print(middle);
  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print(bottom);
  display.display();

  // Mirror the transient screen to the ATmega LCD (2 lines).
  lcdShow(top, middle.length() ? middle : bottom);
}

void showHome() {
  clearUi();
  drawHeader();
  display.setTextSize(1);
  display.setCursor(0, 17);

  if (!simReady) {
    display.print("No SIM / locked");
  } else if (!registeredOnNetwork()) {
    display.print("No connection");
  } else {
    display.print(operatorName.length() ? operatorName : "Registered");
  }

  display.setCursor(0, 31);
  display.print("Signal ");
  if (signalCsq == 99)
    display.print("unknown");
  else {
    display.print(signalCsq);
    display.print("/31");
  }

  display.setCursor(0, 48);
  display.print("# menu  digits call");
  display.display();
}

void showMenu(const char *title, const char **items, int count, int selected) {
  clearUi();
  drawHeader();
  display.setCursor(0, 15);
  display.print(title);

  for (int i = 0; i < count && i < 3; i++) {
    int y = 29 + i * 11;
    display.setCursor(0, y);
    display.print(i == selected ? "> " : "  ");
    display.print(items[i]);
  }

  display.display();
}

void showDial() {
  clearUi();
  drawHeader();
  display.setCursor(0, 16);
  display.print("Dial number");
  display.setTextSize(2);
  display.setCursor(0, 31);
  String shown = dialNumber;

  if (shown.length() > 10)
    shown = shown.substring(shown.length() - 10);

  display.print(shown);
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("# call  * del/back");
  display.display();
}

void showCallScreen(const String &title, const String &number) {
  clearUi();
  drawHeader();
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(title);
  display.setTextSize(1);
  display.setCursor(0, 47);
  display.print(number);
  display.display();
}

void showInbox() {
  clearUi();
  drawHeader();
  display.setCursor(0, 15);
  display.print("Inbox ");
  display.print(inboxCount);

  if (inboxCount == 0) {
    display.setCursor(0, 34);
    display.print("No messages");
    display.setCursor(0, 52);
    display.print("* back");
  } else {
    int first = inboxSelected;

    if (first > inboxCount - 3)
      first = max(0, inboxCount - 3);
    for (int i = 0; i < 3 && first + i < inboxCount; i++) {
      int idx = first + i;

      display.setCursor(0, 29 + i * 11);
      display.print(idx == inboxSelected ? "> " : "  ");
      display.print(inbox[idx].sender.length() ? inbox[idx].sender : "Unknown");
    }
  }

  display.display();
}

String smsLine(const String &text, int lineIndex) {
  int start = lineIndex * 21;

  if (start >= text.length())
    return "";

  return text.substring(start, min(start + 21, (int)text.length()));
}

int smsLineCount(const String &text) {
  if (!text.length())
    return 1;

  return ((int)text.length() + 20) / 21;
}

void showReadSms() {
  clearUi();
  drawHeader();

  if (inboxCount == 0) {
    showInbox();
    return;
  }

  SmsMessage &msg = inbox[inboxSelected];
  display.setCursor(0, 14);
  display.print("From: ");
  display.print(msg.sender);

  for (int i = 0; i < 3; i++) {
    display.setCursor(0, 28 + i * 11);
    display.print(smsLine(msg.text, smsScrollLine + i));
  }

  display.display();
}

void showSmsTo() {
  clearUi();
  drawHeader();
  display.setCursor(0, 16);
  display.print("SMS to:");
  display.setTextSize(2);
  display.setCursor(0, 31);
  String shown = smsTo;

  if (shown.length() > 10)
    shown = shown.substring(shown.length() - 10);

  display.print(shown);
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("# next  * del/back");
  display.display();
}

void showSmsBody() {
  clearUi();
  drawHeader();
  display.setCursor(0, 14);
  display.print("Message:");

  for (int i = 0; i < 3; i++) {
    display.setCursor(0, 28 + i * 11);

    int start = max(0, (int)smsBody.length() - 63) + i * 21;

    display.print(
        smsBody.substring(start, min(start + 21, (int)smsBody.length())));
  }

  display.display();
}

void redrawState() {
  switch (state) {
  case ST_HOME:
    showHome();
    break;
  case ST_MENU:
    showMenu("Menu", MAIN_MENU, MAIN_MENU_COUNT, mainMenuIndex);
    break;
  case ST_DIAL:
    showDial();
    break;
  case ST_CALLING:
    showCallScreen("Calling", dialNumber);
    break;
  case ST_RINGING_OUT:
    showCallScreen("Ringing", dialNumber);
    break;
  case ST_IN_CALL:
    showCallScreen("In call",
                   dialNumber.length() ? dialNumber : incomingNumber);
    break;
  case ST_INCOMING_CALL:
    showCallScreen("Incoming",
                   incomingNumber.length() ? incomingNumber : "Unknown");
    break;
  case ST_MESSAGES:
    showMenu("Messages", MESSAGE_MENU, MESSAGE_MENU_COUNT, messageMenuIndex);
    break;
  case ST_INBOX:
    showInbox();
    break;
  case ST_READ_SMS:
    showReadSms();
    break;
  case ST_SMS_TO:
    showSmsTo();
    break;
  case ST_SMS_BODY:
    showSmsBody();
    break;
  default:
    break;
  }

  // Mirror the same state to the ATmega LCD.
  lcdRenderState();
}
