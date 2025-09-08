// Combined SNMP Checker and Main Net Checker
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "WiFi.h"
#include "BluetoothSerial.h"
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Arduino_SNMP_Manager.h>

BluetoothSerial SerialBT;

#define BUTTON_UP     15
#define BUTTON_DOWN   2
#define BUTTON_SELECT 4
#define BUTTON_BACK   13

int mode = 0;
const int maxModes = 6;
bool selected = false;

LiquidCrystal_I2C lcd(0x27, 16, 4);

// SNMP Config
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress local_ip(192, 168, 1, 177);
IPAddress router_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

const char *community = "public";
const int snmpVersion = 1;
const char *oidCPU = "1.3.6.1.4.1.9.2.1.58.0";
const char *oidTemp = "1.3.6.1.4.1.9.2.1.57.0";
const char *oidUptime = "1.3.6.1.2.1.1.3.0";

int cpuUsage = 0;
int temperature = 0;
uint32_t uptime = 0;

EthernetUDP udp;
SNMPManager snmp(community);
SNMPGet snmpRequest(community, snmpVersion);
ValueCallback *callbackCPU;
ValueCallback *callbackTemp;
ValueCallback *callbackUptime;
unsigned long lastPoll = 0;
const int pollInterval = 10000;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_NetChecker");
  delay(1000);

  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("NET CHECKER");
  delay(1000);
  updateDisplay();
}

void loop() {
  checkButtons();
  checkBluetooth();

  if (selected && mode == 2) {
    snmp.loop();
    if (millis() - lastPoll >= pollInterval) {
      lastPoll = millis();
      getSNMPData();
      printSNMPResults();
    }
  }
}

void checkButtons() {
  if (!selected) {
    if (digitalRead(BUTTON_UP) == LOW) {
      mode = (mode + 1) % maxModes;
      updateDisplay();
      delay(300);
    }
    if (digitalRead(BUTTON_DOWN) == LOW) {
      mode = (mode - 1 + maxModes) % maxModes;
      updateDisplay();
      delay(300);
    }
    if (digitalRead(BUTTON_SELECT) == LOW) {
      selected = true;
      lcd.setCursor(0, 1);
      lcd.print("Selected!");
      Serial.println("Selected Mode: " + String(mode + 1));
      SerialBT.println("Selected Mode: " + String(mode + 1));
      delay(500);
      runSelectedMode();
    }
  } else if (digitalRead(BUTTON_BACK) == LOW) {
    selected = false;
    lcd.clear();
    updateDisplay();
    delay(300);
  }
}

void checkBluetooth() {
  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n');
    command.trim();
    if (command.startsWith("mode:")) {
      int selectedMode = command.substring(5).toInt();
      if (selectedMode >= 1 && selectedMode <= maxModes) {
        mode = selectedMode - 1;
        selected = true;
        lcd.setCursor(0, 1);
        lcd.print("BT Selected!");
        delay(500);
        runSelectedMode();
      }
    }
  }
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode ");
  lcd.print(mode + 1);
  lcd.setCursor(0, 1);
  switch (mode) {
    case 0: lcd.print("LAN Cable Test"); break;
    case 1: lcd.print("IP Scanning"); break;
    case 2: lcd.print("Router Check"); break;
    case 3: lcd.print("MAC Spoofing"); break;
    case 4: lcd.print("Wi-Fi Check"); break;
    case 5: lcd.print("Change IP"); break;
  }
}

void runSelectedMode() {
  switch (mode) {
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Please connect");
      lcd.setCursor(0, 1);
      lcd.print("LAN cable to P2");

      Serial.println("Please connect the LAN cable to Port 2");
      SerialBT.println("Please connect the LAN cable to Port 2");
      delay(3000);
      break;

    case 2:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Router Check");
      lcd.setCursor(0, 1);
      lcd.print("Please wait...");
      Serial.println("Router Check mode started.");
      SerialBT.println("Router Check mode started.");
      runSNMPCheck();
      break;

    case 4:
      runWiFiScan();
      break;

    default:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Mode ");
      lcd.print(mode + 1);
      lcd.setCursor(0, 1);
      lcd.print("Not implemented");
      break;
  }

  if (mode != 2) {
    selected = false;
    updateDisplay();
  }
}

void runWiFiScan() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Wi-Fi Scanning...");

  Serial.println("Starting Wi-Fi Scan...");
  SerialBT.println("Starting Wi-Fi Scan...");

  int n = WiFi.scanNetworks();

  if (n == 0) {
    lcd.setCursor(0, 1);
    lcd.print("No networks");

    Serial.println("No networks found.");
    SerialBT.println("No networks found.");
  } else {
    Serial.print("Networks found: ");
    Serial.println(n);
    SerialBT.print("Networks found: ");
    SerialBT.println(n);

    for (int i = 0; i < n && i < 5; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      wifi_auth_mode_t encType = WiFi.encryptionType(i);

      String info = String(i + 1) + ") " + ssid +
                    " | RSSI: " + String(rssi) +
                    " | ENC: " + String(getEncryptionType(encType));

      Serial.println(info);
      SerialBT.println(info);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(ssid.substring(0, 16));
      lcd.setCursor(0, 1);
      lcd.print("RSSI:");
      lcd.print(rssi);

      delay(2500);
    }
  }

  Serial.println("Wi-Fi Scan complete.");
  SerialBT.println("Wi-Fi Scan complete.");
}

void runSNMPCheck() {
  Ethernet.init(5);

  Serial.println("Initializing Ethernet with static IP...");
  SerialBT.println("Initializing Ethernet with static IP...");

  Ethernet.begin(mac, local_ip, dns, gateway, subnet);
  delay(1000);

  Serial.print("Ethernet local IP: ");
  Serial.println(Ethernet.localIP());
  SerialBT.print("Ethernet local IP: ");
  SerialBT.println(Ethernet.localIP());

  if (Ethernet.localIP()[0] == 0) {
    Serial.println("ERROR: Ethernet failed to init. Returning to menu.");
    SerialBT.println("ERROR: Ethernet failed to init. Returning to menu.");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ethernet error!");
    lcd.setCursor(0, 1);
    lcd.print("Check cable.");

    delay(3000);
    selected = false;
    updateDisplay();
    return;
  }

  snmp.setUDP(&udp);
  snmp.begin();
  callbackCPU    = snmp.addIntegerHandler(router_ip, oidCPU, &cpuUsage);
  callbackTemp   = snmp.addIntegerHandler(router_ip, oidTemp, &temperature);
  callbackUptime = snmp.addTimestampHandler(router_ip, oidUptime, &uptime);
}

void getSNMPData() {
  Serial.println("[DEBUG] getSNMPData() called");
  SerialBT.println("[DEBUG] getSNMPData() called");

  snmpRequest.addOIDPointer(callbackCPU);
  snmpRequest.addOIDPointer(callbackTemp);
  snmpRequest.addOIDPointer(callbackUptime);
  snmpRequest.setIP(Ethernet.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(random(1000, 9999));
  snmpRequest.sendTo(router_ip);
  snmpRequest.clearOIDList();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SNMP Request Sent");
  lcd.setCursor(0, 1);
  lcd.print("Waiting reply...");
}

void printSNMPResults() {
  Serial.println("[DEBUG] printSNMPResults() called");
  SerialBT.println("[DEBUG] printSNMPResults() called");

  if (cpuUsage == 0 && temperature == 0) {
    Serial.println("No SNMP response. Returning to menu.");
    SerialBT.println("No SNMP response. Returning to menu.");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("No router result");
    lcd.setCursor(0, 1);
    lcd.print("Please try again");

    delay(3000);
    selected = false;
    updateDisplay();
    return;
  }

  String cpu = "CPU Usage: " + String(cpuUsage) + " %";
  String temp = "Temperature: " + String(temperature) + " C";
  String up = "Uptime: " + String(uptime) + " (centiseconds)";

  Serial.println("------ SNMP Results [Ethernet] ------");
  Serial.println(cpu);
  Serial.println(temp);
  Serial.println(up);
  Serial.println("--------------------------------------\n");

  SerialBT.println("------ SNMP Results [Ethernet] ------");
  SerialBT.println(cpu);
  SerialBT.println(temp);
  SerialBT.println(up);
  SerialBT.println("--------------------------------------\n");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CPU:");
  lcd.print(cpuUsage);
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("Temp:");
  lcd.print(temperature);
  lcd.print("C");
}

const char* getEncryptionType(wifi_auth_mode_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    default: return "Unknown";
  }
}
