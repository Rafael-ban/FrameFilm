import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';

class FakeGateway implements DeviceGateway {
  FakeGateway({this.supported = true});
  final events = StreamController<DeviceSnapshot>.broadcast(sync: true);
  final calls = <String>[];

  @override
  final bool supported;

  @override
  Stream<DeviceSnapshot> get snapshots => events.stream;

  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    calls.add(method);
  }
}

Future<void> openPage(WidgetTester tester, String title) async {
  final bottom = find.widgetWithText(NavigationDestination, title);
  if (bottom.evaluate().isNotEmpty) {
    await tester.tap(bottom);
  } else {
    await tester.tap(find.descendant(of: find.byType(NavigationRail), matching: find.text(title)));
  }
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('six pages, offline boundary, theme selection and form state', (
    tester,
  ) async {
    final gateway = FakeGateway(supported: false);
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    expect(find.textContaining('当前平台仅支持界面预览'), findsOneWidget);
    expect(find.text('扫描设备'), findsNothing);

    for (final title in ['Frame', 'Film', '动画', '通行证', '设置']) {
      await openPage(tester, title);
      expect(find.text(title), findsWidgets);
    }
    await tester.tap(find.text('明日方舟预览'));
    await tester.pumpAndSettle();
    expect(find.text('RHODES ISLAND / PRTS'), findsOneWidget);
    await tester.tap(find.text('原 ForFilm'));
    await tester.pumpAndSettle();
    expect(find.text('FrameFilm'), findsOneWidget);

    await openPage(tester, '通行证');
    await tester.enterText(find.byKey(const Key('operator-name')), 'Amiya');
    await tester.pump();
    await openPage(tester, 'Frame');
    await openPage(tester, '通行证');
    expect(
      tester
          .widget<TextField>(find.byKey(const Key('operator-name')))
          .controller!
          .text,
      'Amiya',
    );
    expect(find.textContaining('关闭应用后不保留'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets(
    'real gateway event starts transition and disconnect cancels it',
    (tester) async {
      final gateway = FakeGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      await tester.pump();
      gateway.events.add(const DeviceSnapshot(connected: true));
      await tester.pump();
      expect(find.byKey(const Key('connection-transition')), findsOneWidget);
      expect(find.text('RHODES ISLAND / PRTS'), findsOneWidget);
      await tester.pump(const Duration(milliseconds: 1000));
      gateway.events.add(
        const DeviceSnapshot(
          connected: true,
          name: 'FRAMEFILMARK-123',
          battery: 75,
          width: 720,
          height: 480,
        ),
      );
      await tester.pump(const Duration(milliseconds: 700));
      expect(find.byKey(const Key('connection-transition')), findsNothing);
      gateway.events.add(const DeviceSnapshot());
      await tester.pump();
      gateway.events.add(const DeviceSnapshot(connected: true));
      await tester.pump();
      expect(find.byKey(const Key('connection-transition')), findsOneWidget);
      gateway.events.add(const DeviceSnapshot());
      await tester.pump();
      expect(find.byKey(const Key('connection-transition')), findsNothing);
      expect(find.text('FrameFilm'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
    },
  );

  testWidgets('explicit ForFilm choice survives Ark connection', (
    tester,
  ) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    await openPage(tester, '设置');
    await tester.tap(find.text('原 ForFilm'));
    await tester.pumpAndSettle();
    gateway.events.add(
      const DeviceSnapshot(connected: true, name: 'FRAMEFILMARK-123'),
    );
    await tester.pump();
    expect(find.byKey(const Key('connection-transition')), findsNothing);
    expect(find.text('FrameFilm'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });
}
