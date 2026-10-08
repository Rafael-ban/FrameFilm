import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

class DeviceInfo {
  const DeviceInfo({required this.address, required this.name});
  final String address;
  final String name;
}

class DeviceSnapshot {
  const DeviceSnapshot({
    this.connected = false,
    this.message = '尚未连接设备',
    this.devices = const [],
    this.name,
    this.battery,
    this.width,
    this.height,
  });

  factory DeviceSnapshot.fromMap(Map<Object?, Object?> data) {
    return DeviceSnapshot(
      connected: data['connected'] == true,
      message: data['message'] as String? ?? '尚未连接设备',
      devices: (data['devices'] as List? ?? const []).map((value) {
        final item = value as Map;
        return DeviceInfo(
          address: item['address'] as String? ?? '',
          name: item['name'] as String? ?? '未命名设备',
        );
      }).toList(),
      name: data['name'] as String?,
      battery: (data['battery'] as num?)?.toInt(),
      width: (data['width'] as num?)?.toInt(),
      height: (data['height'] as num?)?.toInt(),
    );
  }

  final bool connected;
  final String message;
  final List<DeviceInfo> devices;
  final String? name;
  final int? battery;
  final int? width;
  final int? height;
}

abstract class DeviceGateway {
  bool get supported;
  Stream<DeviceSnapshot> get snapshots;
  Future<void> invoke(String method, [Map<String, Object?>? arguments]);
}

class PlatformDeviceGateway implements DeviceGateway {
  static const _methods = MethodChannel('org.framefilm.ark/methods');
  static const _events = EventChannel('org.framefilm.ark/events');

  @override
  bool get supported =>
      !kIsWeb && defaultTargetPlatform == TargetPlatform.android;

  @override
  Stream<DeviceSnapshot> get snapshots => supported
      ? _events.receiveBroadcastStream().map(
          (event) => DeviceSnapshot.fromMap(event as Map<Object?, Object?>),
        )
      : const Stream.empty();

  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    if (supported) await _methods.invokeMethod<Object?>(method, arguments);
  }
}

class DeviceController extends ChangeNotifier {
  DeviceController(this.gateway) {
    if (gateway.supported) {
      _subscription = gateway.snapshots.listen(
        (value) {
          snapshot = value;
          notifyListeners();
        },
        onError: (Object error) {
          errorMessage = '蓝牙状态读取失败：$error';
          notifyListeners();
        },
      );
      unawaited(command('snapshot'));
    }
  }

  final DeviceGateway gateway;
  DeviceSnapshot snapshot = const DeviceSnapshot();
  String? errorMessage;
  bool busy = false;
  bool _disposed = false;
  StreamSubscription<DeviceSnapshot>? _subscription;

  Future<void> command(String method, [Map<String, Object?>? arguments]) async {
    if (busy || !gateway.supported) return;
    busy = true;
    errorMessage = null;
    notifyListeners();
    try {
      await gateway.invoke(method, arguments);
    } catch (error) {
      errorMessage = '操作未完成：$error';
    } finally {
      busy = false;
      if (!_disposed) notifyListeners();
    }
  }

  @override
  void dispose() {
    _disposed = true;
    unawaited(_subscription?.cancel());
    super.dispose();
  }
}
