import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';

import 'device_gateway.dart';
import 'preview_device_gateway.dart';
import 'github_release_service.dart';
import 'passport_editor.dart';

enum FilmTheme { automatic, forFilm, arknights }

class FrameFilmApp extends StatefulWidget {
  const FrameFilmApp({super.key, this.gateway});
  final DeviceGateway? gateway;

  @override
  State<FrameFilmApp> createState() => _FrameFilmAppState();
}

class _FrameFilmAppState extends State<FrameFilmApp> {
  late DeviceController device;
  final releases = GitHubReleaseService();
  GitHubFirmwareRelease? onlineFirmware;
  bool checkingReleases = false;
  String? releaseMessage;
  Future<void> checkReleases() async {
    setState(() {
      checkingReleases = true;
      releaseMessage = null;
      onlineFirmware = null;
    });
    try {
      final release = await releases.latest();
      if (mounted) {
        setState(() {
          onlineFirmware = release;
          releaseMessage = release == null
              ? '暂无包含 frame_film_ark.bin 的正式发布。'
              : null;
        });
      }
    } catch (error) {
      if (mounted) setState(() => releaseMessage = '发布查询失败：$error');
    } finally {
      if (mounted) setState(() => checkingReleases = false);
    }
  }

  FilmTheme selectedTheme = FilmTheme.automatic;
  int page = 0;
  bool loading = false;
  bool wasConnected = false;
  Timer? loadingTimer;
  final passportEditor = PassportEditorController();

  final deviceSuffix = TextEditingController();
  final wakeInterval = TextEditingController();
  bool? autoSleepDraft, timedWakeDraft;
  DeviceSnapshot settingsSnapshot = const DeviceSnapshot();

  static const destinations = [
    ('连接', Icons.bluetooth_rounded),
    ('Frame', Icons.dashboard_outlined),
    ('Film', Icons.image_outlined),
    ('动画', Icons.animation_outlined),
    ('通行证', Icons.badge_outlined),
    ('设置', Icons.tune_rounded),
  ];

  bool get ark =>
      selectedTheme == FilmTheme.arknights ||
      (selectedTheme == FilmTheme.automatic && device.snapshot.connected);

  @override
  void initState() {
    super.initState();
    device = DeviceController(widget.gateway ?? PlatformDeviceGateway());
    device.addListener(onDeviceChanged);
  }

  void onDeviceChanged() {
    final snapshot = device.snapshot;
    final connected = snapshot.connected;
    if (!connected) {
      deviceSuffix.clear();
      wakeInterval.clear();
      autoSleepDraft = null;
      timedWakeDraft = null;
    } else {
      if (snapshot.name != settingsSnapshot.name && snapshot.name != null) {
        deviceSuffix.text = snapshot.name!.startsWith('FRAMEFILMARK-')
            ? snapshot.name!.substring('FRAMEFILMARK-'.length)
            : '';
      }
      if (snapshot.autoSleep != settingsSnapshot.autoSleep) {
        autoSleepDraft = snapshot.autoSleep;
      }
      if (snapshot.timedWake != settingsSnapshot.timedWake) {
        timedWakeDraft = snapshot.timedWake;
      }
      if (snapshot.wakeMinutes != settingsSnapshot.wakeMinutes) {
        wakeInterval.text = snapshot.wakeMinutes?.toString() ?? '';
      }
    }
    settingsSnapshot = snapshot;
    if (connected && !wasConnected && ark) {
      loading = true;
      loadingTimer?.cancel();
      loadingTimer = Timer(const Duration(milliseconds: 1600), () {
        if (mounted) setState(() => loading = false);
      });
    } else if (!connected) {
      loadingTimer?.cancel();
      loading = false;
    }
    wasConnected = connected;
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    releases.dispose();
    loadingTimer?.cancel();
    device.removeListener(onDeviceChanged);
    device.dispose();
    if (device.gateway case final PreviewDeviceGateway preview) {
      preview.dispose();
    }

    deviceSuffix.dispose();
    wakeInterval.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final scheme = ark
        ? const ColorScheme.dark(
            primary: Color(0xff83afc1),
            onPrimary: Color(0xff151719),
            secondary: Color(0xff83afc1),
            onSecondary: Color(0xff151719),
            tertiary: Color(0xff83afc1),
            primaryContainer: Color(0xff41484d),
            onPrimaryContainer: Color(0xfff0f1f2),
            secondaryContainer: Color(0xff41484d),
            onSecondaryContainer: Color(0xfff0f1f2),
            surface: Color(0xff222629),
            onSurface: Color(0xfff0f1f2),
            onSurfaceVariant: Color(0xfff0f1f2),
            outline: Color(0xff41484d),
            surfaceContainerHighest: Color(0xff222629),
          )
        : ColorScheme.fromSeed(
            seedColor: const Color(0xff577baa),
            surface: const Color(0xfffffcf5),
          );
    final controlShape = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(ark ? 0 : 20),
    );
    return MaterialApp(
      title: 'FrameFilm',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: scheme,
        scaffoldBackgroundColor: ark
            ? const Color(0xff151719)
            : const Color(0xfff4f1e9),
        useMaterial3: true,
        cardTheme: CardThemeData(
          elevation: 0,
          margin: EdgeInsets.zero,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(ark ? 0 : 24),
          ),
        ),
        filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(shape: controlShape),
        ),
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(shape: controlShape),
        ),
        segmentedButtonTheme: SegmentedButtonThemeData(
          style: ButtonStyle(shape: WidgetStatePropertyAll(controlShape)),
        ),
        chipTheme: ChipThemeData(shape: controlShape),
        navigationBarTheme: NavigationBarThemeData(
          backgroundColor: scheme.surface,
          indicatorShape: controlShape,
          indicatorColor: scheme.secondaryContainer,
        ),
        navigationRailTheme: NavigationRailThemeData(
          backgroundColor: scheme.surface,
          indicatorShape: controlShape,
          indicatorColor: scheme.secondaryContainer,
        ),
        appBarTheme: AppBarTheme(
          backgroundColor: scheme.surface,
          foregroundColor: scheme.onSurface,
          surfaceTintColor: Colors.transparent,
        ),
        inputDecorationTheme: InputDecorationTheme(
          border: OutlineInputBorder(
            borderRadius: BorderRadius.circular(ark ? 0 : 12),
          ),
          enabledBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(ark ? 0 : 12),
            borderSide: BorderSide(color: scheme.outline),
          ),
          fillColor: scheme.surface,
          filled: true,
        ),
      ),
      home: LayoutBuilder(
        builder: (context, constraints) {
          final wide = constraints.maxWidth >= 760;
          return Scaffold(
            appBar: AppBar(
              title: Text(ark ? 'RHODES ISLAND / PRTS' : 'FrameFilm'),
              actions: [
                Padding(
                  padding: const EdgeInsets.only(right: 16),
                  child: Chip(
                    avatar: Icon(
                      device.snapshot.connected
                          ? Icons.bluetooth_connected
                          : Icons.bluetooth_disabled,
                      size: 16,
                    ),
                    label: Text(device.snapshot.connected ? '已连接' : '离线'),
                  ),
                ),
              ],
            ),
            body: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                if (device.gateway case final PreviewDeviceGateway preview)
                  previewControls(context, preview),
                Expanded(
                  child: Stack(
                    children: [
                      Row(
                        children: [
                          if (wide)
                            NavigationRail(
                              selectedIndex: page,
                              onDestinationSelected: (value) =>
                                  setState(() => page = value),
                              labelType: NavigationRailLabelType.all,
                              destinations: destinations
                                  .map(
                                    (item) => NavigationRailDestination(
                                      icon: Icon(item.$2),
                                      label: Text(item.$1),
                                    ),
                                  )
                                  .toList(),
                            ),
                          Expanded(
                            child: Align(
                              alignment: Alignment.topCenter,
                              child: ConstrainedBox(
                                constraints: const BoxConstraints(
                                  maxWidth: 980,
                                ),
                                child: ListView(
                                  padding: EdgeInsets.all(wide ? 32 : 20),
                                  children: [
                                    Text(
                                      ark
                                          ? 'TERMINAL / 0${page + 1}'
                                          : '你的电子纸工作台',
                                      style: Theme.of(context)
                                          .textTheme
                                          .labelMedium,
                                    ),
                                    const SizedBox(height: 8),
                                    Text(
                                      destinations[page].$1,
                                      style: Theme.of(context)
                                          .textTheme
                                          .headlineLarge
                                          ?.copyWith(
                                            fontWeight: FontWeight.w700,
                                          ),
                                    ),
                                    const SizedBox(height: 24),
                                    ...pageContent(context),
                                  ],
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                      if (loading)
                        Positioned.fill(child: connectionTransition()),
                    ],
                  ),
                ),
              ],
            ),
            bottomNavigationBar: wide
                ? null
                : NavigationBar(
                    selectedIndex: page,
                    onDestinationSelected: (value) =>
                        setState(() => page = value),
                    labelBehavior:
                        NavigationDestinationLabelBehavior.alwaysShow,
                    destinations: destinations
                        .map(
                          (item) => NavigationDestination(
                            icon: Icon(item.$2),
                            label: item.$1,
                          ),
                        )
                        .toList(),
                  ),
          );
        },
      ),
    );
  }

  void enterPreview() {
    device.removeListener(onDeviceChanged);
    device.dispose();
    device = DeviceController(PreviewDeviceGateway());
    device.addListener(onDeviceChanged);
    setState(() {});
  }

  Widget previewControls(BuildContext context, PreviewDeviceGateway preview) =>
      Material(
        color: Theme.of(context).colorScheme.secondaryContainer,
        child: SafeArea(
          bottom: false,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  PreviewDeviceGateway.warning,
                  key: Key('preview-warning'),
                  style: TextStyle(fontWeight: FontWeight.bold),
                ),
                Wrap(
                  spacing: 16,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: [
                    DropdownButton<PreviewScenario>(
                      key: const Key('preview-scenario'),
                      value: preview.scenario,
                      items: const [
                        DropdownMenuItem(
                          value: PreviewScenario.normal,
                          child: Text('正常流程'),
                        ),
                        DropdownMenuItem(
                          value: PreviewScenario.transferFailure,
                          child: Text('传输失败（重试恢复）'),
                        ),
                        DropdownMenuItem(
                          value: PreviewScenario.unconfirmed,
                          child: Text('升级结果待确认'),
                        ),
                        DropdownMenuItem(
                          value: PreviewScenario.sameBuild,
                          child: Text('相同构建（跳过升级）'),
                        ),
                      ],
                      onChanged: (value) {
                        if (value != null) preview.reset(value);
                      },
                    ),
                    TextButton.icon(
                      key: const Key('reset-preview'),
                      onPressed: () => preview.reset(),
                      icon: const Icon(Icons.restart_alt),
                      label: const Text('重置演示'),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      );

  Widget panel(BuildContext context, {required Widget child}) => Card(
    child: Padding(padding: const EdgeInsets.all(24), child: child),
  );

  List<Widget> pageContent(BuildContext context) => switch (page) {
    0 => connectionPage(context),
    1 => stagePage(
      context,
      Icons.dashboard_customize_outlined,
      '从一张画面开始',
      'Frame 是设备与画面的工作区。',
      ['读取设备名称、电量和屏幕参数已接入', '画面排版与设备端设置将在后续阶段接入'],
    ),
    2 => filmPage(context),
    3 => stagePage(
      context,
      Icons.movie_filter_outlined,
      '画面，也可以流动',
      '动画编辑与多帧传输尚未接入。',
      ['后续接入帧序列与播放参数', '后续接入设备存储与播放控制'],
    ),
    4 => passportPage(context),
    _ => settingsPage(context),
  };

  List<Widget> connectionPage(BuildContext context) {
    final snapshot = device.snapshot;
    final supported = device.gateway.supported;
    return [
      panel(
        context,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Icon(
              snapshot.connected
                  ? Icons.bluetooth_connected
                  : Icons.bluetooth_searching,
              size: 40,
              color: Theme.of(context).colorScheme.primary,
            ),
            const SizedBox(height: 16),
            Text(
              snapshot.connected
                  ? snapshot.name ?? 'FrameFilm 设备'
                  : '连接你的 FrameFilm',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 8),
            Text(
              supported ? snapshot.message : '当前平台仅支持界面预览，蓝牙连接请使用 Android 客户端。',
            ),
            if (device.errorMessage != null)
              Padding(
                padding: const EdgeInsets.only(top: 12),
                child: Text(
                  device.errorMessage!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
            const SizedBox(height: 20),
            if (kIsWeb && !supported)
              Padding(
                padding: const EdgeInsets.only(bottom: 16),
                child: FilledButton(
                  key: const Key('enter-preview'),
                  onPressed: enterPreview,
                  child: const Text('进入模拟设备演示'),
                ),
              ),
            if (supported)
              Wrap(
                spacing: 12,
                runSpacing: 12,
                children: [
                  if (!snapshot.connected)
                    FilledButton.icon(
                      onPressed:
                          device.busy ||
                              device.snapshot.transfer.active ||
                              device.snapshot.importing
                          ? null
                          : () => device.command('scan'),
                      icon: const Icon(Icons.search),
                      label: Text(device.busy ? '正在处理…' : '扫描设备'),
                    ),
                  if (snapshot.connected) ...[
                    FilledButton.icon(
                      onPressed:
                          device.busy ||
                              device.snapshot.transfer.active ||
                              device.snapshot.importing
                          ? null
                          : () => device.command('refresh'),
                      icon: const Icon(Icons.refresh),
                      label: const Text('刷新信息'),
                    ),
                    OutlinedButton(
                      onPressed:
                          device.busy ||
                              device.snapshot.transfer.active ||
                              device.snapshot.importing
                          ? null
                          : () => device.command('disconnect'),
                      child: const Text('断开连接'),
                    ),
                  ],
                ],
              ),
            if (snapshot.connected) ...[
              const SizedBox(height: 24),
              Wrap(
                spacing: 24,
                runSpacing: 16,
                children: [
                  information(
                    '电量',
                    snapshot.battery == null ? '等待读取' : '${snapshot.battery}%',
                  ),
                  information(
                    '屏幕',
                    snapshot.width == null || snapshot.height == null
                        ? '等待读取'
                        : '${snapshot.width} × ${snapshot.height}',
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
      if (supported && !snapshot.connected) ...[
        const SizedBox(height: 24),
        Text('附近设备', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 12),
        if (snapshot.devices.isEmpty)
          panel(context, child: const Text('暂无扫描结果。打开设备蓝牙后，点击扫描设备。')),
        for (final item in snapshot.devices)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Card(
              child: ListTile(
                leading: const Icon(Icons.developer_board),
                title: Text(item.name),
                subtitle: Text(item.address),
                trailing: FilledButton.tonal(
                  onPressed:
                      device.busy ||
                          device.snapshot.transfer.active ||
                          device.snapshot.importing
                      ? null
                      : () => device.command('connect', {
                          'address': item.address,
                        }),
                  child: const Text('连接'),
                ),
              ),
            ),
          ),
      ],
      const SizedBox(height: 20),
      Text(
        device.gateway is PreviewDeviceGateway
            ? '演示中的扫描、文件选择、传输及升级均为模拟。Film 页体验直传，设置页体验 OTA。'
            : '原生连接与文件传输\n扫描、连接和读取由 Android 蓝牙服务提供。film 文件直传请进入 Film 页。',
      ),
    ];
  }

  List<Widget> filmPage(BuildContext context) {
    final snapshot = device.snapshot;
    final file = snapshot.importedFile;
    final transfer = snapshot.transfer;
    final supported = device.gateway.supported;
    final available =
        supported &&
        !device.busy &&
        !snapshot.importing &&
        !snapshot.settingsBusy &&
        !snapshot.passportBusy &&
        !transfer.active &&
        !transfer.canConfirm;
    final editable =
        available && !(transfer.canRetry && !transfer.cleanupCompleted);
    return [
      panel(
        context,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'film 文件 · Wi-Fi 直传',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 12),
            Text(
              supported
                  ? '导入已有 .film 文件，通过已连接的 Ark 建立 Wi-Fi Direct 传输。'
                  : '当前平台仅供界面预览。文件导入与 Wi-Fi Direct 直传需要 Android 原生客户端。',
            ),
            const SizedBox(height: 20),
            Text(
              file?.name ?? '尚未选择 film 文件',
              key: const Key('film-file-name'),
            ),
            if (file != null) ...[
              const SizedBox(height: 8),
              Text('${file.size} 字节'),
              Text(
                file.width != null && file.height != null
                    ? '${file.width} × ${file.height}'
                    : '尺寸信息不可用',
              ),
            ],
            if (snapshot.importing)
              const Padding(
                padding: EdgeInsets.only(top: 12),
                child: Text('正在导入并校验文件…'),
              ),
            const SizedBox(height: 20),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: [
                OutlinedButton.icon(
                  key: const Key('pick-film'),
                  onPressed: editable ? () => device.command('pickFilm') : null,
                  icon: const Icon(Icons.file_open_outlined),
                  label: const Text('选择 film 文件'),
                ),
                FilledButton.icon(
                  key: const Key('start-transfer'),
                  onPressed:
                      editable &&
                          file != null &&
                          snapshot.connected &&
                          !(transfer.kind == 'film' && transfer.canRetry)
                      ? () => device.command('startTransfer')
                      : null,
                  icon: const Icon(Icons.wifi),
                  label: const Text('Wi-Fi 直传'),
                ),
                OutlinedButton(
                  key: const Key('clear-film'),
                  onPressed: editable && file != null
                      ? () => device.command('clearFilm')
                      : null,
                  child: const Text('清除文件'),
                ),
              ],
            ),
            if (supported && !snapshot.connected)
              const Padding(
                padding: EdgeInsets.only(top: 12),
                child: Text('请先在连接页连接 Ark 设备。'),
              ),
            if (device.errorMessage != null)
              Padding(
                padding: const EdgeInsets.only(top: 12),
                child: Text(
                  device.errorMessage!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
          ],
        ),
      ),
      const SizedBox(height: 20),
      transferStatus(context, kind: 'film'),
    ];
  }

  String shortBuild(String value) =>
      value.length > 16 ? value.substring(0, 16) : value;

  Widget transferStatus(BuildContext context, {required String kind}) {
    final snapshot = device.snapshot;
    final globalTransfer = snapshot.transfer;
    final firmware = kind == 'firmware';
    final transfer = globalTransfer.kind == kind
        ? globalTransfer
        : FilmTransfer(
            kind: kind,
            message: firmware ? '选择固件后检查并升级' : '选择 film 文件后开始直传',
          );
    final hasFile = firmware
        ? snapshot.importedFirmware != null
        : snapshot.importedFile != null;
    final supported = device.gateway.supported;
    final available =
        supported &&
        !device.busy &&
        !snapshot.importing &&
        !snapshot.settingsBusy &&
        !snapshot.passportBusy &&
        !globalTransfer.active &&
        !globalTransfer.canConfirm;
    final phase = switch (transfer.phase) {
      'idle' => hasFile ? '文件已就绪' : '等待文件',
      'preparing' => '准备直传',
      'connecting' => '建立 Wi-Fi 连接',
      'downloading' => '设备接收中',
      'restoring' => firmware ? '校验固件 / 恢复 Wi-Fi' : '设备保存 / 恢复 Wi-Fi',
      'validating' => '检查目标固件',
      'ready' => '固件已校验',
      'applying' => '提交升级',
      'rebooting' => '等待设备重启',
      'confirming' => '核对运行构建',
      'unconfirmed' => '升级结果待确认',
      'cancelling' => '正在取消',
      'cleanup' => '清理临时连接',
      'done' =>
        transfer.success && transfer.cleanupCompleted
            ? (firmware ? '固件构建已确认' : '传输完成')
            : '等待完成确认',
      'cancelled' => '已取消',
      'error' => '传输未完成',
      _ => transfer.phase,
    };
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            phase,
            key: const Key('transfer-phase'),
            style: Theme.of(context).textTheme.titleMedium,
          ),
          const SizedBox(height: 12),
          Text(transfer.message),
          if (globalTransfer.kind != kind &&
              (globalTransfer.active || globalTransfer.canConfirm))
            Text(firmware ? '设备正在传输 film，请等待传输结束。' : '设备正在升级固件，请在设置页处理。'),
          if (firmware && transfer.targetBuild != null)
            Text('目标构建：${shortBuild(transfer.targetBuild!)}'),
          if (transfer.active || transfer.total > 0) ...[
            const SizedBox(height: 16),
            LinearProgressIndicator(
              key: const Key('transfer-progress'),
              value: transfer.progress,
            ),
            const SizedBox(height: 8),
            Text('${transfer.received} / ${transfer.total} 字节'),
          ],
          if (transfer.phase == 'done' ||
              transfer.phase == 'cancelled' ||
              transfer.phase == 'error') ...[
            const SizedBox(height: 12),
            Text(
              transfer.cleanupCompleted
                  ? '临时服务与连接清理已确认'
                  : '清理尚未确认，请查看上方原因；可重试以重新处理连接。',
            ),
          ],
          const SizedBox(height: 16),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              OutlinedButton(
                key: const Key('cancel-transfer'),
                onPressed: supported && transfer.canCancel
                    ? () => device.command('cancelTransfer')
                    : null,
                child: Text(firmware ? '取消升级' : '取消传输'),
              ),
              if (transfer.canConfirm)
                FilledButton(
                  key: const Key('confirm-firmware'),
                  onPressed: supported && !device.busy && !transfer.active
                      ? () => device.command('confirmFirmwareTransfer')
                      : null,
                  child: const Text('重连并确认升级'),
                ),
              FilledButton.tonal(
                key: const Key('retry-transfer'),
                onPressed:
                    available &&
                        hasFile &&
                        snapshot.connected &&
                        transfer.canRetry
                    ? () => device.command('retryTransfer')
                    : null,
                child: Text(firmware ? '重试升级' : '重试传输'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget firmwarePanel(BuildContext context) {
    final snapshot = device.snapshot;
    final file = snapshot.importedFirmware;
    final transfer = snapshot.transfer;
    final supported = device.gateway.supported;
    final editable =
        supported &&
        !device.busy &&
        !snapshot.importing &&
        !snapshot.settingsBusy &&
        !snapshot.passportBusy &&
        !transfer.active &&
        !transfer.canConfirm &&
        !(transfer.canRetry && !transfer.cleanupCompleted);
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('固件升级', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              OutlinedButton(
                key: const Key('check-github'),
                onPressed: checkingReleases ? null : checkReleases,
                child: Text(checkingReleases ? '正在查询…' : '检查 GitHub 发布'),
              ),
              if (device.gateway is PreviewDeviceGateway)
                OutlinedButton(
                  key: const Key('demo-online-firmware'),
                  onPressed: () => setState(() {
                    onlineFirmware = GitHubFirmwareRelease.demo;
                    releaseMessage = null;
                  }),
                  child: const Text('演示在线固件'),
                ),
            ],
          ),
          if (releaseMessage != null) Text(releaseMessage!),
          if (onlineFirmware case final release?) ...[
            Text('${release.name} · ${release.tag}'),
            Text('发布于 ${release.publishedAt.toLocal()} · ${release.size} 字节'),
            Text(
              release.sha256 == null
                  ? '发布未提供有效 SHA-256；下载后检查镜像自身完整性。'
                  : '下载后核对 GitHub 资产 SHA-256 与镜像完整性。',
            ),
            const Text('发布版本仅供展示；升级以 ELF 构建指纹判定。下载完成只导入，仍需点击“检查并升级”。'),
            OutlinedButton(
              key: const Key('download-firmware'),
              onPressed: editable
                  ? () => device.command(
                      'downloadFirmware',
                      release.downloadArguments,
                    )
                  : null,
              child: const Text('下载并校验'),
            ),
            if (!supported) const Text('当前浏览器仅支持查询发布；下载并校验需要 Android 客户端。'),
          ],
          if (snapshot.firmwareDownload.phase != 'idle') ...[
            Text(snapshot.firmwareDownload.message),
            Text(
              '${snapshot.firmwareDownload.received} / ${snapshot.firmwareDownload.total} 字节',
            ),
            if (snapshot.firmwareDownload.canCancel) ...[
              LinearProgressIndicator(
                value: snapshot.firmwareDownload.progress,
              ),
              OutlinedButton(
                key: const Key('cancel-firmware-download'),
                onPressed: () => device.command('cancelFirmwareDownload'),
                child: const Text('取消下载'),
              ),
            ],
          ],
          const SizedBox(height: 12),
          Text(
            supported
                ? '导入 Ark 应用固件 .bin，通过 Wi-Fi 直传升级。设备重启并核对构建指纹后，才会确认完成。'
                : '固件导入与升级需要 Android 客户端。',
          ),
          const SizedBox(height: 16),
          Text(file?.name ?? '尚未选择固件', key: const Key('firmware-file-name')),
          if (file != null) ...[
            const SizedBox(height: 8),
            Text('${file.size} 字节 · ${file.project}'),
            Text('构建指纹：${shortBuild(file.elfSha256)}'),
            Text('固件版本：${file.version}'),
          ],
          if (snapshot.importing)
            const Padding(
              padding: EdgeInsets.only(top: 12),
              child: Text('正在导入并校验文件…'),
            ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              OutlinedButton.icon(
                key: const Key('pick-firmware'),
                onPressed: editable
                    ? () => device.command('pickFirmware')
                    : null,
                icon: const Icon(Icons.file_open_outlined),
                label: const Text('选择固件'),
              ),
              FilledButton.icon(
                key: const Key('start-firmware'),
                onPressed:
                    editable &&
                        file != null &&
                        snapshot.connected &&
                        !(transfer.kind == 'firmware' && transfer.canRetry)
                    ? () => device.command('startFirmwareTransfer')
                    : null,
                icon: const Icon(Icons.system_update_alt),
                label: const Text('检查并升级'),
              ),
              OutlinedButton(
                key: const Key('clear-firmware'),
                onPressed: editable && file != null
                    ? () => device.command('clearFirmware')
                    : null,
                child: const Text('清除固件'),
              ),
            ],
          ),
          if (supported && !snapshot.connected && !transfer.canConfirm)
            const Padding(
              padding: EdgeInsets.only(top: 12),
              child: Text('请先连接 Ark 设备。'),
            ),
          if (device.errorMessage != null)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Text(
                device.errorMessage!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
            ),
        ],
      ),
    );
  }

  Widget information(String label, String value) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(label),
      const SizedBox(height: 4),
      Text(
        value,
        style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w600),
      ),
    ],
  );

  List<Widget> stagePage(
    BuildContext context,
    IconData icon,
    String title,
    String subtitle,
    List<String> steps,
  ) => [
    panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 48, color: Theme.of(context).colorScheme.primary),
          const SizedBox(height: 24),
          Text(title, style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          Text(subtitle),
          const SizedBox(height: 24),
          for (final step in steps)
            Padding(
              padding: const EdgeInsets.only(bottom: 12),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Icon(Icons.circle_outlined, size: 18),
                  const SizedBox(width: 12),
                  Expanded(child: Text(step)),
                ],
              ),
            ),
          const Divider(),
          const SizedBox(height: 8),
          const Text('阶段说明 · 当前页面未提供编辑或传输操作。'),
        ],
      ),
    ),
  ];

  List<Widget> passportPage(BuildContext context) => [
    if (device.gateway is PreviewDeviceGateway)
      const Text('模拟设备：发送仅保存到演示内存，重置演示会清除；本地草稿单独保留。'),
    if (device.errorMessage != null)
      Text(
        device.errorMessage!,
        style: TextStyle(color: Theme.of(context).colorScheme.error),
      ),
    panel(
      context,
      child: PassportEditor(
        controller: passportEditor,
        canOperate: device.canEditSettings,
        busy: device.snapshot.passportBusy || device.busy,
        operation: device.snapshot.passport,
        onRead: () => device.command('readPassport'),
        onSave: (json, bin) =>
            device.command('savePassport', {'json': json, 'bin': bin}),
        onCancel: () => device.command('cancelPassport'),
        onOpen: () => device.command('openDevicePage', {'appId': 6}),
      ),
    ),
  ];

  Widget remotePanel(BuildContext context) {
    Widget key(String label, int value, IconData icon) => OutlinedButton.icon(
      key: Key('remote-key-$value'),
      onPressed: device.canEditSettings
          ? () => device.command('remoteKey', {'key': value})
          : null,
      icon: Icon(icon),
      label: Text(label),
    );
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('设备遥控', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text('回包表示按键已收到，电子纸刷新仍需等待。休眠后需按实体确认键唤醒。'),
          const SizedBox(height: 12),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              // Match the existing ForFilm panel and Ark physical key direction.
              key('上', 3, Icons.arrow_upward),
              key('确认', 0, Icons.check),
              key('下', 2, Icons.arrow_downward),
              key('返回主菜单', 4, Icons.keyboard_return),
              key('休眠', 1, Icons.bedtime_outlined),
            ],
          ),
          const SizedBox(height: 12),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              for (final entry in const [
                (5, '主菜单'),
                (6, '通行证'),
                (0, '图片'),
                (4, '系统设置'),
              ])
                TextButton(
                  onPressed: device.canEditSettings
                      ? () => device.command('openDevicePage', {
                          'appId': entry.$1,
                        })
                      : null,
                  child: Text('打开${entry.$2}'),
                ),
            ],
          ),
          if (device.snapshot.settingsMessage != null)
            Text(device.snapshot.settingsMessage!),
          if (device.errorMessage != null)
            Text(
              device.errorMessage!,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
        ],
      ),
    );
  }

  Widget deviceSettingsPanel(BuildContext context) {
    final snapshot = device.snapshot;
    final enabled = device.canEditSettings;
    final suffixError = validateDeviceSuffix(deviceSuffix.text);
    final minutes = int.tryParse(wakeInterval.text);
    final validMinutes = minutes != null && minutes >= 10 && minutes <= 2880;
    Widget settingSwitch(
      String label,
      bool? draft,
      bool? actual,
      String method,
      ValueChanged<bool> change,
    ) => Row(
      children: [
        Expanded(child: Text(actual == null ? '$label（等待读取）' : label)),
        Switch(
          value: draft ?? false,
          onChanged: enabled && actual != null ? change : null,
        ),
        TextButton(
          onPressed: enabled && actual != null && draft != null
              ? () => device.command(method, {'enabled': draft})
              : null,
          child: const Text('保存'),
        ),
      ],
    );
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('设备设置', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          if (device.gateway is PreviewDeviceGateway)
            const Text('模拟设备设置，不会修改真实设备。'),
          if (!snapshot.connected) const Text('连接 Ark 后读取并修改设备设置。'),
          Align(
            alignment: Alignment.centerRight,
            child: TextButton.icon(
              key: const Key('refresh-device-settings'),
              onPressed: enabled ? () => device.command('refresh') : null,
              icon: const Icon(Icons.refresh),
              label: const Text('刷新设备设置'),
            ),
          ),
          settingSwitch(
            '自动休眠',
            autoSleepDraft,
            snapshot.autoSleep,
            'setAutoSleep',
            (value) => setState(() => autoSleepDraft = value),
          ),
          const Text('开启后按固件规则自动休眠；蓝牙连接期间不会自动计时。'),
          settingSwitch(
            '定时唤醒',
            timedWakeDraft,
            snapshot.timedWake,
            'setTimedWake',
            (value) => setState(() => timedWakeDraft = value),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: wakeInterval,
            key: const Key('wake-minutes'),
            enabled: enabled && snapshot.wakeMinutes != null,
            keyboardType: TextInputType.number,
            onChanged: (_) => setState(() {}),
            decoration: InputDecoration(
              labelText: '唤醒间隔（分钟）',
              helperText: snapshot.wakeMinutes == null
                  ? '等待读取设备值'
                  : '10–2880 分钟',
              errorText: wakeInterval.text.isNotEmpty && !validMinutes
                  ? '请输入 10–2880 的整数'
                  : null,
            ),
          ),
          TextButton(
            onPressed: enabled && snapshot.wakeMinutes != null && validMinutes
                ? () => device.command('setWakeMinutes', {'minutes': minutes})
                : null,
            child: const Text('保存唤醒间隔'),
          ),
          const Divider(height: 28),
          TextField(
            controller: deviceSuffix,
            key: const Key('device-name-suffix'),
            enabled: enabled && snapshot.name != null,
            onChanged: (_) => setState(() {}),
            decoration: InputDecoration(
              labelText: '设备名称后缀',
              prefixText: 'FRAMEFILMARK-',
              helperText: '1–16 UTF-8 字节；保存后重启设备使广播名生效',
              errorText: deviceSuffix.text.isNotEmpty ? suffixError : null,
            ),
          ),
          TextButton(
            key: const Key('save-device-name'),
            onPressed: enabled && snapshot.name != null && suffixError == null
                ? () => device.command('renameDevice', {
                    'suffix': deviceSuffix.text,
                  })
                : null,
            child: const Text('保存设备名称'),
          ),
          const Divider(height: 28),
          OutlinedButton.icon(
            key: const Key('sync-device-time'),
            onPressed: enabled ? () => device.command('syncTime') : null,
            icon: const Icon(Icons.schedule),
            label: const Text('同步手机时间与时区'),
          ),
          if (snapshot.syncedAt != null)
            Text(
              '最近同步完成：${DateTime.fromMillisecondsSinceEpoch(snapshot.syncedAt!).toLocal()}',
            ),
          if (snapshot.settingsMessage != null) ...[
            const SizedBox(height: 8),
            Text(snapshot.settingsMessage!),
          ],
        ],
      ),
    );
  }

  List<Widget> settingsPage(BuildContext context) => [
    panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('界面主题', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text('自动模式在连接 Ark 后启用明日方舟主题；断连回到 ForFilm。选择仅在本次应用会话内生效。'),
          const SizedBox(height: 20),
          SegmentedButton<FilmTheme>(
            segments: const [
              ButtonSegment(value: FilmTheme.automatic, label: Text('自动')),
              ButtonSegment(value: FilmTheme.forFilm, label: Text('原 ForFilm')),
              ButtonSegment(value: FilmTheme.arknights, label: Text('明日方舟预览')),
            ],
            selected: {selectedTheme},
            onSelectionChanged: (value) =>
                setState(() => selectedTheme = value.first),
          ),
        ],
      ),
    ),
    const SizedBox(height: 20),
    deviceSettingsPanel(context),
    const SizedBox(height: 20),
    remotePanel(context),
    const SizedBox(height: 20),
    firmwarePanel(context),
    const SizedBox(height: 20),
    transferStatus(context, kind: 'firmware'),
    const SizedBox(height: 20),
    panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('连接信息', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 16),
          Text(
            device.snapshot.connected
                ? device.snapshot.name ?? '已连接设备'
                : '未连接设备',
          ),
          const SizedBox(height: 8),
          Text(
            device.gateway.supported
                ? device.snapshot.message
                : '当前平台无蓝牙连接能力，仅供界面预览',
          ),
          const Divider(height: 32),
          const Text(
            '当前阶段\n原生六页导航、双主题、Android 蓝牙连接、设备基础信息与休眠设置、改名和时间同步、film 导入与 Wi-Fi 直传、固件升级（进度、取消、重试与构建确认）、设备遥控、通行证读取/发送与本地草稿。\n\n后续阶段\n图片与动画编辑、film 转换及统一 UI。',
          ),
        ],
      ),
    ),
  ];

  Widget connectionTransition() => ColoredBox(
    key: const Key('connection-transition'),
    color: const Color(0xff151719),
    child: Center(
      child: TweenAnimationBuilder<double>(
        tween: Tween(begin: 0, end: 1),
        duration: const Duration(milliseconds: 700),
        builder: (context, value, child) => Opacity(
          opacity: value,
          child: Transform.translate(
            offset: Offset(0, 16 * (1 - value)),
            child: child,
          ),
        ),
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Image.asset(
                'assets/rhodes_logo.png',
                width: 100,
                height: 100,
                errorBuilder: (_, _, _) => const Icon(
                  Icons.change_history,
                  color: Color(0xfff0f1f2),
                  size: 80,
                ),
              ),
              const SizedBox(height: 28),
              const Text(
                'PRTS / TERMINAL ONLINE',
                textAlign: TextAlign.center,
                style: TextStyle(
                  color: Color(0xfff0f1f2),
                  letterSpacing: 3,
                  fontWeight: FontWeight.w600,
                ),
              ),
              const SizedBox(height: 12),
              Text(
                device.snapshot.name ?? 'FrameFilm',
                style: const TextStyle(color: Color(0xff83afc1)),
              ),
              const SizedBox(height: 24),
              const Text(
                '连接已建立 · 正在进入工作台',
                style: TextStyle(color: Color(0xff83afc1)),
              ),
            ],
          ),
        ),
      ),
    ),
  );
}
