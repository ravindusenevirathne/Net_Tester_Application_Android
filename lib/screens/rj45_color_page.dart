import 'package:flutter/material.dart';

class Rj45ColorPage extends StatelessWidget {
  const Rj45ColorPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('RJ45 Color Code')),
      body: Container(
        color: Colors.black, // gives the image nice contrast
        width: double.infinity,
        height: double.infinity,
        child: InteractiveViewer(
          maxScale: 5.0,
          minScale: 0.5,
          child: Center(
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Image.asset(
                'assets/images/rj45_color.png',
                fit: BoxFit.contain,
                errorBuilder: (context, error, stack) => const Text(
                  '⚠️ Image not found: assets/images/rj45_color.png',
                  style: TextStyle(color: Colors.white70),
                  textAlign: TextAlign.center,
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
