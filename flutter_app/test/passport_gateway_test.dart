import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

class Gateway implements DeviceGateway {
  final events = StreamController<DeviceSnapshot>.broadcast();
  final calls = <String>[];
  @override
  bool get supported => true;
  @override
  Stream<DeviceSnapshot> get snapshots => events.stream;
  @override
  Future<void> invoke(String method, [Map<String, Object?>? args]) async {
    calls.add(method);
  }
}

void main() {
  testWidgets(
    'passport activity blocks unrelated writes but permits cancellation',
    (tester) async {
      final gateway = Gateway();
      final controller = DeviceController(gateway);
      addTearDown(() {
        controller.dispose();
        gateway.events.close();
      });
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': true,
          'passport': {'phase': 'sending', 'canCancel': true},
        }),
      );
      await tester.pump();
      await controller.command('remoteKey', {'key': 0});
      await controller.command('downloadFirmware');
      await controller.command('pickFilm');
      expect(controller.canEditSettings, false);
      expect(gateway.calls, ['snapshot']);
      await controller.command('cancelPassport');
      expect(gateway.calls.last, 'cancelPassport');
      final partial = DeviceSnapshot.fromMap({'battery': 82})
          .retainingTransfer(controller.snapshot);
      expect(partial.passportBusy, true);
    },
  );
  testWidgets(
    'cancel retains draft input and reconnect allows fresh simulated save/read',
    (tester) async {
      final gateway = PreviewDeviceGateway(
        stepDuration: const Duration(milliseconds: 10),
      );
      final controller = DeviceController(gateway);
      addTearDown(() {
        controller.dispose();
        gateway.dispose();
      });
      await tester.pump();
      await controller.command('connect');
      await tester.pump();
      const document = '{"version":1,"codename":"博士"}';
      final bin = Uint8List(33456);
      await controller.command('savePassport', {'json': document, 'bin': bin});
      await tester.pump();
      expect(controller.snapshot.passportBusy, true);
      await controller.command('cancelPassport');
      await tester.pump(const Duration(milliseconds: 100));
      expect(controller.snapshot.passport['phase'], 'cancelled');
      expect(controller.snapshot.connected, false);
      await tester.pump();
      await controller.command('connect');
      await tester.pump();
      await controller.command('savePassport', {'json': document, 'bin': bin});
      await tester.pump(const Duration(milliseconds: 50));
      expect(controller.snapshot.passport['phase'], 'done');
      final revision = controller.snapshot.passport['revision'] as int;
      await controller.command('readPassport');
      await tester.pump(const Duration(milliseconds: 50));
      expect(controller.snapshot.passport['json'], document);
      expect(controller.snapshot.passport['revision'], greaterThan(revision));
      gateway.reset();
      await tester.pump();
      expect(controller.snapshot.passport, isEmpty);
    },
  );
}
