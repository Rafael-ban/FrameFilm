import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

class DeviceInfo {
  const DeviceInfo({required this.address, required this.name});
  final String address;
  final String name;
}

class ImportedFilm {
  const ImportedFilm({
    required this.name,
    required this.size,
    this.width,
    this.height,
  });
  factory ImportedFilm.fromMap(Map data) => ImportedFilm(
    name: data['name'] as String? ?? 'film',
    size: (data['size'] as num?)?.toInt() ?? 0,
    width: (data['width'] as num?)?.toInt(),
    height: (data['height'] as num?)?.toInt(),
  );
  final String name;
  final int size;
  final int? width;
  final int? height;
}

class ImportedFirmware {
  const ImportedFirmware({
    required this.name,
    required this.size,
    required this.version,
    required this.project,
    required this.elfSha256,
    required this.fileSha256,
  });
  factory ImportedFirmware.fromMap(Map data) => ImportedFirmware(
    name: data['name'] as String? ?? 'firmware.bin',
    size: (data['size'] as num?)?.toInt() ?? 0,
    version: data['version'] as String? ?? '',
    project: data['project'] as String? ?? '',
    elfSha256: data['elfSha256'] as String? ?? '',
    fileSha256: data['fileSha256'] as String? ?? '',
  );
  final String name;
  final int size;
  final String version;
  final String project;
  final String elfSha256;
  final String fileSha256;
}

class FilmTransfer {
  const FilmTransfer({
    this.phase = 'idle',
    this.message = '选择 film 文件后开始直传',
    this.received = 0,
    this.total = 0,
    this.canCancel = false,
    this.canRetry = false,
    this.cleanupCompleted = false,
    this.success = false,
    this.kind = 'film',
    this.targetVersion,
    this.targetBuild,
    this.canConfirm = false,
  });
  factory FilmTransfer.fromMap(Map data) => FilmTransfer(
    phase: data['phase'] as String? ?? 'idle',
    message: data['message'] as String? ?? '',
    received: (data['received'] as num?)?.toInt() ?? 0,
    total: (data['total'] as num?)?.toInt() ?? 0,
    canCancel: data['canCancel'] == true,
    canRetry: data['canRetry'] == true,
    cleanupCompleted: data['cleanupCompleted'] == true,
    success: data['success'] == true,
    kind: data['kind'] as String? ?? 'film',
    targetVersion: data['targetVersion'] as String?,
    targetBuild: data['targetBuild'] as String?,
    canConfirm: data['canConfirm'] == true,
  );
  final String phase;
  final String message;
  final int received;
  final int total;
  final bool canCancel;
  final bool canRetry;
  final bool cleanupCompleted;
  final bool success;
  final String kind;
  final String? targetVersion;
  final String? targetBuild;
  final bool canConfirm;
  bool get active =>
      canCancel ||
      const {
        'preparing',
        'connecting',
        'downloading',
        'restoring',
        'cancelling',
        'cleanup',
        'validating',
        'ready',
        'applying',
        'rebooting',
        'confirming',
      }.contains(phase);
  double? get progress => total > 0 ? (received / total).clamp(0.0, 1.0) : null;
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
    this.importedFile,
    this.importedFirmware,
    this.importing = false,
    this.transfer = const FilmTransfer(),
    this.hasFilmState = false,
    this.hasFirmwareState = false,
    this.hasImportingState = false,
    this.hasTransferState = false,
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
      importedFile: data['importedFile'] is Map
          ? ImportedFilm.fromMap(data['importedFile'] as Map)
          : null,
      importing: data['importing'] == true,
      importedFirmware: data['importedFirmware'] is Map
          ? ImportedFirmware.fromMap(data['importedFirmware'] as Map)
          : null,
      transfer: data['transfer'] is Map
          ? FilmTransfer.fromMap(data['transfer'] as Map)
          : const FilmTransfer(),
      hasFilmState: data.containsKey('importedFile'),
      hasFirmwareState: data.containsKey('importedFirmware'),
      hasImportingState: data.containsKey('importing'),
      hasTransferState: data.containsKey('transfer'),
    );
  }

  final bool connected;
  final String message;
  final List<DeviceInfo> devices;
  final String? name;
  final int? battery;
  final int? width;
  final int? height;
  final ImportedFilm? importedFile;
  final ImportedFirmware? importedFirmware;
  final bool importing;
  final FilmTransfer transfer;
  final bool hasFilmState;
  final bool hasFirmwareState;
  final bool hasImportingState;
  final bool hasTransferState;

  DeviceSnapshot retainingTransfer(DeviceSnapshot previous) => DeviceSnapshot(
    connected: connected,
    message: message,
    devices: devices,
    name: name,
    battery: battery,
    width: width,
    height: height,
    importedFile: hasFilmState || importedFile != null
        ? importedFile
        : previous.importedFile,
    importing: hasImportingState || importing ? importing : previous.importing,
    importedFirmware: hasFirmwareState || importedFirmware != null
        ? importedFirmware
        : previous.importedFirmware,
    transfer: hasTransferState || transfer.phase != 'idle'
        ? transfer
        : previous.transfer,
  );
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
          snapshot = value.retainingTransfer(snapshot);
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
    final cancelling = method == 'cancelTransfer';
    if (!gateway.supported || (busy && !cancelling)) return;
    if ((snapshot.transfer.active || snapshot.transfer.canConfirm) &&
        const {
          'pickFilm',
          'clearFilm',
          'connect',
          'disconnect',
          'scan',
          'startTransfer',
          'pickFirmware',
          'clearFirmware',
          'startFirmwareTransfer',
        }.contains(method)) {
      return;
    }
    if (snapshot.importing &&
        const {
          'pickFilm',
          'clearFilm',
          'startTransfer',
          'retryTransfer',
          'pickFirmware',
          'clearFirmware',
          'startFirmwareTransfer',
        }.contains(method)) {
      return;
    }
    if (method == 'startTransfer' &&
        (snapshot.importedFile == null || !snapshot.connected)) {
      return;
    }
    if (method == 'startFirmwareTransfer' &&
        (snapshot.importedFirmware == null || !snapshot.connected)) {
      return;
    }
    if (snapshot.transfer.canRetry &&
        !snapshot.transfer.cleanupCompleted &&
        const {
          'pickFilm',
          'clearFilm',
          'startTransfer',
          'pickFirmware',
          'clearFirmware',
          'startFirmwareTransfer',
        }.contains(method)) {
      return;
    }
    if (method == 'retryTransfer' &&
        (!snapshot.transfer.canRetry ||
            snapshot.transfer.active ||
            (snapshot.transfer.kind == 'firmware'
                ? snapshot.importedFirmware == null
                : snapshot.importedFile == null) ||
            !snapshot.connected)) {
      return;
    }
    if (method == 'confirmFirmwareTransfer' &&
        (!snapshot.transfer.canConfirm || snapshot.transfer.active)) {
      return;
    }
    if (cancelling && !snapshot.transfer.canCancel) return;
    if (!cancelling) busy = true;
    errorMessage = null;
    notifyListeners();
    try {
      await gateway.invoke(method, arguments);
    } catch (error) {
      errorMessage = '操作未完成：$error';
    } finally {
      if (!cancelling) busy = false;
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
