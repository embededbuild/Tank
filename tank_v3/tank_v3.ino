#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEClient.h>
#include <RF24.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

// ================================================================
// PICO BLE COMMAND CONFIGURATION
// ================================================================
#define PICO_DEVICE_NAME_PREFIX "PicoDucky-"
#define PICO_DEVICE_NAME        "BLEDUCKY"
#define SCAN_DURATION_SECONDS   10
#define BLE_UART_SERVICE_UUID   "0000FFE0-0000-1000-8000-00805F9B34FB"
#define BLE_UART_RX_CHAR_UUID   "0000FFE2-0000-1000-8000-00805F9B34FB"

// ================================================================
// SD CARD CONFIGURATION
// ================================================================
#define SD_CS    15
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_DET   34

bool sdReady = false;

// ================================================================
// WiFi CONFIG
// ================================================================
const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";

// ================================================================
// OLED
// ================================================================
#define OLED_SDA 21
#define OLED_SCL 22
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ================================================================
// BUTTONS
// ================================================================
#define BTN_UP      32
#define BTN_DOWN    33
#define BTN_SELECT  25
#define BTN_BACK    26
#define DEBOUNCE_MS 150
unsigned long lastBtnTime = 0;

// ================================================================
// nRF24 - VSPI
// ================================================================
#define NRF24_CE   4
#define NRF24_CSN  5
#define NRF24_SCK  18
#define NRF24_MOSI 23
#define NRF24_MISO 19
SPIClass vspi(VSPI);
RF24 radio(NRF24_CE, NRF24_CSN);

// ================================================================
// nRF24 CAPTURE SYSTEM
// ================================================================
#define NUM_CHANNELS          125
#define MAX_CAPTURED_PACKETS  64
#define CAPTURE_DURATION_MS   20
#define SCAN_REPS             80

struct CapturedPacket {
  uint8_t  data[32];
  uint8_t  length;
  uint8_t  channel;
  uint32_t timestamp;
};

int            chActivity[NUM_CHANNELS];
int            chPackets[NUM_CHANNELS];
int            totalPackets   = 0;
int            activeChannels = 0;
CapturedPacket captureBuffer[MAX_CAPTURED_PACKETS];
int            captureCount   = 0;

// ================================================================
// BLE TRACKER IDs
// ================================================================
#define APPLE_COMPANY_ID   0x004C
#define TILE_COMPANY_ID    0x00E0
#define SAMSUNG_COMPANY_ID 0x0075

// ================================================================
// BLE DEVICE STORAGE (for scanning)
// ================================================================
#define MAX_BLE_DEVICES 20
struct BLEDevInfo {
  String  address;
  String  name;
  int     rssi;
  bool    isTracker;
  String  trackerType;
  uint8_t addrType;
};
BLEDevInfo bleDevices[MAX_BLE_DEVICES];
int        bleDevCount = 0;

// ================================================================
// PICO BLE CLIENT STORAGE
// ================================================================
bool picoConnected = false;
NimBLEClient* pPicoClient = nullptr;
NimBLERemoteCharacteristic* pCommandCharacteristic = nullptr;
String picoAuthCode = "";

// ================================================================
// SYSTEM STATE
// ================================================================
bool wifiConnected = false;
bool nrfReady      = false;
bool bleReady      = false;

NimBLEScan* pBLEScan = nullptr;

// ================================================================
// SD FILE BROWSER
// ================================================================
#define MAX_SD_FILES 32
String sdFiles[MAX_SD_FILES];
int    sdFileCount = 0;
int    sdFileSel   = 0;
String selectedFile = "";

// ================================================================
// MENU SYSTEM
// ================================================================
enum MenuLevel {
  MENU_MAIN      = 0,
  MENU_WIFI      = 1,
  MENU_BLE_SCAN  = 2,
  MENU_PICO      = 3,
  MENU_RF24      = 4,
  MENU_SD        = 5,
  MENU_RESULT    = 6,
  MENU_SD_BROWSE = 7
};

MenuLevel currentMenu = MENU_MAIN;
int       menuIndex   = 0;
int       menuScroll  = 0;
bool      inResult    = false;
String    resultTitle = "";

const char* mainItems[] = {
  "1. WiFi",
  "2. BLE Scan",
  "3. Pico Control",
  "4. RF24",
  "5. SD Card",
  "6. Status"
};
const int mainCount = 6;

const char* wifiItems[] = {
  "Scan All",
  "Open Only",
  "Connect",
  "Save Last Scan",
  "< Back"
};
const int wifiCount = 5;

const char* bleScanItems[] = {
  "Scan Devices",
  "Find Trackers",
  "Save Last Scan",
  "< Back"
};
const int bleScanCount = 4;

const char* picoItems[] = {
  "Connect to Pico",
  "Disconnect",
  "Type Text",
  "Send Combo",
  "Run Payload",
  "Send Command",
  "Status",
  "< Back"
};
const int picoCount = 8;

const char* rf24Items[] = {
  "Scan Spectrum",
  "Capture Pkts",
  "Replay Pkts",
  "Save Capture",
  "< Back"
};
const int rf24Count = 5;

const char* sdItems[] = {
  "Browse Files",
  "Card Info",
  "Delete File",
  "< Back"
};
const int sdCount = 4;

// ================================================================
// RESULT STORAGE
// ================================================================
#define MAX_RESULT_LINES 64
String resultLines[MAX_RESULT_LINES];
int    resultLineCount = 0;

String lastWifiScan = "";
String lastBLEScan  = "";
String lastRF24Data = "";

// ================================================================
// FORWARD DECLARATIONS
// ================================================================
void drawProgress(const char* label, int percent);
void drawMenu();
void handleSelect();
void handleBack();
void runWifiScanAll(bool openOnly);
void runWifiConnect();
void runBLEScan(bool trackersOnly);
void runNRF24Scan();
void runNRF24Capture();
void runNRF24Replay();
void drawStatusScreen();
void drawResult();
void addResult(String line);
void clearResults(String title);
void showMessage(const char* l1, const char* l2 = "", const char* l3 = "");
void showBigMessage(const char* msg);
void initSD();
void runSDBrowse();
void runSDCardInfo();
void runSDDeleteFile();
void runSDViewFile(String filename);
void saveToSD(String filename, String data);
void saveWifiScan();
void saveBLEScan();
void saveRF24Data();
void drawSDBrowser();
String getTimestamp();

// PICO BLE Functions
void connectToPico();
void disconnectFromPico();
void sendCommandToPico(String command);
void sendTextToPico();
void sendComboToPico();
void runPicoPayload();
void sendCustomCommand();
void picoStatus();

// BLE Client Callbacks
class PicoClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("[PICO] Connected!");
        picoConnected = true;
        addResult("Connected to Pico!");
    }
    void onDisconnect(NimBLEClient* pClient) {
        Serial.println("[PICO] Disconnected");
        picoConnected = false;
        addResult("Pico disconnected");
    }
};

// ================================================================
// BUTTON HELPERS
// ================================================================
bool btnPressed(int pin) {
  if (millis() - lastBtnTime < DEBOUNCE_MS) return false;
  if (digitalRead(pin) == LOW) { lastBtnTime = millis(); return true; }
  return false;
}

// ================================================================
// OLED HELPERS
// ================================================================
void oledClear() { u8g2.clearBuffer(); }
void oledSend()  { u8g2.sendBuffer(); }

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

// ================================================================
// DRAW MENU LIST
// ================================================================
void drawMenuList(const char* title, const char** items, int count) {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  int maxVisible = 4;
  int startIdx   = 0;
  if (menuIndex >= maxVisible) startIdx = menuIndex - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < count; i++) {
    int itemIdx = startIdx + i;
    int y       = 14 + (i * 14);
    if (itemIdx == menuIndex) {
      u8g2.drawBox(0, y - 10, 128, 13);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(4, y, items[itemIdx]);
    u8g2.setDrawColor(1);
  }
  if (startIdx > 0)                  u8g2.drawTriangle(120, 3,  124, 3,  122, 0);
  if (startIdx + maxVisible < count) u8g2.drawTriangle(120, 61, 124, 61, 122, 64);
  oledSend();
}

// ================================================================
// DRAW SD FILE BROWSER
// ================================================================
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
    u8g2.drawStr(4, 48, "on SD card");
    oledSend();
    return;
  }

  int maxVisible = 4;
  int startIdx   = 0;
  if (sdFileSel >= maxVisible) startIdx = sdFileSel - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < sdFileCount; i++) {
    int idx = startIdx + i;
    int y   = 24 + (i * 12);
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

  if (startIdx > 0)                        u8g2.drawTriangle(120, 13, 124, 13, 122, 10);
  if (startIdx + maxVisible < sdFileCount) u8g2.drawTriangle(120, 61, 124, 61, 122, 64);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 63, "SEL=View  BCK=Back");
  oledSend();
}

// ================================================================
// DRAW MENU (dispatcher)
// ================================================================
void drawMenu() {
  switch (currentMenu) {
    case MENU_MAIN:      drawMenuList("[ Tank v3.0 ]", mainItems,  mainCount);  break;
    case MENU_WIFI:      drawMenuList("[ WIFI ]",      wifiItems,  wifiCount);  break;
    case MENU_BLE_SCAN:  drawMenuList("[ BLE SCAN ]",  bleScanItems, bleScanCount); break;
    case MENU_PICO:      drawMenuList("[ PICO ]",      picoItems,  picoCount); break;
    case MENU_RF24:      drawMenuList("[ RF24 ]",      rf24Items,  rf24Count);  break;
    case MENU_SD:        drawMenuList("[ SD CARD ]",   sdItems,    sdCount);    break;
    case MENU_SD_BROWSE: drawSDBrowser();                                        break;
    case MENU_RESULT:    drawResult();                                           break;
  }
}

// ================================================================
// DRAW RESULT SCREEN
// ================================================================
void drawResult() {
  oledClear();
  u8g2.setFont(u8g2_font_5x7_tf);
  int maxLines = 9;
  int lineH    = 7;
  int startY   = 7;
  for (int i = 0; i < maxLines && (menuScroll + i) < resultLineCount; i++) {
    u8g2.drawStr(2, startY + (i * lineH),
                 resultLines[menuScroll + i].c_str());
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

// ================================================================
// RESULT HELPERS
// ================================================================
void clearResults(String title) {
  resultLineCount = 0;
  menuScroll      = 0;
  resultTitle     = title;
  for (int i = 0; i < MAX_RESULT_LINES; i++) resultLines[i] = "";
}

void addResult(String line) {
  if (resultLineCount >= MAX_RESULT_LINES) return;
  if (line.length() > 24) line = line.substring(0, 23) + "~";
  resultLines[resultLineCount++] = line;
}

// ================================================================
// TIMESTAMP HELPER
// ================================================================
String getTimestamp() {
  unsigned long s = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "%02luh%02lum%02lus",
           s / 3600, (s % 3600) / 60, s % 60);
  return String(buf);
}

// ================================================================
// PICO BLE CONNECTION FUNCTIONS
// ================================================================
void connectToPico() {
    clearResults("CONNECT TO PICO");
    
    if (picoConnected) {
        addResult("Already connected!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    showMessage("Scanning for Pico", PICO_DEVICE_NAME, "10 seconds...");
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    NimBLEScanResults results = pScan->getResults(SCAN_DURATION_SECONDS * 1000, false);
    
    const NimBLEAdvertisedDevice* picoDevice = nullptr;
    bool found = false;
    String receivedAuthCode = "";
    
    for (int i = 0; i < results.getCount(); i++) {
        const NimBLEAdvertisedDevice* device = results.getDevice(i);
        String deviceName = String(device->getName().c_str());
        
        Serial.printf("Found device: %s\n", deviceName.c_str());
        
        if (deviceName.startsWith(PICO_DEVICE_NAME_PREFIX)) {
            picoDevice = device;
            found = true;
            receivedAuthCode = deviceName.substring(strlen(PICO_DEVICE_NAME_PREFIX));
            picoAuthCode = receivedAuthCode;
            addResult("Found Pico: " + deviceName);
            Serial.printf("[PICO] Found with auth code: %s\n", receivedAuthCode.c_str());
            break;
        }
    }
    
    if (!found) {
        addResult("Pico not found!");
        addResult("Make sure Pico is");
        addResult("powered and ready");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    showMessage("Found Pico!", "Connecting...", "");
    
    pPicoClient = NimBLEDevice::createClient();
    pPicoClient->setClientCallbacks(new PicoClientCallbacks(), false);
    
    if (!pPicoClient->connect(picoDevice)) {
        addResult("Connection failed!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    delay(1000);
    
    NimBLERemoteService* pService = pPicoClient->getService(BLE_UART_SERVICE_UUID);
    if (!pService) {
        addResult("UART service not found!");
        pPicoClient->disconnect();
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    pCommandCharacteristic = pService->getCharacteristic(BLE_UART_RX_CHAR_UUID);
    if (!pCommandCharacteristic) {
        addResult("Command char not found!");
        pPicoClient->disconnect();
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    delay(500);
    String authToSend = "AUTH:" + receivedAuthCode;
    Serial.printf("[PICO] Sending auth: %s\n", authToSend.c_str());
    
    if (pCommandCharacteristic->canWrite()) {
        pCommandCharacteristic->writeValue(authToSend.c_str(), authToSend.length());
        addResult("Auth sent to Pico");
        delay(500);
    }
    
    picoConnected = true;
    addResult("Connected to Pico!");
    addResult("Ready to send commands");
    
    delay(500);
    sendCommandToPico("STATUS");
    
    currentMenu = MENU_RESULT;
    drawResult();
}

void disconnectFromPico() {
    clearResults("DISCONNECT");
    
    if (pPicoClient) {
        pPicoClient->disconnect();
        picoConnected = false;
        addResult("Disconnected from Pico");
    } else {
        addResult("Not connected");
    }
    
    currentMenu = MENU_RESULT;
    drawResult();
}

void sendCommandToPico(String command) {
    if (!picoConnected || !pCommandCharacteristic) {
        addResult("Not connected to Pico!");
        return;
    }
    
    Serial.printf("[CMD] Sending: %s\n", command.c_str());
    pCommandCharacteristic->writeValue(command.c_str(), command.length());
    addResult("Sent: " + command.substring(0, 20));
}

void sendTextToPico() {
    if (!picoConnected) {
        clearResults("ERROR");
        addResult("Not connected to Pico!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    const char* presets[] = {
        "hello world", "cmd", "powershell", "notepad", 
        "whoami", "ipconfig /all", "ls -la", "uname -a",
        "cat /etc/passwd", "echo Hello"
    };
    int presetCount = 10;
    int presetSel = 0;
    
    bool picking = true;
    while (picking) {
        oledClear();
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawBox(0, 0, 128, 10);
        u8g2.setDrawColor(0);
        u8g2.drawStr(2, 8, "TYPE TEXT (preset)");
        u8g2.setDrawColor(1);
        
        int maxVisible = 6;
        int startIdx = 0;
        if (presetSel >= maxVisible) startIdx = presetSel - maxVisible + 1;
        
        for (int i = 0; i < maxVisible && (startIdx + i) < presetCount; i++) {
            int idx = startIdx + i;
            int y = 20 + (i * 9);
            if (idx == presetSel) {
                u8g2.drawBox(0, y - 7, 128, 9);
                u8g2.setDrawColor(0);
            }
            u8g2.drawStr(4, y, presets[idx]);
            u8g2.setDrawColor(1);
        }
        oledSend();
        
        if (btnPressed(BTN_UP)) presetSel = (presetSel > 0) ? presetSel - 1 : presetCount - 1;
        if (btnPressed(BTN_DOWN)) presetSel = (presetSel < presetCount - 1) ? presetSel + 1 : 0;
        if (btnPressed(BTN_SELECT)) picking = false;
        if (btnPressed(BTN_BACK)) {
            currentMenu = MENU_PICO;
            menuIndex = 0;
            drawMenu();
            return;
        }
        delay(50);
    }
    
    showMessage("Sending text...", presets[presetSel], "");
    sendCommandToPico("TYPE:" + String(presets[presetSel]));
    delay(100);
    
    clearResults("TEXT SENT");
    addResult("Text sent to Pico:");
    addResult(presets[presetSel]);
    currentMenu = MENU_RESULT;
    drawResult();
}

void sendComboToPico() {
    if (!picoConnected) {
        clearResults("ERROR");
        addResult("Not connected to Pico!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    const char* combos[] = {
        "CTRL+c", "CTRL+v", "CTRL+x", "CTRL+z", "CTRL+a",
        "GUI+r", "GUI+d", "ALT+f4", "CTRL+ALT+del", "CTRL+SHIFT+esc"
    };
    int comboCount = 10;
    int comboSel = 0;
    
    bool picking = true;
    while (picking) {
        oledClear();
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawBox(0, 0, 128, 10);
        u8g2.setDrawColor(0);
        u8g2.drawStr(2, 8, "SEND COMBO");
        u8g2.setDrawColor(1);
        
        int maxVisible = 6;
        int startIdx = 0;
        if (comboSel >= maxVisible) startIdx = comboSel - maxVisible + 1;
        
        for (int i = 0; i < maxVisible && (startIdx + i) < comboCount; i++) {
            int idx = startIdx + i;
            int y = 20 + (i * 9);
            if (idx == comboSel) {
                u8g2.drawBox(0, y - 7, 128, 9);
                u8g2.setDrawColor(0);
            }
            u8g2.drawStr(4, y, combos[idx]);
            u8g2.setDrawColor(1);
        }
        oledSend();
        
        if (btnPressed(BTN_UP)) comboSel = (comboSel > 0) ? comboSel - 1 : comboCount - 1;
        if (btnPressed(BTN_DOWN)) comboSel = (comboSel < comboCount - 1) ? comboSel + 1 : 0;
        if (btnPressed(BTN_SELECT)) picking = false;
        if (btnPressed(BTN_BACK)) {
            currentMenu = MENU_PICO;
            menuIndex = 0;
            drawMenu();
            return;
        }
        delay(50);
    }
    
    showMessage("Sending combo...", combos[comboSel], "");
    sendCommandToPico("COMBO:" + String(combos[comboSel]));
    delay(100);
    
    clearResults("COMBO SENT");
    addResult("Combo sent to Pico:");
    addResult(combos[comboSel]);
    currentMenu = MENU_RESULT;
    drawResult();
}

void sendCustomCommand() {
    if (!picoConnected) {
        clearResults("ERROR");
        addResult("Not connected to Pico!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    const char* customCmds[] = {
        "PING", "STATUS", "DELAY:500", "ENTER", "BACKSPACE", "TAB", "ESC", "SPACE"
    };
    int cmdCount = 8;
    int cmdSel = 0;
    
    bool picking = true;
    while (picking) {
        oledClear();
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawBox(0, 0, 128, 10);
        u8g2.setDrawColor(0);
        u8g2.drawStr(2, 8, "CUSTOM COMMAND");
        u8g2.setDrawColor(1);
        
        int maxVisible = 6;
        int startIdx = 0;
        if (cmdSel >= maxVisible) startIdx = cmdSel - maxVisible + 1;
        
        for (int i = 0; i < maxVisible && (startIdx + i) < cmdCount; i++) {
            int idx = startIdx + i;
            int y = 20 + (i * 9);
            if (idx == cmdSel) {
                u8g2.drawBox(0, y - 7, 128, 9);
                u8g2.setDrawColor(0);
            }
            u8g2.drawStr(4, y, customCmds[idx]);
            u8g2.setDrawColor(1);
        }
        oledSend();
        
        if (btnPressed(BTN_UP)) cmdSel = (cmdSel > 0) ? cmdSel - 1 : cmdCount - 1;
        if (btnPressed(BTN_DOWN)) cmdSel = (cmdSel < cmdCount - 1) ? cmdSel + 1 : 0;
        if (btnPressed(BTN_SELECT)) picking = false;
        if (btnPressed(BTN_BACK)) {
            currentMenu = MENU_PICO;
            menuIndex = 0;
            drawMenu();
            return;
        }
        delay(50);
    }
    
    sendCommandToPico(customCmds[cmdSel]);
    
    clearResults("COMMAND SENT");
    addResult("Sent: " + String(customCmds[cmdSel]));
    currentMenu = MENU_RESULT;
    drawResult();
}

void runPicoPayload() {
    if (!picoConnected) {
        clearResults("ERROR");
        addResult("Not connected to Pico!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    if (!sdReady) {
        clearResults("ERROR");
        addResult("SD card not ready!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    const char* payloadFolder = "/tank/ducky";
    if (!SD.exists(payloadFolder)) SD.mkdir(payloadFolder);
    
    String payloads[30];
    int payloadCount = 0;
    
    File dir = SD.open(payloadFolder);
    if (dir) {
        File entry = dir.openNextFile();
        while (entry && payloadCount < 30) {
            if (!entry.isDirectory()) {
                String name = String(entry.name());
                if (name.endsWith(".txt") || name.endsWith(".duck")) {
                    payloads[payloadCount++] = String(payloadFolder) + "/" + name;
                }
            }
            entry = dir.openNextFile();
        }
        dir.close();
    }
    
    if (payloadCount == 0) {
        clearResults("ERROR");
        addResult("No payloads found!");
        addResult("Add .txt files to");
        addResult("/tank/ducky/ on SD");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    int payloadSel = 0;
    bool picking = true;
    while (picking) {
        oledClear();
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawBox(0, 0, 128, 10);
        u8g2.setDrawColor(0);
        u8g2.drawStr(2, 8, "SELECT PAYLOAD");
        u8g2.setDrawColor(1);
        
        int maxVisible = 6;
        int startIdx = 0;
        if (payloadSel >= maxVisible) startIdx = payloadSel - maxVisible + 1;
        
        for (int i = 0; i < maxVisible && (startIdx + i) < payloadCount; i++) {
            int idx = startIdx + i;
            int y = 20 + (i * 9);
            String name = payloads[idx];
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.length() > 20) name = name.substring(0, 19) + "~";
            
            if (idx == payloadSel) {
                u8g2.drawBox(0, y - 7, 128, 9);
                u8g2.setDrawColor(0);
            }
            u8g2.drawStr(4, y, name.c_str());
            u8g2.setDrawColor(1);
        }
        oledSend();
        
        if (btnPressed(BTN_UP)) payloadSel = (payloadSel > 0) ? payloadSel - 1 : payloadCount - 1;
        if (btnPressed(BTN_DOWN)) payloadSel = (payloadSel < payloadCount - 1) ? payloadSel + 1 : 0;
        if (btnPressed(BTN_SELECT)) picking = false;
        if (btnPressed(BTN_BACK)) {
            currentMenu = MENU_PICO;
            menuIndex = 0;
            drawMenu();
            return;
        }
        delay(50);
    }
    
    String filename = payloads[payloadSel];
    String shortName = filename.substring(filename.lastIndexOf('/') + 1);
    
    showMessage("Streaming payload...", shortName.c_str(), "");
    
    File f = SD.open(filename, FILE_READ);
    if (!f) {
        clearResults("ERROR");
        addResult("Cannot open file!");
        currentMenu = MENU_RESULT;
        drawResult();
        return;
    }
    
    sendCommandToPico("PAYLOAD:START:" + shortName);
    delay(500);
    
    int lineCount = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0 && !line.startsWith("//") && !line.startsWith("#") && !line.startsWith("REM")) {
            sendCommandToPico("LINE:" + line);
            delay(100);
            lineCount++;
            
            if (lineCount % 10 == 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d lines sent", lineCount);
                showMessage("Streaming...", buf, "");
            }
        }
    }
    
    sendCommandToPico("PAYLOAD:END");
    f.close();
    
    clearResults("PAYLOAD COMPLETE");
    addResult("Sent " + String(lineCount) + " lines");
    addResult("to Pico");
    addResult(shortName);
    currentMenu = MENU_RESULT;
    drawResult();
}

void picoStatus() {
    clearResults("PICO STATUS");
    if (picoConnected) {
        addResult("Connected to Pico!");
        addResult("");
        addResult("Commands available:");
        addResult("- TYPE:text");
        addResult("- COMBO:key+key");
        addResult("- DELAY:ms");
        addResult("- ENTER/BACKSPACE/TAB");
        addResult("- GUI/CTRL/ALT/SHIFT");
        addResult("- PAYLOAD streaming");
    } else {
        addResult("Not connected");
        addResult("Use 'Connect to'");
        addResult("connect to Pico");
    }
    currentMenu = MENU_RESULT;
    drawResult();
}

// ================================================================
// WIFI FUNCTIONS
// ================================================================
void runWifiScanAll(bool openOnly) {
  showMessage("Scanning WiFi...", "Please wait", "");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks(false, true);
  String title = openOnly ? "WIFI: OPEN ONLY" : "WIFI: ALL NETWORKS";
  clearResults(title);

  lastWifiScan = "=== WiFi Scan [" + getTimestamp() + "] ===\n";
  lastWifiScan += openOnly ? "Mode: Open Only\n" : "Mode: All Networks\n";
  lastWifiScan += "Found: " + String(n) + "\n\n";

  if (n == 0) {
    addResult("No networks found");
  } else {
    for (int i = 0; i < n; i++) {
      bool isOpen = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
      if (openOnly && !isOpen) continue;
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) ssid = "[Hidden]";
      addResult((isOpen ? "! " : "  ") + ssid);
      addResult("  " + String(WiFi.RSSI(i)) + "dBm");
      addResult("  --------");
    }
  }
  WiFi.scanDelete();
  currentMenu = MENU_RESULT;
  drawResult();
}

void runWifiConnect() {
  clearResults("WIFI: CONNECT");
  if (wifiConnected) {
    addResult("Already connected!");
    addResult(WiFi.SSID().c_str());
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  showMessage("Connecting...", WIFI_SSID, "Please wait");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    addResult("Connected!");
    addResult(WiFi.SSID().c_str());
    addResult(WiFi.localIP().toString().c_str());
  } else {
    addResult("Connection failed");
  }
  currentMenu = MENU_RESULT;
  drawResult();
}

// ================================================================
// BLE SCAN FUNCTIONS
// ================================================================
String detectTracker(const NimBLEAdvertisedDevice* dev) {
  if (dev->haveManufacturerData()) {
    std::string mfData = dev->getManufacturerData();
    if (mfData.length() >= 2) {
      uint16_t companyID = (uint8_t)mfData[1] << 8 | (uint8_t)mfData[0];
      if (companyID == APPLE_COMPANY_ID) {
        if (mfData.length() >= 4 && (uint8_t)mfData[2] == 0x12 && (uint8_t)mfData[3] == 0x19) return "AirTag";
        return "Apple";
      }
      if (companyID == TILE_COMPANY_ID) return "Tile";
      if (companyID == SAMSUNG_COMPANY_ID) return "SmartTag";
    }
  }
  if (dev->haveServiceUUID()) {
    if (dev->isAdvertisingService(NimBLEUUID("FEED"))) return "Tile";
    if (dev->isAdvertisingService(NimBLEUUID("FD5A"))) return "SmartTag";
  }
  return "";
}

class MyScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) {
    if (bleDevCount >= MAX_BLE_DEVICES) return;
    String addr = String(dev->getAddress().toString().c_str());
    for (int i = 0; i < bleDevCount; i++) {
      if (bleDevices[i].address == addr) {
        bleDevices[i].rssi = dev->getRSSI();
        return;
      }
    }
    bleDevices[bleDevCount].address = addr;
    bleDevices[bleDevCount].rssi = dev->getRSSI();
    bleDevices[bleDevCount].name = String(dev->getName().c_str());
    bleDevices[bleDevCount].addrType = dev->getAddressType();
    String tracker = detectTracker(dev);
    bleDevices[bleDevCount].isTracker = (tracker.length() > 0);
    bleDevices[bleDevCount].trackerType = tracker;
    bleDevCount++;
  }
  void onScanEnd(NimBLEScanResults results) {}
};

// ================================================================
// FIX: Scan is configured fresh on every call. pBLEScan is left as
//      nullptr at boot so nothing can auto-trigger in loop().
// ================================================================
void runBLEScan(bool trackersOnly) {
  showMessage("Scanning BLE...", "5 seconds", "Please wait");
  bleDevCount = 0;

  // Always configure the scan object here, never at boot.
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyScanCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();

  NimBLEScanResults results = pBLEScan->getResults(5000, false);

  for (int i = 0; i < results.getCount() && bleDevCount < MAX_BLE_DEVICES; i++) {
    const NimBLEAdvertisedDevice* dev = results.getDevice(i);
    String addr = String(dev->getAddress().toString().c_str());

    bool exists = false;
    for (int j = 0; j < bleDevCount; j++) {
      if (bleDevices[j].address == addr) { exists = true; break; }
    }
    if (exists) continue;

    bleDevices[bleDevCount].address    = addr;
    bleDevices[bleDevCount].name       = String(dev->getName().c_str());
    bleDevices[bleDevCount].rssi       = dev->getRSSI();
    bleDevices[bleDevCount].addrType   = dev->getAddressType();
    String tracker = detectTracker(dev);
    bleDevices[bleDevCount].isTracker   = (tracker.length() > 0);
    bleDevices[bleDevCount].trackerType = tracker;
    bleDevCount++;
  }
  pBLEScan->clearResults();

  clearResults(trackersOnly ? "BLE: TRACKERS" : "BLE: ALL DEVICES");

  int shown = 0;
  for (int i = 0; i < bleDevCount; i++) {
    BLEDevInfo& d = bleDevices[i];
    if (trackersOnly && !d.isTracker) continue;
    String display = d.name.length() > 0 ? d.name : d.address;
    addResult((d.isTracker ? "! " : "  ") + display);
    addResult("  " + String(d.rssi) + "dBm" + (d.isTracker ? " [" + d.trackerType + "]" : ""));
    addResult("  --------");
    shown++;
  }

  if (shown == 0) addResult(trackersOnly ? "No trackers found" : "No devices found");

  currentMenu = MENU_RESULT;
  drawResult();
}

// ================================================================
// NRF24 FUNCTIONS
// ================================================================
void runNRF24Scan() {
  clearResults("RF24: SPECTRUM");
  addResult("nRF24 Ready");
  addResult("Scan feature ready");
  currentMenu = MENU_RESULT;
  drawResult();
}

void runNRF24Capture() {
  clearResults("RF24: CAPTURE");
  addResult("nRF24 Ready");
  currentMenu = MENU_RESULT;
  drawResult();
}

void runNRF24Replay() {
  clearResults("RF24: REPLAY");
  addResult("nRF24 Ready");
  currentMenu = MENU_RESULT;
  drawResult();
}

// ================================================================
// SD CARD FUNCTIONS
// ================================================================
void initSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  
  if (!SD.begin(SD_CS, vspi)) {
    sdReady = false;
    Serial.println("[SD] Init failed");
    return;
  }
  
  sdReady = true;
  Serial.println("[SD] Init OK");
  
  if (!SD.exists("/tank")) SD.mkdir("/tank");
  if (!SD.exists("/tank/wifi")) SD.mkdir("/tank/wifi");
  if (!SD.exists("/tank/ble")) SD.mkdir("/tank/ble");
  if (!SD.exists("/tank/rf24")) SD.mkdir("/tank/rf24");
  if (!SD.exists("/tank/ducky")) SD.mkdir("/tank/ducky");
}

void saveToSD(String filename, String data) {
  if (!sdReady) {
    clearResults("SD: SAVE ERROR");
    addResult("SD card not ready!");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  
  File f = SD.open(filename, FILE_WRITE);
  if (!f) {
    clearResults("SD: SAVE ERROR");
    addResult("Could not open file");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  
  f.println(data);
  f.close();
  
  clearResults("SD: SAVED OK");
  int lastSlash = filename.lastIndexOf('/');
  String shortName = lastSlash >= 0 ? filename.substring(lastSlash + 1) : filename;
  addResult("File saved: " + shortName);
  currentMenu = MENU_RESULT;
  drawResult();
}

void saveWifiScan() {
  if (lastWifiScan.length() == 0) {
    clearResults("SD: SAVE WIFI");
    addResult("No WiFi scan data!");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  saveToSD("/tank/wifi/wifi_" + getTimestamp() + ".txt", lastWifiScan);
}

void saveBLEScan() {
  if (lastBLEScan.length() == 0) {
    clearResults("SD: SAVE BLE");
    addResult("No BLE scan data!");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  saveToSD("/tank/ble/ble_" + getTimestamp() + ".txt", lastBLEScan);
}

void saveRF24Data() { saveToSD("/tank/rf24/rf24_" + getTimestamp() + ".txt", "RF24 Data"); }

void runSDBrowse() {
  if (!sdReady) {
    clearResults("SD: BROWSE");
    addResult("SD card not ready!");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  
  sdFileCount = 0;
  sdFileSel = 0;
  
  const char* folders[] = { "/tank/wifi", "/tank/ble", "/tank/rf24", "/tank/ducky" };
  for (int f = 0; f < 4; f++) {
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
  
  if (sdFileCount == 0) {
    clearResults("SD: BROWSE");
    addResult("No saved files yet.");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  
  currentMenu = MENU_SD_BROWSE;
  drawSDBrowser();
}

void runSDViewFile(String filename) {
  int lastSlash = filename.lastIndexOf('/');
  String shortName = lastSlash >= 0 ? filename.substring(lastSlash + 1) : filename;
  clearResults("SD: " + shortName);
  
  File f = SD.open(filename, FILE_READ);
  if (!f) {
    addResult("Cannot open file!");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  
  String line = "";
  while (f.available() && resultLineCount < MAX_RESULT_LINES) {
    char c = f.read();
    if (c == '\n') {
      if (line.length() > 0) { addResult(line); line = ""; }
    } else if (c != '\r') {
      line += c;
    }
  }
  if (line.length() > 0) addResult(line);
  f.close();
  
  currentMenu = MENU_RESULT;
  drawResult();
}

void runSDCardInfo() {
  clearResults("SD: CARD INFO");
  if (!sdReady) {
    addResult("SD not ready!");
  } else {
    uint32_t totalMB = (uint32_t)(SD.totalBytes() / (1024 * 1024));
    uint32_t usedMB = (uint32_t)(SD.usedBytes() / (1024 * 1024));
    addResult("Total: " + String(totalMB) + " MB");
    addResult("Used: " + String(usedMB) + " MB");
    addResult("Free: " + String(totalMB - usedMB) + " MB");
  }
  currentMenu = MENU_RESULT;
  drawResult();
}

void runSDDeleteFile() {
  if (!sdReady) {
    clearResults("SD: DELETE");
    addResult("SD not ready!");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  
  if (selectedFile.length() == 0) {
    clearResults("SD: DELETE");
    addResult("No file selected!");
    currentMenu = MENU_RESULT;
    drawResult();
    return;
  }
  
  if (SD.remove(selectedFile)) {
    clearResults("SD: DELETE");
    addResult("Deleted: " + selectedFile);
    selectedFile = "";
  } else {
    clearResults("SD: DELETE");
    addResult("Delete failed!");
  }
  currentMenu = MENU_RESULT;
  drawResult();
}

// ================================================================
// STATUS SCREEN
// ================================================================
void drawStatusScreen() {
  clearResults("STATUS");
  addResult("Tank v3.0 (Pico Commander)");
  addResult("----------------");
  addResult("WiFi: " + String(wifiConnected ? "Connected" : "Disconnected"));
  if (wifiConnected) {
    addResult("  " + WiFi.SSID());
    addResult("  " + WiFi.localIP().toString());
  }
  addResult("BLE Scanner: " + String(bleReady ? "Ready" : "Failed"));
  addResult("Pico Connection: " + String(picoConnected ? "Connected" : "Disconnected"));
  addResult("nRF24: " + String(nrfReady ? "Ready" : "Not found"));
  addResult("SD: " + String(sdReady ? "Ready" : "Not found"));
  addResult("----------------");
  addResult("CPU: " + String(ESP.getCpuFreqMHz()) + "MHz");
  addResult("RAM: " + String(ESP.getFreeHeap() / 1024) + "KB free");
  if (picoConnected) {
    addResult("----------------");
    addResult("Pico ready to");
    addResult("execute commands!");
  }
  currentMenu = MENU_RESULT;
  drawResult();
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
  u8g2.sendBuffer();
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
        case 2: currentMenu = MENU_PICO; menuIndex = 0; break;
        case 3: currentMenu = MENU_RF24; menuIndex = 0; break;
        case 4: currentMenu = MENU_SD; menuIndex = 0; break;
        case 5: drawStatusScreen(); currentMenu = MENU_RESULT; break;
      }
      break;
      
    case MENU_WIFI:
      switch (menuIndex) {
        case 0: runWifiScanAll(false); break;
        case 1: runWifiScanAll(true); break;
        case 2: runWifiConnect(); break;
        case 3: saveWifiScan(); break;
        case 4: currentMenu = MENU_MAIN; menuIndex = 0; break;
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
      
    case MENU_PICO:
      switch (menuIndex) {
        case 0: connectToPico(); break;
        case 1: disconnectFromPico(); break;
        case 2: sendTextToPico(); break;
        case 3: sendComboToPico(); break;
        case 4: runPicoPayload(); break;
        case 5: sendCustomCommand(); break;
        case 6: picoStatus(); break;
        case 7: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;
      
    case MENU_RF24:
      switch (menuIndex) {
        case 0: runNRF24Scan(); break;
        case 1: runNRF24Capture(); break;
        case 2: runNRF24Replay(); break;
        case 3: saveRF24Data(); break;
        case 4: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
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
      if (sdFileCount > 0) {
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
    case MENU_PICO:
    case MENU_RF24:
    case MENU_SD:
      currentMenu = MENU_MAIN;
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
// BOOT SPLASH
// FIX: After BLE init, call stop() + clearResults() to ensure the
//      scan is NOT running or armed when we return to the main menu.
// ================================================================
void drawSplash() {
    // Simple logo
    oledClear();
    u8g2.setFont(u8g2_font_7x14B_tf);
    int w = u8g2.getStrWidth("Tank");
    u8g2.drawStr((128 - w) / 2, 30, "Tank");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(82, 42, "v3.0");
    u8g2.drawStr(2, 55, "PICO CMD");
    oledSend();
    delay(800);
    
    // Init BLE - only initialise the stack here, do NOT configure or
    // start the scan. Scan is configured fresh each time in runBLEScan()
    // to prevent NimBLE from auto-resolving a stale scan on first loop.
    drawProgress("Init BLE...", 30);
    NimBLEDevice::init("Tank-Commander");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    pBLEScan = nullptr; // will be fetched and configured in runBLEScan()
    bleReady = true;
    drawProgress("BLE OK", 60);
    delay(200);
    
    // Init nRF24
    drawProgress("Init nRF24...", 70);
    vspi.begin(NRF24_SCK, NRF24_MISO, NRF24_MOSI, NRF24_CSN);
    if (radio.begin(&vspi)) {
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setChannel(108);
        radio.setAutoAck(false);
        radio.setRetries(0, 0);
        radio.stopListening();
        nrfReady = true;
    }
    drawProgress(nrfReady ? "nRF24 OK" : "nRF24 Failed", 80);
    delay(200);
    
    // Init SD
    drawProgress("Init SD Card...", 90);
    initSD();
    drawProgress(sdReady ? "SD OK" : "SD Failed", 100);
    delay(400);
    
    showBigMessage("[ READY ]");
    delay(800);
    oledClear();
    oledSend();
    
    // WiFi left off at boot - user connects manually via menu.
    // Do NOT call WiFi.mode() here; on ESP32 it triggers a background
    // scan that auto-populates results and skips the main menu.
    WiFi.mode(WIFI_OFF);
    WiFi.disconnect(true);
    wifiConnected = false;
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
    } else {
      int count = 0;
      switch (currentMenu) {
        case MENU_MAIN: count = mainCount; break;
        case MENU_WIFI: count = wifiCount; break;
        case MENU_BLE_SCAN: count = bleScanCount; break;
        case MENU_PICO: count = picoCount; break;
        case MENU_RF24: count = rf24Count; break;
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
    } else {
      int count = 0;
      switch (currentMenu) {
        case MENU_MAIN: count = mainCount; break;
        case MENU_WIFI: count = wifiCount; break;
        case MENU_BLE_SCAN: count = bleScanCount; break;
        case MENU_PICO: count = picoCount; break;
        case MENU_RF24: count = rf24Count; break;
        case MENU_SD: count = sdCount; break;
        default: break;
      }
      if (count > 0) {
        if (menuIndex < count - 1) menuIndex++;
        else menuIndex = 0;
        redraw = true;
      }
    }
  }
  
  if (btnPressed(BTN_SELECT)) { handleSelect(); redraw = true; }
  if (btnPressed(BTN_BACK)) { handleBack(); redraw = true; }
  
  if (redraw) drawMenu();
  
  delay(20);
}
