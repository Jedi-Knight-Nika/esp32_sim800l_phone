// ---------------------------------------------------------------------------
// SMS text entry
// ---------------------------------------------------------------------------
const char *lettersForKey(char key) {
  switch (key) {
  case '2':
    return "ABC2";
  case '3':
    return "DEF3";
  case '4':
    return "GHI4";
  case '5':
    return "JKL5";
  case '6':
    return "MNO6";
  case '7':
    return "PQRS7";
  case '8':
    return "TUV8";
  case '9':
    return "WXYZ9";
  case '0':
    return " 0";
  case '1':
    return ".,?1";
  default:
    return "";
  }
}

void resetTextCycle() {
  lastTextKey = 0;
  lastTextIndex = 0;
  lastTextPressMs = 0;
}

void addTextKey(char key) {
  const char *letters = lettersForKey(key);
  int count = strlen(letters);

  if (count == 0)
    return;

  unsigned long now = millis();
  if (smsBody.length() && key == lastTextKey &&
      now - lastTextPressMs < TEXT_CYCLE_MS) {
    lastTextIndex = (lastTextIndex + 1) % count;
    smsBody.setCharAt(smsBody.length() - 1, letters[lastTextIndex]);
  } else {
    lastTextKey = key;
    lastTextIndex = 0;
    smsBody += letters[0];
  }

  lastTextPressMs = now;
}

void maybeFinishTextCycle() {
  if (lastTextKey && millis() - lastTextPressMs > TEXT_CYCLE_MS)
    resetTextCycle();
}
