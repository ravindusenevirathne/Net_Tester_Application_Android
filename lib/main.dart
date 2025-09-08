import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle; // <-- for loading logo bytes
import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';
import 'package:permission_handler/permission_handler.dart';

// Splash
import 'screens/splash_screen.dart';

// Export helpers
import 'package:path_provider/path_provider.dart';
import 'package:pdf/pdf.dart';
import 'package:pdf/widgets.dart' as pw;
import 'package:open_filex/open_filex.dart';
import 'package:share_plus/share_plus.dart';

// Drawer-linked screens
import 'screens/results_history_page.dart';
import 'screens/saved_files_page.dart';
import 'screens/rj45_color_page.dart';
import 'screens/port_address_page.dart';

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
      home: const SplashScreen(),
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

  // generic one-line input (kept for mode:3 and mode:6)
  final TextEditingController _genericController = TextEditingController();

  // Mode 2: IP Scan -> single field
  final TextEditingController _scanSingleCtrl = TextEditingController();

  // Mode 4: Ping
  final TextEditingController _pingIpCtrl = TextEditingController();
  final TextEditingController _pingMaskCtrl = TextEditingController();

  String currentMode = ""; // e.g. "mode:2"
  bool get _showAnyInput =>
      currentMode == "mode:2" ||
      currentMode == "mode:3" ||
      currentMode == "mode:4" ||
      currentMode == "mode:6";

  @override
  void initState() {
    super.initState();
    requestBluetoothPermissions();
  }

  // ---------- Permissions ----------
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

  // ---------- BT Connect / IO ----------
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
        if (received.isNotEmpty) {
          logMessage("[ESP32] $received");
        }
      }).onDone(() {
        logMessage("Disconnected by ESP32");
        setState(() => isConnected = false);
      });
    } catch (e) {
      logMessage("Connection failed: $e");
    }
  }

  void logMessage(String msg) {
    setState(() => log.insert(0, msg)); // newest first
  }

  void _sendRaw(String line) {
    if (isConnected && connection != null) {
      connection!.output.add(Uint8List.fromList(("$line\n").codeUnits));
      logMessage("[APP] Sent: $line");
    } else {
      logMessage("⚠️ Not connected");
    }
  }

  Future<void> selectDeviceAndConnect() async {
    final devices = await FlutterBluetoothSerial.instance.getBondedDevices();

    final device = await showDialog<BluetoothDevice>(
      context: context,
      builder: (context) => SimpleDialog(
        title: const Text("Select Device"),
        children: devices
            .map(
              (d) => SimpleDialogOption(
                child: Text(d.name ?? d.address),
                onPressed: () => Navigator.pop(context, d),
              ),
            )
            .toList(),
      ),
    );

    if (device != null) {
      connectToESP32(device);
    }
  }

  // ---------- Mode Buttons ----------
  Widget _buildModeButton(String label, String command) {
    return ElevatedButton(
      onPressed: () {
        _sendRaw(command); // notify ESP32 we changed mode
        setState(() {
          currentMode = command;
        });
      },
      child: Text(label),
    );
  }

  // ---------- Soft validators ----------
  bool _looksLikeIp(String s) {
    final parts = s.split('.');
    if (parts.length != 4) return false;
    for (final p in parts) {
      final v = int.tryParse(p);
      if (v == null || v < 0 || v > 255) return false;
    }
    return true;
  }

  bool _looksLikeCidr(String s) {
    final parts = s.split('/');
    if (parts.length != 2) return false;
    if (!_looksLikeIp(parts[0])) return false;
    final m = int.tryParse(parts[1]);
    return m != null && m >= 0 && m <= 32;
  }

  bool _looksLikeRange(String s) {
    final parts = s.split('-');
    if (parts.length != 2) return false;
    return _looksLikeIp(parts[0].trim()) && _looksLikeIp(parts[1].trim());
  }

  // ---------- Input rows ----------
  /// Mode 2: single field for CIDR or range
  Widget _buildIpScanSingleInput() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 6, 12, 6),
      child: Row(
        children: [
          Expanded(
            child: TextField(
              controller: _scanSingleCtrl,
              style: const TextStyle(color: Colors.white),
              keyboardType: TextInputType.text,
              decoration: InputDecoration(
                hintText: "192.168.1.0/24  OR  192.168.1.1-192.168.1.254",
                hintStyle: const TextStyle(color: Colors.white54),
                filled: true,
                fillColor: Colors.blueGrey.shade800,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(14),
                  borderSide: BorderSide.none,
                ),
              ),
              onSubmitted: (_) => _sendScanSingle(),
            ),
          ),
          const SizedBox(width: 8),
          ElevatedButton(
            onPressed: _sendScanSingle,
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 14),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(14),
              ),
            ),
            child: const Icon(Icons.send),
          ),
        ],
      ),
    );
  }

  void _sendScanSingle() {
    final text = _scanSingleCtrl.text.trim();
    if (text.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Enter CIDR or IP range")),
      );
      return;
    }
    final ok = _looksLikeCidr(text) || _looksLikeRange(text) || _looksLikeIp(text);
    if (!ok) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text("Use CIDR or range: 192.168.1.0/24 OR 192.168.1.1-192.168.1.254"),
        ),
      );
      return;
    }
    _sendRaw("SCAN:$text");
  }

  /// Mode 4: Ping IP + mask
  Widget _buildPingInputs() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 6, 12, 6),
      child: Row(
        children: [
          Expanded(
            child: TextField(
              controller: _pingIpCtrl,
              style: const TextStyle(color: Colors.white),
              keyboardType: TextInputType.number,
              decoration: InputDecoration(
                hintText: "IP (e.g. 8.8.8.8 or 192.168.1.10)",
                hintStyle: const TextStyle(color: Colors.white54),
                filled: true,
                fillColor: Colors.blueGrey.shade800,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(14),
                  borderSide: BorderSide.none,
                ),
              ),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: TextField(
              controller: _pingMaskCtrl,
              style: const TextStyle(color: Colors.white),
              keyboardType: TextInputType.number,
              decoration: InputDecoration(
                hintText: "Subnet Mask (e.g. 255.255.255.0)",
                hintStyle: const TextStyle(color: Colors.white54),
                filled: true,
                fillColor: Colors.blueGrey.shade800,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(14),
                  borderSide: BorderSide.none,
                ),
              ),
            ),
          ),
          const SizedBox(width: 8),
          ElevatedButton(
            onPressed: () {
              final ip = _pingIpCtrl.text.trim();
              final mask = _pingMaskCtrl.text.trim();
              if (!_looksLikeIp(ip) || !_looksLikeIp(mask)) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text("Enter valid IP & subnet mask")),
                );
                return;
              }
              _sendRaw("PING:$ip,$mask");
            },
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 14),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(14),
              ),
            ),
            child: const Icon(Icons.send),
          ),
        ],
      ),
    );
  }

  /// Generic input for mode:3 and mode:6
  Widget _buildGenericInput() {
    final hint =
        currentMode == "mode:3" ? "Enter Gateway and Local IP" : "Enter IP/MAC or Params";
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 6, 12, 6),
      child: Row(
        children: [
          Expanded(
            child: TextField(
              controller: _genericController,
              style: const TextStyle(color: Colors.white),
              keyboardType: TextInputType.text,
              decoration: InputDecoration(
                hintText: hint,
                hintStyle: const TextStyle(color: Colors.white54),
                filled: true,
                fillColor: Colors.blueGrey.shade800,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(14),
                  borderSide: BorderSide.none,
                ),
              ),
              onSubmitted: (_) {
                final input = _genericController.text.trim();
                if (input.isNotEmpty) {
                  _sendRaw(input);
                  _genericController.clear();
                }
              },
            ),
          ),
          const SizedBox(width: 8),
          ElevatedButton(
            onPressed: () {
              final input = _genericController.text.trim();
              if (input.isNotEmpty) {
                _sendRaw(input);
                _genericController.clear();
              }
            },
            style: ElevatedButton.styleFrom(
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 14),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(14),
              ),
            ),
            child: const Icon(Icons.send),
          ),
        ],
      ),
    );
  }

  // ------------------ SAVE / EXPORT ------------------
  Future<void> _onSavePressed() async {
    if (log.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Nothing to save (log is empty)")),
      );
      return;
    }

    final choice = await showModalBottomSheet<String>(
      context: context,
      showDragHandle: true,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const ListTile(
              title: Text("Save test result"),
              subtitle: Text("Choose a format"),
            ),
            ListTile(
              leading: const Icon(Icons.description),
              title: const Text("Save as TXT"),
              onTap: () => Navigator.pop(ctx, "txt"),
            ),
            ListTile(
              leading: const Icon(Icons.picture_as_pdf),
              title: const Text("Save as PDF"),
              onTap: () => Navigator.pop(ctx, "pdf"),
            ),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );

    if (!mounted || choice == null) return;

    try {
      final String path = (choice == "txt") ? await _saveAsTxt() : await _saveAsPdf();

      // Append summary into CSV history
      await _appendCsvLog();

      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text("Saved to:\n$path"),
          action: SnackBarAction(
            label: "OPEN",
            onPressed: () => OpenFilex.open(path),
          ),
        ),
      );

      // Optional quick share
      await showDialog(
        context: context,
        builder: (ctx) => AlertDialog(
          title: const Text("Result saved"),
          content: Text(path),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text("Close"),
            ),
            TextButton(
              onPressed: () {
                Navigator.pop(ctx);
                Share.shareXFiles([XFile(path)]);
              },
              child: const Text("Share"),
            ),
          ],
        ),
      );
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text("Failed to save: $e")),
      );
    }
  }

  Future<String> _saveAsTxt() async {
    final dir = await getApplicationDocumentsDirectory();
    final ts =
        DateTime.now().toIso8601String().replaceAll(":", "-").replaceAll(".", "-");

    final file = File("${dir.path}/nettest-$ts.txt");
    final buffer = StringBuffer()
      ..writeln("Portable Embedded Network Multi Tester")
      ..writeln("Time: ${DateTime.now().toLocal()}")
      ..writeln("Mode: $currentMode")
      ..writeln(
          "Device: ${selectedDevice?.name ?? 'N/A'} (${selectedDevice?.address ?? 'N/A'})")
      ..writeln("Connected: $isConnected")
      ..writeln("\n--- LOG (latest first) ---");

    for (final line in log) {
      buffer.writeln(line);
    }
    await file.writeAsString(buffer.toString());
    return file.path;
  }

  Future<String> _saveAsPdf() async {
    final dir = await getApplicationDocumentsDirectory();
    final ts =
        DateTime.now().toIso8601String().replaceAll(":", "-").replaceAll(".", "-");

    // Load logo from assets
    final logoBytes = await rootBundle.load('assets/images/net_logo.png');
    final logoImage = pw.MemoryImage(logoBytes.buffer.asUint8List());

    final pdf = pw.Document();

    final headerLines = [
      "Portable Embedded Network Multi Tester",
      "Time: ${DateTime.now().toLocal()}",
      "Mode: $currentMode",
      "Device: ${selectedDevice?.name ?? 'N/A'} (${selectedDevice?.address ?? 'N/A'})",
      "Connected: $isConnected",
    ];

    pdf.addPage(
      pw.MultiPage(
        pageFormat: PdfPageFormat.a4,
        margin: const pw.EdgeInsets.all(24),
        // HEADER on every page with logo at top-right
        header: (context) => pw.Row(
          crossAxisAlignment: pw.CrossAxisAlignment.center,
          children: [
            pw.Expanded(
              child: pw.Text(
                headerLines.first,
                style: pw.TextStyle(fontSize: 16, fontWeight: pw.FontWeight.bold),
              ),
            ),
            pw.SizedBox(width: 8),
            pw.Container(
              height: 32,
              width: 32,
              child: pw.Image(logoImage, fit: pw.BoxFit.contain),
            ),
          ],
        ),
        build: (context) => [
          pw.SizedBox(height: 6),
          pw.Text(headerLines[1]),
          pw.Text(headerLines[2]),
          pw.Text(headerLines[3]),
          pw.Text(headerLines[4]),
          pw.SizedBox(height: 12),
          pw.Text("LOG (latest first)",
              style: pw.TextStyle(fontWeight: pw.FontWeight.bold)),
          pw.SizedBox(height: 6),
          pw.Container(
            decoration: pw.BoxDecoration(border: pw.Border.all(width: 0.5)),
            padding: const pw.EdgeInsets.all(8),
            child: pw.Column(
              crossAxisAlignment: pw.CrossAxisAlignment.start,
              children: log
                  .map((line) => pw.Padding(
                        padding: const pw.EdgeInsets.symmetric(vertical: 2),
                        child: pw.Text(line),
                      ))
                  .toList(),
            ),
          ),
          pw.SizedBox(height: 12),
          pw.Align(
            alignment: pw.Alignment.centerRight,
            child: pw.Text("Generated by Net Checker App",
                style: const pw.TextStyle(fontSize: 10)),
          ),
        ],
      ),
    );

    final path = "${dir.path}/nettest-$ts.pdf";
    await File(path).writeAsBytes(await pdf.save());
    return path;
  }

  /// Append one compact summary row to CSV history file.
  Future<void> _appendCsvLog() async {
    final dir = await getApplicationDocumentsDirectory();
    final file = File("${dir.path}/nettest_log.csv");
    final exists = await file.exists();

    final ts = DateTime.now().toIso8601String();
    final device =
        "${selectedDevice?.name ?? 'N/A'}(${selectedDevice?.address ?? 'N/A'})";
    final firstLine = (log.isNotEmpty ? log.first.replaceAll(",", " ") : "");

    final line = '$ts,$currentMode,$device,$isConnected,$firstLine';

    if (!exists) {
      await file.writeAsString(
        "timestamp,mode,device,connected,firstLine\n",
        mode: FileMode.write,
      );
    }
    await file.writeAsString("$line\n", mode: FileMode.append);
  }

  // ------------------ UI ------------------
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey.shade900,
      drawer: _buildAppDrawer(context),
      appBar: AppBar(
        title: const Text('NET Checker'),
        centerTitle: true,
      ),
      
      body: Container(
        margin: const EdgeInsets.symmetric(vertical: 8, horizontal: 6),
        decoration: BoxDecoration(
          color: Colors.grey.shade900,
          border: Border.all(color: Colors.blueGrey.shade700, width: 1.2),
          borderRadius: BorderRadius.circular(12),
        ),
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 8.0),
          child: Column(
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
                            padding:
                                EdgeInsets.symmetric(horizontal: 24.0, vertical: 12),
                            child: Text("Connect", style: TextStyle(fontSize: 18)),
                          ),
                        ),
                      ),
              ),

              // mode-dependent inputs
              if (_showAnyInput)
                if (currentMode == "mode:2")
                  _buildIpScanSingleInput()
                else if (currentMode == "mode:4")
                  _buildPingInputs()
                else
                  _buildGenericInput(),

              const Divider(color: Colors.white38),
              Expanded(
                child: ListView.builder(
                  reverse: true,
                  padding: const EdgeInsets.fromLTRB(8, 8, 8, 40),
                  itemCount: log.length,
                  itemBuilder: (context, index) => Text(log[index]),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  // ------------------ Drawer ------------------
  Drawer _buildAppDrawer(BuildContext context) {
    return Drawer(
      child: SafeArea(
        child: Column(
          children: [
            // Header with big, full-width logo + app name
            SizedBox(
              height: 160,
              child: DrawerHeader(
                margin: EdgeInsets.zero,
                padding: EdgeInsets.zero,
                child: Stack(
                  fit: StackFit.expand,
                  children: [
                    Image.asset(
                      'assets/images/netlogo.png',
                      fit: BoxFit.cover,
                      filterQuality: FilterQuality.high,
                    ),
                    Container(
                      color: Colors.blueGrey.shade900.withOpacity(0.25),
                    ),
                    Align(
                      alignment: Alignment.bottomLeft,
                      child: Padding(
                        padding: const EdgeInsets.all(12),
                        child: const Text(
                          'Portable Embedded Network Multi Tester',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),

            ListTile(
              leading: const Icon(Icons.history),
              title: const Text('Previous logs'),
              onTap: () {
                Navigator.pop(context);
                Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => const ResultsHistoryPage()),
                );
              },
            ),
            ListTile(
              leading: const Icon(Icons.save_alt),
              title: const Text('Save'),
              onTap: () {
                Navigator.pop(context);
                _onSavePressed();
              },
            ),
            ListTile(
              leading: const Icon(Icons.insert_drive_file),
              title: const Text('Saved files'),
              onTap: () {
                Navigator.pop(context);
                Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => const SavedFilesPage()),
                );
              },
            ),
            ListTile(
              leading: const Icon(Icons.bluetooth),
              title: const Text('BT connection'),
              onTap: () {
                Navigator.pop(context);
                selectDeviceAndConnect();
              },
            ),
            ListTile(
              leading: const Icon(Icons.palette),
              title: const Text('RJ45 color code'),
              onTap: () {
                Navigator.pop(context);
                Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => const Rj45ColorPage()),
                );
              },
            ),
            ListTile(
              leading: const Icon(Icons.list_alt),
              title: const Text('Port address'),
              onTap: () {
                Navigator.pop(context);
                Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => const PortAddressPage()),
                );
              },
            ),
            const Spacer(),
            ListTile(
              leading: const Icon(Icons.info_outline),
              title: const Text('About'),
              onTap: () {
                Navigator.pop(context);
                showAboutDialog(
                  context: context,
                  applicationName: 'Portable Embedded Network Multi Tester',
                  applicationVersion: 'v1.0.0',
                  applicationIcon: const CircleAvatar(
                    radius: 18,
                    backgroundImage: AssetImage('assets/images/net_logo.png'),
                  ),
                  children: const [
                    SizedBox(height: 8),
                    Text(
                      'A portable Flutter app for cable testing, router MAC check, '
                      'IP scanning, ping, SNMP and more. Connected via Bluetooth to ESP32.',
                    ),
                  ],
                );
              },
            ),
          ],
        ),
      ),
    );
  }
}
