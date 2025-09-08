import 'dart:io';
import 'package:flutter/material.dart';
import 'package:open_filex/open_filex.dart';
import 'package:path_provider/path_provider.dart';
import 'package:share_plus/share_plus.dart';

class SavedFilesPage extends StatefulWidget {
  const SavedFilesPage({super.key});

  @override
  State<SavedFilesPage> createState() => _SavedFilesPageState();
}

class _SavedFilesPageState extends State<SavedFilesPage> {
  late Future<List<FileSystemEntity>> _filesFuture;

  @override
  void initState() {
    super.initState();
    _filesFuture = _loadFiles();
  }

  Future<List<FileSystemEntity>> _loadFiles() async {
    final dir = await getApplicationDocumentsDirectory();
    final list = dir.listSync()
      ..sort((a, b) => b.statSync().modified.compareTo(a.statSync().modified));
    // show only txt/pdf
    return list.where((e) {
      final name = e.path.toLowerCase();
      return name.endsWith('.txt') || name.endsWith('.pdf');
    }).toList();
  }

  Future<void> _delete(FileSystemEntity ent) async {
    try {
      await ent.delete();
      setState(() => _filesFuture = _loadFiles());
      // ignore: use_build_context_synchronously
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Deleted')),
      );
    } catch (e) {
      // ignore: use_build_context_synchronously
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Delete failed: $e')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Saved files')),
      body: FutureBuilder<List<FileSystemEntity>>(
        future: _filesFuture,
        builder: (context, snap) {
          if (snap.connectionState != ConnectionState.done) {
            return const Center(child: CircularProgressIndicator());
          }
          final files = snap.data ?? [];
          if (files.isEmpty) {
            return const Center(child: Text('No files yet'));
          }
          return ListView.separated(
            itemCount: files.length,
            separatorBuilder: (_, __) => const Divider(height: 1),
            itemBuilder: (context, i) {
              final f = files[i];
              final stat = f.statSync();
              final name = f.uri.pathSegments.last;
              final date = stat.modified.toLocal();

              return ListTile(
                leading: Icon(
                  name.toLowerCase().endsWith('.pdf')
                      ? Icons.picture_as_pdf
                      : Icons.description,
                ),
                title: Text(name),
                subtitle: Text(date.toString()),
                onTap: () => OpenFilex.open(f.path),
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    IconButton(
                      tooltip: 'Share',
                      icon: const Icon(Icons.share),
                      onPressed: () => Share.shareXFiles([XFile(f.path)]),
                    ),
                    IconButton(
                      tooltip: 'Delete',
                      icon: const Icon(Icons.delete_outline),
                      onPressed: () => _delete(f),
                    ),
                  ],
                ),
              );
            },
          );
        },
      ),
    );
  }
}
