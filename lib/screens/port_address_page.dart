import 'package:flutter/material.dart';

class PortAddressPage extends StatelessWidget {
  const PortAddressPage({super.key});

  @override
  Widget build(BuildContext context) {
    const items = [
      ['20/21', 'FTP'],
      ['22', 'SSH'],
      ['23', 'Telnet'],
      ['25', 'SMTP'],
      ['53', 'DNS'],
      ['67/68', 'DHCP'],
      ['80', 'HTTP'],
      ['110', 'POP3'],
      ['123', 'NTP'],
      ['143', 'IMAP'],
      ['161/162', 'SNMP'],
      ['389', 'LDAP'],
      ['443', 'HTTPS'],
      ['502', 'Modbus/TCP'],
      ['1883', 'MQTT'],
    ];

    return Scaffold(
      appBar: AppBar(title: const Text('Port Address Reference')),
      body: ListView.separated(
        padding: const EdgeInsets.all(12),
        itemCount: items.length,
        separatorBuilder: (_, __) => const Divider(height: 1),
        itemBuilder: (_, i) => ListTile(
          leading: const Icon(Icons.local_play_outlined),
          title: Text('Port ${items[i][0]}'),
          subtitle: Text(items[i][1]),
        ),
      ),
    );
  }
}
