import 'package:flutter/material.dart';

import 'device_gateway.dart';

/// Manages the device's active file directory through the existing BLE protocol.
class DeviceFilesPage extends StatelessWidget {
  const DeviceFilesPage({super.key, required this.controller});
  final DeviceController controller;

  Future<void> _delete(BuildContext context, Map entry) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('删除设备文件？'),
        content: Text('“${entry['name']}”将从设备 SD 卡删除。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('删除'),
          ),
        ],
      ),
    );
    if (confirmed == true) {
      await controller.command('deleteDeviceFile', {
        'id': entry['id'],
        'name': entry['name'],
      });
    }
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: controller,
    builder: (context, _) {
      final snapshot = controller.snapshot;
      final files = snapshot.files;
      final entries = (files['entries'] as List? ?? const [])
          .whereType<Map>()
          .toList();
      final available = controller.canEditSettings;
      final directory = switch (files['directory']) {
        'film' => '图片 / film',
        'animation' => '动画 / animation',
        _ => '设备当前目录',
      };
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text('设备文件', style: Theme.of(context).textTheme.headlineSmall),
          const SizedBox(height: 8),
          Text(snapshot.connected ? directory : '连接设备后读取 SD 卡中的 film 文件'),
          const SizedBox(height: 16),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              FilledButton.tonalIcon(
                onPressed: available
                    ? () => controller.command('listDeviceFiles')
                    : null,
                icon: const Icon(Icons.refresh),
                label: const Text('刷新列表'),
              ),
              OutlinedButton(
                onPressed: available
                    ? () => controller.command('setDeviceFileDirectory', {
                        'appId': 0,
                      })
                    : null,
                child: const Text('图片目录'),
              ),
              OutlinedButton(
                onPressed: available
                    ? () => controller.command('setDeviceFileDirectory', {
                        'appId': 3,
                      })
                    : null,
                child: const Text('动画目录'),
              ),
            ],
          ),
          const SizedBox(height: 12),
          const Text(
            '目录切换会切换设备上的图片或动画 app。空目录或未注册的 app 可能无法进入；可刷新设备当前目录。协议不提供任意路径浏览。',
          ),
          if (controller.busy || snapshot.settingsBusy) ...[
            const SizedBox(height: 16),
            const LinearProgressIndicator(),
          ],
          if (controller.errorMessage != null) ...[
            const SizedBox(height: 12),
            Text(
              controller.errorMessage!,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
          ],
          if (snapshot.settingsMessage != null)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Text(snapshot.settingsMessage!),
            ),
          const SizedBox(height: 16),
          if (entries.isEmpty)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 32),
              child: Column(
                children: [
                  const Icon(Icons.folder_open, size: 40),
                  const SizedBox(height: 12),
                  Text(
                    !snapshot.connected
                        ? '尚未连接设备'
                        : files['loaded'] == true
                        ? '当前目录没有文件'
                        : '点击刷新读取设备文件',
                  ),
                ],
              ),
            ),
          for (final entry in entries)
            Card(
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.insert_drive_file_outlined),
                        const SizedBox(width: 10),
                        Expanded(
                          child: Text(
                            entry['name'] as String? ?? '',
                            style: Theme.of(context).textTheme.titleSmall,
                          ),
                        ),
                        if (files['currentId'] == entry['id'])
                          const Chip(label: Text('当前 ID')),
                      ],
                    ),
                    const SizedBox(height: 8),
                    Wrap(
                      spacing: 8,
                      children: [
                        TextButton.icon(
                          onPressed: available
                              ? () => controller.command('displayDeviceFile', {
                                  'id': entry['id'],
                                  'name': entry['name'],
                                })
                              : null,
                          icon: const Icon(Icons.slideshow),
                          label: const Text('显示'),
                        ),
                        TextButton.icon(
                          onPressed: available
                              ? () => _delete(context, entry)
                              : null,
                          icon: const Icon(Icons.delete_outline),
                          label: const Text('删除'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
          const SizedBox(height: 12),
          const Text('显示状态来自设备文件 ID 回读，不代表电子纸已完成刷新。删除后重新读取列表，文件 ID 可能改变。'),
        ],
      );
    },
  );
}
