import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';

import 'widget_test.dart' show FakeGateway, openPage;

void main() {
  for (final kind in ['firmware', 'film']) {
    for (final phase in ['downloading', 'done', 'cancelled', 'unconfirmed']) {
      testWidgets('$kind $phase stays on its own transfer page', (
        tester,
      ) async {
        final gateway = FakeGateway();
        addTearDown(gateway.events.close);
        await tester.pumpWidget(FrameFilmApp(gateway: gateway));
        final active = phase == 'downloading';
        final pending = phase == 'unconfirmed';
        gateway.events.add(
          DeviceSnapshot.fromMap({
            'connected': true,
            'importedFile': {'name': 'photo.film', 'size': 100},
            'importedFirmware': {'name': 'ark.bin', 'size': 100},
            'transfer': {
              'kind': kind,
              'phase': phase,
              'message': 'private-$kind-$phase',
              'canCancel': active,
              'canConfirm': pending,
              'canRetry': phase == 'cancelled',
              'cleanupCompleted': true,
              'success': phase == 'done',
              'received': 50,
              'total': 100,
            },
          }),
        );
        await tester.pump(const Duration(seconds: 2));
        final otherPage = kind == 'firmware' ? 'Film' : '设置';
        await openPage(tester, otherPage);
        await tester.scrollUntilVisible(
          find.byKey(const Key('transfer-phase')),
          300,
        );
        expect(find.text('private-$kind-$phase'), findsNothing);
        expect(find.text('固件构建已确认'), findsNothing);
        expect(find.text('文件已就绪'), findsOneWidget);
        expect(find.byKey(const Key('transfer-progress')), findsNothing);
        expect(find.byKey(const Key('confirm-firmware')), findsNothing);
        expect(
          tester
              .widget<OutlinedButton>(find.byKey(const Key('cancel-transfer')))
              .onPressed,
          isNull,
        );
        expect(
          tester
              .widget<FilledButton>(find.byKey(const Key('retry-transfer')))
              .onPressed,
          isNull,
        );
        if (kind == 'firmware') {
          expect(find.text('重试升级'), findsNothing);
        }
        final startKey = kind == 'firmware'
            ? 'start-transfer'
            : 'start-firmware';
        expect(
          tester.widget<FilledButton>(find.byKey(Key(startKey))).onPressed,
          active || pending ? isNull : isNotNull,
        );
        await openPage(tester, kind == 'firmware' ? '设置' : 'Film');
        await tester.scrollUntilVisible(
          find.byKey(const Key('transfer-phase')),
          300,
        );
        await tester.pump();
        expect(find.text('private-$kind-$phase'), findsOneWidget);
        await tester.pumpWidget(const SizedBox());
      });
    }
  }
  testWidgets(
    'another completed transfer leaves empty Film waiting for a file',
    (tester) async {
      final gateway = FakeGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      gateway.events.add(
        const DeviceSnapshot(
          transfer: FilmTransfer(
            kind: 'firmware',
            phase: 'done',
            success: true,
            cleanupCompleted: true,
          ),
        ),
      );
      await tester.pump();
      await openPage(tester, 'Film');
      expect(find.text('等待文件'), findsOneWidget);
      expect(find.text('固件构建已确认'), findsNothing);
      await tester.pumpWidget(const SizedBox());
    },
  );
}
