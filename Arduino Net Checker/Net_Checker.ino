// =================== Combined Main + IP Scanner + Ping + Change IP ===================
// ESP32 + W5500 + I2C 16x4 LCD + BT Serial + WiFi + SNMP
// Modes:
// 0: LAN Cable Test
// 1: IP Scanning  (via BT or USB; accepts SCAN: prefix, CIDR, ranges, commas)
// 2: Router Check (SNMP over Ethernet with static IP)
// 3: Ping Testing (4 probes; ICMP if libs exist, else TCP timing)
// 4: Wi-Fi Check
// 5: Change IP (static IP + subnet; saved to NVS; BT setip supported)

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "WiFi.h"
#include "BluetoothSerial.h"
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Arduino_SNMP_Manager.h>
#include <Preferences.h>

// ---------- Optional ICMP ping libraries (auto-detect) ----------
#define HAS_ESP32PING 0
#define HAS_ICMPPING  0
#ifdef __has_include
  #if __has_include(<ESP32Ping.h>)
    #include <ESP32Ping.h>
    #undef HAS_ESP32PING
    #define HAS_ESP32PING 1
  #endif
  #if __has_include(<ICMPPing.h>)
    #include <ICMPPing.h>
    #undef HAS_ICMPPING
    #define HAS_ICMPPING 1
    static const uint8_t PING_SOCK = 6;   // any free socket 0..7
    ICMPPing EthPing(PING_SOCK, 0x1234);  // 16-bit identifier
  #endif
#endif
// -----------------------------------------------------------------

// ----------------- Buttons -----------------
#define BUTTON_UP     15
#define BUTTON_DOWN   2
#define BUTTON_SELECT 4
#define BUTTON_BACK   13

int mode = 0;
const int maxModes = 6;
bool selected = false;

// ----------------- LCD -----------------
LiquidCrystal_I2C lcd(0x27, 16, 4);

// ----------------- Bluetooth -----------------
BluetoothSerial SerialBT;

// ----------------- Preferences (NVS) -----------------
Preferences prefs; // namespace: "netcfg"

// ----------------- Shared Ethernet MAC -----------------
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// =======================================================
//                      SNMP CONFIG (static)
//   local_ip + subnet are LOADED from NVS on boot.
// =======================================================
IPAddress local_ip(192, 168, 1, 177);   // defaults (used if no saved config)
IPAddress router_ip(192, 168, 1, 1);
IPAddress gateway  (192, 168, 1, 1);
IPAddress subnet   (255, 255, 255, 0);
IPAddress dns      (8, 8, 8, 8);

const char *community  = "public";
const int   snmpVersion = 1;
const char *oidCPU    = "1.3.6.1.4.1.9.2.1.58.0";
const char *oidTemp   = "1.3.6.1.4.1.9.2.1.57.0";
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

// =======================================================
//                     IP SCANNER CONFIG
// =======================================================
static const int W5500_CS_PIN  = 5;   // SS/CS
static const int W5500_RST_PIN = -1;  // -1 = not used

uint16_t PORTS_TO_SCAN[] = {22,23,53,80,88,123,135,139,389,404,443,445,500,8080};
const size_t NUM_PORTS = sizeof(PORTS_TO_SCAN) / sizeof(PORTS_TO_SCAN[0]);

const uint16_t W5500_RETRANS_TIMEOUT_MS = 200;
const uint8_t  W5500_RETRANS_COUNT      = 2;
const uint16_t INTER_CONNECT_DELAY_MS   = 10;
const uint32_t SERIAL_IDLE_MS           = 1000;

// ----------------- Helpers (shared) -----------------
static inline void printlnBoth(const String &s){ Serial.println(s); SerialBT.println(s); }
static inline void printBoth(const String &s){ Serial.print(s); SerialBT.print(s); }

String trimStr(const String& s){ String t=s; t.trim(); return t; }

bool ipStringToU32(const String& s, uint32_t& out){
  int a,b,c,d;
  if (sscanf(s.c_str(), "%d.%d.%d.%d",&a,&b,&c,&d) != 4) return false;
  if (a<0||a>255||b<0||b>255||c<0||c>255||d<0||d>255) return false;
  out = ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)c<<8)|(uint32_t)d;
  return true;
}
IPAddress u32ToIP(uint32_t v){
  return IPAddress((uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v);
}
bool parseIP(const String& s, IPAddress& ip){
  uint32_t u; if(!ipStringToU32(trimStr(s), u)) return false; ip = u32ToIP(u); return true;
}
bool cidrToMask(int prefix, IPAddress& maskOut){
  if(prefix < 0 || prefix > 32) return false;
  uint32_t m = (prefix == 0) ? 0 : (0xFFFFFFFFUL << (32 - prefix));
  maskOut = IPAddress((uint8_t)(m>>24),(uint8_t)(m>>16),(uint8_t)(m>>8),(uint8_t)m);
  return true;
}

// Strip app prefixes like "SCAN:", "scan ", "START:", "END:", "PING:"
String stripScanPrefix(const String& in) {
  String t = trimStr(in);
  auto cutAfter = [&](char ch)->String {
    int i = t.indexOf(ch);
    return (i >= 0) ? trimStr(t.substring(i + 1)) : t;
  };

  if (t.startsWith("SCAN:") || t.startsWith("scan:") || t.startsWith("Scan:")) return cutAfter(':');
  if (t.startsWith("SCAN ") || t.startsWith("scan ") || t.startsWith("Scan ")) return trimStr(t.substring(5));
  if (t.startsWith("START:")|| t.startsWith("start:")) return cutAfter(':');
  if (t.startsWith("END:")  || t.startsWith("end:"))   return cutAfter(':');
  if (t.startsWith("PING:") || t.startsWith("ping:"))  return cutAfter(':');
  return t;
}

// LCD helpers for scan
void lcdSplash(const char* line2=""){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("NET CHECKER");
  lcd.setCursor(0,1); lcd.print("ESP32 + W5500");
  lcd.setCursor(0,2); lcd.print("BT + LCD + ETH");
  lcd.setCursor(0,3); lcd.print(line2);
}
void lcdProgress(const IPAddress& ip, uint32_t idx, uint32_t total){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("IP Scanning...");
  lcd.setCursor(0,1);
  lcd.print(ip.toString());
  lcd.setCursor(0,2);
  lcd.print("Host "); lcd.print(idx); lcd.print("/"); lcd.print(total);
}
void lcdLastFound(const IPAddress& ip){
  lcd.setCursor(0,3);
  String line="Found: "+ip.toString();
  if(line.length()>16) line=line.substring(0,16);
  lcd.print(line);
}

// Read a "line" from USB Serial: newline OR idle pause
String readSerialLine(uint32_t idleTimeoutMs = SERIAL_IDLE_MS){
  String s; uint32_t lastRx = millis();
  while(!Serial.available()) delay(5);
  while(true){
    while(Serial.available()){
      char c=(char)Serial.read(); lastRx = millis();
      if(c=='\n' || c=='\r'){ if(s.length()>0){ s.trim(); return s; } }
      else s += c;
    }
    if(s.length()>0 && (millis()-lastRx) > idleTimeoutMs){ s.trim(); return s; }
    delay(5);
  }
}

// Read a line from **either** USB Serial or Bluetooth (first source wins)
String readLineAny(uint32_t idleTimeoutMs = SERIAL_IDLE_MS){
  String s; uint32_t lastRx = millis();
  while(!Serial.available() && !SerialBT.available()) delay(5);
  while(true){
    while(Serial.available()){
      char c=(char)Serial.read(); lastRx = millis();
      if(c=='\n'||c=='\r'){ if(s.length()>0){ s.trim(); return s; } } else s += c;
    }
    while(SerialBT.available()){
      char c=(char)SerialBT.read(); lastRx = millis();
      if(c=='\n'||c=='\r'){ if(s.length()>0){ s.trim(); return s; } } else s += c;
    }
    if(s.length()>0 && (millis()-lastRx) > idleTimeoutMs){ s.trim(); return s; }
    delay(5);
  }
}

// Range parsers (scanner)
bool parseCIDR(const String& in, uint32_t& startU32, uint32_t& endU32){
  int slash=in.indexOf('/'); if(slash<=0) return false;
  String ipStr=trimStr(in.substring(0,slash));
  String maskStr=trimStr(in.substring(slash+1));
  uint32_t base; if(!ipStringToU32(ipStr, base)) return false;
  int mask=maskStr.toInt(); if(mask<0||mask>32) return false;
  uint32_t maskU32=(mask==0)?0:(0xFFFFFFFFUL<<(32-mask));
  uint32_t network=base & maskU32;
  uint32_t broadcast=network | (~maskU32);
  startU32=network; endU32=broadcast; return true;
}
bool parseStartEnd(const String& in, uint32_t& startU32, uint32_t& endU32){
  int dash=in.indexOf('-'); if(dash<=0) return false;
  String left=trimStr(in.substring(0,dash));
  String right=trimStr(in.substring(dash+1));
  if(!ipStringToU32(left,startU32)) return false;
  if(!ipStringToU32(right,endU32))  return false;
  return endU32 >= startU32;
}
// "ip1,ip2" -> start & end (commas)
bool parseCommaRange(const String& in, uint32_t& startU32, uint32_t& endU32) {
  int comma = in.indexOf(',');
  if (comma <= 0) return false;
  String left  = trimStr(in.substring(0, comma));
  String right = trimStr(in.substring(comma + 1));
  if (!ipStringToU32(left,  startU32)) return false;
  if (!ipStringToU32(right, endU32))  return false;
  return endU32 >= startU32;
}
bool tryParseSingleLineRange(const String& line, uint32_t& s, uint32_t& e){
  if (line.indexOf('/') > 0) return parseCIDR(line, s, e);      // CIDR
  if (line.indexOf('-') > 0) return parseStartEnd(line, s, e);  // "start - end"
  if (line.indexOf(',') > 0) return parseCommaRange(line, s, e);// "start,end"
  return false;
}

// Bring up Ethernet (DHCP) for scanner/ping
void bringUpEthernetForScan(){
  if(W5500_RST_PIN >= 0){
    pinMode(W5500_RST_PIN, OUTPUT);
    digitalWrite(W5500_RST_PIN, LOW); delay(50);
    digitalWrite(W5500_RST_PIN, HIGH); delay(200);
  }
  SPI.begin(18,19,23,W5500_CS_PIN);
  Ethernet.init(W5500_CS_PIN);
  Ethernet.setRetransmissionTimeout(W5500_RETRANS_TIMEOUT_MS);
  Ethernet.setRetransmissionCount(W5500_RETRANS_COUNT);

  printlnBoth("Bringing up Ethernet via DHCP (scanner/ping)...");
  if(Ethernet.begin(mac) == 0){
    printlnBoth("DHCP failed. Using fallback 192.168.1.200/24 gw 192.168.1.1");
    IPAddress ip(192,168,1,200), _dns(192,168,1,1), gw(192,168,1,1), mask(255,255,255,0);
    Ethernet.begin(mac, ip, _dns, gw, mask);
  }
  delay(500);
  printlnBoth("Local IP: " + Ethernet.localIP().toString());
}

// TCP sweep for one host
String scanHostOpenPorts(const IPAddress& host){
  EthernetClient client;
  String openPorts="";
  for(size_t i=0;i<NUM_PORTS;i++){
    uint16_t port=PORTS_TO_SCAN[i];
    if(client.connect(host, port)){
      if(openPorts.length()>0) openPorts += ",";
      openPorts += String(port);
      client.stop();
    } else {
      client.stop();
    }
    delay(INTER_CONNECT_DELAY_MS);
  }
  return openPorts;
}

// =================== Preferences helpers (save/load) ===================
void saveNetConfigToNVS(const IPAddress& ip, const IPAddress& mask){
  prefs.begin("netcfg", false);
  prefs.putUChar("ip0", ip[0]); prefs.putUChar("ip1", ip[1]);
  prefs.putUChar("ip2", ip[2]); prefs.putUChar("ip3", ip[3]);
  prefs.putUChar("mk0", mask[0]); prefs.putUChar("mk1", mask[1]);
  prefs.putUChar("mk2", mask[2]); prefs.putUChar("mk3", mask[3]);
  prefs.putBool("ok", true);
  prefs.end();
}
bool loadNetConfigFromNVS(IPAddress& ipOut, IPAddress& maskOut){
  prefs.begin("netcfg", true);
  bool ok = prefs.getBool("ok", false);
  if(ok){
    ipOut   = IPAddress(prefs.getUChar("ip0",ipOut[0]), prefs.getUChar("ip1",ipOut[1]),
                        prefs.getUChar("ip2",ipOut[2]), prefs.getUChar("ip3",ipOut[3]));
    maskOut = IPAddress(prefs.getUChar("mk0",maskOut[0]), prefs.getUChar("mk1",maskOut[1]),
                        prefs.getUChar("mk2",maskOut[2]), prefs.getUChar("mk3",maskOut[3]));
  }
  prefs.end();
  return ok;
}

// Re-apply static Ethernet (used by SNMP mode)
void applyStaticEthernet(){
  Ethernet.init(W5500_CS_PIN);
  Ethernet.begin(mac, local_ip, dns, gateway, subnet);
  delay(500);
  printlnBoth("Applied static IP: " + Ethernet.localIP().toString() +
              "  Mask: " + subnet.toString());
}

// =======================================================
//                         SETUP
// =======================================================
void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_NetChecker");
  delay(800);

  // Load saved IP/subnet (if present)
  if(loadNetConfigFromNVS(local_ip, subnet)){
    printlnBoth("Loaded IP from NVS -> IP: " + local_ip.toString() + "  Mask: " + subnet.toString());
  } else {
    printlnBoth("Using default static IP -> IP: " + local_ip.toString() + "  Mask: " + subnet.toString());
  }

  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("NET CHECKER");
  delay(700);
  updateDisplay();
}

// =======================================================
//                          LOOP
// =======================================================
void loop() {
  checkButtons();
  checkBluetooth();

  // SNMP polling while mode 2 is active
  if (selected && mode == 2) {
    snmp.loop();
    if (millis() - lastPoll >= pollInterval) {
      lastPoll = millis();
      getSNMPData();
      printSNMPResults();
    }
  }
}

// =======================================================
//                    UI / Navigation
// =======================================================
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
      printlnBoth("Selected Mode: " + String(mode + 1));
      delay(350);
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
  if (!SerialBT.available()) return;

  String command = SerialBT.readStringUntil('\n');
  command.trim();

  // Switch mode via BT
  if (command.startsWith("mode:")) {
    int selectedMode = command.substring(5).toInt();
    if (selectedMode >= 1 && selectedMode <= maxModes) {
      mode = selectedMode - 1;
      selected = true;
      lcd.setCursor(0, 1); lcd.print("BT Selected!");
      delay(250);
      runSelectedMode();
      return;
    }
  }

  // One-shot scan: scan:<range>
  if (command.startsWith("scan:") || command.startsWith("SCAN:")) {
    String args = stripScanPrefix(command);
    mode = 1; selected = true;
    runIPScan(args);   // preset first line
    selected = false;  // return to menu
    updateDisplay();
    return;
  }

  // One-shot ping: ping:<ip>[,mask]
  if (command.startsWith("ping:") || command.startsWith("PING:")) {
    String args = stripScanPrefix(command);
    mode = 3; selected = true;
    runPingTest(args); // preset line
    selected = false;
    updateDisplay();
    return;
  }

  // ---------- Change IP via Bluetooth ----------
  //   setip 192.168.1.177 255.255.255.0
  //   setip:192.168.1.177,255.255.255.0
  //   setip 192.168.1.177/24
  if (command.startsWith("setip")) {
    String args = command.substring(command.indexOf("setip") + 5);
    args.trim();

    IPAddress newIP, newMask;
    bool ok = false;

    if (args.indexOf(',') > 0) {
      int comma = args.indexOf(',');
      ok = parseIP(args.substring(0, comma), newIP) &&
           parseIP(args.substring(comma + 1), newMask);
    } else if (args.indexOf(' ') > 0) {
      int sp = args.indexOf(' ');
      String left = trimStr(args.substring(0, sp));
      String right= trimStr(args.substring(sp + 1));
      if (right.startsWith("/")) {
        if (parseIP(left, newIP)) {
          int p = right.substring(1).toInt();
          ok = cidrToMask(p, newMask);
        }
      } else {
        ok = parseIP(left, newIP) && parseIP(right, newMask);
      }
    } else if (args.indexOf('/') > 0) {
      int slash = args.indexOf('/');
      ok = parseIP(args.substring(0, slash), newIP) &&
           cidrToMask(args.substring(slash + 1).toInt(), newMask);
    }

    if (ok) {
      local_ip = newIP;
      subnet   = newMask;
      saveNetConfigToNVS(local_ip, subnet);
      printlnBoth("Saved new IP via BT -> IP: " + local_ip.toString() + "  Mask: " + subnet.toString());
      if (mode == 2) applyStaticEthernet();
    } else {
      printlnBoth("setip parse error. Examples:");
      printlnBoth("  setip 192.168.1.177 255.255.255.0");
      printlnBoth("  setip:192.168.1.177,255.255.255.0");
      printlnBoth("  setip 192.168.1.177/24");
    }
    return;
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
    case 1: lcd.print("IP Scanning");    break;
    case 2: lcd.print("Router Check");   break;
    case 3: lcd.print("Ping Testing");   break;
    case 4: lcd.print("Wi-Fi Check");    break;
    case 5: lcd.print("Change IP");      break;
  }
}

void runSelectedMode() {
  switch (mode) {
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Please connect");
      lcd.setCursor(0, 1); lcd.print("LAN cable to P2");
      printlnBoth("Please connect the LAN cable to Port 2");
      delay(2000);
      break;

    case 1: runIPScan(""); break;      // no preset
    case 2:
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Router Check");
      lcd.setCursor(0, 1); lcd.print("Please wait...");
      printlnBoth("Router Check mode started.");
      runSNMPCheck();
      break;

    case 3: runPingTest(""); break;    // no preset
    case 4: runWiFiScan(); break;
    case 5: runChangeIP(); break;
  }

  if (mode != 2) { // SNMP keeps running until BACK
    selected = false;
    updateDisplay();
  }
}

// =======================================================
//                    MODE 1: IP SCANNING
// =======================================================
void runIPScan(String firstLineOpt){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("IP Scanning");
  lcd.setCursor(0,1); lcd.print("Bluetooth/USB");

  bringUpEthernetForScan();

  printlnBoth("");
  printlnBoth("==== LAN IP/Port Scanner ====");
  printlnBoth("Send via Bluetooth **or** USB Serial:");
  printlnBoth("  192.168.1.0/24");
  printlnBoth("  192.168.1.1-192.168.1.254");
  printlnBoth("  192.168.1.1,192.168.1.254");
  printlnBoth("Or two-step: SCAN:<start> then END:<end>");
  printlnBoth("Enter Start (or single-line form):");

  bool waitingEnd=false;
  uint32_t startIP=0, endIP=0;

  // Get start (or single-line range)
  while(true){
    String line = firstLineOpt.length() ? stripScanPrefix(firstLineOpt)
                                        : stripScanPrefix(readLineAny());
    firstLineOpt = ""; // consume preset
    uint32_t s,e;
    if(tryParseSingleLineRange(line, s, e)){ startIP=s; endIP=e; break; }
    if(ipStringToU32(line, startIP)){
      printlnBoth("\nEnter End IP and press Enter:");
      waitingEnd = true; break;
    } else {
      printlnBoth("Invalid input. Try again:");
    }
    delay(10);
  }

  // If needed, get end
  while(waitingEnd){
    String line = stripScanPrefix(readLineAny());
    if(!ipStringToU32(line, endIP)) printlnBoth("Invalid IP. Enter End IP again:");
    else if(endIP < startIP){ printlnBoth("End IP must be >= Start IP. Start over."); return; }
    else waitingEnd=false;
    delay(10);
  }

  if(endIP < startIP){ printlnBoth("Invalid range. Start over."); return; }

  // Scan
  uint32_t totalHosts = (endIP - startIP) + 1;
  printlnBoth("");
  printBoth("Scanning "); printBoth(String(totalHosts));
  printBoth(" host(s) from "); printBoth(u32ToIP(startIP).toString());
  printBoth(" to "); printlnBoth(u32ToIP(endIP).toString());
  printlnBoth("Results will appear below...\n");

  uint32_t idx=0;
  for(uint32_t h=startIP; h<=endIP; h++){
    idx++;
    IPAddress host = u32ToIP(h);

    lcdProgress(host, idx, totalHosts);

    String openPorts = scanHostOpenPorts(host);
    if(openPorts.length() > 0){
      printlnBoth(host.toString() + " online : ports " + openPorts);
      lcdLastFound(host);
    } else {
      printBoth(".");
    }

    delay(2);
    if(idx >= totalHosts) break;
  }

  printlnBoth("\n\nScan complete.");
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Scan complete");
  lcd.setCursor(0,1); lcd.print("Start:"); lcd.setCursor(7,1); lcd.print(u32ToIP(startIP).toString());
  lcd.setCursor(0,2); lcd.print("End  :"); lcd.setCursor(7,2); lcd.print(u32ToIP(endIP).toString());
  lcd.setCursor(0,3); lcd.print("Check Serial/BT");
  delay(1400);
}

// =======================================================
//                    MODE 2: SNMP Router Check
// =======================================================
void runSNMPCheck() {
  Ethernet.init(W5500_CS_PIN);
  printlnBoth("Initializing Ethernet with static IP...");
  Ethernet.begin(mac, local_ip, dns, gateway, subnet);
  delay(800);
  printlnBoth("Ethernet local IP: " + Ethernet.localIP().toString());

  if (Ethernet.localIP()[0] == 0) {
    printlnBoth("ERROR: Ethernet failed to init. Returning to menu.");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Ethernet error!");
    lcd.setCursor(0, 1); lcd.print("Check cable.");
    delay(1500);
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
  printlnBoth("[DEBUG] getSNMPData() called");
  snmpRequest.addOIDPointer(callbackCPU);
  snmpRequest.addOIDPointer(callbackTemp);
  snmpRequest.addOIDPointer(callbackUptime);
  snmpRequest.setIP(Ethernet.localIP());
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(random(1000, 9999));
  snmpRequest.sendTo(router_ip);
  snmpRequest.clearOIDList();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SNMP Request Sent");
  lcd.setCursor(0, 1); lcd.print("Waiting reply...");
}

void printSNMPResults() {
  printlnBoth("[DEBUG] printSNMPResults() called");

  if (cpuUsage == 0 && temperature == 0) {
    printlnBoth("No SNMP response. Returning to menu.");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("No router result");
    lcd.setCursor(0, 1); lcd.print("Please try again");
    delay(1500);
    selected = false;
    updateDisplay();
    return;
  }

  String cpu = "CPU Usage: " + String(cpuUsage) + " %";
  String temp = "Temperature: " + String(temperature) + " C";
  String up = "Uptime: " + String(uptime) + " (cs)";

  printlnBoth("------ SNMP Results [Ethernet] ------");
  printlnBoth(cpu); printlnBoth(temp); printlnBoth(up);
  printlnBoth("--------------------------------------");

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("CPU:");  lcd.print(cpuUsage); lcd.print("%");
  lcd.setCursor(0, 1); lcd.print("Temp:"); lcd.print(temperature); lcd.print("C");
}

// =======================================================
//                    MODE 3: PING TESTING
// =======================================================
void runPingTest(String presetLine){
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Ping Testing");
  printlnBoth("Enter IP to ping (e.g., 8.8.8.8). Also accepts PING:<ip>[,mask]");

  String line = presetLine.length() ? stripScanPrefix(presetLine) : stripScanPrefix(readLineAny());

  // Allow "ip,mask" -> use only IP
  int comma = line.indexOf(',');
  if (comma > 0) line = trimStr(line.substring(0, comma));

  uint32_t ip32;
  if (!ipStringToU32(line, ip32)) { printlnBoth("Invalid IP. Returning to menu."); delay(1200); return; }
  IPAddress target = u32ToIP(ip32);

#if HAS_ESP32PING
  bool wifiCanPing = (WiFi.status() == WL_CONNECTED);
#else
  bool wifiCanPing = false;
#endif
  if (!wifiCanPing) bringUpEthernetForScan();

  printlnBoth("\nPinging " + target.toString() + " with 32 bytes of data:");

  const int ATTEMPTS = 4;
  int received = 0;
  long minT = 0x7FFFFFFF, maxT = 0, sumT = 0;

  for (int i = 0; i < ATTEMPTS; i++) {
    bool ok = false; long rtt = -1; int ttl = -1;

#if HAS_ICMPPING
    uint8_t payload[32]; for (int k = 0; k < 32; k++) payload[k] = k;
    ICMPEchoReply echoReply = EthPing(target, payload, sizeof(payload), 1000);
    if (echoReply.status == SUCCESS) { ok = true; rtt = echoReply.time; ttl = echoReply.ttl; }
#elif HAS_ESP32PING
    if (wifiCanPing && Ping.ping(target, 1)) { ok = true; rtt = (long)Ping.averageTime(); }
#endif

    if (!ok) { // TCP timing fallback
      EthernetClient c; uint32_t t0 = millis();
      if (c.connect(target, 80)) { ok = true; rtt = (long)(millis() - t0); }
      c.stop();
    }

    if (ok) {
      received++; if (rtt < minT) minT = rtt; if (rtt > maxT) maxT = rtt; sumT += rtt;
      String out = "Reply from " + target.toString() + ": bytes=32 time=" + String(rtt) + "ms";
      if (ttl >= 0) out += " TTL=" + String(ttl);
      printlnBoth(out);
      lcd.setCursor(0, 1); lcd.print("Reply "); lcd.print(i + 1); lcd.print(": "); lcd.print(rtt); lcd.print("ms   ");
    } else {
      printlnBoth("Request timed out.");
      lcd.setCursor(0, 1); lcd.print("Timeout #"); lcd.print(i + 1); lcd.print("   ");
    }
    delay(1000);
  }

  int lost = ATTEMPTS - received; int lossPct = (lost * 100) / ATTEMPTS;
  printlnBoth("\nPing statistics for " + target.toString() + ":");
  printlnBoth("    Packets: Sent = " + String(ATTEMPTS) + ", Received = " + String(received) +
              ", Lost = " + String(lost) + " (" + String(lossPct) + "% loss)");
  if (received > 0) {
    printlnBoth("Approximate round trip times in ms:");
    printlnBoth("    Minimum = " + String(minT) + "ms, Maximum = " + String(maxT) +
                "ms, Average = " + String(sumT / received) + "ms");
  }
  delay(1400);
}

// =======================================================
//                    MODE 4: Wi-Fi Scan
// =======================================================
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

void runWiFiScan() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Wi-Fi Scanning...");
  printlnBoth("Starting Wi-Fi Scan...");

  int n = WiFi.scanNetworks();

  if (n == 0) {
    lcd.setCursor(0, 1); lcd.print("No networks");
    printlnBoth("No networks found.");
  } else {
    printlnBoth("Networks found: " + String(n));
    for (int i = 0; i < n && i < 5; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      wifi_auth_mode_t encType = WiFi.encryptionType(i);

      String info = String(i + 1) + ") " + ssid +
                    " | RSSI: " + String(rssi) +
                    " | ENC: " + String(getEncryptionType(encType));
      printlnBoth(info);

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(ssid.substring(0, 16));
      lcd.setCursor(0, 1); lcd.print("RSSI:"); lcd.print(rssi);
      delay(1500);
    }
  }
  printlnBoth("Wi-Fi Scan complete.");
}

// =======================================================
//                    MODE 5: CHANGE IP
// =======================================================
void runChangeIP(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Change IP Mode");
  lcd.setCursor(0,1); lcd.print("USB/BT input");

  printlnBoth("=== Change IP Mode ===");
  printlnBoth("Current IP : " + local_ip.toString());
  printlnBoth("Current Mask: " + subnet.toString());
  printlnBoth("Enter NEW IP (a.b.c.d) or 'skip':");

  String line = readLineAny();
  IPAddress newIP = local_ip;
  if (line.equalsIgnoreCase("skip")) {
    // keep old
  } else if (!parseIP(line, newIP)) {
    printlnBoth("Invalid IP. Aborting.");
    delay(1200);
    return;
  }

  printlnBoth("Enter NEW Subnet Mask (a.b.c.d) or CIDR like /24, or 'skip':");
  line = readLineAny();
  IPAddress newMask = subnet;
  if (line.equalsIgnoreCase("skip")) {
    // keep old
  } else if (line.startsWith("/")) {
    if (!cidrToMask(line.substring(1).toInt(), newMask)) {
      printlnBoth("Invalid CIDR. Aborting."); delay(1200); return;
    }
  } else if (!parseIP(line, newMask)) {
    printlnBoth("Invalid Mask. Aborting."); delay(1200); return;
  }

  printlnBoth("Save changes? type 'save' or 'cancel':");
  line = readLineAny();
  if (!line.equalsIgnoreCase("save")) {
    printlnBoth("Canceled. No changes made.");
    delay(1000);
    return;
  }

  local_ip = newIP;
  subnet   = newMask;
  saveNetConfigToNVS(local_ip, subnet);
  printlnBoth("Saved -> IP: " + local_ip.toString() + "  Mask: " + subnet.toString());

  // If SNMP mode is active, re-apply immediately
  applyStaticEthernet();

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Saved new IP");
  lcd.setCursor(0,1); lcd.print(local_ip.toString());
  lcd.setCursor(0,2); lcd.print("Mask:");
  lcd.setCursor(6,2); lcd.print(subnet.toString());
  delay(1500);
}
