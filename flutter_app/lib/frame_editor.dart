import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';

import 'frame_codec.dart';
import 'frame_io.dart' as io;
import 'passport_storage.dart' as storage;

class FrameEditorController {
  Uint8List? source, crop;
  FrameOptions options = const FrameOptions();
  FrameResult? result;
  int revision = 0, resultRevision = -1;
  bool get hasCurrentResult => result != null && resultRevision == revision;
  void clear() {
    source = null;
    crop = null;
    result = null;
    revision++;
    resultRevision = -1;
  }

  void dispose() => clear();
}

class FrameEditor extends StatefulWidget {
  const FrameEditor({
    super.key,
    required this.controller,
    required this.canImportFilm,
    required this.onImportFilm,
    this.pickImage,
  });
  final FrameEditorController controller;
  final bool canImportFilm;
  final Future<void> Function(Uint8List film, String name) onImportFilm;
  final Future<Uint8List?> Function()? pickImage;
  @override
  State<FrameEditor> createState() => _FrameEditorState();
}

class _FrameEditorState extends State<FrameEditor> {
  FrameEditorController get c => widget.controller;
  int _generation = 0;
  bool _working = false, _cancellable = false;
  double _progress = 0;
  String _status = '';
  Timer? _previewTimer;
  @override
  void initState() {
    super.initState();
    if (c.source != null && c.crop == null) unawaited(_preview());
  }

  @override
  void dispose() {
    _generation++;
    _previewTimer?.cancel();
    super.dispose();
  }

  void _cancel() {
    _generation++;
    setState(() {
      _working = false;
      _status = '已取消，可以重试';
    });
  }

  Future<void> _pick() async {
    _previewTimer?.cancel();
    final generation = ++_generation;
    setState(() {
      _working = true;
      _cancellable = true;
      _status = '正在加载照片';
    });
    try {
      final bytes = await (widget.pickImage ?? storage.pickPassportAvatar)();
      if (!mounted || generation != _generation) return;
      if (bytes == null) {
        setState(() {
          _working = false;
          _status = '已取消选择';
        });
        return;
      }
      final crop = await prepareFrame(
        bytes,
        c.options,
        isCancelled: () => !mounted || generation != _generation,
      );
      if (!mounted || generation != _generation) return;
      setState(() {
        c.source = bytes;
        c.crop = crop;
        c.revision++;
        _working = false;
        _status = '照片已加载，请调整后生成';
      });
    } catch (e) {
      if (!mounted || generation != _generation) return;
      setState(() {
        _working = false;
        _status = '加载失败：$e';
      });
    }
  }

  void _change({
    FrameFormat? format,
    FrameFit? fit,
    int? turns,
    double? zoom,
    double? x,
    double? y,
    double? brightness,
    double? contrast,
    double? saturation,
    bool? dither,
  }) {
    final old = c.options;
    _generation++;
    setState(() {
      c.options = FrameOptions(
        format: format ?? old.format,
        fit: fit ?? old.fit,
        quarterTurns: turns ?? old.quarterTurns,
        zoom: zoom ?? old.zoom,
        panX: x ?? old.panX,
        panY: y ?? old.panY,
        brightness: brightness ?? old.brightness,
        contrast: contrast ?? old.contrast,
        saturation: saturation ?? old.saturation,
        dither: dither ?? old.dither,
      );
      c.revision++;
      c.crop = null;
      _status = '参数已更新，请重新生成';
    });
    _previewTimer?.cancel();
    _previewTimer = Timer(const Duration(milliseconds: 180), _preview);
  }

  Future<void> _preview() async {
    final source = c.source;
    if (source == null) return;
    final generation = _generation;
    try {
      final crop = await prepareFrame(
        source,
        c.options,
        isCancelled: () => !mounted || generation != _generation,
      );
      if (mounted && generation == _generation) setState(() => c.crop = crop);
    } catch (e) {
      if (mounted && generation == _generation) {
        setState(() => _status = '预览失败：$e');
      }
    }
  }

  Future<void> _generate() async {
    final source = c.source;
    if (source == null) return;
    _previewTimer?.cancel();
    final generation = ++_generation, revision = c.revision;
    setState(() {
      _working = true;
      _cancellable = true;
      _progress = 0;
      _status = '正在生成';
    });
    try {
      final result = await convertFrame(
        source,
        c.options,
        isCancelled: () => !mounted || generation != _generation,
        onProgress: (value) {
          if (mounted && generation == _generation) {
            setState(() => _progress = value);
          }
        },
      );
      if (!mounted || generation != _generation) return;
      setState(() {
        c.result = result;
        c.resultRevision = revision;
        _working = false;
        _status = '生成完成';
      });
    } catch (e) {
      if (!mounted || generation != _generation) return;
      setState(() {
        _working = false;
        _status = '生成失败：$e';
      });
    }
  }

  Future<void> _export(bool download) async {
    if (_working || !c.hasCurrentResult) return;
    final result = c.result!;
    final name = result.format == FrameFormat.monoFast
        ? 'frame_mono.film'
        : 'frame_color.film';
    setState(() {
      _working = true;
      _cancellable = false;
    });
    try {
      if (download) {
        await io.downloadFrame(result.film, name);
      } else {
        await widget.onImportFilm(result.film, name);
      }
      if (mounted) {
        setState(() {
          _working = false;
          _status = download ? '已下载' : '已转入 Film';
        });
      }
    } catch (e) {
      if (mounted) {
        setState(() {
          _working = false;
          _status = '导出失败：$e';
        });
      }
    }
  }

  Widget _slider(
    String label,
    double value,
    double min,
    double max,
    ValueChanged<double> change,
  ) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text('$label  ${value.toStringAsFixed(2)}'),
      Slider(
        value: value,
        min: min,
        max: max,
        onChanged: _working ? null : change,
      ),
    ],
  );
  Widget _image(String title, Uint8List? png) => Expanded(
    child: Column(
      children: [
        Text(title),
        const SizedBox(height: 8),
        AspectRatio(
          aspectRatio: 2 / 3,
          child: DecoratedBox(
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.surfaceContainerHighest,
              border: Border.all(
                color: Theme.of(context).colorScheme.outlineVariant,
              ),
            ),
            child: png == null
                ? const Center(child: Icon(Icons.image_outlined))
                : Image.memory(
                    png,
                    fit: BoxFit.contain,
                    gaplessPlayback: true,
                    errorBuilder: (_, error, stack) =>
                        const Center(child: Text('预览不可用')),
                  ),
          ),
        ),
      ],
    ),
  );
  @override
  Widget build(BuildContext context) {
    final o = c.options;
    return ListView(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      padding: const EdgeInsets.all(16),
      children: [
        Text('Frame 图片', style: Theme.of(context).textTheme.headlineSmall),
        const Text('为 Ark 制作竖屏图片 · 480 × 720'),
        const SizedBox(height: 16),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            FilledButton.icon(
              onPressed: _working ? null : _pick,
              icon: const Icon(Icons.add_photo_alternate_outlined),
              label: const Text('选择照片'),
            ),
            OutlinedButton(
              onPressed: _working || c.source == null
                  ? null
                  : () => _change(turns: (o.quarterTurns + 1) % 4),
              child: const Text('旋转 90°'),
            ),
            TextButton(
              onPressed: _working || c.source == null
                  ? null
                  : () {
                      _previewTimer?.cancel();
                      _generation++;
                      setState(() {
                        c.clear();
                        _status = '已清除';
                      });
                    },
              child: const Text('清除'),
            ),
          ],
        ),
        const SizedBox(height: 16),
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _image('构图预览', c.crop),
            const SizedBox(width: 12),
            _image(
              c.hasCurrentResult ? '最终预览' : '最终预览 · 待生成',
              c.hasCurrentResult ? c.result!.png : null,
            ),
          ],
        ),
        const SizedBox(height: 16),
        SegmentedButton<FrameFit>(
          segments: const [
            ButtonSegment(value: FrameFit.cover, label: Text('铺满')),
            ButtonSegment(value: FrameFit.contain, label: Text('完整显示')),
          ],
          selected: {o.fit},
          onSelectionChanged: _working ? null : (v) => _change(fit: v.first),
        ),
        const SizedBox(height: 12),
        SegmentedButton<FrameFormat>(
          segments: const [
            ButtonSegment(value: FrameFormat.sixColor, label: Text('六色')),
            ButtonSegment(
              value: FrameFormat.monoFast,
              label: Text('黑白 MonoFast'),
            ),
          ],
          selected: {o.format},
          onSelectionChanged: _working ? null : (v) => _change(format: v.first),
        ),
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: const Text('抖动'),
          value: o.dither,
          onChanged: _working ? null : (v) => _change(dither: v),
        ),
        _slider('缩放', o.zoom, 0.25, 4, (v) => _change(zoom: v)),
        _slider('水平位置', o.panX, -480, 480, (v) => _change(x: v)),
        _slider('垂直位置', o.panY, -720, 720, (v) => _change(y: v)),
        _slider('亮度', o.brightness, -1, 1, (v) => _change(brightness: v)),
        _slider('对比度', o.contrast, 0, 2, (v) => _change(contrast: v)),
        _slider('饱和度', o.saturation, 0, 2, (v) => _change(saturation: v)),
        if (_working)
          LinearProgressIndicator(value: _progress > 0 ? _progress : null),
        if (_status.isNotEmpty)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Text(_status),
          ),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            FilledButton(
              onPressed: _working || c.source == null ? null : _generate,
              child: const Text('生成 Film'),
            ),
            if (_working && _cancellable)
              TextButton(onPressed: _cancel, child: const Text('取消')),
            OutlinedButton(
              onPressed:
                  _working || !c.hasCurrentResult || !widget.canImportFilm
                  ? null
                  : () => _export(false),
              child: const Text('转入 Film 页面'),
            ),
            if (io.canDownloadFrame)
              OutlinedButton(
                onPressed: _working || !c.hasCurrentResult
                    ? null
                    : () => _export(true),
                child: const Text('下载 Film'),
              ),
          ],
        ),
      ],
    );
  }
}
