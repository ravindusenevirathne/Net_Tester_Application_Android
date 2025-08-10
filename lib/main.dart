import 'dart:typed_data';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';
import 'package:permission_handler/permission_handler.dart';

void main() => runApp(const MyApp());

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Net Checker',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        primarySwatch: Colors.blueGrey,
        elevatedButtonTheme: ElevatedButtonThemeData(
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.blueGrey.shade700,
            foregroundColor: Colors.white,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(15),
            ),
          ),
        ),
      ),
      home: const BluetoothHomePage(),
    );
  }
}

class BluetoothHomePage extends StatefulWidget {
  const BluetoothHomePage({super.key});

  @override
  State<BluetoothHomePage> createState() => _BluetoothHomePageState();
}

class _BluetoothHomePageState extends State<BluetoothHomePage> {
  BluetoothConnection? connection;
  bool isConnected = false;
  List<String> log = [];
  BluetoothDevice? selectedDevice;
  final TextEditingController _inputController = TextEditingController();
  String currentMode = "";
  bool showInputBox = false;

  @override
  void initState() {
    super.initState();
    requestBluetoothPermissions();
  }

  void requestBluetoothPermissions() async {
    if (Platform.isAndroid) {
      if (await Permission.bluetoothScan.status != PermissionStatus.granted) {
        await Permission.bluetoothScan.request();
      }
      if (await Permission.bluetoothConnect.status != PermissionStatus.granted) {
        await Permission.bluetoothConnect.request();
      }
      if (await Permission.location.status != PermissionStatus.granted) {
        await Permission.location.request();
      }
    }
  }

  void connectToESP32(BluetoothDevice device) async {
    try {
      logMessage("Connecting to ${device.name}...");
      connection = await BluetoothConnection.toAddress(device.address);
      logMessage("Connected to ${device.name}");
      setState(() {
        isConnected = true;
        selectedDevice = device;
      });

      connection!.input?.listen((data) {
        final received = String.fromCharCodes(data).trim();
        logMessage("[ESP32] $received");
      }).onDone(() {
        logMessage("Disconnected by ESP32");
        setState(() => isConnected = false);
      });
    } catch (e) {
      logMessage("Connection failed: $e");
    }
  }

  void logMessage(String msg) {
    setState(() => log.insert(0, msg));
  }

  void sendCommand(String command) {
    if (isConnected && connection != null) {
      connection!.output.add(Uint8List.fromList(("$command\n").codeUnits));
      logMessage("[APP] Sent: $command");
    }
  }

  Future<void> selectDeviceAndConnect() async {
    List<BluetoothDevice> devices = await FlutterBluetoothSerial.instance.getBondedDevices();
    BluetoothDevice? device = await showDialog(
      context: context,
      builder: (context) => SimpleDialog(
        title: const Text("Select Device"),
        children: devices.map((d) => SimpleDialogOption(
          child: Text(d.name ?? d.address),
          onPressed: () => Navigator.pop(context, d),
        )).toList(),
      ),
    );

    if (device != null) {
      connectToESP32(device);
    }
  }

  Widget _buildModeButton(String label, String command) {
    return ElevatedButton(
      onPressed: () {
        sendCommand(command);
        setState(() {
          currentMode = command;
          showInputBox = (command == "mode:3" || command == "mode:6");
        });
      },
      child: Text(label),
    );
  }

  Widget _buildInputBox() {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(
        children: [
          Expanded(
            child: TextField(
              controller: _inputController,
              style: const TextStyle(color: Colors.white),
              keyboardType: TextInputType.text,
              decoration: InputDecoration(
                hintText: currentMode == "mode:3"
                    ? "Enter Gateway and Local IP"
                    : "Enter IP/MAC or Params",
                hintStyle: const TextStyle(color: Colors.white54),
                filled: true,
                fillColor: Colors.blueGrey.shade800,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(12),
                  borderSide: BorderSide.none,
                ),
              ),
            ),
          ),
          const SizedBox(width: 8),
          ElevatedButton(
            onPressed: () {
              final input = _inputController.text.trim();
              if (input.isNotEmpty) {
                sendCommand(input);
                _inputController.clear();
              }
            },
            child: const Icon(Icons.send),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey.shade900,
      appBar: AppBar(
        title: const Text('Network Multi Tester'),
        centerTitle: true,
        actions: [
          IconButton(
            icon: const Icon(Icons.bluetooth_searching),
            onPressed: selectDeviceAndConnect,
          ),
        ],
      ),
      body: Column(
        children: [
          AnimatedSwitcher(
            duration: const Duration(milliseconds: 300),
            child: isConnected
                ? Padding(
                    padding: const EdgeInsets.all(8.0),
                    child: Wrap(
                      spacing: 10,
                      runSpacing: 10,
                      alignment: WrapAlignment.center,
                      children: [
                        _buildModeButton("LAN Test", "mode:1"),
                        _buildModeButton("IP Scan", "mode:2"),
                        _buildModeButton("Router Check", "mode:3"),
                        _buildModeButton("Ping Test", "mode:4"),
                        _buildModeButton("Wi-Fi Check", "mode:5"),
                        _buildModeButton("IP Change", "mode:6"),
                      ],
                    ),
                  )
                : Padding(
                    padding: const EdgeInsets.symmetric(vertical: 30),
                    child: ElevatedButton(
                      onPressed: selectDeviceAndConnect,
                      child: const Padding(
                        padding: EdgeInsets.symmetric(horizontal: 24.0, vertical: 12),
                        child: Text("Select Device", style: TextStyle(fontSize: 18)),
                      ),
                    ),
                  ),
          ),
          if (showInputBox) _buildInputBox(),
          const Divider(color: Colors.white38),
          Expanded(
            child: ListView.builder(
              reverse: true,
              padding: const EdgeInsets.all(8),
              itemCount: log.length,
              itemBuilder: (context, index) => Text(log[index]),
            ),
          ),
        ],
      ),
    );
  }
}
