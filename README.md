📡 Portable Embedded Network Multi Tester
A compact, low-cost, and multi-functional network diagnostic tool built with ESP32 + W5500 Ethernet module and controlled via a Flutter mobile application.
Designed to assist network engineers, IT technicians, and students in testing, troubleshooting, and monitoring networks in the field.

🚀 Features
🛠️ Hardware Components
🧠 ESP32 Microcontroller — the main controller with Wi-Fi & Bluetooth capabilities.

🌐 W5500 Ethernet Module — for stable wired network diagnostics.

🔌 RJ45 Cable Tester — checks for continuity and detects cable faults.

🛰 IP Scanner — discovers active hosts on the LAN.

🧾 MAC Address Checker — retrieves MAC addresses of router or LAN devices.

📶 Wi-Fi Scanner — scans 2.4GHz wireless networks.

🧠 SNMP Status Monitoring — queries routers/switches for stats like CPU or uptime.

🌍 Internet Connectivity Test — performs ping and DNS resolution tests.

🔧 MAC Address Spoofing (Planned) — for advanced diagnostics.

📲 Mobile Application (Flutter)
🎛️ Mode Selector UI — Choose test types like IP scan, cable test, ping, SNMP.

📡 Bluetooth Classic Communication — Control the ESP32 over serial Bluetooth.

🧾 Save Results — Locally store test reports.

🧾 Export to PDF or TXT — For documentation and auditing purposes.

📈 Real-Time Results — See test output as it happens.

📦 ESP32 + W5500 Ethernet Module Based Network Tester
The core of this project is the Ethernet-enabled ESP32 network tester, which allows robust and rapid diagnostics via LAN.

🔍 Ethernet Capabilities:
🔧 DHCP and static IP assignment.

🔁 Ping testing to target IPs or internet hosts (e.g., 8.8.8.8).

🖥️ Device discovery on the local subnet.

🚦 LED indicators to confirm link status.

📟 Serial monitor input for real-time configuration (local IP, subnet, gateway).

💡 Ideal for environments where Wi-Fi is unreliable or disabled.

📁 Project Structure
arduino
Copy
/arduino/
   └── ESP32_W5500_NetworkTester.ino   ← Arduino code for Ethernet module
/flutter/
   └── net_tester_app/                 ← Flutter app for controlling and viewing results
🧰 Setup & Getting Started
🔌 Wire up ESP32 with W5500 module (CS → GPIO 5 or 15 depending on your board).

⚙️ Upload the Arduino sketch using Arduino IDE.

📲 Install the Flutter app on your Android device.

📡 Connect via Bluetooth and start testing.

🧪 This Project Uses
Ethernet Library

flutter_bluetooth_serial

ICMP Ping, SNMP Manager (Arduino libraries)

🧰 Serial tools and manual config via Serial Monitor

📎 Additional Resources for Flutter Beginners
🧪 Lab: Write your first Flutter app

🍳 Cookbook: Useful Flutter samples

📘 Full Flutter Documentation
