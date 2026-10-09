import 'dart:math' as math;
import 'dart:typed_data';

/// Names mirror the options in tools/ForFilm/js/convert.js.
enum FrameAlgorithm {
  atkinsonEnhanced,
  floydSteinberg,
  atkinson,
  stucki,
  jarvis,
  gammaFloydSteinberg,
  bayer,
}

// Film code order: black, white, yellow, red, blue, green.
const filmPalette = <List<int>>[
  [0, 0, 0],
  [255, 255, 255],
  [255, 255, 0],
  [255, 0, 0],
  [0, 0, 255],
  [41, 204, 20],
];
const _aeDisplay = <List<int>>[
  [0, 0, 0],
  [255, 255, 255],
  [255, 255, 0],
  [255, 0, 0],
  [0, 255, 0],
  [0, 0, 255],
];
const _aeResidual = <List<int>>[
  [0, 0, 0],
  [255, 255, 255],
  [255, 235, 0],
  [154, 0, 0],
  [20, 85, 16],
  [0, 36, 154],
];
const _aeToFilm = <int>[0, 1, 2, 3, 5, 4];

int _clamp(int x) => x < 0 ? 0 : (x > 255 ? 255 : x);

// Uint8ClampedArray assignment uses ties-to-even, unlike Dart's round().
int _clamped(double x) {
  if (x <= 0) return 0;
  if (x >= 255) return 255;
  final n = x.floor();
  final fraction = x - n;
  return fraction > .5 || (fraction == .5 && n.isOdd) ? n + 1 : n;
}

List<double> _hsl(num red, num green, num blue) {
  final r = red / 255, g = green / 255, b = blue / 255;
  final high = math.max(r, math.max(g, b));
  final low = math.min(r, math.min(g, b));
  var h = 0.0, s = 0.0;
  final l = (high + low) / 2;
  if (high != low) {
    final d = high - low;
    s = l > .5 ? d / (2 - high - low) : d / (high + low);
    if (high == r) {
      h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
    } else if (high == g) {
      h = ((b - r) / d + 2) / 6;
    } else {
      h = ((r - g) / d + 4) / 6;
    }
  }
  return [h * 360, s, l];
}

double _linear(num channel) {
  final v = channel / 255;
  return v > .04045 ? math.pow((v + .055) / 1.055, 2.4).toDouble() : v / 12.92;
}

double _labPivot(double t) =>
    t > .008856 ? math.pow(t, 1 / 3).toDouble() : 7.787 * t + 16 / 116;

List<double> _lab(num red, num green, num blue, {bool ae = false}) {
  final r = _linear(red) * 100, g = _linear(green) * 100;
  final b = _linear(blue) * 100;
  final x =
      (r * (ae ? .4124564 : .4124) +
          g * (ae ? .3575761 : .3576) +
          b * (ae ? .1804375 : .1805)) /
      95.047;
  final y =
      (r * (ae ? .2126729 : .2126) +
          g * (ae ? .7151522 : .7152) +
          b * (ae ? .0721750 : .0722)) /
      100;
  final z =
      (r * (ae ? .0193339 : .0193) +
          g * (ae ? .1191920 : .1192) +
          b * (ae ? .9503041 : .9505)) /
      108.883;
  final fx = _labPivot(x), fy = _labPivot(y), fz = _labPivot(z);
  return [116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)];
}

double _labDistance(List<double> a, List<double> b) {
  final l = a[0] - b[0], x = a[1] - b[1], y = a[2] - b[2];
  return math.sqrt(l * l + x * x + y * y);
}

final _paletteHsl = filmPalette.map((p) => _hsl(p[0], p[1], p[2])).toList();
final _blackLab = _lab(0, 0, 0), _whiteLab = _lab(255, 255, 255);
final _aeLabs = _aeDisplay
    .map((p) => _lab(p[0], p[1], p[2], ae: true))
    .toList();

int _closest(num r, num g, num b) {
  final input = _hsl(r, g, b);
  if (input[1] < .12) return input[2] > .5 ? 1 : 0;
  var best = 2, minimum = double.infinity;
  for (var i = 2; i < filmPalette.length; i++) {
    final p = _paletteHsl[i];
    var dh = (input[0] - p[0]).abs();
    if (dh > 180) dh = 360 - dh;
    final distance =
        dh + (input[1] - p[1]).abs() * 120 + (input[2] - p[2]).abs() * 80;
    if (distance < minimum) {
      minimum = distance;
      best = i;
    }
  }
  final lab = _lab(r, g, b);
  final black = _labDistance(lab, _blackLab);
  final white = _labDistance(lab, _whiteLab);
  final neutral = black < white ? 0 : 1;
  final chosen = _lab(
    filmPalette[best][0],
    filmPalette[best][1],
    filmPalette[best][2],
  );
  return math.min(black, white) < _labDistance(lab, chosen) * .45
      ? neutral
      : best;
}

/// Applies the web converter's sequential contrast then saturation adjustment.
void adjustFrameColors(
  Uint8List rgba, {
  double contrast = 1,
  double saturation = 1,
}) {
  for (var i = 0; i < rgba.length; i += 4) {
    for (var c = 0; c < 3; c++) {
      rgba[i + c] = _clamped((rgba[i + c] - 128) * contrast + 128);
    }
    final r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
    final gray = .299 * r + .587 * g + .114 * b;
    rgba[i] = _clamped(gray + (r - gray) * saturation);
    rgba[i + 1] = _clamped(gray + (g - gray) * saturation);
    rgba[i + 2] = _clamped(gray + (b - gray) * saturation);
  }
}

typedef _Tap = (int, int, int);
const _fs = <_Tap>[(1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)];
const _atkinson = <_Tap>[
  (1, 0, 1),
  (2, 0, 1),
  (-1, 1, 1),
  (0, 1, 1),
  (1, 1, 1),
  (0, 2, 1),
];
const _stucki = <_Tap>[
  (1, 0, 8),
  (2, 0, 4),
  (-2, 1, 2),
  (-1, 1, 4),
  (0, 1, 8),
  (1, 1, 4),
  (2, 1, 2),
  (-2, 2, 1),
  (-1, 2, 2),
  (0, 2, 4),
  (1, 2, 2),
  (2, 2, 1),
];
const _jarvis = <_Tap>[
  (1, 0, 7),
  (2, 0, 5),
  (-2, 1, 3),
  (-1, 1, 5),
  (0, 1, 7),
  (1, 1, 5),
  (2, 1, 3),
  (-2, 2, 1),
  (-1, 2, 3),
  (0, 2, 5),
  (1, 2, 3),
  (2, 2, 1),
];
const _bayer = <int>[0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

void _paint(Uint8List data, int pixel, int code) {
  final p = pixel * 4, c = filmPalette[code];
  data[p] = c[0];
  data[p + 1] = c[1];
  data[p + 2] = c[2];
  data[p + 3] = 255;
}

int _cell(int r, int g, int b) => ((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2);

int _aeSelect(int r, int g, int b, Uint8List correction, Uint8List selection) {
  final lab = _lab(r, g, b, ae: true);
  if (lab[2] < -10 || lab[1] < -35) {
    final cell = _cell(r, g, b) * 3;
    final r2 = r - ((r - correction[cell]) >> 2);
    final g2 = g - ((g - correction[cell + 1]) >> 2);
    final b2 = b - ((b - correction[cell + 2]) >> 2);
    final index = selection[_cell(r2, g2, b2)];
    return index < 6 ? index : 0;
  }
  var best = 0, minimum = 0x7fffffff;
  for (var i = 0; i < 6; i++) {
    final p = _aeLabs[i];
    final dl = lab[0] - p[0], da = lab[1] - p[1], db = lab[2] - p[2];
    final d = (2 * dl * dl + .8 * da * da + db * db).truncate();
    if (d < minimum) {
      minimum = d;
      best = i;
    }
  }
  return best;
}

Future<void> _afterRow(
  int row,
  int height,
  double from,
  double to,
  void Function()? checkCancelled,
  void Function(double)? onProgress,
) async {
  checkCancelled?.call();
  onProgress?.call(from + (to - from) * (row + 1) / height);
  checkCancelled?.call();
  if ((row + 1) % 8 == 0 || row + 1 == height) {
    await Future<void>.delayed(Duration.zero);
    checkCancelled?.call();
  }
}

Future<Uint8List> _enhanced(
  Uint8List data,
  int width,
  int height,
  Uint8List correction,
  Uint8List selection,
  void Function()? checkCancelled,
  void Function(double)? onProgress,
) async {
  if (correction.length != 786432 || selection.length != 262144) {
    throw ArgumentError(
      'Atkinson LUT 尺寸不正确: correction=${correction.length} '
      '(期望 786432), selection=${selection.length} (期望 262144)',
    );
  }
  final codes = Uint8List(width * height);
  final stride = (width + 3) * 3;
  var current = Int32List(stride),
      next = Int32List(stride),
      second = Int32List(stride);
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final slot = (x + 1) * 3, pos = (y * width + x) * 4;
      final r = _clamp(data[pos] + ((current[slot] + 4) >> 3));
      final g = _clamp(data[pos + 1] + ((current[slot + 1] + 4) >> 3));
      final b = _clamp(data[pos + 2] + ((current[slot + 2] + 4) >> 3));
      final index = _aeSelect(r, g, b, correction, selection);
      codes[y * width + x] = _aeToFilm[index];
      final residual = _aeResidual[index];
      for (var c = 0; c < 3; c++) {
        final error = (c == 0 ? r : (c == 1 ? g : b)) - residual[c];
        current[slot + 3 + c] += error;
        current[slot + 6 + c] += error;
        next[slot - 3 + c] += error;
        next[slot + c] += error;
        next[slot + 3 + c] += error;
        second[slot + c] += error;
      }
    }
    final old = current;
    current = next;
    next = second;
    second = old;
    second.fillRange(0, stride, 0);
    await _afterRow(y, height, .05, .8, checkCancelled, onProgress);
  }
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final i = y * width + x;
      _paint(data, i, codes[i]);
    }
    await _afterRow(y, height, .8, .9, checkCancelled, onProgress);
  }
  return codes;
}

Future<Uint8List> _regular(
  Uint8List data,
  int width,
  int height,
  FrameAlgorithm algorithm,
  double strength,
  void Function()? checkCancelled,
  void Function(double)? onProgress,
) async {
  final codes = Uint8List(width * height);
  if (algorithm == FrameAlgorithm.bayer) {
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        final i = y * width + x, p = i * 4;
        final bias = (_bayer[(y & 3) * 4 + (x & 3)] - 8) * strength;
        codes[i] = _closest(
          math.max(0, math.min(255, data[p] + bias)),
          math.max(0, math.min(255, data[p + 1] + bias)),
          math.max(0, math.min(255, data[p + 2] + bias)),
        );
      }
      await _afterRow(y, height, .05, .8, checkCancelled, onProgress);
    }
  } else if (algorithm == FrameAlgorithm.gammaFloydSteinberg) {
    final linear = Float32List(width * height * 3);
    final gamma = List<double>.generate(256, (v) {
      final f = v / 255;
      return f <= .0031308 ? f * 12.92 : 1.055 * math.pow(f, 1 / 2.4) - .055;
    });
    int toSrgb(double v) =>
        _clamped(gamma[(math.max(0, math.min(1, v)) * 255).round()] * 255);
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        final i = y * width + x;
        for (var c = 0; c < 3; c++) {
          linear[i * 3 + c] = _linear(data[i * 4 + c]);
        }
      }
      await _afterRow(y, height, .05, .15, checkCancelled, onProgress);
    }
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        final i = y * width + x, p = i * 3;
        final code = _closest(
          toSrgb(linear[p]),
          toSrgb(linear[p + 1]),
          toSrgb(linear[p + 2]),
        );
        codes[i] = code;
        for (var c = 0; c < 3; c++) {
          final error =
              (linear[p + c] - _linear(filmPalette[code][c])) * strength;
          for (final (dx, dy, weight) in _fs) {
            final nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < width && ny < height) {
              final np = (ny * width + nx) * 3 + c;
              linear[np] = linear[np] + error * weight / 16;
            }
          }
        }
      }
      await _afterRow(y, height, .15, .8, checkCancelled, onProgress);
    }
  } else {
    final temp = Uint8List.fromList(data);
    final taps = switch (algorithm) {
      FrameAlgorithm.floydSteinberg => _fs,
      FrameAlgorithm.atkinson => _atkinson,
      FrameAlgorithm.stucki => _stucki,
      FrameAlgorithm.jarvis => _jarvis,
      _ => throw ArgumentError('不支持的算法'),
    };
    final divisor = switch (algorithm) {
      FrameAlgorithm.floydSteinberg => 16,
      FrameAlgorithm.atkinson => 8,
      FrameAlgorithm.stucki => 42,
      FrameAlgorithm.jarvis => 48,
      _ => 1,
    };
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        final i = y * width + x, p = i * 4;
        final code = _closest(temp[p], temp[p + 1], temp[p + 2]);
        codes[i] = code;
        for (var c = 0; c < 3; c++) {
          final error = (temp[p + c] - filmPalette[code][c]) * strength;
          for (final (dx, dy, weight) in taps) {
            final nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < width && ny < height) {
              final np = (ny * width + nx) * 4 + c;
              temp[np] = _clamped(temp[np] + error * weight / divisor);
            }
          }
        }
      }
      final secondPass =
          algorithm == FrameAlgorithm.floydSteinberg ||
          algorithm == FrameAlgorithm.stucki;
      await _afterRow(
        y,
        height,
        .05,
        secondPass ? .65 : .8,
        checkCancelled,
        onProgress,
      );
    }
    // The web FS and Stucki implementations do a second nearest-colour pass.
    if (algorithm == FrameAlgorithm.floydSteinberg ||
        algorithm == FrameAlgorithm.stucki) {
      for (var y = 0; y < height; y++) {
        for (var x = 0; x < width; x++) {
          final i = y * width + x;
          codes[i] = _closest(temp[i * 4], temp[i * 4 + 1], temp[i * 4 + 2]);
        }
        await _afterRow(y, height, .65, .8, checkCancelled, onProgress);
      }
    }
  }
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final i = y * width + x;
      _paint(data, i, codes[i]);
    }
    await _afterRow(y, height, .8, .9, checkCancelled, onProgress);
  }
  return codes;
}

/// Mutates RGBA into the film preview and returns logical row-major film codes.
Future<Uint8List> quantizeFramePixels(
  Uint8List rgba,
  int width,
  int height, {
  FrameAlgorithm algorithm = FrameAlgorithm.atkinsonEnhanced,
  double strength = 1,
  bool dither = true,
  Uint8List? correctionLut,
  Uint8List? selectionLut,
  void Function()? checkCancelled,
  void Function(double)? onProgress,
}) async {
  if (width <= 0 || height <= 0 || rgba.length != width * height * 4) {
    throw ArgumentError('像素尺寸不匹配');
  }
  if (!dither) {
    final codes = Uint8List(width * height);
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        final i = y * width + x;
        codes[i] = _closest(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
        _paint(rgba, i, codes[i]);
      }
      await _afterRow(y, height, .05, .9, checkCancelled, onProgress);
    }
    return codes;
  }
  if (algorithm == FrameAlgorithm.atkinsonEnhanced) {
    if (correctionLut == null || selectionLut == null) {
      throw ArgumentError('Atkinson 增强需要原版 LUT');
    }
    return _enhanced(
      rgba,
      width,
      height,
      correctionLut,
      selectionLut,
      checkCancelled,
      onProgress,
    );
  }
  return _regular(
    rgba,
    width,
    height,
    algorithm,
    strength,
    checkCancelled,
    onProgress,
  );
}
