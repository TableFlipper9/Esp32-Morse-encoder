#include <WiFi.h>
#include <WebServer.h>

// laser pin
const int txPin = 23;

// timings 
const int DOT = 100;
const int DASH = 300;
const int GAP = 100;
const int LETTER_GAP = 300;
const int WORD_GAP = 700;

const int LEAD_IN_OFF = 400;
const int REPEAT_PAUSE = 1000;

struct Morse { char c; const char *code; };

Morse table[] = {
  {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
  {'E', "."},    {'F', "..-."}, {'G', "--."},  {'H', "...."},
  {'I', ".."},   {'J', ".---"}, {'K', "-.-"},  {'L', ".-.."},
  {'M', "--"},   {'N', "-."},   {'O', "---"},  {'P', ".--."},
  {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
  {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"},
  {'Y', "-.--"}, {'Z', "--.."},
  {'0', "-----"},{'1', ".----"},{'2', "..---"},{'3', "...--"},
  {'4', "....-"},{'5', "....."},{'6', "-...."},{'7', "--..."},
  {'8', "---.."},{'9', "----."}
};

const char* lookup(char c) {
  for (auto &m : table) if (m.c == c) return m.code;
  return nullptr;
}

static inline void laserOn()  { digitalWrite(txPin, HIGH); }
static inline void laserOff() { digitalWrite(txPin, LOW);  }

// class for morse non-bloccking sender
class MorseSender {
public:
  void begin() {
    laserOff();
    state = State::IDLE;
    nextChangeMs = 0;
    idxChar = 0;
    idxSym = 0;
    curCode = nullptr;
  }

  void setMessage(const String& m) {
    msg = m;
  // restart here
    idxChar = 0;
    idxSym = 0;
    curCode = nullptr;
    state = State::LEADIN;
    nextChangeMs = millis() + LEAD_IN_OFF;
    laserOff();
  }

  void stopNow() {
    laserOff();
    state = State::IDLE;
    nextChangeMs = 0;
    idxChar = 0;
    idxSym = 0;
    curCode = nullptr;
  }

  void setEnabled(bool en) {
    enabled = en;
    if (!enabled) stopNow();
    else if (state == State::IDLE && msg.length() > 0) {
      state = State::LEADIN;
      nextChangeMs = millis() + LEAD_IN_OFF;
      idxChar = 0;
      idxSym = 0;
      curCode = nullptr;
      laserOff();
    }
  }

  bool isEnabled() const { return enabled; }
  String getMessage() const { return msg; }

  void update() {
    if (!enabled) return;

    unsigned long now = millis();
    if (state != State::IDLE && now < nextChangeMs) return;

    switch (state) {
      case State::IDLE:
        state = State::LEADIN;
        nextChangeMs = now + LEAD_IN_OFF;
        laserOff();
        break;

      case State::LEADIN:
        if (msg.length() == 0) {
          state = State::REPEAT_PAUSE_STATE;
          nextChangeMs = now + REPEAT_PAUSE;
          break;
        }
        idxChar = 0;
        idxSym = 0;
        curCode = nullptr;
        state = State::NEXT_CHAR;
        nextChangeMs = now;
        break;

      case State::NEXT_CHAR: {
        if (idxChar >= msg.length()) {
          // end of message
          laserOff();
          state = State::REPEAT_PAUSE_STATE;
          nextChangeMs = now + REPEAT_PAUSE;
          break;
        }

        char c = msg[idxChar];

        // ignore newline etc
        if (c == '\r' || c == '\n' || c == '\t') {
          idxChar++;
          nextChangeMs = now;
          break;
        }

        if (c == ' ') {
          // If there are multiple spaces
          laserOff();
          idxChar++;
          state = State::GAP_OFF;
          nextChangeMs = now + WORD_GAP;
          break;
        }

        c = toupper((unsigned char)c);
        curCode = lookup(c);
        idxSym = 0;

        // Unknown char: skip with a small letter gap so receiver isn't confused
        if (!curCode) {
          idxChar++;
          laserOff();
          state = State::GAP_OFF;
          nextChangeMs = now + LETTER_GAP;
          break;
        }

        // go send first symbol
        state = State::SYMBOL_MARK_ON;
        nextChangeMs = now;
        break;
      }

      case State::SYMBOL_MARK_ON: {
        // start mark (laser ON) for current symbol
        char sym = curCode[idxSym];
        if (sym == '\0') {
          // no symbols? treat as letter done
          finishLetter(now);
          break;
        }

        laserOn();
        int dur = (sym == '.') ? DOT : DASH;
        state = State::SYMBOL_MARK_OFF;
        nextChangeMs = now + dur;
        break;
      }

      case State::SYMBOL_MARK_OFF: {
        // end mark (laser OFF)
        laserOff();
        state = State::INTRA_GAP_OFF;
        nextChangeMs = now + GAP;
        break;
      }

      case State::INTRA_GAP_OFF: {
        // move to next symbol or finish letter
        idxSym++;
        if (curCode[idxSym] != '\0') {
          state = State::SYMBOL_MARK_ON;
          nextChangeMs = now;
        } else {
          finishLetter(now);
        }
        break;
      }

      case State::GAP_OFF:
        // finished some OFF gap; proceed to next char
        state = State::NEXT_CHAR;
        nextChangeMs = now;
        break;

      case State::REPEAT_PAUSE_STATE:
        // restart
        state = State::LEADIN;
        nextChangeMs = now + LEAD_IN_OFF;
        break;
    }
  }

private:
  enum class State {
    IDLE,
    LEADIN,
    NEXT_CHAR,
    SYMBOL_MARK_ON,
    SYMBOL_MARK_OFF,
    INTRA_GAP_OFF,
    GAP_OFF,
    REPEAT_PAUSE_STATE
  };

  void finishLetter(unsigned long now) {
    laserOff();
    idxChar++;

    // lookahead for spaces
    if (idxChar < msg.length() && msg[idxChar] == ' ') {
      while (idxChar < msg.length() && msg[idxChar] == ' ') idxChar++;
      state = State::GAP_OFF;
      nextChangeMs = now + (unsigned long)max(0, WORD_GAP - GAP);
    } else {
      state = State::GAP_OFF;
      nextChangeMs = now + (unsigned long)max(0, LETTER_GAP - GAP);
    }
  }

  bool enabled = true;
  String msg = "HELLO WORLD 123";

  State state = State::IDLE;
  unsigned long nextChangeMs = 0;

  size_t idxChar = 0;
  size_t idxSym = 0;
  const char* curCode = nullptr;
};

MorseSender sender;

// wifi
WebServer server(80);

const char* AP_SSID = "ESP32-Morse";
const char* AP_PASS = "morse1234";

String currentMsg = "HELLO WORLD 123";
volatile bool txEnabled = true;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Morse Laser</title>
  <style>
    body{font-family:system-ui,Arial;margin:20px;max-width:720px}
    input,button{font-size:18px;padding:10px}
    input{width:100%;box-sizing:border-box;margin:8px 0}
    button{width:100%;margin-top:8px}
    .row{display:flex;gap:10px}
    .row button{width:50%}
    code{background:#f2f2f2;padding:2px 6px;border-radius:6px}
    #status{margin-top:10px;white-space:pre-wrap}
  </style>
</head>
<body>
  <h2>ESP32 Morse Laser TX</h2>
  <p>Type a message (A-Z, 0-9, spaces). Press <b>Send</b> to repeat continuously.</p>

  <input id="msg" maxlength="120" placeholder="HELLO WORLD 123">

  <div class="row">
    <button onclick="sendMsg()">Send</button>
    <button onclick="stopTx()">Stop</button>
  </div>

  <div id="status"></div>

<script>
async function sendMsg(){
  const m = document.getElementById('msg').value;
  const r = await fetch('/set?msg=' + encodeURIComponent(m));
  document.getElementById('status').textContent = await r.text();
}
async function stopTx(){
  const r = await fetch('/stop');
  document.getElementById('status').textContent = await r.text();
}
(async () => {
  const r = await fetch('/status');
  document.getElementById('status').textContent = await r.text();
})();
</script>

</body>
</html>
)HTML";

void handleRoot() {
  server.send(200, "text/html", FPSTR(INDEX_HTML));
}

void handleSet() {
  if (!server.hasArg("msg")) {
    server.send(400, "text/plain", "Missing msg parameter.");
    return;
  }

  String m = server.arg("msg");
  m.trim();
  if (m.length() == 0) {
    server.send(400, "text/plain", "Message is empty.");
    return;
  }
  if (m.length() > 120) m = m.substring(0, 120);

  currentMsg = m;
  txEnabled = true;

  sender.setMessage(currentMsg);
  sender.setEnabled(true);

  server.send(200, "text/plain", String("OK\nRepeating: ") + currentMsg + "\n");
}

void handleStop() {
  txEnabled = false;
  sender.setEnabled(false);
  server.send(200, "text/plain", "Stopped (immediate).");
}

void handleStatus() {
  String s;
  s += String("txEnabled=") + (sender.isEnabled() ? "1" : "0") + "\n";
  s += "msg=" + sender.getMessage() + "\n";
  server.send(200, "text/plain", s);
}

void setup() {
  pinMode(txPin, OUTPUT);
  laserOff();

  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("ESP32 Morse Laser TX (non-blocking)");
  Serial.print("AP SSID: "); Serial.println(AP_SSID);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/stop", HTTP_GET, handleStop);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();

  sender.begin();
  sender.setMessage(currentMsg);
  sender.setEnabled(true);

  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
  sender.update(); // non-blocki
}
