import 'dart:async';

import 'device_gateway.dart';

enum PreviewScenario { normal, transferFailure, unconfirmed, sameBuild }

/// In-memory demo: never reads files or invokes platform channels.
class PreviewDeviceGateway implements DeviceGateway {
  PreviewDeviceGateway({this.stepDuration = const Duration(milliseconds: 450)});
  final Duration stepDuration;
  final _events = StreamController<DeviceSnapshot>.broadcast();
  Timer? _timer;
  bool _disposed = false,
      _connected = false,
      _scanned = false,
      _failedOnce = false;
  ImportedFilm? _film;
  ImportedFirmware? _firmware;
  FilmTransfer _transfer = const FilmTransfer();
  PreviewScenario scenario = PreviewScenario.normal;
  static const warning = '模拟设备 / 演示数据，不会连接或修改真实设备';
  static const _build = 'demo-build-20261008';
  @override
  bool get supported => true;
  @override
  Stream<DeviceSnapshot> get snapshots => _events.stream;

  void _emit() {
    if (_disposed) return;
    _events.add(
      DeviceSnapshot(
        connected: _connected,
        message: warning,
        devices: _scanned && !_connected
            ? const [
                DeviceInfo(address: 'DEMO-ARK', name: 'FRAMEFILMARK / 模拟设备'),
              ]
            : const [],
        name: _connected ? 'FRAMEFILMARK / 模拟设备' : null,
        battery: _connected ? 86 : null,
        width: _connected ? 720 : null,
        height: _connected ? 480 : null,
        importedFile: _film,
        importedFirmware: _firmware,
        transfer: _transfer,
        hasFilmState: true,
        hasFirmwareState: true,
        hasTransferState: true,
        hasImportingState: true,
      ),
    );
  }

  void reset([PreviewScenario? value]) {
    _timer?.cancel();
    scenario = value ?? scenario;
    _connected = false;
    _scanned = false;
    _failedOnce = false;
    _film = null;
    _firmware = null;
    _transfer = const FilmTransfer();
    _emit();
  }

  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    if (_disposed) return;
    switch (method) {
      case 'scan':
        _scanned = true;
      case 'connect':
        _connected = true;
      case 'disconnect':
        _connected = false;
      case 'pickFilm':
        _film = const ImportedFilm(
          name: '演示画面.film',
          size: 172832,
          width: 720,
          height: 480,
        );
        _transfer = const FilmTransfer();
      case 'clearFilm':
        _film = null;
        _transfer = const FilmTransfer();
      case 'pickFirmware':
        _firmware = const ImportedFirmware(
          name: '演示固件.bin',
          size: 1048576,
          version: 'demo-1.0',
          project: 'frame_film_ark',
          elfSha256: _build,
          fileSha256: 'demo-file-fingerprint',
        );
        _transfer = const FilmTransfer(
          kind: 'firmware',
          message: '已选择演示固件元信息，未读取真实文件',
        );
      case 'clearFirmware':
        _firmware = null;
        _transfer = const FilmTransfer();
      case 'startTransfer':
        _start(false);
      case 'startFirmwareTransfer':
        _start(true);
      case 'retryTransfer':
        _start(_transfer.kind == 'firmware');
      case 'cancelTransfer':
        _timer?.cancel();
        _transfer = FilmTransfer(
          phase: 'cancelled',
          kind: _transfer.kind,
          message: '演示操作已取消，可重试',
          canRetry: true,
          cleanupCompleted: true,
        );
      case 'confirmFirmwareTransfer':
        _connected = true;
        _transfer = const FilmTransfer(
          phase: 'done',
          kind: 'firmware',
          message: '模拟重连完成，运行构建与目标一致',
          targetBuild: _build,
          success: true,
          cleanupCompleted: true,
        );
      case 'snapshot':
      case 'refresh':
        break;
      default:
        throw UnsupportedError('演示模式未实现：$method');
    }
    _emit();
  }

  void _start(bool firmware) {
    _timer?.cancel();
    final total = firmware ? _firmware?.size : _film?.size;
    if (!_connected || total == null) throw StateError('请先连接模拟设备并选择演示文件');
    if (firmware && scenario == PreviewScenario.sameBuild) {
      _transfer = const FilmTransfer(
        phase: 'done',
        kind: 'firmware',
        message: '模拟设备已运行相同构建，跳过升级',
        success: true,
        cleanupCompleted: true,
        targetBuild: _build,
      );
      return;
    }
    var step = 0;
    final phases = firmware
        ? [
            'validating',
            'connecting',
            'downloading',
            'downloading',
            'restoring',
            'applying',
            'rebooting',
            'confirming',
          ]
        : [
            'preparing',
            'connecting',
            'downloading',
            'downloading',
            'restoring',
          ];
    void advance() {
      if (step == 3 &&
          scenario == PreviewScenario.transferFailure &&
          !_failedOnce) {
        _failedOnce = true;
        _timer?.cancel();
        _transfer = FilmTransfer(
          phase: 'error',
          kind: firmware ? 'firmware' : 'film',
          message: '模拟传输中断，临时连接已清理；重试可恢复',
          canRetry: true,
          cleanupCompleted: true,
          total: total,
          received: total ~/ 2,
        );
      } else if (step >= phases.length) {
        _timer?.cancel();
        final pending = firmware && scenario == PreviewScenario.unconfirmed;
        _connected = !pending;
        _transfer = FilmTransfer(
          phase: pending ? 'unconfirmed' : 'done',
          kind: firmware ? 'firmware' : 'film',
          message: pending ? '模拟设备重启后暂未回应，请重连并确认升级' : '演示完成，未传输或修改真实设备',
          total: total,
          received: total,
          success: !pending,
          cleanupCompleted: true,
          canConfirm: pending,
          targetBuild: firmware ? _build : null,
        );
      } else {
        final phase = phases[step];
        _transfer = FilmTransfer(
          phase: phase,
          kind: firmware ? 'firmware' : 'film',
          message: '演示流程：$phase',
          total: total,
          received: step < 2
              ? 0
              : (total * ((step - 1) / 3).clamp(0, 1)).round(),
          canCancel: step < 5,
          targetBuild: firmware ? _build : null,
        );
        step++;
      }
      _emit();
    }

    advance();
    _timer = Timer.periodic(stepDuration, (_) => advance());
  }

  void dispose() {
    _disposed = true;
    _timer?.cancel();
    unawaited(_events.close());
  }
}
