import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import bridge as b


class BridgeTests(unittest.TestCase):
    def test_concurrent_agents_refresh_credentials_once(self):
        state = Path(self.tmp.name)
        token = {'expires_at': '2000-01-01T00:00:00Z', 'refresh_token': 'fixture-old'}
        b.atomic_bytes(state / 'login.dpapi', b.protect(json.dumps(token).encode()))
        script = '''
import bridge as b
import datetime as dt, json, time
def refresh(form):
    assert form['refresh_token'] == 'fixture-old'
    with (b.STATE / 'refresh-count.txt').open('a') as log:
        log.write('refresh\\n')
    time.sleep(0.3)
    token = {'expires_at': (dt.datetime.now(dt.timezone.utc) + dt.timedelta(hours=1)).isoformat(),
             'refresh_token': 'fixture-new'}
    b.atomic_bytes(b.STATE / 'login.dpapi', b.protect(json.dumps(token).encode()))
    return token
b.exchange = refresh
assert b.tokens()['refresh_token'] == 'fixture-new'
'''
        env = {**os.environ, 'FAB_BRIDGE_HOME': str(state)}
        processes = [subprocess.Popen([sys.executable, '-c', script], cwd=Path(b.__file__).parent,
                                     env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                     for _ in range(4)]
        try:
            for process in processes:
                _, error = process.communicate(timeout=15)
                self.assertEqual(process.returncode, 0, error)
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        self.assertEqual((state / 'refresh-count.txt').read_text(), 'refresh\n')

    def test_failed_refresh_preserves_credentials_and_releases_lock(self):
        path = Path(self.tmp.name) / 'login.dpapi'
        payload = b.protect(json.dumps({'expires_at': '2000-01-01T00:00:00Z',
                                       'refresh_token': 'fixture'}).encode())
        b.atomic_bytes(path, payload)
        with patch.object(b, 'exchange', side_effect=ValueError('offline')):
            for _ in range(2):
                with self.assertRaisesRegex(ValueError, 'offline'):
                    b.tokens()
                self.assertEqual(path.read_bytes(), payload)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.state = patch.object(b, 'STATE', Path(self.tmp.name))
        self.state.start()

    def tearDown(self):
        self.state.stop()
        self.tmp.cleanup()

    def item(self, uid='one'):
        return {'assetId': uid, 'title': 'Forest kit', 'assetNamespace': 'marketplace',
                'distributionMethod': 'ASSET_PACK', 'projectVersions': [{'artifactId': 'ForestV1', 'engineVersions': ['UE_5.7']}],
                'access_token': 'must-not-persist'}

    def test_cloud_pagination_deduplicates_and_excludes_secret_fields(self):
        pages = iter([{'results': [self.item()], 'cursors': {'next': 'page2'}},
                      {'results': [self.item(), self.item('two')], 'cursors': {'next': None}}])
        urls = []
        def fetch(url, headers):
            urls.append(url)
            return next(pages)
        result = b.fetch_library({'account_id': '123', 'access_token': 'secret'}, fetch)
        self.assertEqual(len(result), 2)
        self.assertIn('cursor=page2', urls[1])
        self.assertNotIn('must-not-persist', json.dumps(result))

    def test_repeated_cursor_rejected(self):
        with self.assertRaisesRegex(ValueError, 'pagination'):
            b.fetch_library({'account_id': '123', 'access_token': 'x'},
                            lambda *args: {'results': [self.item()], 'cursors': {'next': 'same'}})

    def test_missing_results_or_cursor_rejected(self):
        for value in ({}, {'results': []}, {'results': [], 'cursors': {}}, {'results': 'bad', 'cursors': {'next': None}}):
            with self.assertRaises(ValueError):
                b.fetch_library({'account_id': '123', 'access_token': 'x'}, lambda *args: value)

    def test_failed_sync_preserves_previous_snapshot(self):
        b.save_json(b.STATE / 'cloud.json', {'records': [{'id': 'previous'}]})
        before = (b.STATE / 'cloud.json').read_bytes()
        with patch.object(b, 'tokens', return_value={}), patch.object(b, 'fetch_library', side_effect=ValueError('offline')):
            with self.assertRaises(ValueError):
                b.sync_cloud()
        self.assertEqual((b.STATE / 'cloud.json').read_bytes(), before)

    def test_switching_account_removes_old_catalogue_before_sync(self):
        b.save_json(b.STATE / 'cloud.json', {'account_key': 'old', 'records': [{'id': 'old'}]})
        with patch.object(b.getpass, 'getpass', return_value='test-code'), \
             patch.object(b, 'exchange', return_value={'account_id': 'new'}), \
             patch.object(b, 'sync_cloud', side_effect=ValueError('offline')), patch('builtins.print'):
            with self.assertRaises(ValueError):
                b.login()
        self.assertFalse((b.STATE / 'cloud.json').exists())

    def test_cloud_only_search_and_exact_version_rule(self):
        rows = b.fetch_library({'account_id': '123', 'access_token': 'x'},
                               lambda *args: {'results': [self.item()], 'cursors': {'next': None}})
        b.save_json(b.STATE / 'cloud.json', {'records': rows})
        result = b.search('forest', source='owned')
        self.assertEqual(result['total'], 1)
        self.assertEqual(result['items'][0]['local_state'], 'not_found_in_scanned_roots')
        self.assertEqual(b.prepare('fab:one', 'UE_5.8')['compatibility'], 'not_established')
        self.assertEqual(b.prepare('fab:one', 'UE_5.7')['compatibility'], 'listed_for_engine')

    def test_local_presence_does_not_grant_ownership(self):
        b.save_json(b.STATE / 'local.json', {'records': [{'id': 'x', 'title': 'forest', 'owned': None}]})
        self.assertEqual(b.search('forest', source='owned')['total'], 0)
        self.assertEqual(b.search('forest', source='local')['total'], 1)

    def test_local_scan_real_fixtures(self):
        root = b.STATE / 'projects' / 'Example'
        (root / 'Content').mkdir(parents=True)
        (root / 'Plugins/Test').mkdir(parents=True)
        (root / 'Example.uproject').write_text('{}')
        (root / 'Content/Tree.uasset').write_bytes(b'fixture')
        b.save_json(root / 'Plugins/Test/Test.uplugin', {'FriendlyName': 'Example plugin'})
        vault = b.STATE / 'vault'
        (vault / 'ForestV1').mkdir(parents=True)
        b.save_json(b.STATE / 'config.json', {'project_roots': [str(root.parent)], 'vault_roots': [str(vault)]})
        with patch.object(b, 'PROGRAM_DATA', b.STATE / 'empty'):
            result = b.scan_local()
        self.assertEqual(result['local_records'], 3)
        self.assertEqual(b.search('tree')['items'][0]['object_path'], '/Game/Tree')
        self.assertTrue(all(r['owned'] is None for r in b.catalogue()))

    def test_unreal_comments_and_trailing_commas_preserve_strings(self):
        p = b.STATE / 'sample.uplugin'
        p.write_text('{"URL":"https://example.com", // comment\n "text":"x,}","items":[1, /* trailing */],}')
        self.assertEqual(b.read_descriptor(p), {'URL': 'https://example.com', 'text': 'x,}', 'items': [1]})

    @unittest.skipUnless(os.name == 'nt', 'DPAPI is Windows only')
    def test_dpapi_roundtrip(self):
        secret = b'only-a-test-not-an-account-token'
        encrypted = b.protect(secret)
        self.assertNotIn(secret, encrypted)
        self.assertEqual(b.protect(encrypted, decrypt=True), secret)

    def test_rpc_input_validation_and_notifications(self):
        self.assertIsNone(b.rpc({'jsonrpc': '2.0', 'method': 'notifications/initialized'}))
        for args in ({'limit': True}, {'query': 5}, {'unexpected': 'x'}):
            result = b.rpc({'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call', 'params': {'name': 'fab_search', 'arguments': args}})
            self.assertEqual(result['error']['code'], -32602)
        self.assertEqual(b.rpc([])['error']['code'], -32600)

    def test_stdio_handshake_and_search(self):
        requests = [
            {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2025-06-18'}},
            {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
            {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
            {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {'name': 'fab_search', 'arguments': {}}},
        ]
        proc = subprocess.run([sys.executable, str(Path(b.__file__)), 'serve'],
                              input='\n'.join(map(json.dumps, requests)) + '\n', text=True, capture_output=True,
                              env={**os.environ, 'FAB_BRIDGE_HOME': str(b.STATE)}, timeout=15)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        output = list(map(json.loads, proc.stdout.splitlines()))
        self.assertEqual([r['id'] for r in output], [1, 2, 3])
        self.assertEqual(len(output[1]['result']['tools']), 14)
        self.assertFalse(output[2]['result']['isError'])


if __name__ == '__main__':
    unittest.main()
