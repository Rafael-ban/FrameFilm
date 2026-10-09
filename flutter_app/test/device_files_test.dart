import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

void main() {
  test('file state survives partial events and is cleared on disconnect', () {
    final original = DeviceSnapshot.fromMap({
      'connected': true,
      'files': {
        'loaded': true,
        'entries': [
          {'id': 0, 'name': 'a.film'},
        ],
      },
    });
    final retained = DeviceSnapshot.fromMap({'message': 'progress'})
        .retainingTransfer(original);
    expect(retained.files['loaded'], true);
    expect(
      DeviceSnapshot.fromMap({'connected': false})
          .retainingTransfer(retained)
          .files,
      isEmpty,
    );
  });

  test('preview file operations validate IDs, refresh after delete, and gate disconnect', () async {
    final gateway = PreviewDeviceGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.dispose();
    });
    await Future<void>.delayed(Duration.zero);
    await controller.command('listDeviceFiles');
    expect(controller.snapshot.files, isEmpty);
    await controller.command('connect', {'address': 'DEMO-ARK'});
    await Future<void>.delayed(Duration.zero);
    await controller.command('listDeviceFiles');
    await Future<void>.delayed(Duration.zero);
    expect((controller.snapshot.files['entries'] as List).length, 2);
    await controller.command('deleteDeviceFile', {
      'id': 0,
      'name': 'wrong.film',
    });
    expect(controller.errorMessage, contains('列表已变化'));
    await controller.command('deleteDeviceFile', {
      'id': 0,
      'name': '演示画面.film',
    });
    await Future<void>.delayed(Duration.zero);
    expect((controller.snapshot.files['entries'] as List).length, 1);
    await controller.command('playAnimation', {
      'intervalMs': 400,
      'loopSeconds': 1,
      'playMode': 1,
    });
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['directory'], 'animation');
    await controller.command('setDeviceFileDirectory', {'appId': 3});
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['directory'], 'animation');
    await controller.command('displayDeviceFile', {
      'id': 0,
      'name': '演示动画.film',
    });
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['currentId'], 0);
    await controller.command('disconnect');
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files, isEmpty);
  });
}
