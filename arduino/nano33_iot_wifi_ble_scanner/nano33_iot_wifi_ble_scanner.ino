/*
 * Nano 33 IoT: Wi-Fi scan + BLE scan + pot/button UI + BLE central to ESP32-C3 (NUS).
 * FSM: HOME, WIFI_SCAN, WIFI_LIST, BLE_SCAN, BLE_LIST, BLE_CONNECTED, ERROR.
 * Helpers: potStableIndex(), pollButton(), bleConnectAt(), UI lists.
 * See README.md for wiring, UUIDs, and libraries.
 */

#include <Arduino.h>
#include <string.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <ArduinoBLE.h>
#if __has_include(<FlashStorage.h>)
#include <FlashStorage.h>
#define HAVE_FLASHSTORAGE 1
#else
#define HAVE_FLASHSTORAGE 0
#endif

// ---------------------------------------------------------------------------
#define ENABLE_LCD_I2C 1
#if ENABLE_LCD_I2C
#include <LiquidCrystal_I2C.h>
// Most PCF8574 backpacks are 0x27; some are 0x3F.
static LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif

// ---------------------------------------------------------------------------
static const int PIN_POT = A0;
static const int PIN_BTN = 2;

/* Must match ESP32 default / NVS device_name (see config_store DEFAULT_NAME, main.c advertise). */
static const char SB1_BLE_FILTER_NAME[] = "SB1 MIDI INTERFACE";
static const char NUS_SVC_UUID[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char NUS_RX_UUID[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char NUS_TX_UUID[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

static const uint32_t BLE_SCAN_MS = 9000;
static const uint32_t BLE_RESCAN_MS = 12000;
static const uint32_t BTN_DEBOUNCE_MS = 35;
static const uint32_t LONG_PRESS_MS = 650;
static const uint32_t HOLD_BT_RESET_MS = 3000;
static const uint8_t WIFI_RESET_CONFIRM_HOLD_S = 10;
static const uint8_t WIFI_CAP = 24;
static const uint8_t BLE_CAP = 24;

#define BTN_NONE 0
#define BTN_SHORT 1
#define BTN_LONG 2
#define BTN_HOLD_BT_RESET 3

#if HAVE_FLASHSTORAGE
FlashStorage(flash_saved_ble_addr, char[18]);
#endif

enum AppState : uint8_t {
  ST_HOME = 0,
  ST_WIFI_SCAN,
  ST_WIFI_LIST,
  ST_WIFI_PASS_ENTRY,
  ST_WIFI_PASS_MENU,
  ST_WIFI_TEST,
  ST_BLE_SCAN,
  ST_BLE_LIST,
  ST_BLE_CONNECTED,
  ST_ERROR,
};

static AppState state = ST_HOME;
static char errMsg[48];

static const uint8_t HOME_ITEMS = 2;

static int wifiCount = 0;
static int wifiRssi[WIFI_CAP];
static char wifiSsid[WIFI_CAP][33];
static uint8_t wifiEnc[WIFI_CAP];
static int wifiSelectedIdx = -1;
static const char WIFI_PASS_CHARS[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"#$%&'()*+,-./:;<=>?@[]^_`{|}~";
static char wifiPass[65];
static uint8_t wifiPassLen = 0;
static uint8_t wifiPassCharIdx = 0;
static uint8_t wifiPassMenuIdx = 0;
static uint32_t wifiTestStartMs = 0;
static uint32_t wifiAnimLastMs = 0;
static bool wifiTestDone = false;
static bool wifiTestSuccess = false;
static bool wifiTestPrinted = false;
static char wifiIpStr[16];
static uint8_t wifiAnimFrame = 0;

static int bleCount = 0;
static char bleAddr[BLE_CAP][18];
static char bleName[BLE_CAP][24];
static int bleRssi[BLE_CAP];
static uint32_t bleScanStartMs = 0;
static bool bleScanPhaseStarted = false;

static BLEDevice blePeer;
static BLECharacteristic nusRx;
static BLECharacteristic nusTx;
static bool blePeerValid = false;
static bool nusReady = false;
static uint32_t lastNotifyLog = 0;
static bool wifiResetConfirmActive = false;
static uint32_t wifiResetHoldStartMs = 0;
static int8_t wifiResetShownN = -1;

static void resetWifiStack();

static const char* encTypeStr(uint8_t t) {
  switch (t) {
    case ENC_TYPE_NONE:
      return "OPEN";
    case ENC_TYPE_WEP:
      return "WEP";
    case ENC_TYPE_TKIP:
      return "WPA";
    case ENC_TYPE_CCMP:
      return "WPA2";
    default:
      return "?";
  }
}

static void startWifiPassEntry(int idx) {
  wifiSelectedIdx = idx;
  memset(wifiPass, 0, sizeof(wifiPass));
  wifiPassLen = 0;
  wifiPassCharIdx = 0;
  wifiPassMenuIdx = 0;
  state = ST_WIFI_PASS_ENTRY;
  Serial.print("WiFi password entry for SSID: ");
  Serial.println(wifiSsid[idx]);
}

static void startWifiTest() {
  if (wifiSelectedIdx < 0 || wifiSelectedIdx >= wifiCount) {
    setError("No WiFi SSID selected");
    return;
  }
  wifiTestStartMs = millis();
  wifiAnimLastMs = 0;
  wifiAnimFrame = 0;
  wifiTestDone = false;
  wifiTestSuccess = false;
  wifiTestPrinted = false;
  strncpy(wifiIpStr, "0.0.0.0", sizeof(wifiIpStr) - 1);
  wifiIpStr[sizeof(wifiIpStr) - 1] = '\0';
  WiFi.disconnect();
  delay(60);
  WiFi.begin(wifiSsid[wifiSelectedIdx], wifiPass);
  state = ST_WIFI_TEST;
  Serial.print("WiFi test started for SSID: ");
  Serial.println(wifiSsid[wifiSelectedIdx]);
}

static void wifiTestPoll() {
  if (wifiTestDone) return;
  uint8_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    snprintf(wifiIpStr, sizeof(wifiIpStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    wifiTestDone = true;
    wifiTestSuccess = true;
    return;
  }
  if ((uint32_t)(millis() - wifiTestStartMs) > 18000) {
    wifiTestDone = true;
    wifiTestSuccess = false;
  }
}

static void renderWifiPassEntry(bool force) {
  static uint8_t lastCharIdx = 255;
  static uint8_t lastLen = 255;
  if (!force && lastCharIdx == wifiPassCharIdx && lastLen == wifiPassLen) return;
  lastCharIdx = wifiPassCharIdx;
  lastLen = wifiPassLen;

  char l1[17];
  char l2[17];
  strncpy(l1, wifiSsid[wifiSelectedIdx], 16);
  l1[16] = '\0';
  char c = WIFI_PASS_CHARS[wifiPassCharIdx];
  snprintf(l2, sizeof(l2), "L%02u [%c] short=add", (unsigned)wifiPassLen, c);
  Serial.print("Pass len=");
  Serial.print(wifiPassLen);
  Serial.print(" current=");
  Serial.println(c);
#if ENABLE_LCD_I2C
  lcdClearPrint2(l1, l2);
#else
  (void)l1;
  (void)l2;
#endif
}

static void renderWifiPassMenu(bool force) {
  static uint8_t last = 255;
  if (!force && last == wifiPassMenuIdx) return;
  last = wifiPassMenuIdx;
  const char* opts[3] = {"DONE", "DEL", "CANCEL"};
  char l2[17];
  snprintf(l2, sizeof(l2), "Action: %s", opts[wifiPassMenuIdx]);
  Serial.println("--- Pass Menu ---");
  Serial.println(l2);
#if ENABLE_LCD_I2C
  lcdClearPrint2("Pass Menu", l2);
#else
  (void)l2;
#endif
}

static void renderWifiTest(bool force) {
  uint32_t now = millis();
  if (!force && (now - wifiAnimLastMs) < 140) return;
  wifiAnimLastMs = now;

  if (!wifiTestDone) {
    // "hacker" animation with moving hex and frame marker.
    static const char* glyph = "0123456789ABCDEF";
    char l2[17];
    for (int i = 0; i < 16; i++) {
      l2[i] = glyph[(wifiAnimFrame + i * 3) & 0x0F];
    }
    l2[16] = '\0';
    wifiAnimFrame++;
    if (wifiSelectedIdx >= 0) {
      Serial.print("HACK ");
      Serial.print(wifiSsid[wifiSelectedIdx]);
      Serial.print(" ... status=");
      Serial.println((int)WiFi.status());
    }
#if ENABLE_LCD_I2C
    lcdClearPrint2("HACKING WIFI...", l2);
#else
    (void)l2;
#endif
    return;
  }

  if (!wifiTestPrinted) {
    wifiTestPrinted = true;
    if (wifiTestSuccess) {
      Serial.print("WiFi connected, IP=");
      Serial.println(wifiIpStr);
    } else {
      Serial.println("WiFi connect failed (timeout)");
    }
  }

  if (wifiTestSuccess) {
#if ENABLE_LCD_I2C
    lcdClearPrint2("NET LINK OK", wifiIpStr);
#endif
  } else {
#if ENABLE_LCD_I2C
    lcdClearPrint2("NET LINK FAIL", "S=Retry L=Home");
#endif
  }
}

static uint8_t potStableIndex(uint8_t count) {
  static uint8_t lastIdx = 0;
  static uint8_t s0 = 0, s1 = 0, s2 = 0;
  if (count <= 1) return 0;
  int raw = analogRead(PIN_POT);
  uint8_t idx = (uint32_t)raw * (uint32_t)count / 1024u;
  if (idx >= count) idx = count - 1;
  s0 = s1;
  s1 = s2;
  s2 = idx;
  if (s0 == s1 && s1 == s2) lastIdx = s2;
  return lastIdx;
}

static uint8_t pollButton() {
  static int lastStable = HIGH;
  static uint32_t lastEdgeMs = 0;
  static uint32_t downAt = 0;
  static bool btResetSent = false;
  static bool suppressRelease = false;
  int raw = digitalRead(PIN_BTN);
  uint32_t now = millis();
  if (raw == lastStable) {
    if (raw == LOW) {
      uint32_t dur = now - downAt;
      if (!btResetSent && dur >= HOLD_BT_RESET_MS) {
        btResetSent = true;
        suppressRelease = true;
        return BTN_HOLD_BT_RESET;
      }
    }
    return BTN_NONE;
  }
  if (now - lastEdgeMs < BTN_DEBOUNCE_MS) {
    return BTN_NONE;
  }
  lastEdgeMs = now;
  lastStable = raw;
  if (raw == LOW) {
    downAt = now;
    btResetSent = false;
    suppressRelease = false;
    return BTN_NONE;
  }
  if (suppressRelease) {
    suppressRelease = false;
    btResetSent = false;
    return BTN_NONE;
  }
  uint32_t dur = now - downAt;
  if (dur >= LONG_PRESS_MS) {
    return BTN_LONG;
  }
  return BTN_SHORT;
}

#if ENABLE_LCD_I2C
static void lcdClearPrint2(const char* l1, const char* l2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
}
#else
#define lcdClearPrint2(a, b) ((void)0)
#endif

static void uiLine(const char* title, const char* sub) {
  Serial.println(title);
  if (sub) Serial.println(sub);
#if ENABLE_LCD_I2C
  lcdClearPrint2(title, sub ? sub : "");
#endif
}

static void setError(const char* msg) {
  strncpy(errMsg, msg, sizeof(errMsg) - 1);
  errMsg[sizeof(errMsg) - 1] = '\0';
  state = ST_ERROR;
}

static void saveBleAddressToFlash(const char* addr) {
#if HAVE_FLASHSTORAGE
  char buf[18];
  strncpy(buf, addr, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  flash_saved_ble_addr.write(buf);
#else
  (void)addr;
#endif
}

static bool bleMatchesFilter(const BLEDevice& p) {
  String name = p.localName();
  if (name.indexOf(SB1_BLE_FILTER_NAME) >= 0) return true;
  if (name.indexOf("SB1") >= 0) return true;
  int nu = p.advertisedServiceUuidCount();
  for (int k = 0; k < nu; k++) {
    String u = p.advertisedServiceUuid(k);
    u.toLowerCase();
    if (u.indexOf("6e400001") >= 0) return true;
  }
  return false;
}

static void runWifiScanBlocking() {
  wifiCount = 0;
  uiLine("WiFi scanning...", nullptr);
  int n = WiFi.scanNetworks();
  if (n < 0) {
    setError("WiFi scan failed");
    return;
  }
  if (n == 0) {
    wifiCount = 0;
    state = ST_WIFI_LIST;
    uiLine("WiFi: none", "Short tap=home");
    return;
  }
  int take = n > WIFI_CAP ? WIFI_CAP : n;
  wifiCount = take;
  for (int i = 0; i < take; i++) {
    wifiRssi[i] = WiFi.RSSI(i);
    wifiEnc[i] = (uint8_t)WiFi.encryptionType(i);
    String ssid = WiFi.SSID(i);
    strncpy(wifiSsid[i], ssid.c_str(), sizeof(wifiSsid[i]) - 1);
    wifiSsid[i][sizeof(wifiSsid[i]) - 1] = '\0';
  }
  state = ST_WIFI_LIST;
}

static bool bleSeen(const char* addr) {
  for (int i = 0; i < bleCount; i++) {
    if (strncmp(bleAddr[i], addr, sizeof(bleAddr[0])) == 0) return true;
  }
  return false;
}

static void bleAdd(BLEDevice& p) {
  if (bleCount >= BLE_CAP) return;
  String a = p.address();
  a.toCharArray(bleAddr[bleCount], sizeof(bleAddr[0]));
  String n = p.localName();
  if (n.length() == 0) {
    n = "(no name)";
  }
  n.toCharArray(bleName[bleCount], sizeof(bleName[0]));
  bleRssi[bleCount] = p.rssi();
  bleCount++;
}

static void bleScanBegin() {
  bleCount = 0;
  BLE.stopScan();
  delay(40);
  uiLine("BLE scanning...", SB1_BLE_FILTER_NAME);
  if (!BLE.scan()) {
    setError("BLE.scan failed");
    return;
  }
  bleScanStartMs = millis();
  bleScanPhaseStarted = true;
}

static void bleScanPoll() {
  BLEDevice p = BLE.available();
  if (p) {
    String ad = p.address();
    if (ad.length() > 0 && bleMatchesFilter(p) && !bleSeen(ad.c_str())) {
      bleAdd(p);
    }
  }
  if ((uint32_t)(millis() - bleScanStartMs) >= BLE_SCAN_MS) {
    BLE.stopScan();
    delay(50);
    state = ST_BLE_LIST;
    bleScanPhaseStarted = false;
  }
}

static bool findBleDeviceByIndex(int idx, BLEDevice& out) {
  BLE.stopScan();
  delay(30);
  if (!BLE.scan()) return false;
  uint32_t t0 = millis();
  const char* want = bleAddr[idx];
  while ((uint32_t)(millis() - t0) < BLE_RESCAN_MS) {
    BLE.poll();
    BLEDevice p = BLE.available();
    if (p) {
      if (p.address() == want) {
        out = p;
        BLE.stopScan();
        delay(30);
        return true;
      }
    }
    delay(1);
  }
  BLE.stopScan();
  return false;
}

static bool bleConnectAt(int idx) {
  BLEDevice p;
  if (!findBleDeviceByIndex(idx, p)) {
    setError("Device not found");
    return false;
  }
  if (!p.connect()) {
    setError("BLE connect failed");
    return false;
  }
  if (!p.discoverAttributes()) {
    p.disconnect();
    setError("GATT discover fail");
    return false;
  }
  BLEService svc = p.service(NUS_SVC_UUID);
  if (!svc) {
    p.disconnect();
    setError("NUS service missing");
    return false;
  }
  nusRx = svc.characteristic(NUS_RX_UUID);
  nusTx = svc.characteristic(NUS_TX_UUID);
  if (!nusRx || !nusTx) {
    p.disconnect();
    setError("NUS chars missing");
    return false;
  }
  if (!nusTx.subscribe()) {
    p.disconnect();
    setError("TX subscribe failed");
    return false;
  }
  blePeer = p;
  blePeerValid = true;
  nusReady = true;

  saveBleAddressToFlash(bleAddr[idx]);

  const uint8_t testPayload[] = {'M', 'B', 0x01};
  if (nusRx.writeValue(testPayload, sizeof(testPayload))) {
    Serial.println("Wrote test payload to NUS RX");
  } else {
    Serial.println("NUS RX write failed");
  }
  state = ST_BLE_CONNECTED;
  uiLine("BLE connected", bleName[idx]);
  return true;
}

static void bleDisconnect() {
  nusReady = false;
  if (blePeerValid) {
    blePeer.disconnect();
    blePeerValid = false;
  }
  BLE.stopScan();
  delay(40);
  bleCount = 0;
}

static void startWifiResetConfirm() {
  wifiResetConfirmActive = true;
  wifiResetHoldStartMs = 0;
  wifiResetShownN = -1;
  Serial.println("Reset Wifi?");
  Serial.println("Hold for Ns");
  Serial.println("to confirm");
}

static void renderWifiResetConfirmCountdown(int n) {
  if (n < 0) n = 0;
  if (n > 99) n = 99;
  char l2[17];
  snprintf(l2, sizeof(l2), "Hold for %2ds", n);
  uiLine("Reset Wifi?", l2);
}

static void pollWifiResetConfirm() {
  if (!wifiResetConfirmActive) return;

  bool down = (digitalRead(PIN_BTN) == LOW);
  if (!down) {
    wifiResetConfirmActive = false;
    wifiResetHoldStartMs = 0;
    wifiResetShownN = -1;
    state = ST_HOME;
    uiLine("Home", "WiFi / BLE scan");
    return;
  }

  uint32_t now = millis();
  if (wifiResetHoldStartMs == 0) {
    wifiResetHoldStartMs = now;
  }
  uint32_t heldMs = now - wifiResetHoldStartMs;
  int remain = (int)WIFI_RESET_CONFIRM_HOLD_S - (int)(heldMs / 1000u);
  if (remain < 0) remain = 0;

  if (wifiResetShownN != remain) {
    wifiResetShownN = (int8_t)remain;
    renderWifiResetConfirmCountdown(remain);
  }

  if (remain == 0) {
    wifiResetConfirmActive = false;
    wifiResetHoldStartMs = 0;
    wifiResetShownN = -1;
    resetWifiStack();
    uiLine("Home", "WiFi / BLE scan");
  }
}

static void resetBluetoothStack() {
  uiLine("MIDI RESET", "please wait...");
  delay(2000);
  bleDisconnect();
  BLE.end();
  delay(120);
  if (!BLE.begin()) {
    uiLine("MIDI FAIL", "BLE.begin failed");
    setError("BLE reset failed");
    return;
  }
  BLE.setLocalName("SB1ControllerUI");
  BLE.central();
  bleScanPhaseStarted = false;
  blePeerValid = false;
  nusReady = false;
  state = ST_HOME;
  uiLine("MIDI RESET DONE", "Release for Home");
  delay(2000);
  startWifiResetConfirm();
  renderWifiResetConfirmCountdown(WIFI_RESET_CONFIRM_HOLD_S);
}

static void resetWifiStack() {
  uiLine("WIFI RESETTING", "disconnecting...");
  WiFi.disconnect();
  delay(120);
  uiLine("WIFI RESETTING", "scan module...");
  int n = WiFi.scanNetworks();
  if (n < 0) {
    uiLine("WIFI RESET FAIL", "scan failed");
  } else {
    uiLine("WIFI RESET DONE", "module ready");
  }
  wifiTestDone = false;
  wifiTestSuccess = false;
  wifiTestPrinted = false;
  wifiCount = 0;
  state = ST_HOME;
  delay(500);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  pinMode(PIN_BTN, INPUT_PULLUP);
  analogReadResolution(10);

#if ENABLE_LCD_I2C
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
#endif

  if (!BLE.begin()) {
    setError("BLE.begin failed");
    return;
  }
  BLE.setLocalName("SB1ControllerUI");
  BLE.central();
  randomSeed(analogRead(A0));

#if HAVE_FLASHSTORAGE
  char saved[18];
  flash_saved_ble_addr.read(saved);
  if (saved[0] != 0 && saved[0] != (char)0xFF) {
    Serial.print("Saved peer addr: ");
    Serial.println(saved);
  }
#else
  Serial.println("FlashStorage not installed: peer address will not persist.");
#endif

  uiLine("SB1 UI ready", "Pot+btn; Serial");
}

void loop() {
  BLE.poll();
  pollWifiResetConfirm();
  uint8_t ev = pollButton();

  if (wifiResetConfirmActive) {
    delay(5);
    return;
  }

  if (ev == BTN_HOLD_BT_RESET) {
    resetBluetoothStack();
  } else if (ev == BTN_LONG) {
    switch (state) {
      case ST_WIFI_LIST:
        if (wifiCount > 0) {
          startWifiPassEntry(potStableIndex((uint8_t)wifiCount));
        }
        break;
      case ST_WIFI_PASS_ENTRY:
        state = ST_WIFI_PASS_MENU;
        renderWifiPassMenu(true);
        break;
      case ST_WIFI_TEST:
        bleDisconnect();
        state = ST_HOME;
        bleScanPhaseStarted = false;
        uiLine("Home", "WiFi / BLE scan");
        break;
      default:
        bleDisconnect();
        state = ST_HOME;
        bleScanPhaseStarted = false;
        uiLine("Home", "WiFi / BLE scan");
        break;
    }
  } else if (ev == BTN_SHORT) {
    switch (state) {
      case ST_HOME: {
        uint8_t i = potStableIndex(HOME_ITEMS);
        delay(80);
        if (i == 0) {
          state = ST_WIFI_SCAN;
        } else {
          state = ST_BLE_SCAN;
        }
        break;
      }
      case ST_WIFI_LIST:
        state = ST_HOME;
        uiLine("Home", "WiFi / BLE scan");
        break;
      case ST_WIFI_PASS_ENTRY: {
        if (wifiPassLen < 63) {
          wifiPass[wifiPassLen++] = WIFI_PASS_CHARS[wifiPassCharIdx];
          wifiPass[wifiPassLen] = '\0';
          renderWifiPassEntry(true);
        }
        break;
      }
      case ST_WIFI_PASS_MENU:
        if (wifiPassMenuIdx == 0) {
          if (wifiPassLen < 8) {
            uiLine("Pass too short", "Need >= 8 chars");
            state = ST_WIFI_PASS_ENTRY;
          } else {
            startWifiTest();
          }
        } else if (wifiPassMenuIdx == 1) {
          if (wifiPassLen > 0) {
            wifiPass[--wifiPassLen] = '\0';
          }
          state = ST_WIFI_PASS_ENTRY;
        } else {
          state = ST_WIFI_LIST;
        }
        break;
      case ST_WIFI_TEST:
        if (wifiTestDone && !wifiTestSuccess) {
          startWifiTest();
        }
        break;
      case ST_BLE_LIST: {
        if (bleCount <= 0) {
          state = ST_HOME;
          uiLine("Home", "WiFi / BLE scan");
          break;
        }
        int sel = potStableIndex((uint8_t)bleCount);
        uiLine("Connecting...", bleAddr[sel]);
        if (!bleConnectAt(sel)) {
        }
        break;
      }
      case ST_BLE_CONNECTED:
        bleDisconnect();
        state = ST_HOME;
        uiLine("Home", "WiFi / BLE scan");
        break;
      case ST_ERROR:
        state = ST_HOME;
        uiLine("Home", "WiFi / BLE scan");
        break;
      default:
        break;
    }
  }

  if (state == ST_WIFI_SCAN) {
    runWifiScanBlocking();
  }

  if (state == ST_WIFI_PASS_ENTRY) {
    wifiPassCharIdx = potStableIndex((uint8_t)(sizeof(WIFI_PASS_CHARS) - 1));
  }
  if (state == ST_WIFI_PASS_MENU) {
    wifiPassMenuIdx = potStableIndex(3);
  }
  if (state == ST_WIFI_TEST) {
    wifiTestPoll();
  }

  if (state == ST_BLE_SCAN) {
    if (!bleScanPhaseStarted) {
      bleScanBegin();
    }
    if (state == ST_BLE_SCAN && bleScanPhaseStarted) {
      bleScanPoll();
    }
  }

  switch (state) {
    case ST_HOME: {
      uint8_t i = potStableIndex(HOME_ITEMS);
      static uint8_t lastHome = 255;
      if (i != lastHome) {
        lastHome = i;
        Serial.println("--- Home ---");
        Serial.println(i == 0 ? "> WiFi Scan" : "  WiFi Scan");
        Serial.println(i == 1 ? "> BLE Scan" : "  BLE Scan");
#if ENABLE_LCD_I2C
        if (i == 0) lcdClearPrint2("WiFi Scan", "*");
        else lcdClearPrint2("BLE Scan", "*");
#endif
      }
      break;
    }
    case ST_WIFI_LIST: {
      if (wifiCount <= 0) break;
      int sel = potStableIndex((uint8_t)wifiCount);
      static int lastSel = -1;
      if (sel != lastSel) {
        lastSel = sel;
        Serial.println("--- WiFi ---");
        for (int j = 0; j < wifiCount; j++) {
          Serial.print(j == sel ? "* " : "  ");
          Serial.print(wifiSsid[j]);
          Serial.print("  ");
          Serial.print(wifiRssi[j]);
          Serial.print(" dBm  ");
          Serial.println(encTypeStr(wifiEnc[j]));
        }
        Serial.println("Long: select SSID for password entry");
        Serial.println("Short: home");
#if ENABLE_LCD_I2C
        char l1[17];
        char l2[17];
        strncpy(l1, wifiSsid[sel], 16);
        l1[16] = '\0';
        snprintf(l2, sizeof(l2), "%d %s", wifiRssi[sel], encTypeStr(wifiEnc[sel]));
        lcdClearPrint2(l1, l2);
#endif
      }
      break;
    }
    case ST_WIFI_PASS_ENTRY:
      renderWifiPassEntry(false);
      break;
    case ST_WIFI_PASS_MENU:
      renderWifiPassMenu(false);
      break;
    case ST_WIFI_TEST:
      renderWifiTest(false);
      break;
    case ST_BLE_LIST: {
      if (bleCount <= 0) break;
      int sel = potStableIndex((uint8_t)bleCount);
      static int lastBle = -1;
      if (sel != lastBle) {
        lastBle = sel;
        Serial.println("--- BLE (SB1 / NUS) ---");
        for (int j = 0; j < bleCount; j++) {
          Serial.print(j == sel ? "* " : "  ");
          Serial.print(bleName[j]);
          Serial.print("  ");
          Serial.print(bleAddr[j]);
          Serial.print("  ");
          Serial.println(bleRssi[j]);
        }
        Serial.println("Short tap: connect");
#if ENABLE_LCD_I2C
        lcdClearPrint2(bleName[sel], bleAddr[sel]);
#endif
      }
      break;
    }
    case ST_BLE_CONNECTED:
      if (blePeerValid && nusReady && nusTx.valueUpdated()) {
        uint8_t buf[64];
        int n = nusTx.readValue(buf, sizeof(buf));
        if (n > 0) {
          Serial.print("NUS TX: ");
          for (int i = 0; i < n; i++) {
            if (buf[i] < 16) Serial.print('0');
            Serial.print(buf[i], HEX);
            Serial.print(' ');
          }
          Serial.println();
        }
      }
      if (millis() - lastNotifyLog > 4000) {
        lastNotifyLog = millis();
        Serial.println("BLE connected (long press = home)");
      }
      break;
    default:
      break;
  }

  if (state == ST_ERROR) {
    Serial.print("ERROR: ");
    Serial.println(errMsg);
#if ENABLE_LCD_I2C
    lcdClearPrint2("ERROR", errMsg);
#endif
    delay(300);
    state = ST_HOME;
    bleScanPhaseStarted = false;
    uiLine("Home", "WiFi / BLE scan");
  }

  delay(5);
}
