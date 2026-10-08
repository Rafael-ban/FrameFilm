import 'dart:async';

import 'package:flutter/material.dart';

import 'device_gateway.dart';

enum FilmTheme { automatic, forFilm, arknights }

class FrameFilmApp extends StatefulWidget {
  const FrameFilmApp({super.key, this.gateway});
  final DeviceGateway? gateway;

  @override
  State<FrameFilmApp> createState() => _FrameFilmAppState();
}

class _FrameFilmAppState extends State<FrameFilmApp> {
  late final DeviceController device;
  FilmTheme selectedTheme = FilmTheme.automatic;
  int page = 0;
  bool loading = false;
  bool wasConnected = false;
  Timer? loadingTimer;
  final operatorName = TextEditingController();
  final operatorId = TextEditingController();
  final operatorRole = TextEditingController();

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
    final connected = device.snapshot.connected;
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
    loadingTimer?.cancel();
    device.removeListener(onDeviceChanged);
    device.dispose();
    operatorName.dispose();
    operatorId.dispose();
    operatorRole.dispose();
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
            body: Stack(
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
                          constraints: const BoxConstraints(maxWidth: 980),
                          child: ListView(
                            padding: EdgeInsets.all(wide ? 32 : 20),
                            children: [
                              Text(
                                ark ? 'TERMINAL / 0${page + 1}' : '你的电子纸工作台',
                                style: Theme.of(context).textTheme.labelMedium,
                              ),
                              const SizedBox(height: 8),
                              Text(
                                destinations[page].$1,
                                style: Theme.of(context).textTheme.headlineLarge
                                    ?.copyWith(fontWeight: FontWeight.w700),
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
                if (loading) Positioned.fill(child: connectionTransition()),
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
    2 => stagePage(
      context,
      Icons.photo_library_outlined,
      '让照片留在电子纸上',
      '照片转换与 film 文件传输尚未接入。',
      ['后续接入图片选择与裁剪', '后续接入颜色转换、预览与上传'],
    ),
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
            if (supported)
              Wrap(
                spacing: 12,
                runSpacing: 12,
                children: [
                  if (!snapshot.connected)
                    FilledButton.icon(
                      onPressed: device.busy
                          ? null
                          : () => device.command('scan'),
                      icon: const Icon(Icons.search),
                      label: Text(device.busy ? '正在处理…' : '扫描设备'),
                    ),
                  if (snapshot.connected) ...[
                    FilledButton.icon(
                      onPressed: device.busy
                          ? null
                          : () => device.command('refresh'),
                      icon: const Icon(Icons.refresh),
                      label: const Text('刷新信息'),
                    ),
                    OutlinedButton(
                      onPressed: device.busy
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
                  onPressed: device.busy
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
      const Text('第一阶段 · 原生连接与界面\n扫描、连接和读取由 Android 蓝牙服务提供。画面编辑与上传尚未接入。'),
    ];
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
    panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('本地表单预览 · 未保存到设备；关闭应用后不保留。'),
          const SizedBox(height: 20),
          TextField(
            key: const Key('operator-name'),
            controller: operatorName,
            decoration: const InputDecoration(
              labelText: '代号',
              hintText: '输入干员代号',
            ),
            onChanged: (_) => setState(() {}),
          ),
          const SizedBox(height: 16),
          TextField(
            controller: operatorId,
            decoration: const InputDecoration(
              labelText: '编号',
              hintText: '输入通行证编号',
            ),
            onChanged: (_) => setState(() {}),
          ),
          const SizedBox(height: 16),
          TextField(
            controller: operatorRole,
            decoration: const InputDecoration(
              labelText: '职能',
              hintText: '输入所属职能',
            ),
            onChanged: (_) => setState(() {}),
          ),
        ],
      ),
    ),
    const SizedBox(height: 24),
    Container(
      padding: const EdgeInsets.all(28),
      decoration: BoxDecoration(
        color: const Color(0xff222629),
        border: Border.all(color: const Color(0xff83afc1)),
        borderRadius: BorderRadius.zero,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'RHODES ISLAND',
            style: TextStyle(
              color: Color(0xfff0f1f2),
              letterSpacing: 4,
              fontSize: 12,
            ),
          ),
          const SizedBox(height: 6),
          const Text(
            'OPERATOR PASS',
            style: TextStyle(color: Color(0xff83afc1), letterSpacing: 2),
          ),
          const SizedBox(height: 32),
          Text(
            operatorName.text.isEmpty ? '待填写代号' : operatorName.text,
            style: const TextStyle(
              color: Color(0xfff0f1f2),
              fontSize: 32,
              fontWeight: FontWeight.w700,
            ),
          ),
          const SizedBox(height: 20),
          Text(
            'ID  /  ${operatorId.text.isEmpty ? '—' : operatorId.text}',
            style: const TextStyle(color: Color(0xfff0f1f2)),
          ),
          const SizedBox(height: 8),
          Text(
            'CLASS  /  ${operatorRole.text.isEmpty ? '—' : operatorRole.text}',
            style: const TextStyle(color: Color(0xfff0f1f2)),
          ),
          const SizedBox(height: 28),
          const Divider(color: Color(0xff41484d)),
          const Text(
            'LOCAL PREVIEW · NOT SYNCED',
            style: TextStyle(
              color: Color(0xff83afc1),
              fontSize: 11,
              letterSpacing: 2,
            ),
          ),
        ],
      ),
    ),
  ];

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
            '当前阶段\n原生六页导航、双主题、Android 蓝牙连接、设备基础信息读取、通行证内存表单预览。\n\n后续阶段\n图片与动画编辑、film 转换与上传、设备参数及通行证同步。',
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
