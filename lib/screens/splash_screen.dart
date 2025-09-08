import 'package:flutter/material.dart';
import 'dart:async';

import '../main.dart'; // for MyApp or BluetoothHomePage

class SplashScreen extends StatefulWidget {
  const SplashScreen({super.key});

  @override
  State<SplashScreen> createState() => _SplashScreenState();
}

class _SplashScreenState extends State<SplashScreen> {
  @override
  void initState() {
    super.initState();
    // Navigate after 2.5 seconds
    Timer(const Duration(seconds: 3), () {
      Navigator.of(context).pushReplacement(
        MaterialPageRoute(builder: (_) => const BluetoothHomePage()),
      );
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey.shade900,
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Image.asset(
              'assets/images/net_logo.png',
              width: 200,
              height: 150,
            ),
            const SizedBox(height: 20),
            const Text(
              "Portable Embedded Network Multi Tester",
              textAlign: TextAlign.center,
              style: TextStyle(
                fontSize: 16,
                color: Colors.white70,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 30),
            const CircularProgressIndicator(
              color: Colors.blueGrey,
            ),
          ],
        ),
      ),
    );
  }
}
