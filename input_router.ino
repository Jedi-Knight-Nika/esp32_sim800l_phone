// ---------------------------------------------------------------------------
// Input routing
// ---------------------------------------------------------------------------
void moveMenu(int &index, int count, int delta) {
  index += delta;

  if (index < 0)
    index = count - 1;
  if (index >= count)
    index = 0;
}

void handleHomeKey(char key) {
  if (key == '#') {
    mainMenuIndex = 0;
    enterState(ST_MENU);
  } else if (key == '*') {
    enterState(ST_INBOX);
  } else if (isdigit(key) || key == '+') {
    dialNumber = String(key);
    enterState(ST_DIAL);
  }

  redrawState();
}

void handleMenuKey(char key) {
  if (key == '2')
    moveMenu(mainMenuIndex, MAIN_MENU_COUNT, -1);
  else if (key == '8')
    moveMenu(mainMenuIndex, MAIN_MENU_COUNT, 1);
  else if (key == '*')
    enterState(ST_HOME);
  else if (key == '#') {
    if (mainMenuIndex == 0) {
      dialNumber = "";

      enterState(ST_DIAL);
    } else {
      messageMenuIndex = 0;

      enterState(ST_MESSAGES);
    }
  }

  redrawState();
}

void handleMessagesKey(char key) {
  if (key == '2')
    moveMenu(messageMenuIndex, MESSAGE_MENU_COUNT, -1);
  else if (key == '8')
    moveMenu(messageMenuIndex, MESSAGE_MENU_COUNT, 1);
  else if (key == '*')
    enterState(ST_MENU);
  else if (key == '#') {
    if (messageMenuIndex == 0) {
      inboxSelected = 0;
      enterState(ST_INBOX);
    } else {
      smsTo = "";
      smsBody = "";
      resetTextCycle();
      enterState(ST_SMS_TO);
    }
  }

  redrawState();
}

void handleDialKey(char key) {
  if (isdigit(key) || key == '+' || key == '#') {
    if (key == '#')
      startCall(dialNumber);
    else
      dialNumber += key;
  } else if (key == '*') {
    if (dialNumber.length())
      dialNumber.remove(dialNumber.length() - 1);
    else
      enterState(ST_HOME);
  }

  if (state == ST_DIAL)
    redrawState();
}

void handleInboxKey(char key) {
  if (key == '*') {
    enterState(ST_MESSAGES);
  } else if (inboxCount > 0 && key == '2') {
    moveMenu(inboxSelected, inboxCount, -1);
  } else if (inboxCount > 0 && key == '8') {
    moveMenu(inboxSelected, inboxCount, 1);
  } else if (inboxCount > 0 && key == '#') {
    unreadSms = false;
    smsScrollLine = 0;
    enterState(ST_READ_SMS);
  }

  redrawState();
}

void handleReadSmsKey(char key) {
  if (key == '*') {
    enterState(ST_INBOX);
  } else if (key == '2') {
    if (smsScrollLine > 0)
      smsScrollLine--;
  } else if (key == '8') {
    int maxScroll = max(0, smsLineCount(inbox[inboxSelected].text) - 3);

    if (smsScrollLine < maxScroll)
      smsScrollLine++;
  } else if (key == '4' && inboxCount > 0) {
    moveMenu(inboxSelected, inboxCount, -1);
    smsScrollLine = 0;
  } else if (key == '6' && inboxCount > 0) {
    moveMenu(inboxSelected, inboxCount, 1);
    smsScrollLine = 0;
  }

  redrawState();
}

void handleSmsToKey(char key) {
  if (isdigit(key) || key == '+') {
    smsTo += key;
  } else if (key == '#') {
    if (smsTo.length()) {
      resetTextCycle();
      enterState(ST_SMS_BODY);
    }
  } else if (key == '*') {
    if (smsTo.length())
      smsTo.remove(smsTo.length() - 1);
    else
      enterState(ST_MESSAGES);
  }

  redrawState();
}

void handleSmsBodyKey(char key) {
  if (isdigit(key)) {
    addTextKey(key);
  } else if (key == '*') {
    resetTextCycle();

    if (smsBody.length())
      smsBody.remove(smsBody.length() - 1);
    else
      enterState(ST_SMS_TO);
  } else if (key == '#') {
    resetTextCycle();

    enterState(ST_SENDING_SMS);

    showCentered("SMS", "SEND", smsTo);

    bool ok = sendSms(smsTo, smsBody);

    showCentered("SMS", ok ? "SENT" : "FAIL",
                 ok ? "Delivered to modem" : "Try again");

    delay(1600);
    smsTo = "";
    smsBody = "";
    enterState(ST_HOME);
  }
  if (state == ST_SMS_BODY)
    redrawState();
}

void handleKey(char key) {
  if (!key || state == ST_OFF || state == ST_BOOTING ||
      state == ST_POWERING_OFF)
    return;

  keyBeep();

  switch (state) {
  case ST_HOME:
    handleHomeKey(key);
    break;
  case ST_MENU:
    handleMenuKey(key);
    break;
  case ST_DIAL:
    handleDialKey(key);
    break;
  case ST_CALLING:
  case ST_RINGING_OUT:
  case ST_IN_CALL:
    if (key == '*')
      hangUp("Hung up");
    break;
  case ST_INCOMING_CALL:
    if (key == '#')
      answerCall();
    else if (key == '*')
      hangUp("Rejected");
    break;
  case ST_MESSAGES:
    handleMessagesKey(key);
    break;
  case ST_INBOX:
    handleInboxKey(key);
    break;
  case ST_READ_SMS:
    handleReadSmsKey(key);
    break;
  case ST_SMS_TO:
    handleSmsToKey(key);
    break;
  case ST_SMS_BODY:
    handleSmsBodyKey(key);
    break;
  default:
    break;
  }
}

void pollSerialDebug() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == 'd' || c == 'D') {
      Serial.println(F("----- diagnostics -----"));
      Serial.println(simCommand("AT+CPIN?", 1200));
      Serial.println(simCommand("AT+CREG?", 1200));
      Serial.println(simCommand("AT+CSQ", 1200));
      Serial.println(simCommand("AT+COPS?", 1200));
      Serial.println(F("-----------------------"));
    } else if (c == 'p' || c == 'P') {
      if (state == ST_OFF)
        bootPhone();
      else
        shutdownPhone();
    }
  }
}
