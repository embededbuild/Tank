#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <RF24.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <esp_wifi.h>

// ================================================================
// HARDWARE PINS
// ================================================================
#define SD_CS    15
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define NRF24_CE 4
#define NRF24_CSN 5
#define OLED_SDA 21
#define OLED_SCL 22
#define BTN_UP   32
#define BTN_DOWN 33
#define BTN_SELECT 25
#define BTN_BACK 26

// ================================================================
// GLOBALS
// ================================================================
bool sdReady = false;
bool nrfReady = false;
bool bleReady = false;
bool wifiConnected = false;
bool probeSniffing = false;
unsigned long lastBtnTime = 0;
#define DEBOUNCE_MS 150

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
SPIClass vspi(VSPI);
RF24 radio(NRF24_CE, NRF24_CSN);
NimBLEScan* pBLEScan = nullptr;

// ================================================================
// RF24 CAPTURE
// ================================================================
#define MAX_PACKETS 200
#define NUM_CHANNELS 125

struct RFPacket {
  uint8_t data[32];
  uint8_t len;
  uint8_t channel;
  uint32_t time;
};

RFPacket rfPackets[MAX_PACKETS];
int rfPacketCount = 0;
int channelActivity[NUM_CHANNELS];
int totalPackets = 0;
int activeChannels = 0;

// ================================================================
// BLE DEVICES
// ================================================================
#define MAX_BLE_DEVICES 30
struct BLEDeviceInfo {
  String addr;
  String name;
  int rssi;
  bool isTracker;
  String trackerType;
};
BLEDeviceInfo bleDevices[MAX_BLE_DEVICES];
int bleDeviceCount = 0;

// ================================================================
// WIFI NETWORKS
// ================================================================
#define MAX_WIFI_NETWORKS 50
struct WiFiNetwork {
  String ssid;
  String bssid;
  int rssi;
  int channel;
  bool open;
  uint8_t encryption;
};
WiFiNetwork wifiNetworks[MAX_WIFI_NETWORKS];
int wifiNetworkCount = 0;
String lastWifiScanData = "";

// ================================================================
// PROBE REQUESTS
// ================================================================
#define MAX_PROBE_REQUESTS 300
struct ProbeRequest {
  String mac;
  String ssid;
  int rssi;
  int channel;
  uint32_t time;
};
ProbeRequest probeRequests[MAX_PROBE_REQUESTS];
int probeRequestCount = 0;

// ================================================================
// DEVICE SPOOFING
// ================================================================
#define MAX_SPOOF_PROFILES 20
struct SpoofProfile {
  String name;
  String macAddress;
  String ipAddress;
  bool active;
};
SpoofProfile spoofProfiles[MAX_SPOOF_PROFILES];
int spoofProfileCount = 0;
int spoofSelectedIndex = 0;

// ================================================================
// SD FILE BROWSER
// ================================================================
#define MAX_SD_FILES 32
String sdFiles[MAX_SD_FILES];
int sdFileCount = 0;
int sdFileSel = 0;
String selectedFile = "";

// ================================================================
// MENU SYSTEM
// ================================================================
enum MenuLevel {
  MENU_MAIN = 0,
  MENU_WIFI = 1,
  MENU_BLE_SCAN = 2,
  MENU_RF24 = 3,
  MENU_PROBE = 4,
  MENU_SPOOF = 5,
  MENU_SD = 6,
  MENU_RESULT = 7,
  MENU_SD_BROWSE = 8,
  MENU_SPOOF_LIST = 9
};

MenuLevel currentMenu = MENU_MAIN;
int menuIndex = 0;
int menuScroll = 0;
String resultTitle = "";

// Result storage
#define MAX_RESULT_LINES 64
String resultLines[MAX_RESULT_LINES];
int resultLineCount = 0;

// Menu items
const char* mainItems[] = {
  "1. WiFi Scan",
  "2. BLE Scan",
  "3. RF24 Tools",
  "4. Probe Sniff",
  "5. Device Spoof",
  "6. SD Card",
  "7. Status"
};
const int mainCount = 7;

const char* wifiItems[] = {
  "Scan All Networks",
  "Scan Open Only",
  "Save Last Scan",
  "< Back"
};
const int wifiCount = 4;

const char* bleItems[] = {
  "Scan Devices",
  "Find Trackers",
  "Save Scan",
  "< Back"
};
const int bleCount = 4;

const char* rf24Items[] = {
  "Spectrum Analyze",
  "Capture Packets",
  "Replay Packets",
  "Save Capture",
  "< Back"
};
const int rf24Count = 5;

const char* probeItems[] = {
  "Start Sniffing",
  "Stop Sniffing",
  "Save Probes",
  "< Back"
};
const int probeCount = 4;

const char* spoofItems[] = {
  "Create Profile",
  "Activate Profile",
  "Delete Profile",
  "< Back"
};
const int spoofCount = 4;

const char* sdItems[] = {
  "Browse Files",
  "Card Info",
  "Delete File",
  "< Back"
};
const int sdCount = 4;

// ================================================================
// BUTTON HELPERS
// ================================================================
bool btnPressed(int pin) {
  if (millis() - lastBtnTime < DEBOUNCE_MS) return false;
  if (digitalRead(pin) == LOW) { 
    lastBtnTime = millis(); 
    return true; 
  }
  return false;
}

// ================================================================
// DISPLAY FUNCTIONS
// ================================================================
void oledClear() { u8g2.clearBuffer(); }
void oledSend() { u8g2.sendBuffer(); }

void showMessage(const char* line1, const char* line2, const char* line3) {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  if (strlen(line1)) u8g2.drawStr(2, 20, line1);
  if (strlen(line2)) u8g2.drawStr(2, 34, line2);
  if (strlen(line3)) u8g2.drawStr(2, 48, line3);
  oledSend();
}

void showBigMessage(const char* msg) {
  oledClear();
  u8g2.setFont(u8g2_font_7x14B_tf);
  int w = u8g2.getStrWidth(msg);
  u8g2.drawStr((128 - w) / 2, 38, msg);
  oledSend();
}

void drawMenuList(const char* title, const char** items, int count) {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  int maxVisible = 4;
  int startIdx = 0;
  if (menuIndex >= maxVisible) startIdx = menuIndex - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < count; i++) {
    int itemIdx = startIdx + i;
    int y = 14 + (i * 14);
    if (itemIdx == menuIndex) {
      u8g2.drawBox(0, y - 10, 128, 13);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(4, y, items[itemIdx]);
    u8g2.setDrawColor(1);
  }
  if (startIdx > 0) u8g2.drawTriangle(120, 3, 124, 3, 122, 0);
  if (startIdx + maxVisible < count) u8g2.drawTriangle(120, 61, 124, 61, 122, 64);
  oledSend();
}

void drawSDBrowser() {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  String hdr = "FILES(" + String(sdFileCount) + ")";
  u8g2.drawStr(2, 10, hdr.c_str());
  u8g2.setDrawColor(1);

  if (sdFileCount == 0) {
    u8g2.drawStr(4, 36, "No files found");
    oledSend();
    return;
  }

  int maxVisible = 4;
  int startIdx = 0;
  if (sdFileSel >= maxVisible) startIdx = sdFileSel - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < sdFileCount; i++) {
    int idx = startIdx + i;
    int y = 24 + (i * 12);
    if (idx == sdFileSel) {
      u8g2.drawBox(0, y - 9, 128, 11);
      u8g2.setDrawColor(0);
    }
    String fn = sdFiles[idx];
    int lastSlash = fn.lastIndexOf('/');
    if (lastSlash >= 0) fn = fn.substring(lastSlash + 1);
    if (fn.length() > 20) fn = fn.substring(0, 19) + "~";
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, y, fn.c_str());
    u8g2.setDrawColor(1);
  }
  oledSend();
}

void drawSpoofList() {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  u8g2.drawStr(2, 10, "SPOOF PROFILES");
  u8g2.setDrawColor(1);

  if (spoofProfileCount == 0) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 36, "No profiles found");
    oledSend();
    return;
  }

  int maxVisible = 4;
  int startIdx = 0;
  if (spoofSelectedIndex >= maxVisible) startIdx = spoofSelectedIndex - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < spoofProfileCount; i++) {
    int idx = startIdx + i;
    int y = 24 + (i * 12);
    if (idx == spoofSelectedIndex) {
      u8g2.drawBox(0, y - 9, 128, 11);
      u8g2.setDrawColor(0);
    }
    String display = spoofProfiles[idx].name;
    if (display.length() > 16) display = display.substring(0, 15) + "~";
    u8g2.drawStr(4, y, display.c_str());
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, y + 8, spoofProfiles[idx].macAddress.c_str());
    if (spoofProfiles[idx].active) u8g2.drawStr(85, y + 8, "ACTIVE");
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setDrawColor(1);
  }
  oledSend();
}

void drawResult() {
  oledClear();
  u8g2.setFont(u8g2_font_5x7_tf);
  int maxLines = 9;
  int lineH = 7;
  int startY = 7;
  for (int i = 0; i < maxLines && (menuScroll + i) < resultLineCount; i++) {
    u8g2.drawStr(2, startY + (i * lineH), resultLines[menuScroll + i].c_str());
  }
  if (resultLineCount > maxLines) {
    int barH = 64 * maxLines / resultLineCount;
    int barY = 64 * menuScroll / resultLineCount;
    u8g2.drawFrame(124, 0, 4, 64);
    u8g2.drawBox(124, barY, 4, barH);
  }
  if (resultLineCount == 0) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(10, 35, "No results found");
  }
  oledSend();
}

void drawMenu() {
  switch (currentMenu) {
    case MENU_MAIN: drawMenuList("TANK v7", mainItems, mainCount); break;
    case MENU_WIFI: drawMenuList("WIFI", wifiItems, wifiCount); break;
    case MENU_BLE_SCAN: drawMenuList("BLE SCAN", bleItems, bleCount); break;
    case MENU_RF24: drawMenuList("RF24", rf24Items, rf24Count); break;
    case MENU_PROBE: drawMenuList("PROBE SNIFF", probeItems, probeCount); break;
    case MENU_SPOOF: drawMenuList("DEVICE SPOOF", spoofItems, spoofCount); break;
    case MENU_SPOOF_LIST: drawSpoofList(); break;
    case MENU_SD: drawMenuList("SD CARD", sdItems, sdCount); break;
    case MENU_SD_BROWSE: drawSDBrowser(); break;
    case MENU_RESULT: drawResult(); break;
  }
}

void clearResults(String title) {
  resultLineCount = 0;
  menuScroll = 0;
  resultTitle = title;
  for (int i = 0; i < MAX_RESULT_LINES; i++) resultLines[i] = "";
}

void addResult(String line) {
  if (resultLineCount >= MAX_RESULT_LINES) return;
  if (line.length() > 24) line = line.substring(0, 23) + "~";
  resultLines[resultLineCount++] = line;
}

// ================================================================
// TIMESTAMP
// ================================================================
String getTimestamp() {
  unsigned long s = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "%02luh%02lum%02lus", s/3600, (s%3600)/60, s%60);
  return String(buf);
}

// ================================================================
// GLITCHY BOOT ANIMATION
// ================================================================
void drawGlitchEffect(int intensity, int offset) {
  oledClear();
  
  // Random scanlines
  for (int i = 0; i < intensity; i++) {
    int y = random(0, 64);
    u8g2.drawBox(0, y, 128, random(1, 4));
  }
  
  // Random horizontal shifts
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.setDrawColor(1);
  int w = u8g2.getStrWidth("Tank");
  u8g2.drawStr((128 - w) / 2 + random(-offset, offset), 30 + random(-offset/2, offset/2), "Tank");
  
  u8g2.setFont(u8g2_font_5x7_tf);
  char verBuf[16];
  snprintf(verBuf, sizeof(verBuf), "v7.0");
  u8g2.drawStr(82 + random(-offset/2, offset/2), 42 + random(-offset/2, offset/2), verBuf);
  u8g2.drawStr(2 + random(-offset/2, offset/2), 55 + random(-offset/2, offset/2), "READY");
  
  // Pixel noise
  for (int i = 0; i < intensity * 5; i++) {
    u8g2.drawPixel(random(0, 128), random(0, 64));
  }
  
  oledSend();
  delay(40 + random(0, 30));
}

void drawSplash() {
  // Glitchy startup sequence
  for (int glitch = 0; glitch < 25; glitch++) {
    int intensity = 20 - (glitch / 2);
    if (intensity < 3) intensity = 3;
    int offset = (25 - glitch) / 3;
    drawGlitchEffect(intensity, offset);
  }
  
  // Clear to normal
  oledClear();
  u8g2.setFont(u8g2_font_7x14B_tf);
  int w = u8g2.getStrWidth("Tank");
  u8g2.drawStr((128 - w) / 2, 30, "Tank");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(82, 42, "v7.0");
  u8g2.drawStr(2, 55, "PROBE+SPOOF");
  oledSend();
  delay(500);
  
  // Fast glitch flash
  for (int i = 0; i < 5; i++) {
    oledClear();
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 0, 128, 64);
    oledSend();
    delay(20);
    drawProgress("BOOT", 10 + (i * 10));
    delay(20);
  }
  
  drawProgress("Init BLE...", 30);
  NimBLEDevice::init("Tank");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  pBLEScan = nullptr;
  bleReady = true;
  drawProgress("BLE OK", 40);
  delay(200);
  
  drawProgress("Init nRF24...", 50);
  vspi.begin(SD_SCK, SD_MISO, SD_MOSI, NRF24_CSN);
  if (radio.begin(&vspi)) {
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(76);
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.stopListening();
    nrfReady = true;
  }
  drawProgress(nrfReady ? "nRF24 OK" : "nRF24 Failed", 60);
  delay(200);
  
  drawProgress("Init SD...", 70);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  initSD();
  drawProgress(sdReady ? "SD OK" : "SD Failed", 80);
  delay(200);
  
  drawProgress("Load Profiles...", 90);
  loadSpoofProfiles();
  drawProgress("Ready", 100);
  delay(400);
  
  // Final glitch effect
  for (int i = 0; i < 8; i++) {
    drawGlitchEffect(8 - i, 8 - i);
  }
  
  showBigMessage("READY");
  delay(800);
  oledClear();
  oledSend();
  
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  delay(100);
  wifiConnected = false;
}

// ================================================================
// PROGRESS BAR
// ================================================================
void drawProgress(const char* label, int percent) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  int w = u8g2.getStrWidth(label);
  u8g2.drawStr((128 - w) / 2, 28, label);
  u8g2.drawFrame(4, 38, 120, 10);
  int fillW = (int)(116 * percent / 100.0);
  if (fillW > 0) u8g2.drawBox(6, 40, fillW, 6);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  u8g2.setFont(u8g2_font_5x7_tf);
  int pw = u8g2.getStrWidth(pct);
  u8g2.drawStr((128 - pw) / 2, 58, pct);
  oledSend();
}

// ================================================================
// SD CARD
// ================================================================
void initSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  vspi.begin(SD_SCK, SD_MISO, SD_MOSI, NRF24_CSN);
  if (!SD.begin(SD_CS, vspi, 4000000)) {
    sdReady = false;
    return;
  }
  sdReady = true;
  if (!SD.exists("/tank")) SD.mkdir("/tank");
  if (!SD.exists("/tank/wifi")) SD.mkdir("/tank/wifi");
  if (!SD.exists("/tank/ble")) SD.mkdir("/tank/ble");
  if (!SD.exists("/tank/rf24")) SD.mkdir("/tank/rf24");
  if (!SD.exists("/tank/probes")) SD.mkdir("/tank/probes");
  if (!SD.exists("/tank/spoof")) SD.mkdir("/tank/spoof");
  loadSpoofProfiles();
}

void saveToSD(String filename, String data) {
  if (!sdReady) { addResult("SD ERR"); return; }
  File f = SD.open(filename, FILE_WRITE);
  if (!f) { addResult("Can't create"); return; }
  f.println(data);
  f.close();
  addResult("Saved: " + filename.substring(filename.lastIndexOf('/') + 1));
}

void runSDBrowse() {
  sdFileCount = 0;
  sdFileSel = 0;
  const char* folders[] = {"/tank/wifi", "/tank/ble", "/tank/rf24", "/tank/probes", "/tank/spoof"};
  for (int f = 0; f < 5; f++) {
    File dir = SD.open(folders[f]);
    if (!dir) continue;
    File entry = dir.openNextFile();
    while (entry && sdFileCount < MAX_SD_FILES) {
      if (!entry.isDirectory()) {
        sdFiles[sdFileCount++] = String(folders[f]) + "/" + entry.name();
      }
      entry = dir.openNextFile();
    }
    dir.close();
  }
  currentMenu = MENU_SD_BROWSE;
}

void runSDViewFile(String filename) {
  int lastSlash = filename.lastIndexOf('/');
  String shortName = lastSlash >= 0 ? filename.substring(lastSlash + 1) : filename;
  clearResults(shortName);
  File f = SD.open(filename, FILE_READ);
  if (!f) { addResult("Cannot open!"); return; }
  while (f.available() && resultLineCount < MAX_RESULT_LINES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) addResult(line);
  }
  f.close();
  currentMenu = MENU_RESULT;
}

void runSDCardInfo() {
  clearResults("SD INFO");
  if (!sdReady) { addResult("SD not ready"); }
  else {
    char buf[32];
    snprintf(buf, sizeof(buf), "Total: %d MB", (int)(SD.totalBytes()/1048576));
    addResult(buf);
    snprintf(buf, sizeof(buf), "Used: %d MB", (int)(SD.usedBytes()/1048576));
    addResult(buf);
    snprintf(buf, sizeof(buf), "Free: %d MB", (int)((SD.totalBytes()-SD.usedBytes())/1048576));
    addResult(buf);
  }
  currentMenu = MENU_RESULT;
}

void runSDDeleteFile() {
  if (!sdReady) { addResult("SD not ready"); currentMenu = MENU_RESULT; return; }
  if (selectedFile.length() == 0) { addResult("No file selected"); currentMenu = MENU_RESULT; return; }
  if (SD.remove(selectedFile)) {
    addResult("Deleted: " + selectedFile);
    selectedFile = "";
  } else {
    addResult("Delete failed!");
  }
  currentMenu = MENU_RESULT;
}

// ================================================================
// RF24 FUNCTIONS
// ================================================================
void initRF24() {
  if (radio.begin(&vspi)) {
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(76);
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.stopListening();
    nrfReady = true;
  }
}

void spectrumAnalyzeRF24() {
  clearResults("SPECTRUM ANALYZE");
  if (!nrfReady) { addResult("nRF24 not ready"); currentMenu = MENU_RESULT; return; }
  
  showMessage("Analyzing...", "125 channels", "2 sec each");
  memset(channelActivity, 0, sizeof(channelActivity));
  totalPackets = 0;
  activeChannels = 0;
  
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    unsigned long start = millis();
    int packetsOnChannel = 0;
    while (millis() - start < 2000) {
      if (radio.available()) {
        uint8_t buf[32];
        radio.read(buf, 32);
        channelActivity[ch]++;
        packetsOnChannel++;
        totalPackets++;
      }
      yield();
    }
    radio.stopListening();
    if (packetsOnChannel > 3) activeChannels++;
    
    if (ch % 20 == 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "CH%d: %d pkts", ch, packetsOnChannel);
      showMessage("Analyzing...", buf, "");
    }
    yield();
  }
  
  char buf[32];
  snprintf(buf, sizeof(buf), "Total: %d packets", totalPackets);
  addResult(buf);
  snprintf(buf, sizeof(buf), "Active channels: %d/125", activeChannels);
  addResult(buf);
  addResult("Top 5 channels:");
  
  for (int top = 0; top < 5; top++) {
    int maxCh = 0, maxVal = 0;
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
      if (channelActivity[ch] > maxVal) {
        maxVal = channelActivity[ch];
        maxCh = ch;
      }
    }
    if (maxVal > 0) {
      snprintf(buf, sizeof(buf), "CH%3d: %d pkts", maxCh, maxVal);
      addResult(buf);
      channelActivity[maxCh] = 0;
    }
  }
  radio.setChannel(76);
  currentMenu = MENU_RESULT;
}

void captureRF24() {
  clearResults("RF24 CAPTURE");
  if (!nrfReady) { addResult("nRF24 not ready"); currentMenu = MENU_RESULT; return; }
  
  rfPacketCount = 0;
  showMessage("Capturing...", "125 channels", "");
  
  for (int ch = 0; ch < NUM_CHANNELS && rfPacketCount < MAX_PACKETS; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    unsigned long start = millis();
    while (millis() - start < 15 && rfPacketCount < MAX_PACKETS) {
      if (radio.available()) {
        RFPacket& p = rfPackets[rfPacketCount];
        p.len = radio.getDynamicPayloadSize();
        if (p.len > 32) p.len = 32;
        radio.read(p.data, p.len);
        p.channel = ch;
        p.time = millis();
        rfPacketCount++;
      }
    }
    radio.stopListening();
    if (ch % 20 == 0) {
      char buf[20];
      snprintf(buf, sizeof(buf), "CH%d/%d", ch, NUM_CHANNELS);
      showMessage("Capturing...", buf, "");
    }
    yield();
  }
  
  char buf[32];
  snprintf(buf, sizeof(buf), "Captured: %d packets", rfPacketCount);
  addResult(buf);
  radio.setChannel(76);
  currentMenu = MENU_RESULT;
}

void replayRF24() {
  clearResults("RF24 REPLAY");
  if (!nrfReady) { addResult("nRF24 not ready"); currentMenu = MENU_RESULT; return; }
  if (rfPacketCount == 0) { addResult("No packets to replay"); currentMenu = MENU_RESULT; return; }
  
  char buf[32];
  snprintf(buf, sizeof(buf), "%d packets", rfPacketCount);
  showMessage("Replaying...", buf, "PRESS BACK");
  
  int replayed = 0;
  bool running = true;
  unsigned long start = millis();
  
  while (running && millis() - start < 30000) {
    for (int i = 0; i < rfPacketCount && running; i++) {
      radio.stopListening();
      radio.setChannel(rfPackets[i].channel);
      radio.write(rfPackets[i].data, rfPackets[i].len);
      replayed++;
      if (digitalRead(BTN_BACK) == LOW) running = false;
      delay(10);
    }
  }
  
  snprintf(buf, sizeof(buf), "Replayed: %d times", replayed);
  addResult(buf);
  radio.setChannel(76);
  currentMenu = MENU_RESULT;
}

void saveRF24() {
  if (rfPacketCount == 0) { addResult("No data"); currentMenu = MENU_RESULT; return; }
  String out = "RF24 Capture " + getTimestamp() + "\nPackets: " + String(rfPacketCount) + "\n---\n";
  for (int i = 0; i < rfPacketCount; i++) {
    out += "CH" + String(rfPackets[i].channel) + " LEN" + String(rfPackets[i].len) + " DATA:";
    for (int j = 0; j < rfPackets[i].len; j++) {
      char h[3];
      snprintf(h, sizeof(h), "%02X", rfPackets[i].data[j]);
      out += h;
    }
    out += "\n";
  }
  saveToSD("/tank/rf24/capture_" + getTimestamp() + ".txt", out);
  currentMenu = MENU_RESULT;
}

// ================================================================
// BLE SCAN
// ================================================================
class MyScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) {
    if (bleDeviceCount >= MAX_BLE_DEVICES) return;
    String addr = String(dev->getAddress().toString().c_str());
    for (int i = 0; i < bleDeviceCount; i++) {
      if (bleDevices[i].addr == addr) {
        bleDevices[i].rssi = dev->getRSSI();
        return;
      }
    }
    bleDevices[bleDeviceCount].addr = addr;
    bleDevices[bleDeviceCount].name = String(dev->getName().c_str());
    bleDevices[bleDeviceCount].rssi = dev->getRSSI();
    
    // Simple tracker detection
    bool isTracker = false;
    String trackerType = "";
    if (dev->haveManufacturerData()) {
      std::string mfData = dev->getManufacturerData();
      if (mfData.length() >= 2) {
        uint16_t companyID = (uint8_t)mfData[1] << 8 | (uint8_t)mfData[0];
        if (companyID == 0x004C) { isTracker = true; trackerType = "Apple"; }
        if (companyID == 0x00E0) { isTracker = true; trackerType = "Tile"; }
        if (companyID == 0x0075) { isTracker = true; trackerType = "Samsung"; }
      }
    }
    bleDevices[bleDeviceCount].isTracker = isTracker;
    bleDevices[bleDeviceCount].trackerType = trackerType;
    bleDeviceCount++;
  }
};

MyScanCallbacks bleCallbacks;

void runBLEScan(bool trackersOnly) {
  clearResults(trackersOnly ? "BLE TRACKERS" : "BLE DEVICES");
  bleDeviceCount = 0;
  showMessage("BLE Scan", "5 seconds", "");
  
  pBLEScan = NimBLEDevice::getScan();
  if (pBLEScan->isScanning()) pBLEScan->stop();
  pBLEScan->setScanCallbacks(&bleCallbacks);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  pBLEScan->start(5, false);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "Found %d devices", bleDeviceCount);
  addResult(buf);
  
  int shown = 0;
  for (int i = 0; i < bleDeviceCount && shown < 30; i++) {
    if (trackersOnly && !bleDevices[i].isTracker) continue;
    String name = bleDevices[i].name.length() > 0 ? bleDevices[i].name : bleDevices[i].addr;
    addResult((bleDevices[i].isTracker ? "! " : "  ") + name);
    snprintf(buf, sizeof(buf), "  RSSI: %d dBm", bleDevices[i].rssi);
    addResult(buf);
    if (bleDevices[i].isTracker) addResult("  Type: " + bleDevices[i].trackerType);
    addResult("  ---");
    shown++;
  }
  if (shown == 0) addResult(trackersOnly ? "No trackers found" : "No devices found");
  
  // Add Back option at the bottom
  addResult("");
  addResult("Press BACK to return");
  
  currentMenu = MENU_RESULT;
}

void saveBLEScan() {
  if (bleDeviceCount == 0) { addResult("No data"); currentMenu = MENU_RESULT; return; }
  String out = "BLE Scan " + getTimestamp() + "\nDevices: " + String(bleDeviceCount) + "\n---\n";
  for (int i = 0; i < bleDeviceCount; i++) {
    out += "MAC: " + bleDevices[i].addr + "\n";
    out += "Name: " + bleDevices[i].name + "\n";
    out += "RSSI: " + String(bleDevices[i].rssi) + "dBm\n";
    if (bleDevices[i].isTracker) out += "Type: TRACKER (" + bleDevices[i].trackerType + ")\n";
    out += "---\n";
  }
  saveToSD("/tank/ble/scan_" + getTimestamp() + ".txt", out);
  currentMenu = MENU_RESULT;
}

// ================================================================
// WIFI SCAN
// ================================================================
void runWifiScanAll(bool openOnly) {
  clearResults(openOnly ? "WIFI OPEN ONLY" : "WIFI ALL");
  showMessage("Scanning WiFi...", "30 seconds", "");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  int n = WiFi.scanNetworks(false, true);
  wifiNetworkCount = 0;
  lastWifiScanData = "";
  
  for (int i = 0; i < n && wifiNetworkCount < MAX_WIFI_NETWORKS; i++) {
    bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    if (openOnly && !isOpen) continue;
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) ssid = "[Hidden]";
    wifiNetworks[wifiNetworkCount].ssid = ssid;
    wifiNetworks[wifiNetworkCount].bssid = WiFi.BSSIDstr(i);
    wifiNetworks[wifiNetworkCount].rssi = WiFi.RSSI(i);
    wifiNetworks[wifiNetworkCount].channel = WiFi.channel(i);
    wifiNetworks[wifiNetworkCount].open = isOpen;
    wifiNetworks[wifiNetworkCount].encryption = WiFi.encryptionType(i);
    wifiNetworkCount++;
    
    lastWifiScanData += (isOpen ? "[OPEN] " : "[SEC] ") + ssid + " (CH" + String(WiFi.channel(i)) + ") " + String(WiFi.RSSI(i)) + "dBm\n";
  }
  
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "Found %d networks", wifiNetworkCount);
  addResult(buf);
  
  for (int i = 0; i < wifiNetworkCount && i < 15; i++) {
    addResult(wifiNetworks[i].ssid);
    snprintf(buf, sizeof(buf), "  CH%d %ddBm", wifiNetworks[i].channel, wifiNetworks[i].rssi);
    addResult(buf);
    addResult("  " + String(wifiNetworks[i].open ? "OPEN" : "SECURED"));
    addResult("  BSSID: " + wifiNetworks[i].bssid);
    addResult("  ---");
  }
  if (wifiNetworkCount > 15) {
    snprintf(buf, sizeof(buf), "... and %d more", wifiNetworkCount - 15);
    addResult(buf);
  }
  
  // Add Back option at the bottom
  addResult("");
  addResult("Press BACK to return");
  
  currentMenu = MENU_RESULT;
}

void saveWifiScan() {
  if (wifiNetworkCount == 0) { addResult("No scan data"); currentMenu = MENU_RESULT; return; }
  String out = "WiFi Scan " + getTimestamp() + "\nNetworks: " + String(wifiNetworkCount) + "\n---\n" + lastWifiScanData;
  saveToSD("/tank/wifi/scan_" + getTimestamp() + ".txt", out);
  currentMenu = MENU_RESULT;
}

// ================================================================
// PROBE SNIFFING
// ================================================================
void promiscuousRx(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!probeSniffing) return;
  
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 24 || len > 512) return;
  
  uint8_t* data = pkt->payload;
  uint16_t fc = data[0] | (data[1] << 8);
  uint8_t typeSub = (fc >> 2) & 0x0F;
  
  if (typeSub == 0x04) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             data[10], data[11], data[12], data[13], data[14], data[15]);
    
    int offset = 24;
    String ssid = "";
    while (offset < len - 2) {
      uint8_t tag = data[offset];
      uint8_t tagLen = data[offset + 1];
      if (tag == 0 && tagLen > 0 && tagLen < 32) {
        char ssidBuf[33];
        memcpy(ssidBuf, &data[offset + 2], tagLen);
        ssidBuf[tagLen] = '\0';
        ssid = String(ssidBuf);
        break;
      }
      offset += 2 + tagLen;
    }
    
    if (ssid.length() > 0 && probeRequestCount < MAX_PROBE_REQUESTS) {
      probeRequests[probeRequestCount].mac = String(mac);
      probeRequests[probeRequestCount].ssid = ssid;
      probeRequests[probeRequestCount].rssi = pkt->rx_ctrl.rssi;
      probeRequests[probeRequestCount].channel = pkt->rx_ctrl.channel;
      probeRequests[probeRequestCount].time = millis();
      probeRequestCount++;
    }
  }
}

void startProbeSniff() {
  clearResults("PROBE SNIFFING");
  probeRequestCount = 0;
  probeSniffing = true;
  
  showMessage("Sniffing probes...", "Channel hopping", "PRESS BACK");
  
  esp_wifi_set_promiscuous_rx_cb(promiscuousRx);
  esp_wifi_set_promiscuous(true);
  
  int ch = 1;
  unsigned long lastHop = millis();
  
  while (probeSniffing) {
    if (digitalRead(BTN_BACK) == LOW) {
      probeSniffing = false;
      break;
    }
    if (millis() - lastHop > 200) {
      ch++;
      if (ch > 11) ch = 1;
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      lastHop = millis();
    }
    static unsigned long lastShow = 0;
    if (millis() - lastShow > 2000) {
      lastShow = millis();
      clearResults("PROBE SNIFFING");
      char buf[32];
      snprintf(buf, sizeof(buf), "Probes: %d", probeRequestCount);
      addResult(buf);
      snprintf(buf, sizeof(buf), "Channel: %d", ch);
      addResult(buf);
      addResult("PRESS BACK TO STOP");
      drawResult();
    }
    delay(50);
  }
  
  esp_wifi_set_promiscuous(false);
  
  clearResults("PROBE RESULTS");
  char buf[32];
  snprintf(buf, sizeof(buf), "Probes captured: %d", probeRequestCount);
  addResult(buf);
  for (int i = 0; i < probeRequestCount && i < 15; i++) {
    addResult(probeRequests[i].mac);
    addResult("  -> " + probeRequests[i].ssid);
    snprintf(buf, sizeof(buf), "  CH%d %ddBm", probeRequests[i].channel, probeRequests[i].rssi);
    addResult(buf);
    addResult("  ---");
  }
  addResult("");
  addResult("Press BACK to return");
  currentMenu = MENU_RESULT;
}

void stopProbeSniff() {
  probeSniffing = false;
  esp_wifi_set_promiscuous(false);
  addResult("Probe sniffing stopped");
  currentMenu = MENU_RESULT;
}

void saveProbes() {
  if (probeRequestCount == 0) { addResult("No data"); currentMenu = MENU_RESULT; return; }
  String out = "Probe Requests " + getTimestamp() + "\nProbes: " + String(probeRequestCount) + "\n---\n";
  for (int i = 0; i < probeRequestCount; i++) {
    out += "MAC: " + probeRequests[i].mac + "\n";
    out += "SSID: " + probeRequests[i].ssid + "\n";
    out += "Channel: " + String(probeRequests[i].channel) + "\n";
    out += "RSSI: " + String(probeRequests[i].rssi) + "dBm\n";
    out += "---\n";
  }
  saveToSD("/tank/probes/probes_" + getTimestamp() + ".txt", out);
  currentMenu = MENU_RESULT;
}

// ================================================================
// DEVICE SPOOFING
// ================================================================
void loadSpoofProfiles() {
  spoofProfileCount = 0;
  if (!sdReady) return;
  File f = SD.open("/tank/spoof/profiles.txt", FILE_READ);
  if (!f) return;
  while (f.available() && spoofProfileCount < MAX_SPOOF_PROFILES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int pos1 = line.indexOf('|');
    int pos2 = line.indexOf('|', pos1 + 1);
    int pos3 = line.indexOf('|', pos2 + 1);
    if (pos1 > 0 && pos2 > 0 && pos3 > 0) {
      spoofProfiles[spoofProfileCount].name = line.substring(0, pos1);
      spoofProfiles[spoofProfileCount].macAddress = line.substring(pos1 + 1, pos2);
      spoofProfiles[spoofProfileCount].ipAddress = line.substring(pos2 + 1, pos3);
      spoofProfiles[spoofProfileCount].active = (line.substring(pos3 + 1) == "1");
      spoofProfileCount++;
    }
  }
  f.close();
}

void saveSpoofProfiles() {
  if (!sdReady) return;
  File f = SD.open("/tank/spoof/profiles.txt", FILE_WRITE);
  if (!f) return;
  for (int i = 0; i < spoofProfileCount; i++) {
    f.println(spoofProfiles[i].name + "|" + spoofProfiles[i].macAddress + "|" + 
              spoofProfiles[i].ipAddress + "|" + String(spoofProfiles[i].active ? "1" : "0"));
  }
  f.close();
}

void createSpoofProfile() {
  clearResults("CREATE PROFILE");
  char mac[18];
  snprintf(mac, sizeof(mac), "02:%02X:%02X:%02X:%02X:%02X",
           random(0x00, 0xFF), random(0x00, 0xFF), random(0x00, 0xFF),
           random(0x00, 0xFF), random(0x00, 0xFF));
  String profileName = "Spoof_" + String(millis() % 10000);
  String profileMAC = String(mac);
  String profileIP = "192.168.1." + String(random(2, 254));
  
  addResult("Name: " + profileName);
  addResult("MAC: " + profileMAC);
  addResult("IP: " + profileIP);
  addResult("");
  addResult("Profile saved to SD");
  
  if (spoofProfileCount < MAX_SPOOF_PROFILES) {
    spoofProfiles[spoofProfileCount].name = profileName;
    spoofProfiles[spoofProfileCount].macAddress = profileMAC;
    spoofProfiles[spoofProfileCount].ipAddress = profileIP;
    spoofProfiles[spoofProfileCount].active = false;
    spoofProfileCount++;
    saveSpoofProfiles();
  }
  currentMenu = MENU_RESULT;
}

void activateSpoofProfile() {
  clearResults("ACTIVATE PROFILE");
  if (spoofProfileCount == 0) {
    addResult("No profiles to activate");
    currentMenu = MENU_RESULT;
    return;
  }
  
  addResult("Selected: " + spoofProfiles[spoofSelectedIndex].name);
  addResult("MAC: " + spoofProfiles[spoofSelectedIndex].macAddress);
  addResult("");
  addResult("Profile marked active");
  
  for (int i = 0; i < spoofProfileCount; i++) {
    spoofProfiles[i].active = false;
  }
  spoofProfiles[spoofSelectedIndex].active = true;
  saveSpoofProfiles();
  currentMenu = MENU_RESULT;
}

void deleteSpoofProfile() {
  clearResults("DELETE PROFILE");
  if (spoofProfileCount == 0 || spoofSelectedIndex >= spoofProfileCount) {
    addResult("No profile selected");
    currentMenu = MENU_RESULT;
    return;
  }
  
  addResult("Deleted: " + spoofProfiles[spoofSelectedIndex].name);
  for (int i = spoofSelectedIndex; i < spoofProfileCount - 1; i++) {
    spoofProfiles[i] = spoofProfiles[i + 1];
  }
  spoofProfileCount--;
  if (spoofSelectedIndex >= spoofProfileCount) spoofSelectedIndex = spoofProfileCount - 1;
  if (spoofSelectedIndex < 0) spoofSelectedIndex = 0;
  saveSpoofProfiles();
  currentMenu = MENU_RESULT;
}

// ================================================================
// STATUS SCREEN
// ================================================================
void drawStatusScreen() {
  clearResults("SYSTEM STATUS");
  addResult("Tank v7.0");
  addResult("----------------");
  addResult("nRF24: " + String(nrfReady ? "READY" : "FAIL"));
  addResult("BLE: " + String(bleReady ? "READY" : "FAIL"));
  addResult("SD: " + String(sdReady ? "READY" : "NO"));
  addResult("----------------");
  char buf[32];
  snprintf(buf, sizeof(buf), "RF Packets: %d", rfPacketCount);
  addResult(buf);
  snprintf(buf, sizeof(buf), "Probes: %d", probeRequestCount);
  addResult(buf);
  snprintf(buf, sizeof(buf), "Heap: %dKB", ESP.getFreeHeap()/1024);
  addResult(buf);
  addResult("");
  addResult("Press BACK to return");
  currentMenu = MENU_RESULT;
}

// ================================================================
// RF24 RESULTS with Back option
// ================================================================
void showRF24Results() {
  // This is called after RF24 operations
  addResult("");
  addResult("Press BACK to return");
  currentMenu = MENU_RESULT;
}

// ================================================================
// HANDLE SELECT
// ================================================================
void handleSelect() {
  switch (currentMenu) {
    case MENU_MAIN:
      switch (menuIndex) {
        case 0: currentMenu = MENU_WIFI; menuIndex = 0; break;
        case 1: currentMenu = MENU_BLE_SCAN; menuIndex = 0; break;
        case 2: currentMenu = MENU_RF24; menuIndex = 0; break;
        case 3: currentMenu = MENU_PROBE; menuIndex = 0; break;
        case 4: currentMenu = MENU_SPOOF; menuIndex = 0; break;
        case 5: currentMenu = MENU_SD; menuIndex = 0; break;
        case 6: drawStatusScreen(); break;
      }
      break;
      
    case MENU_WIFI:
      switch (menuIndex) {
        case 0: runWifiScanAll(false); break;
        case 1: runWifiScanAll(true); break;
        case 2: saveWifiScan(); break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;
      
    case MENU_BLE_SCAN:
      switch (menuIndex) {
        case 0: runBLEScan(false); break;
        case 1: runBLEScan(true); break;
        case 2: saveBLEScan(); break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;
      
    case MENU_RF24:
      switch (menuIndex) {
        case 0: spectrumAnalyzeRF24(); showRF24Results(); break;
        case 1: captureRF24(); showRF24Results(); break;
        case 2: replayRF24(); showRF24Results(); break;
        case 3: saveRF24(); showRF24Results(); break;
        case 4: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;
      
    case MENU_PROBE:
      switch (menuIndex) {
        case 0: startProbeSniff(); break;
        case 1: stopProbeSniff(); break;
        case 2: saveProbes(); break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;
      
    case MENU_SPOOF:
      switch (menuIndex) {
        case 0: createSpoofProfile(); break;
        case 1: activateSpoofProfile(); break;
        case 2: deleteSpoofProfile(); break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;
      
    case MENU_SPOOF_LIST:
      currentMenu = MENU_SPOOF;
      menuIndex = 0;
      break;
      
    case MENU_SD:
      switch (menuIndex) {
        case 0: runSDBrowse(); break;
        case 1: runSDCardInfo(); break;
        case 2: runSDDeleteFile(); break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;
      
    case MENU_SD_BROWSE:
      if (sdFileCount > 0 && sdFileSel < sdFileCount) {
        selectedFile = sdFiles[sdFileSel];
        runSDViewFile(selectedFile);
      }
      break;
      
    case MENU_RESULT:
      break;
  }
}

// ================================================================
// HANDLE BACK
// ================================================================
void handleBack() {
  menuScroll = 0;
  switch (currentMenu) {
    case MENU_MAIN:
      break;
    case MENU_WIFI:
    case MENU_BLE_SCAN:
    case MENU_RF24:
    case MENU_PROBE:
    case MENU_SPOOF:
    case MENU_SD:
      currentMenu = MENU_MAIN;
      menuIndex = 0;
      break;
    case MENU_SPOOF_LIST:
      currentMenu = MENU_SPOOF;
      menuIndex = 0;
      break;
    case MENU_SD_BROWSE:
      currentMenu = MENU_SD;
      menuIndex = 0;
      break;
    case MENU_RESULT:
      currentMenu = MENU_MAIN;
      menuIndex = 0;
      break;
  }
}

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setContrast(200);
  
  randomSeed(analogRead(0));
  
  drawSplash();
  currentMenu = MENU_MAIN;
  menuIndex = 0;
  drawMenu();
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  bool redraw = false;
  
  if (btnPressed(BTN_UP)) {
    if (currentMenu == MENU_RESULT) {
      if (menuScroll > 0) { menuScroll--; redraw = true; }
    } else if (currentMenu == MENU_SD_BROWSE) {
      if (sdFileSel > 0) sdFileSel--;
      else sdFileSel = sdFileCount - 1;
      redraw = true;
    } else if (currentMenu == MENU_SPOOF_LIST) {
      if (spoofSelectedIndex > 0) spoofSelectedIndex--;
      else spoofSelectedIndex = spoofProfileCount - 1;
      redraw = true;
    } else {
      int count = 0;
      switch (currentMenu) {
        case MENU_MAIN: count = mainCount; break;
        case MENU_WIFI: count = wifiCount; break;
        case MENU_BLE_SCAN: count = bleCount; break;
        case MENU_RF24: count = rf24Count; break;
        case MENU_PROBE: count = probeCount; break;
        case MENU_SPOOF: count = spoofCount; break;
        case MENU_SD: count = sdCount; break;
        default: break;
      }
      if (count > 0) {
        if (menuIndex > 0) menuIndex--;
        else menuIndex = count - 1;
        redraw = true;
      }
    }
  }
  
  if (btnPressed(BTN_DOWN)) {
    if (currentMenu == MENU_RESULT) {
      if (menuScroll < resultLineCount - 1) { menuScroll++; redraw = true; }
    } else if (currentMenu == MENU_SD_BROWSE) {
      if (sdFileSel < sdFileCount - 1) sdFileSel++;
      else sdFileSel = 0;
      redraw = true;
    } else if (currentMenu == MENU_SPOOF_LIST) {
      if (spoofSelectedIndex < spoofProfileCount - 1) spoofSelectedIndex++;
      else spoofSelectedIndex = 0;
      redraw = true;
    } else {
      int count = 0;
      switch (currentMenu) {
        case MENU_MAIN: count = mainCount; break;
        case MENU_WIFI: count = wifiCount; break;
        case MENU_BLE_SCAN: count = bleCount; break;
        case MENU_RF24: count = rf24Count; break;
        case MENU_PROBE: count = probeCount; break;
        case MENU_SPOOF: count = spoofCount; break;
        case MENU_SD: count = sdCount; break;
        default: break;
      }
      if (menuIndex < count - 1) menuIndex++;
      else menuIndex = 0;
      redraw = true;
    }
  }
  
  if (btnPressed(BTN_SELECT)) { 
    handleSelect(); 
    redraw = true; 
  }
  if (btnPressed(BTN_BACK)) { 
    handleBack(); 
    redraw = true; 
  }
  
  if (redraw) drawMenu();
  
  delay(20);
}