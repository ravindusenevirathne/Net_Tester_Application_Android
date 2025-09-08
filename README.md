# 📡 Portable Embedded Network Multi Tester

A compact, low-cost, and multi-functional **network diagnostic tool** built using **ESP32 + W5500 Ethernet module**, controlled via a **Flutter mobile app**.

🎯 Ideal for:
- Network Engineers 🧑‍💻
- IT Technicians 🧰
- Networking Students 🎓

---

## 🚀 Features Overview

### 🛠️ Hardware (ESP32 + W5500)
- 🔌 **Wired Ethernet support** (with W5500)
- 🧪 **RJ45 Cable Test**: Detects continuity & miswiring
- 🧠 **MAC Address Detection**: For routers & LAN devices
- 🌐 **IP Scanner**: Scan devices in local subnet
- 📊 **SNMP Monitoring**: Get router/switch info (CPU, uptime)
- 📶 **Wi-Fi Scan** (2.4GHz only)
- 🌍 **Ping Test / DNS Check**: Internet reachability test
- 🌀 **MAC Spoofing** *(optional)*

### 📱 Mobile App (Flutter)
- 🔵 Connect via **Bluetooth Classic**
- 🎛️ Select and run tests
- 📈 View **real-time results**
- 💾 Save results **locally**
- 📝 **Export** reports as PDF / Text
- 🗂️ Access offline results anytime

---

## 🧠 ESP32 + W5500 Capabilities

- ✅ Supports **DHCP or static IP**
- 💡 Link/activity LEDs
- 🔄 Serial-based IP reconfiguration
- ⚡ LAN-based network scan
- 🔍 Discover online devices using port 80 scan

---

## 📁 Project Structure

```bash
/net_tester_project/
├── arduino/
│   └── ESP32_W5500_NetworkTester.ino     # Main firmware for ESP32
└── flutter/
    └── net_tester_app/                   # Flutter mobile app
