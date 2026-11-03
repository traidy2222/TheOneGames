import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

import asset_ops as a
import bridge as b
import epic_manifest as m


def u32(n):
    return struct.pack('<I', n)


def string(s):
    data = s.encode() + b'\0'
    return u32(len(data)) + data


def section(data):
    return u32(len(data) + 4) + data


def binary_fixture(path='Content/Kit/Mesh.uasset', payload=b'example asset package'):
    guid = '00000001000000020000000300000004'
    guid_bytes = struct.pack('<IIII', 1, 2, 3, 4)
    digest = hashlib.sha1(payload).digest()
    compressed = zlib.compress(payload)
    chunk_raw = struct.pack('<IIII', 0xB1FE3AA2, 3, 66, len(compressed)) + guid_bytes + struct.pack('<Q', 123) + b'\x01' + digest + b'\x03' + u32(len(payload)) + compressed
    meta = section(b'\0' + u32(18) + b'\0' + u32(1) + string('TestApp') + string('1.0'))
    chunks = section(b'\0' + u32(1) + guid_bytes + struct.pack('<Q', 123) + digest + b'\x01' + u32(len(payload)) + struct.pack('<q', len(chunk_raw)))
    files = section(b'\0' + u32(1) + string(path) + u32(0) + digest + b'\0' + u32(0) + u32(1) + u32(28) + guid_bytes + u32(0) + u32(len(payload)))
    body = meta + chunks + files
    stored = zlib.compress(body)
    manifest = struct.pack('<IIII', 0x44BEC00C, 41, len(body), len(stored)) + hashlib.sha1(body).digest() + b'\x01' + u32(18) + stored
    return manifest, chunk_raw, payload, guid


class ManifestTests(unittest.TestCase):
    def test_binary_file_and_chunk_integrity(self):
        raw, chunk, payload, guid = binary_fixture()
        manifest = m.parse_manifest(raw)
        self.assertEqual(manifest['files'][0]['size'], len(payload))
        self.assertEqual(m.decode_chunk(chunk, manifest['chunks'][guid]), payload)
        self.assertEqual(m.chunk_path(manifest['chunks'][guid], 18), 'ChunksV4/01/000000000000007B_00000001000000020000000300000004.chunk')

    def test_manifest_hash_and_truncation_rejected(self):
        raw, _, _, _ = binary_fixture()
        changed = bytearray(raw)
        changed[16] ^= 1
        for sample in (bytes(changed), raw[:30], raw[:-1]):
            with self.assertRaises(ValueError):
                m.parse_manifest(sample)

    def test_chunk_corruption_rejected(self):
        raw, chunk, _, guid = binary_fixture()
        expected = m.parse_manifest(raw)['chunks'][guid]
        expected['sha1'] = '0' * 40
        with self.assertRaisesRegex(ValueError, 'SHA1'):
            m.decode_chunk(chunk, expected)

    def test_path_traversal_windows_aliases_and_ads_rejected(self):
        for path in ('../outside', 'Content/../bad', '/absolute', 'C:/bad', '\\host\share', 'Content/a:stream', 'Content/NUL.txt', 'Content/name.', 'Content/name ', 'Content//bad'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                m.relative_path(path)
        self.assertEqual(m.relative_path('Content\\Kit\\Mesh.uasset'), 'Content/Kit/Mesh.uasset')

    def test_json_hash_byte_order_and_chunk_bounds(self):
        def blob(data):
            return ''.join(f'{n:03}' for n in data)
        guid = '1' * 32
        digest = hashlib.sha1(b'example').digest()
        data = {'ManifestFileVersion': blob(u32(13)), 'ChunkFilesizeList': {guid: blob(u32(200))},
                'ChunkHashList': {guid: blob(struct.pack('<Q', 1))}, 'ChunkShaList': {guid: digest.hex()}, 'DataGroupList': {guid: '001'},
                'FileManifestList': [{'Filename': 'Content/Example.uasset', 'FileHash': blob(digest),
                                      'FileChunkParts': [{'Guid': guid, 'Offset': blob(u32(0)), 'Size': blob(u32(7))}]}]}
        parsed = m.parse_manifest(json.dumps(data).encode())
        self.assertEqual(parsed['files'][0]['sha1'], digest.hex())
        data['FileManifestList'][0]['FileChunkParts'][0]['Size'] = blob(u32(2000000))
        with self.assertRaises(ValueError):
            m.parse_manifest(json.dumps(data).encode())


class OperationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.patches = [patch.object(b, 'STATE', self.root / 'state'), patch.object(a, 'ensure_project_closed')]
        for p in self.patches:
            p.start()
        self.project = self.root / 'project/Test.uproject'
        self.project.parent.mkdir()
        self.project.write_text('{"EngineAssociation":"5.8"}')
        self.row = {'id': 'fab:test', 'title': 'Fixture', 'kind': 'ASSET_PACK', 'owned': True}
        self.patches.append(patch.object(a, 'selected', return_value=(self.row, 'TestV1')))
        self.patches[-1].start()
        self.patches.append(patch.object(a, 'project_info', side_effect=lambda *args: (self.project, b.read_descriptor(self.project), self.root / 'engine')))
        self.patches[-1].start()
        self.stage = a.stage_path('TestV1', 'UE_5.8')

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        self.temp.cleanup()

    def receipt(self, paths):
        entries = []
        for name, data in paths.items():
            path = self.stage / 'payload' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            entries.append({'path': name, 'size': len(data), 'sha1': hashlib.sha1(data).hexdigest()})
        b.save_json(self.stage / 'receipt.json', {'id': 'fab:test', 'engine': 'UE_5.8', 'state': 'verified', 'files': entries})

    def test_import_preserves_package_paths_and_is_idempotent(self):
        self.receipt({'Content/Kit/Mesh.uasset': b'mesh', 'Content/Kit/Material.uasset': b'mat', 'Config/DefaultEngine.ini': b'config'})
        result = a.install('fab:test', str(self.project), 'UE_5.8', 'a' * 32)
        self.assertEqual(result['files_copied'], 2)
        self.assertEqual((self.project.parent / 'Content/Kit/Material.uasset').read_bytes(), b'mat')
        self.assertEqual(result['skipped_files'], ['Config/DefaultEngine.ini'])
        again = a.install('fab:test', str(self.project), 'UE_5.8', 'b' * 32)
        self.assertEqual(again['files_copied'], 0)
        self.assertEqual(again['identical_files'], 2)

    def test_conflict_prevents_all_writes(self):
        self.receipt({'Content/Kit/Mesh.uasset': b'mesh', 'Content/Kit/Material.uasset': b'mat'})
        dest = self.project.parent / 'Content/Kit/Mesh.uasset'
        dest.parent.mkdir(parents=True)
        dest.write_bytes(b'user edits')
        with self.assertRaisesRegex(ValueError, 'conflict'):
            a.install('fab:test', str(self.project), 'UE_5.8', 'c' * 32)
        self.assertEqual(dest.read_bytes(), b'user edits')
        self.assertFalse((dest.parent / 'Material.uasset').exists())

    def test_tampered_stage_rejected(self):
        self.receipt({'Content/Kit/Mesh.uasset': b'mesh'})
        (self.stage / 'payload/Content/Kit/Mesh.uasset').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'Staged file'):
            a.import_plan('fab:test', str(self.project))

    def test_plugin_project_enablement_and_backup(self):
        self.row['kind'] = 'CODE_PLUGIN'
        self.receipt({'Engine/Plugins/Marketplace/Any/Foo.uplugin': b'{"FileVersion":3}',
                      'Engine/Plugins/Marketplace/Any/Binaries/Win64/Test.dll': b'fixture'})
        original = self.project.read_bytes()
        result = a.install('fab:test', str(self.project), 'UE_5.8', 'd' * 32)
        self.assertTrue((self.project.parent / 'Plugins/Foo/Foo.uplugin').exists())
        self.assertEqual(b.read_descriptor(self.project)['Plugins'], [{'Name': 'Foo', 'Enabled': True}])
        self.assertEqual(Path(result['backup']).read_bytes(), original)

    def test_interrupted_copy_rolls_back_created_files(self):
        self.receipt({'Content/Kit/A.uasset': b'a', 'Content/Kit/B.uasset': b'b'})
        original_link = os.link
        calls = []
        def link(source, target):
            calls.append(target)
            if len(calls) == 2:
                raise OSError('simulated disk error')
            return original_link(source, target)
        with patch.object(a.os, 'link', side_effect=link), self.assertRaises(OSError):
            a.install('fab:test', str(self.project), 'UE_5.8', 'e' * 32)
        self.assertFalse((self.project.parent / 'Content/Kit/A.uasset').exists())
        self.assertFalse((self.project.parent / 'Content/Kit/B.uasset').exists())

    def test_verified_download_resume_and_repair(self):
        raw, chunk, payload, _ = binary_fixture()
        manifest = m.parse_manifest(raw)
        with patch.object(a, 'resolve', return_value=(self.row, 'TestV1', manifest, 'https://download.epicgames.com/test')), patch.object(a, 'cdn_bytes', return_value=chunk) as fetch:
            result = a.download('fab:test', 'UE_5.8', 1, 'f' * 32)
            self.assertEqual(result['state'], 'verified')
            self.assertEqual(fetch.call_count, 1)
            result = a.download('fab:test', 'UE_5.8', 1, '1' * 32)
            self.assertEqual(result['reused_files'], 1)
            self.assertEqual(fetch.call_count, 1)
            file = self.stage / 'payload/Content/Kit/Mesh.uasset'
            file.write_bytes(b'corrupt')
            a.download('fab:test', 'UE_5.8', 1, '2' * 32)
            self.assertEqual(file.read_bytes(), payload)
            self.assertEqual(fetch.call_count, 1)

    def test_network_size_limit_precedes_payload_write(self):
        raw, _, _, _ = binary_fixture()
        manifest = m.parse_manifest(raw)
        for c in manifest['chunks'].values():
            c['size'] = 2 * 1024 * 1024
        with patch.object(a, 'resolve', return_value=(self.row, 'TestV1', manifest, 'https://download.epicgames.com/test')), self.assertRaisesRegex(ValueError, 'exceeds'):
            a.download('fab:test', 'UE_5.8', 1, '3' * 32)
        self.assertFalse(self.stage.exists())

    def test_existing_vault_file_is_hash_verified_and_reused(self):
        raw, _, payload, _ = binary_fixture()
        manifest = m.parse_manifest(raw)
        vault = self.root / 'vault'
        path = vault / 'data/Content/Kit/Mesh.uasset'
        path.parent.mkdir(parents=True)
        path.write_bytes(payload)
        self.row['local_matches'] = [{'artifact_id': 'TestV1', 'path': str(vault)}]
        with patch.object(a, 'resolve', return_value=(self.row, 'TestV1', manifest, 'https://download.epicgames.com/test')), patch.object(a, 'cdn_bytes') as fetch:
            result = a.download('fab:test', 'UE_5.8', 1, '4' * 32)
        self.assertEqual(result['vault_reused_files'], 1)
        fetch.assert_not_called()

    def test_project_asset_lookup_returns_stable_package_paths(self):
        self.receipt({'Content/Kit/Mesh.uasset': b'mesh', 'Content/Kit/Mat.uasset': b'mat'})
        a.install('fab:test', str(self.project), 'UE_5.8', '5' * 32)
        result = a.project_assets('fab:test', str(self.project), query='mesh')
        self.assertEqual(result['total'], 1)
        self.assertEqual(result['items'][0]['package_path'], '/Game/Kit/Mesh')
        self.assertEqual(result['installation_state'], 'installed_unverified')

    def test_dead_worker_changes_status_to_failed(self):
        job = '6' * 32
        b.save_json(b.STATE / 'jobs' / (job + '.json'), {'state': 'running', 'pid': 123})
        with patch.object(a, 'process_alive', return_value=False):
            self.assertEqual(a.job_status(job)['state'], 'failed')

    def test_failed_download_cleans_partial_output(self):
        raw, _, _, _ = binary_fixture()
        manifest = m.parse_manifest(raw)
        with patch.object(a, 'resolve', return_value=(self.row, 'TestV1', manifest, 'https://download.epicgames.com/test')), \
             patch.object(a, 'cdn_bytes', side_effect=ValueError('offline')), patch.object(a.time, 'sleep'), self.assertRaises(ValueError):
            a.download('fab:test', 'UE_5.8', 1, '7' * 32)
        self.assertFalse((self.stage / 'receipt.json').exists())
        self.assertFalse(list((self.stage / 'payload').rglob('*.tmp')))
        self.assertFalse(list((self.stage / 'payload').rglob('*.uasset')))

    def test_inspect_project_outside_indexed_roots(self):
        program_data = self.root / 'program-data'
        engine = self.root / 'installed-engine'
        executable = engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b'test placeholder; never executed')
        b.save_json(program_data / 'Epic/UnrealEngineLauncher/LauncherInstalled.dat',
                    {'InstallationList': [{'AppName': 'UE_5.8', 'InstallLocation': str(engine)}]})
        b.save_json(b.STATE / 'config.json', {'project_roots': [str(self.root / 'unrelated')]})
        with patch.object(b, 'PROGRAM_DATA', program_data):
            result = a.inspect_project(str(self.project))
            self.assertEqual(result['engine'], 'UE_5.8')
            self.assertTrue(result['matching_engine_installed'])
            self.assertFalse(result['scan_root_registration_required'])
            self.project.write_text('{"EngineAssociation":"{source-build-guid}"}')
            result = a.inspect_project(str(self.project))
            self.assertIsNone(result['engine'])
            self.assertFalse(result['matching_engine_installed'])


if __name__ == '__main__':
    unittest.main()
