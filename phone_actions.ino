// ---------------------------------------------------------------------------
// Call / SMS control
// ---------------------------------------------------------------------------
void startCall(const String &number) {
  if (!number.length())
    return;

  dialNumber = number;
  callEndReason = "";
  Serial.print(F("[CALL] Dial "));
  Serial.println(number);
  String r = simCommand("ATD" + number + ";", 1500);
  Serial.println(r);
  enterState(ST_CALLING);
  lastClccPollMs = 0;
  redrawState();
}

void hangUp(const String &reason) {
  Serial.println(F("[CALL] Hang up"));
  simCommand("ATH", 1000);
  callEndReason = reason;
  enterState(ST_CALL_ENDED);
  showCentered("Call", "Ended", callEndReason);
}

void answerCall() {
  Serial.println(F("[CALL] Answer"));
  String r = simCommand("ATA", 1500);
  Serial.println(r);
  dialNumber = "";
  enterState(ST_IN_CALL);
  redrawState();
}

void handleClcc(const String &resp) {
  int idx = resp.indexOf("+CLCC:");

  if (idx < 0) {
    if (state == ST_CALLING || state == ST_RINGING_OUT || state == ST_IN_CALL)
      hangUp(state == ST_IN_CALL ? "Call ended" : "No answer");
    return;
  }

  String tail = resp.substring(idx + 6);
  int c1 = tail.indexOf(',');
  int c2 = tail.indexOf(',', c1 + 1);
  int c3 = tail.indexOf(',', c2 + 1);

  if (c1 < 0 || c2 < 0)
    return;

  int stat = tail.substring(c2 + 1, c3 < 0 ? tail.length() : c3).toInt();

  if (stat == 0 && state != ST_IN_CALL) {
    enterState(ST_IN_CALL);
    redrawState();
  } else if (stat == 2 && state != ST_CALLING) {
    enterState(ST_CALLING);
    redrawState();
  } else if (stat == 3 && state != ST_RINGING_OUT) {
    enterState(ST_RINGING_OUT);
    redrawState();
  }
}

void pushSms(const String &sender, const String &text) {
  if (inboxCount < MAX_SMS) {
    inbox[inboxCount++] = {sender, text};
  } else {
    for (int i = 1; i < MAX_SMS; i++)
      inbox[i - 1] = inbox[i];
    inbox[MAX_SMS - 1] = {sender, text};
  }

  unreadSms = true;
  smsBeep();
}

void readSmsAtIndex(int index) {
  String r = simCommand("AT+CMGR=" + String(index), 2000);
  int header = r.indexOf("+CMGR:");

  if (header < 0)
    return;

  String sender = "";
  int q1 = r.indexOf('"', header);
  int q2 = r.indexOf('"', q1 + 1);
  int q3 = r.indexOf('"', q2 + 1);
  int q4 = r.indexOf('"', q3 + 1);

  if (q3 >= 0 && q4 > q3)
    sender = r.substring(q3 + 1, q4);

  int bodyStart = r.indexOf('\n', header);
  int ok = r.indexOf("\nOK", bodyStart);
  String body = "";

  if (bodyStart >= 0) {
    body = ok > bodyStart ? r.substring(bodyStart + 1, ok)
                          : r.substring(bodyStart + 1);
    body = cleanPrintable(body);
  }

  pushSms(sender, body.length() ? body : "(empty)");
}

bool sendSms(const String &number, const String &text) {
  while (SIM.available())
    SIM.read();

  SIM.print("AT+CMGS=\"");
  SIM.print(number);
  SIM.print("\"\r\n");

  String prompt = simReadFor(2500);

  if (prompt.indexOf('>') < 0)
    return false;

  SIM.print(text);
  SIM.write(26);
  String result = simReadFor(10000);
  Serial.println(result);

  return result.indexOf("OK") >= 0 || result.indexOf("+CMGS:") >= 0;
}

void parseIncomingNumber(const String &chunk) {
  int clip = chunk.indexOf("+CLIP:");

  if (clip < 0)
    return;

  int q1 = chunk.indexOf('"', clip);

  int q2 = chunk.indexOf('"', q1 + 1);

  if (q1 >= 0 && q2 > q1)
    incomingNumber = chunk.substring(q1 + 1, q2);
}

void handleModemChunk(const String &chunk) {
  if (!chunk.length())
    return;

  if (chunk.indexOf("RING") >= 0) {
    parseIncomingNumber(chunk);
    enterState(ST_INCOMING_CALL);
    redrawState();
    ringBeep();
  }

  int cmti = chunk.indexOf("+CMTI:");
  if (cmti >= 0) {
    int comma = chunk.indexOf(',', cmti);
    if (comma >= 0) {
      int index = chunk.substring(comma + 1).toInt();
      if (index > 0)
        readSmsAtIndex(index);
    }
    if (state == ST_HOME)
      redrawState();
  }

  if (chunk.indexOf("+CLCC:") >= 0)
    handleClcc(chunk);

  String reason = "";
  if (chunk.indexOf("BUSY") >= 0)
    reason = "Busy";
  else if (chunk.indexOf("NO ANSWER") >= 0)
    reason = "No answer";
  else if (chunk.indexOf("NO CARRIER") >= 0)
    reason = state == ST_IN_CALL ? "Call ended" : "Rejected";
  else if (chunk.indexOf("ERROR") >= 0 &&
           (state == ST_CALLING || state == ST_RINGING_OUT))
    reason = "Failed";

  if (reason.length())
    hangUp(reason);
}
