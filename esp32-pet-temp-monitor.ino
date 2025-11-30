#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#define IR_RECEIVE_PIN 15          // IR receiver data pin on ESP32
#include <IRremote.hpp>            

#include <math.h>
#include <string.h>
#include <ctype.h>

#define DHTPIN 4 // Temp sensor data pin
#define DHTTYPE DHT11 // Change DHT TYPE according to your temp sensor

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA 21 // Change it according to your pin
#define I2C_SCL 22 // Change it according to your pin

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHTPIN, DHTTYPE);


const char* ntpServer = "pool.ntp.org";


long gmtOffset_sec      = 8 * 3600;  // CHANGE TO YOUR LOCAL OFFSET
int  daylightOffset_sec = 0;

char localTimeStr[9] = "--:--:--";
unsigned long lastTimeUpdate = 0;
const unsigned long TIME_UPDATE_INTERVAL = 1000; 


char wifiSSID[33] = "";
char wifiPASS[65] = "";
bool wifiConnected = false;

const char* webhookBase = "https://discord.com/api/webhooks/";

// You can set these directly in the IDE (recommended default) or via settings
char webhookId[32]    = "";  // Put numeric ID part here if you want
char webhookToken[96] = "";  // Put token part here if you want
char webhookTestStatus[20] = "Ready";

float lastTempC = NAN;
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 2000; // 2s

bool useFahrenheit = false;


struct PetProfile {
  const char* name;
  float minC;  
  float maxC;  
};

// You may adjust the range
PetProfile pets[] = {
  { "Dog",    16.0, 28.0 },
  { "Cat",    21.0, 27.0 },
  { "Rabbit", 15.0, 26.0 },
  { "Reptile",23.0, 32.0 }
};

const int NUM_PETS = sizeof(pets) / sizeof(pets[0]);
int currentPetIndex = 0;


const int MAX_VISIBLE_PETS = 3;
int petViewOffset = 0;


int lastAlertState = 0;



enum ScreenState {
  SCREEN_CALIBRATE,
  SCREEN_MAIN,
  SCREEN_SETTINGS_MENU,
  SCREEN_SETTING_UNIT,
  SCREEN_KEYBOARD,
  SCREEN_SETTING_PET,
  SCREEN_WIFI_CONNECTING,
  SCREEN_WIFI_SCAN,
  SCREEN_WEBHOOK_SETUP_TOKEN, 
  SCREEN_WEBHOOK_TEST
};

ScreenState screenState = SCREEN_CALIBRATE;

enum Key {
  KEY_NONE,
  KEY_UP,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_OK,
  KEY_BACK
};

struct IRKeyMap {
  uint32_t up;
  uint32_t down;
  uint32_t left;
  uint32_t right;
  uint32_t ok;
  uint32_t back;
};

IRKeyMap irMap = {0, 0, 0, 0, 0, 0};

const int CALIB_STEPS = 6;
int calibStep = 0;
const char* calibNames[CALIB_STEPS] = { "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK" };


int settingsMenuIndex = 0;
const int SETTINGS_MENU_COUNT = 5;


const int KEYBOARD_ROWS = 4;
const char* keyboardRows[KEYBOARD_ROWS] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJKL",
  "<ZXCVBNM_^>" 
};

int kbRow = 0;
int kbCol = 0;

char*       editingBuffer = NULL;
int         editingMaxLen = 0;
const char* editingLabel  = "";


ScreenState keyboardNextDone   = SCREEN_SETTINGS_MENU;
ScreenState keyboardNextCancel = SCREEN_SETTINGS_MENU;


bool shiftOn = false;  



bool wifiConnectingStarted = false;
bool lastConnectSuccess    = false;



#define MAX_SCANNED_SSIDS 8
#define MAX_VISIBLE_SSIDS 4


int    scannedCount         = 0;
String scannedSSIDs[MAX_SCANNED_SSIDS];
int    scannedSelectedIndex = 0;
int    scanViewOffset       = 0;  
bool   scanPerformed        = false;


uint32_t readIrCodeRaw() {
  if (IrReceiver.decode()) {
    uint32_t code = IrReceiver.decodedIRData.decodedRawData;
    IrReceiver.resume();

    if (code == 0xFFFFFFFF) {
      return 0;
    }

    
    Serial.print("IR code: 0x");
    Serial.println(code, HEX);

    return code;
  }
  return 0;
}

Key mapCodeToKey(uint32_t code) {
  if (code == irMap.up)    return KEY_UP;
  if (code == irMap.down)  return KEY_DOWN;
  if (code == irMap.left)  return KEY_LEFT;
  if (code == irMap.right) return KEY_RIGHT;
  if (code == irMap.ok)    return KEY_OK;
  if (code == irMap.back)  return KEY_BACK;
  return KEY_NONE;
}

Key pollKey() {
  uint32_t code = readIrCodeRaw();
  if (code == 0) return KEY_NONE;
  return mapCodeToKey(code);
}


void updateLocalTimeString() {
  if (!wifiConnected) {
    strcpy(localTimeStr, "--:--:--");
    return;
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(localTimeStr, sizeof(localTimeStr), "%H:%M:%S", &timeinfo);
  } else {
    Serial.println("getLocalTime failed");
    strcpy(localTimeStr, "--:--:--");
  }
}

void updateTimeIfNeeded() {
  if (!wifiConnected) return;

  if (lastTimeUpdate == 0 || millis() - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
    lastTimeUpdate = millis();
    updateLocalTimeString();
  }
}



bool sendDiscordMessage(const String& msg) {
  if (!wifiConnected) return false;
  if (webhookId[0] == '\0' || webhookToken[0] == '\0') return false;

  WiFiClientSecure client;
  client.setInsecure(); 

  HTTPClient http;
  String url = String(webhookBase) + webhookId + "/" + webhookToken;

  if (!http.begin(client, url)) {
    Serial.println("Discord http.begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  String payload = "{\"content\":\"" + msg + "\"}";
  int httpCode = http.POST(payload);
  Serial.print("Discord POST code: ");
  Serial.println(httpCode);

  http.end();
  return (httpCode >= 200 && httpCode < 300);
}

void sendDiscordAlert(int alertState, float tempC) {
  String petName = pets[currentPetIndex].name;
  String stateStr = (alertState > 0) ? "too HOT" : "too COLD";
  String msg = "⚠ " + petName + " is " + stateStr +
               " (" + String(tempC, 1) + "C)";
  sendDiscordMessage(msg);
}

bool sendDiscordTestMessage() {
  String msg = "Test webhook from ESP32 pet monitor.";
  return sendDiscordMessage(msg);
}



void checkPetAlert() {
  if (isnan(lastTempC)) return;

  PetProfile &p = pets[currentPetIndex];
  int newState = 0;
  if (lastTempC < p.minC) newState = -1;
  else if (lastTempC > p.maxC) newState = 1;

  if (newState != lastAlertState) {
    lastAlertState = newState;
    if (newState != 0) {
      sendDiscordAlert(newState, lastTempC);
    }
  }
}



void updateTemperature() {
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = millis();
    float t = dht.readTemperature(); // Celsius
    if (!isnan(t)) {
      lastTempC = t;
      checkPetAlert();
    }
  }
}



void drawCalibrationScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("IR Remote Setup");
  display.println("");

  if (calibStep < CALIB_STEPS) {
    display.print("Press ");
    display.println(calibNames[calibStep]);
    display.println("");
    display.println("Point remote at");
    display.println("receiver and press");
    display.println("that button once.");
  } else {
    display.println("Done!");
    display.println("");
    display.println("Starting...");
  }

  display.display();
}

void handleCalibration() {
  drawCalibrationScreen();

  if (calibStep >= CALIB_STEPS) {
    delay(800);
    screenState = SCREEN_MAIN;
    return;
  }

  uint32_t code = readIrCodeRaw();
  if (code != 0) {
    switch (calibStep) {
      case 0: irMap.up    = code; break;
      case 1: irMap.down  = code; break;
      case 2: irMap.left  = code; break;
      case 3: irMap.right = code; break;
      case 4: irMap.ok    = code; break;
      case 5: irMap.back  = code; break;
    }
    calibStep++;
    delay(300); 
  }
}


void drawMainScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  
  display.setCursor(0, 0);
  display.print("Temp: ");
  if (!isnan(lastTempC)) {
    float shown = lastTempC;
    char unitChar = 'C';
    if (useFahrenheit) {
      shown = lastTempC * 9.0 / 5.0 + 32.0;
      unitChar = 'F';
    }
    display.print(shown, 1);
    display.print(" ");
    display.print(unitChar);
  } else {
    display.print("----");
  }

  
  display.setCursor(0, 16);
  display.print("Time: ");
  display.print(localTimeStr);


  display.setCursor(0, 28);
  display.print("Pet: ");
  display.print(pets[currentPetIndex].name);
  display.print(" ");
  if (lastAlertState == 0) {
    display.print("OK");
  } else if (lastAlertState < 0) {
    display.print("COLD!");
  } else {
    display.print("HOT!");
  }


  display.setCursor(0, 40);
  if (!wifiConnected) {
    display.print("WiFi not connected");
  } else {
    display.print("WiFi OK ");
    display.print(WiFi.localIP());
  }

  
  display.setCursor(0, 56);
  display.print("OK: Settings");

  display.display();
}

void handleMainScreen() {
  drawMainScreen();

  Key k = pollKey();
  if (k == KEY_OK) {
    settingsMenuIndex = 0;
    screenState = SCREEN_SETTINGS_MENU;
    delay(200);
  }
}


void drawSettingsMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Settings");
  display.println("");

  const char* items[SETTINGS_MENU_COUNT] = {
    "Temp unit (C/F)",
    "WiFi setup",
    "Pet type",
    "Webhook setup",
    "Webhook test"
  };

  for (int i = 0; i < SETTINGS_MENU_COUNT; i++) {
    if (i == settingsMenuIndex) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(items[i]);
  }

  display.setCursor(0, 56);
  display.print("BACK: Main");

  display.display();
}

void handleSettingsMenu() {
  drawSettingsMenu();
  Key k = pollKey();

  if (k == KEY_UP && settingsMenuIndex > 0) {
    settingsMenuIndex--;
    delay(150);
  } else if (k == KEY_DOWN && settingsMenuIndex < SETTINGS_MENU_COUNT - 1) {
    settingsMenuIndex++;
    delay(150);
  } else if (k == KEY_OK) {
    switch (settingsMenuIndex) {
      case 0:
        screenState = SCREEN_SETTING_UNIT;
        break;
      case 1: 
        scanPerformed        = false;
        scannedSelectedIndex = 0;
        scanViewOffset       = 0;
        screenState          = SCREEN_WIFI_SCAN;
        break;
      case 2: 
        screenState = SCREEN_SETTING_PET;
        break;
      case 3: 
        editingBuffer      = webhookId;
        editingMaxLen      = sizeof(webhookId);
        editingLabel       = "ID";
        kbRow              = 0;
        kbCol              = 0;
        shiftOn            = false;
        keyboardNextDone   = SCREEN_WEBHOOK_SETUP_TOKEN; 
        keyboardNextCancel = SCREEN_SETTINGS_MENU;       
        screenState        = SCREEN_KEYBOARD;
        break;
      case 4: 
        strncpy(webhookTestStatus, "Ready", sizeof(webhookTestStatus));
        webhookTestStatus[sizeof(webhookTestStatus)-1] = '\0';
        screenState = SCREEN_WEBHOOK_TEST;
        break;
    }
    delay(200);
  } else if (k == KEY_BACK) {
    screenState = SCREEN_MAIN;
    delay(200);
  }
}



void drawUnitScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Temperature Unit");
  display.println("");

  display.print("Current: ");
  display.println(useFahrenheit ? "Fahrenheit" : "Celsius");
  display.println("");
  display.println("UP/DOWN: toggle");
  display.println("OK/BACK: return");

  display.display();
}

void handleUnitScreen() {
  drawUnitScreen();
  Key k = pollKey();

  if (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT) {
    useFahrenheit = !useFahrenheit;
    delay(200);
  } else if (k == KEY_OK || k == KEY_BACK) {
    screenState = SCREEN_SETTINGS_MENU;
    delay(200);
  }
}



void drawPetScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Pet type");


  int line = 0;
  for (int i = 0; i < MAX_VISIBLE_PETS; i++) {
    int idx = petViewOffset + i;
    if (idx >= NUM_PETS) break;

    display.setCursor(0, 16 + line * 8);
    if (idx == currentPetIndex) display.print("> ");
    else                        display.print("  ");
    display.println(pets[idx].name);
    line++;
  }


  PetProfile &p = pets[currentPetIndex];
  float minF = p.minC * 9.0 / 5.0 + 32.0;
  float maxF = p.maxC * 9.0 / 5.0 + 32.0;

  display.setCursor(0, 42);
  display.print("C: ");
  display.print(p.minC, 1);
  display.print("-");
  display.print(p.maxC, 1);

  display.setCursor(0, 50);
  display.print("F: ");
  display.print(minF, 1);
  display.print("-");
  display.print(maxF, 1);

  display.setCursor(0, 58);
  display.print("OK/BACK: return");

  display.display();
}

void handlePetScreen() {
  drawPetScreen();
  Key k = pollKey();

  if (k == KEY_UP && currentPetIndex > 0) {
    currentPetIndex--;
    if (currentPetIndex < petViewOffset) {
      petViewOffset = currentPetIndex;
    }
    lastAlertState = 0;  
    delay(150);
  } else if (k == KEY_DOWN && currentPetIndex < NUM_PETS - 1) {
    currentPetIndex++;
    if (currentPetIndex >= petViewOffset + MAX_VISIBLE_PETS) {
      petViewOffset = currentPetIndex - MAX_VISIBLE_PETS + 1;
    }
    lastAlertState = 0;
    delay(150);
  } else if (k == KEY_OK || k == KEY_BACK) {
    screenState = SCREEN_SETTINGS_MENU;
    delay(200);
  }
}


void drawKeyboardScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("Edit ");
  display.println(editingLabel);


  display.setCursor(100, 0);
  display.print(shiftOn ? "a" : "A");

  display.setCursor(0, 10);
  display.println(editingBuffer);

  int keyW = 10;
  int keyH = 8;
  int offsetY = 20;

  for (int r = 0; r < KEYBOARD_ROWS; r++) {
    const char* rowChars = keyboardRows[r];
    int len = strlen(rowChars);
    for (int c = 0; c < len; c++) {
      int x = c * keyW;
      int y = offsetY + r * keyH;
      char ch = rowChars[c];

      if (r == kbRow && c == kbCol) {
        display.fillRect(x, y, keyW, keyH, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRect(x, y, keyW, keyH, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
      }
      display.setCursor(x + 2, y + 1);
      display.write(ch);
    }
  }

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print("<:Bksp  _:Space  ^:Shift  >:Done");

  display.display();
}

void handleKeyboard() {
  drawKeyboardScreen();
  Key k = pollKey();

  if (k == KEY_UP && kbRow > 0) {
    kbRow--;
    int rowLen = strlen(keyboardRows[kbRow]);
    if (kbCol >= rowLen) kbCol = rowLen - 1;
    delay(150);
  } else if (k == KEY_DOWN && kbRow < KEYBOARD_ROWS - 1) {
    kbRow++;
    int rowLen = strlen(keyboardRows[kbRow]);
    if (kbCol >= rowLen) kbCol = rowLen - 1;
    delay(150);
  } else if (k == KEY_LEFT && kbCol > 0) {
    kbCol--;
    delay(150);
  } else if (k == KEY_RIGHT) {
    int rowLen = strlen(keyboardRows[kbRow]);
    if (kbCol < rowLen - 1) {
      kbCol++;
      delay(150);
    }
  } else if (k == KEY_OK) {
    const char* rowChars = keyboardRows[kbRow];
    char ch = rowChars[kbCol];
    size_t len = strlen(editingBuffer);

    if (ch == '<') {
      if (len > 0) {
        editingBuffer[len - 1] = '\0';
      }
    } else if (ch == '>') {
      screenState = keyboardNextDone;
      delay(200);
    } else if (ch == '^') {
      // toggle shift
      shiftOn = !shiftOn;
      delay(200);
    } else {
      if (len < (size_t)editingMaxLen - 1) {
        if (ch == '_') {
          ch = ' ';
        } else if (isalpha((unsigned char)ch)) {
          if (shiftOn) ch = tolower((unsigned char)ch);
          else         ch = toupper((unsigned char)ch);
        }
        editingBuffer[len] = ch;
        editingBuffer[len + 1] = '\0';
      }
    }
  } else if (k == KEY_BACK) {
    screenState = keyboardNextCancel;
    delay(200);
  }
}


void handleWebhookSetupToken() {

  editingBuffer      = webhookToken;
  editingMaxLen      = sizeof(webhookToken);
  editingLabel       = "TOKEN";
  kbRow              = 0;
  kbCol              = 0;
  shiftOn            = false;
  keyboardNextDone   = SCREEN_SETTINGS_MENU;  
  keyboardNextCancel = SCREEN_SETTINGS_MENU; 
  screenState        = SCREEN_KEYBOARD;
}



void drawWebhookTestScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Webhook test");

  display.setCursor(0, 16);
  display.println("OK: send test msg");

  display.setCursor(0, 28);
  display.print("Status: ");
  display.println(webhookTestStatus);

  display.setCursor(0, 56);
  display.print("BACK: Settings");

  display.display();
}

void handleWebhookTestScreen() {
  drawWebhookTestScreen();
  Key k = pollKey();

  if (k == KEY_OK) {
    if (!wifiConnected) {
      strncpy(webhookTestStatus, "No WiFi", sizeof(webhookTestStatus));
    } else if (webhookId[0] == '\0' || webhookToken[0] == '\0') {
      strncpy(webhookTestStatus, "No webhook", sizeof(webhookTestStatus));
    } else {
      bool ok = sendDiscordTestMessage();
      if (ok) {
        strncpy(webhookTestStatus, "Sent", sizeof(webhookTestStatus));
      } else {
        strncpy(webhookTestStatus, "Error", sizeof(webhookTestStatus));
      }
    }
    webhookTestStatus[sizeof(webhookTestStatus)-1] = '\0';
    delay(500);
  } else if (k == KEY_BACK) {
    screenState = SCREEN_SETTINGS_MENU;
    delay(200);
  }
}



void drawWiFiConnecting(const char* line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.setCursor(0, 16);
  display.println(line2);
  display.setCursor(0, 56);
  display.println("OK/BACK: return");

  display.display();
}

bool connectWiFiBlocking() {
  if (!wifiSSID[0]) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);
  WiFi.begin(wifiSSID, wifiPASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    drawWiFiConnecting("Trying...");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;


    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    for (int i = 0; i < 20; i++) {  
      if (getLocalTime(&timeinfo)) {
        Serial.println("Time synced");
        break;
      }
      Serial.println("Waiting for NTP...");
      delay(500);
    }

    lastTimeUpdate = 0;
    updateLocalTimeString();

    return true;
  } else {
    wifiConnected = false;
    return false;
  }
}

void handleWiFiConnecting() {
  if (!wifiConnectingStarted) {
    wifiConnectingStarted = true;
    drawWiFiConnecting("Please wait");
    lastConnectSuccess = connectWiFiBlocking();
  }

  if (lastConnectSuccess) {
    drawWiFiConnecting("Success!");
  } else {
    drawWiFiConnecting("Failed!");
  }

  Key k = pollKey();
  if (k == KEY_OK || k == KEY_BACK) {
    wifiConnectingStarted = false;
    screenState = SCREEN_SETTINGS_MENU;
    delay(200);
  }
}



void startWifiScan() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Scanning WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  scannedCount = (n > 0) ? n : 0;
  if (scannedCount > MAX_SCANNED_SSIDS) scannedCount = MAX_SCANNED_SSIDS;

  for (int i = 0; i < scannedCount; i++) {
    scannedSSIDs[i] = WiFi.SSID(i);
  }
  WiFi.scanDelete();   

  scannedSelectedIndex = 0;
  scanViewOffset       = 0;
  scanPerformed        = true;
}

void drawWifiScanScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("WiFi: choose SSID");

  if (!scanPerformed) {
    display.setCursor(0, 16);
    display.println("Scanning...");
  } else if (scannedCount == 0) {
    display.setCursor(0, 16);
    display.println("No networks");
  } else {
    int line = 0;
    for (int i = 0; i < MAX_VISIBLE_SSIDS; i++) {
      int idx = scanViewOffset + i;
      if (idx >= scannedCount) break;

      display.setCursor(0, 16 + line * 10);
      if (idx == scannedSelectedIndex) display.print("> ");
      else                             display.print("  ");

      String s = scannedSSIDs[idx];
      if (s.length() > 14) s = s.substring(0, 14); 
      display.print(s);

      line++;
    }
  }

  display.setCursor(0, 56);
  display.print("OK:Select  BACK:Settings");
  display.display();
}

void handleWifiScanScreen() {
  if (!scanPerformed) {
    startWifiScan();
  }

  drawWifiScanScreen();
  Key k = pollKey();

  if (k == KEY_BACK) {
    scanPerformed = false;
    screenState   = SCREEN_SETTINGS_MENU;
    delay(200);
    return;
  }

  if (scanPerformed && scannedCount > 0) {
    if (k == KEY_UP && scannedSelectedIndex > 0) {
      scannedSelectedIndex--;
      if (scannedSelectedIndex < scanViewOffset) {
        scanViewOffset = scannedSelectedIndex;
      }
      delay(150);
    } else if (k == KEY_DOWN && scannedSelectedIndex < scannedCount - 1) {
      scannedSelectedIndex++;
      if (scannedSelectedIndex >= scanViewOffset + MAX_VISIBLE_SSIDS) {
        scanViewOffset = scannedSelectedIndex - MAX_VISIBLE_SSIDS + 1;
      }
      delay(150);
    } else if (k == KEY_OK) {
      String s = scannedSSIDs[scannedSelectedIndex];
      s.toCharArray(wifiSSID, sizeof(wifiSSID));

      wifiPASS[0] = '\0';
      editingBuffer      = wifiPASS;
      editingMaxLen      = sizeof(wifiPASS);
      editingLabel       = "PASS";
      kbRow              = 0;
      kbCol              = 0;
      shiftOn            = false;
      keyboardNextDone   = SCREEN_WIFI_CONNECTING;
      keyboardNextCancel = SCREEN_WIFI_SCAN;

      screenState = SCREEN_KEYBOARD;
      delay(200);
    }
  }
}



void setup() {
  Serial.begin(115200);
  delay(1000);

  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;) { }  
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP32 Pet Monitor");
  display.println("Booting...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

 
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);

  wifiSSID[0]   = '\0';
  wifiPASS[0]   = '\0';
  
  petViewOffset = 0;

  delay(1000);
  screenState = SCREEN_CALIBRATE;
}

void loop() {

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  updateTemperature();
  updateTimeIfNeeded();

  switch (screenState) {
    case SCREEN_CALIBRATE:
      handleCalibration();
      break;
    case SCREEN_MAIN:
      handleMainScreen();
      break;
    case SCREEN_SETTINGS_MENU:
      handleSettingsMenu();
      break;
    case SCREEN_SETTING_UNIT:
      handleUnitScreen();
      break;
    case SCREEN_SETTING_PET:
      handlePetScreen();
      break;
    case SCREEN_KEYBOARD:
      handleKeyboard();
      break;
    case SCREEN_WIFI_CONNECTING:
      handleWiFiConnecting();
      break;
    case SCREEN_WIFI_SCAN:
      handleWifiScanScreen();
      break;
    case SCREEN_WEBHOOK_SETUP_TOKEN:
      handleWebhookSetupToken();
      break;
    case SCREEN_WEBHOOK_TEST:
      handleWebhookTestScreen();
      break;
  }
}
