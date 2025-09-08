import 'dart:io';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

class ResultsHistoryPage extends StatefulWidget {
  const ResultsHistoryPage({super.key});

  @override
  State<ResultsHistoryPage> createState() => _ResultsHistoryPageState();
}

class _ResultsHistoryPageState extends State<ResultsHistoryPage> {
  List<String> rows = [];
  String? filePath;

  Future<void> _load() async {
    final dir = await getApplicationDocumentsDirectory();
    final f = File("${dir.path}/nettest_log.csv");
    filePath = f.path;

    if (!await f.exists()) {
      setState(() => rows = ["(no history yet)"]);
      return;
    }
    final text = await f.readAsString();
    final all = text.trim().split("\n");
    setState(() => rows = all);
  }

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("Results History (CSV)")),
      body: ListView.separated(
        itemCount: rows.length,
        separatorBuilder: (_, __) => const Divider(height: 1),
        itemBuilder: (_, i) => ListTile(
          dense: true,
          title: Text(rows[i]),
        ),
      ),
      bottomNavigationBar: filePath == null
          ? null
          : Padding(
              padding: const EdgeInsets.all(8),
              child: Text(
                "File: $filePath",
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 12, color: Colors.white60),
              ),
            ),
    );
  }
}
