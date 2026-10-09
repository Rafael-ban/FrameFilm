import 'dart:typed_data';

bool get canDownloadFrame => false;
Future<void> downloadFrame(Uint8List bytes, String name) async {
  throw UnsupportedError('请先转入 Film 页面');
}
