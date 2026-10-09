import 'dart:typed_data';

import 'package:flutter/material.dart';

import 'passport_profile.dart';
import 'passport_storage.dart' as storage;

class PassportEditorController {
  PassportProfile? profile;
  Object? consumedRevision;
  bool initialized = false;
  Future<void> saving = Future.value();
}

class PassportEditor extends StatefulWidget {
  const PassportEditor({
    super.key,
    required this.canOperate,
    required this.busy,
    required this.operation,
    required this.onRead,
    required this.onSave,
    required this.onCancel,
    required this.onOpen,
    this.loadDraft,
    this.saveDraft,
    this.pickAvatar,
    this.controller,
  });
  final PassportEditorController? controller;
  final bool canOperate, busy;
  final Map<String, Object?> operation;
  final Future<void> Function() onRead;
  final Future<void> Function(String, Uint8List) onSave;
  final VoidCallback onCancel, onOpen;
  final Future<String?> Function()? loadDraft;
  final Future<void> Function(String)? saveDraft;
  final Future<Uint8List?> Function()? pickAvatar;
  @override
  State<PassportEditor> createState() => _PassportEditorState();
}

class _PassportEditorState extends State<PassportEditor> {
  final _fields = {
    for (final name in ['codename', 'number', 'affiliation', 'signature'])
      name: TextEditingController(),
  };
  bool _codeUnset = false, _numberUnset = false, _working = false;
  String? _avatar;
  String _status = '';
  PassportRaster? _raster;
  int _generation = 0, _editRevision = 0;
  Object? _seenRevision;
  Future<void> _saving = Future.value();
  bool get _locked => widget.busy || _working;
  PassportProfile get _profile => PassportProfile(
    codename: _fields['codename']!.text,
    number: _fields['number']!.text,
    affiliation: _fields['affiliation']!.text,
    signature: _fields['signature']!.text,
    codenameUnset: _codeUnset,
    numberUnset: _numberUnset,
    avatar: _avatar,
  );
  @override
  void initState() {
    super.initState();
    _seenRevision = widget.operation['revision'];
    final controller = widget.controller;
    final pendingRead =
        controller != null &&
        controller.initialized &&
        controller.consumedRevision != _seenRevision &&
        widget.operation['json'] is String;
    if (controller != null && !controller.initialized) {
      controller.initialized = true;
      controller.consumedRevision = _seenRevision;
    }
    final saved = widget.controller?.profile;
    if (saved != null) {
      _replace(saved);
    } else {
      if (!pendingRead) _load();
      _refresh();
    }
    if (pendingRead && _seenRevision != null) {
      _acceptRead(widget.operation['json'] as String, _seenRevision!);
    }
  }

  Future<void> _load() async {
    final revision = _editRevision;
    try {
      final raw = await (widget.loadDraft ?? storage.loadPassportDraft)();
      if (raw != null) {
        final profile = PassportProfile.fromJson(raw);
        final avatar = await validatePassportAvatar(profile);
        avatar?.dispose();
        if (mounted && revision == _editRevision) _replace(profile);
      }
    } catch (e) {
      if (mounted) setState(() => _status = '本地草稿读取失败：$e');
    }
  }

  @override
  void didUpdateWidget(covariant PassportEditor oldWidget) {
    super.didUpdateWidget(oldWidget);
    final revision = widget.operation['revision'];
    final raw = widget.operation['json'];
    if (revision != null && revision != _seenRevision) {
      _seenRevision = revision;
      if (raw is String) _acceptRead(raw, revision);
    }
  }

  Future<void> _acceptRead(String raw, Object revision) async {
    final editRevision = _editRevision;
    try {
      final profile = PassportProfile.fromJson(raw);
      final avatar = await validatePassportAvatar(profile);
      avatar?.dispose();
      if (mounted && widget.operation['revision'] == revision) {
        widget.controller?.consumedRevision = revision;
        if (editRevision == _editRevision) {
          _replace(profile);
          _persist();
        } else {
          setState(() => _status = '校验期间资料已编辑，保留当前草稿。');
        }
      }
    } catch (e) {
      if (mounted) setState(() => _status = '读取资料校验失败，当前草稿已保留：$e');
    }
  }

  void _replace(PassportProfile p) {
    widget.controller?.profile = p;
    _editRevision++;
    for (final entry in {
      'codename': p.codename,
      'number': p.number,
      'affiliation': p.affiliation,
      'signature': p.signature,
    }.entries) {
      _fields[entry.key]!.text = entry.value;
    }
    setState(() {
      _codeUnset = p.codenameUnset;
      _numberUnset = p.numberUnset;
      _avatar = p.avatar;
    });
    _refresh();
  }

  void _persist() {
    widget.controller?.profile = _profile;
    final json = _profile.toJson();
    final save = widget.saveDraft ?? storage.savePassportDraft;
    _saving = (widget.controller?.saving ?? _saving)
        .then((_) => save(json))
        .catchError((Object e) {
          if (mounted) setState(() => _status = '草稿保存失败：$e');
        });
    widget.controller?.saving = _saving;
  }

  void _changed() {
    _editRevision++;
    _persist();
    setState(() {});
    _refresh();
  }

  Future<void> _refresh() async {
    final generation = ++_generation;
    final profile = _profile;
    // Do not leave a stale preview visible while a newer raster is pending.
    if (mounted) setState(() => _raster = null);
    try {
      final raster = await renderPassport(profile);
      if (mounted && generation == _generation) {
        setState(() => _raster = raster);
      }
    } catch (e) {
      if (mounted && generation == _generation) {
        setState(() => _status = '预览生成失败：$e');
      }
    }
  }

  Future<void> _avatarPick() async {
    setState(() => _working = true);
    try {
      final bytes = await (widget.pickAvatar ?? storage.pickPassportAvatar)();
      if (bytes != null) {
        final avatar = await cropPassportAvatar(bytes);
        if (mounted) {
          _avatar = avatar;
          _changed();
        }
      }
    } catch (e) {
      if (mounted) setState(() => _status = '头像导入失败：$e');
    } finally {
      if (mounted) setState(() => _working = false);
    }
  }

  Future<void> _read() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('读取并替换当前草稿？'),
        content: const Text('设备资料完整读取并校验成功后会覆盖当前编辑内容和本地草稿。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('保留草稿'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('读取并替换'),
          ),
        ],
      ),
    );
    if (confirmed == true && mounted && widget.canOperate && !_locked) {
      await _run(widget.onRead);
    }
  }

  Future<void> _run(Future<void> Function() action) async {
    setState(() => _working = true);
    try {
      await action();
    } catch (e) {
      if (mounted) setState(() => _status = '$e');
    } finally {
      if (mounted) setState(() => _working = false);
    }
  }

  @override
  void dispose() {
    for (final controller in _fields.values) {
      controller.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final errors = passportLayoutErrors(_profile);
    final op = widget.operation;
    final total = op['total'] as num? ?? 0,
        completed = op['completed'] as num? ?? 0;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('通行证资料', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 12),
        const Text('草稿自动保存在本机。读取设备会在完整校验成功后替换草稿。'),
        for (final entry in {
          'codename': '代号',
          'number': '档案编号',
          'affiliation': '所属',
          'signature': '个人签名',
        }.entries) ...[
          const SizedBox(height: 12),
          TextField(
            key: Key(entry.key == 'codename' ? 'operator-name' : entry.key),
            controller: _fields[entry.key],
            enabled: !_locked,
            maxLength: {
              'codename': 32,
              'number': 36,
              'affiliation': 60,
              'signature': 200,
            }[entry.key],
            maxLines: entry.key == 'signature' ? 3 : 1,
            onChanged: (_) => _changed(),
            decoration: InputDecoration(
              labelText: entry.value,
              errorText: errors[entry.key],
            ),
          ),
          if (entry.key == 'codename' || entry.key == 'number')
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              title: Text('${entry.value}尚未设置'),
              value: entry.key == 'codename' ? _codeUnset : _numberUnset,
              onChanged: _locked
                  ? null
                  : (v) {
                      if (entry.key == 'codename') {
                        _codeUnset = v!;
                      } else {
                        _numberUnset = v!;
                      }
                      _changed();
                    },
            ),
        ],
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton(
              onPressed: _locked ? null : _avatarPick,
              child: const Text('导入头像'),
            ),
            TextButton(
              onPressed: _locked || _avatar == null
                  ? null
                  : () {
                      _avatar = null;
                      _changed();
                    },
              child: const Text('移除头像'),
            ),
          ],
        ),
        const SizedBox(height: 16),
        const Text('最终黑白预览 · 440×608'),
        Center(
          child: SizedBox(
            width: 330,
            child: AspectRatio(
              aspectRatio: 440 / 608,
              child: _raster == null
                  ? const Center(child: Text('正在生成预览…'))
                  : Image.memory(
                      _raster!.png,
                      gaplessPlayback: true,
                      filterQuality: FilterQuality.none,
                    ),
            ),
          ),
        ),
        const SizedBox(height: 16),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton(
              onPressed: widget.canOperate && !_locked ? _read : null,
              child: const Text('读取设备资料'),
            ),
            FilledButton(
              onPressed:
                  widget.canOperate &&
                      !_locked &&
                      errors.isEmpty &&
                      _raster != null
                  ? () => _run(() {
                      final json = _profile.toJson();
                      final bin = _raster!.bin;
                      return widget.onSave(json, bin);
                    })
                  : null,
              child: const Text('从头发送资料'),
            ),
            OutlinedButton(
              onPressed: op['canCancel'] == true ? widget.onCancel : null,
              child: const Text('取消'),
            ),
            TextButton(
              onPressed: widget.canOperate && !_locked ? widget.onOpen : null,
              child: const Text('在设备上打开'),
            ),
          ],
        ),
        if (widget.busy)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: LinearProgressIndicator(
              value: total > 0 ? (completed / total).clamp(0, 1) : null,
            ),
          ),
        if (op['message'] is String) Text(op['message'] as String),
        if (_status.isNotEmpty) Text(_status),
        const SizedBox(height: 8),
        const Text('先发送 profile.bin，再发送 profile.json。中断后重新连接并从头发送；请在设备上按确认加载。'),
      ],
    );
  }
}
