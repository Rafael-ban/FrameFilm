import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

enum FrameFormat { sixColor, monoFast }

enum FrameFit { cover, contain }

class FrameOptions {
  const FrameOptions({
    this.format = FrameFormat.sixColor,
    this.fit = FrameFit.cover,
    this.quarterTurns = 0,
    this.zoom = 1,
    this.panX = 0,
    this.panY = 0,
    this.brightness = 0,
    this.contrast = 1,
    this.saturation = 1,
    this.dither = false,
  });
  final FrameFormat format;
  final FrameFit fit;
  final int quarterTurns;
  final double zoom, panX, panY, brightness, contrast, saturation;
  final bool dither;
}

class FrameResult {
  const FrameResult({
    required this.png,
    required this.film,
    required this.format,
    this.width = 480,
    this.height = 720,
  });
  final Uint8List png, film;
  final int width, height;
  final FrameFormat format;
}

class FrameCancelledException implements Exception {
  const FrameCancelledException();
  @override
  String toString() => '转换已取消';
}

const _palette = <List<int>>[
  [0, 0, 0],
  [255, 255, 255],
  [255, 255, 0],
  [255, 0, 0],
  [0, 0, 255],
  [41, 204, 20],
];

void _check(bool Function()? cancelled) {
  if (cancelled?.call() ?? false) throw const FrameCancelledException();
}

/// Portrait logical coordinates map to physical (height - 1 - y, x).
/// The small dimensions are also useful for testing orientation and bit order.
Uint8List packFramePixels(
  Uint8List indices, {
  int width = 480,
  int height = 720,
  FrameFormat format = FrameFormat.sixColor,
}) {
  if (width <= 0 ||
      height <= 0 ||
      width > 65535 ||
      height > 65535 ||
      indices.length != width * height) {
    throw ArgumentError('像素尺寸不匹配');
  }
  final mono = format == FrameFormat.monoFast;
  final bodySize = (indices.length + (mono ? 7 : 1)) ~/ (mono ? 8 : 2);
  final result = Uint8List(32 + bodySize);
  final header = ByteData.sublistView(result);
  header.setUint32(0, bodySize, Endian.little);
  header.setUint16(4, height, Endian.little);
  header.setUint16(6, width, Endian.little);
  result[8] = mono ? 2 : 6;
  result[9] = mono ? 1 : 0;
  header.setUint16(10, 1, Endian.little);
  if (!mono) result.setRange(16, 22, [0, 255, 252, 224, 3, 28]);
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final value = indices[y * width + x];
      if (value >= (mono ? 2 : 6)) throw ArgumentError('无效颜色索引');
      final physical = x * height + height - 1 - y;
      if (mono) {
        if (value == 0) result[32 + physical ~/ 8] |= 128 >> (physical % 8);
      } else {
        result[32 + physical ~/ 2] |= value << (physical.isEven ? 4 : 0);
      }
    }
  }
  return result;
}

Future<ui.Image> _render(
  Uint8List source,
  FrameOptions options,
  bool Function()? cancelled,
) async {
  _check(cancelled);
  if (source.isEmpty || source.length > 8 * 1024 * 1024) {
    throw ArgumentError('图片须大于0且不超过8 MiB');
  }
  final values = [
    options.zoom,
    options.panX,
    options.panY,
    options.brightness,
    options.contrast,
    options.saturation,
  ];
  if (values.any((v) => !v.isFinite) || options.zoom <= 0) {
    throw ArgumentError('无效图片调整参数');
  }
  // Use the shared API: direct ImageDescriptor dimensions are unavailable on Web.
  ui.Codec? codec;
  ui.Image? image;
  final recorder = ui.PictureRecorder();
  ui.Picture? picture;
  try {
    codec = await ui.instantiateImageCodecWithSize(
      await ui.ImmutableBuffer.fromUint8List(source),
      getTargetSize: (width, height) {
        final ratio = math.min(1.0, 2048 / math.max(width, height));
        return ui.TargetImageSize(
          width: math.max(1, (width * ratio).round()),
          height: math.max(1, (height * ratio).round()),
        );
      },
    );
    image = (await codec.getNextFrame()).image;
    _check(cancelled);
    final canvas = ui.Canvas(recorder);
    canvas.drawColor(const ui.Color(0xffffffff), ui.BlendMode.src);
    final turns = options.quarterTurns % 4;
    final sw = turns.isOdd ? image.height : image.width;
    final sh = turns.isOdd ? image.width : image.height;
    final scale =
        (options.fit == FrameFit.cover
            ? math.max(480 / sw, 720 / sh)
            : math.min(480 / sw, 720 / sh)) *
        options.zoom;
    canvas.translate(240 + options.panX, 360 + options.panY);
    canvas.rotate(turns * math.pi / 2);
    canvas.scale(scale);
    canvas.drawImage(
      image,
      ui.Offset(-image.width / 2, -image.height / 2),
      ui.Paint()..filterQuality = ui.FilterQuality.medium,
    );
    picture = recorder.endRecording();
    return await picture.toImage(480, 720);
  } finally {
    picture?.dispose();
    image?.dispose();
    codec?.dispose();
  }
}

Future<Uint8List> _png(ui.Image image) async =>
    (await image.toByteData(format: ui.ImageByteFormat.png))!.buffer
        .asUint8List();

Future<Uint8List> prepareFrame(
  Uint8List source,
  FrameOptions options, {
  void Function(double)? onProgress,
  bool Function()? isCancelled,
}) async {
  final image = await _render(source, options, isCancelled);
  try {
    final result = await _png(image);
    _check(isCancelled);
    onProgress?.call(1);
    return result;
  } finally {
    image.dispose();
  }
}

Future<FrameResult> convertFrame(
  Uint8List source,
  FrameOptions options, {
  void Function(double)? onProgress,
  bool Function()? isCancelled,
}) async {
  final image = await _render(source, options, isCancelled);
  late Uint8List rgba;
  try {
    rgba = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!.buffer
        .asUint8List();
  } finally {
    image.dispose();
  }
  final indices = Uint8List(480 * 720);
  var current = Float64List(482 * 3);
  var next = Float64List(482 * 3);
  final channels = Float64List(3);
  for (var y = 0; y < 720; y++) {
    _check(isCancelled);
    for (var x = 0; x < 480; x++) {
      final pixel = y * 480 + x;
      final offset = (x + 1) * 3;
      final lum =
          .2126 * rgba[pixel * 4] +
          .7152 * rgba[pixel * 4 + 1] +
          .0722 * rgba[pixel * 4 + 2];
      for (var c = 0; c < 3; c++) {
        final saturated =
            lum + (rgba[pixel * 4 + c] - lum) * options.saturation;
        channels[c] =
            ((saturated - 128) * options.contrast +
                    128 +
                    options.brightness * 255 +
                    current[offset + c])
                .clamp(0, 255);
      }
      var nearest = 0;
      var distance = double.infinity;
      for (
        var p = 0;
        p < (options.format == FrameFormat.monoFast ? 2 : 6);
        p++
      ) {
        var d = 0.0;
        for (var c = 0; c < 3; c++) {
          d += math.pow(channels[c] - _palette[p][c], 2);
        }
        if (d < distance) {
          distance = d;
          nearest = p;
        }
      }
      indices[pixel] = nearest;
      for (var c = 0; c < 3; c++) {
        rgba[pixel * 4 + c] = _palette[nearest][c];
        if (options.dither) {
          final error = channels[c] - _palette[nearest][c];
          current[offset + 3 + c] += error * 7 / 16;
          next[offset - 3 + c] += error * 3 / 16;
          next[offset + c] += error * 5 / 16;
          next[offset + 3 + c] += error / 16;
        }
      }
      rgba[pixel * 4 + 3] = 255;
    }
    final old = current;
    current = next;
    next = old;
    next.fillRange(0, next.length, 0);
    if (y % 8 == 0) {
      onProgress?.call(.05 + .85 * y / 720);
      await Future<void>.delayed(Duration.zero);
    }
  }
  _check(isCancelled);
  final completer = Completer<ui.Image>();
  ui.decodeImageFromPixels(
    rgba,
    480,
    720,
    ui.PixelFormat.rgba8888,
    completer.complete,
  );
  final preview = await completer.future;
  late Uint8List png;
  try {
    png = await _png(preview);
  } finally {
    preview.dispose();
  }
  _check(isCancelled);
  final film = packFramePixels(indices, format: options.format);
  onProgress?.call(1);
  return FrameResult(png: png, film: film, format: options.format);
}
