import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock
import bridge
import common as c
import jobs
import unreal_link as ue


class BridgeTests(unittest.TestCase):
    def test_receipt_write_survives_transient_windows_reader(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'job.json'; c.write(path, {'state':'running'})
            original = os.replace
            calls = []
            def busy_once(source, target):
                calls.append(1)
                if len(calls) == 1: raise PermissionError('sharing violation')
                return original(source, target)
            with patch.object(os, 'replace', side_effect=busy_once): c.write(path, {'state':'succeeded'})
            self.assertEqual(c.read(path)['state'], 'succeeded')
            self.assertEqual(len(calls), 2)

    def test_status_rereads_receipt_after_worker_exits(self):
        with patch.object(c, 'read', side_effect=[{'state':'running','pid':99}, {'state':'succeeded'}]), patch.object(c,'alive',return_value=False):
            self.assertEqual(jobs.status('a'*32)['state'], 'succeeded')

    def test_rpc_rejects_bad_types_and_unknown_parameters(self):
        for args in [{'source': 42}, {'source': 'x', 'unexpected': True}, {'source': 'x', 'force': 'yes'}]:
            r = bridge.rpc({'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call', 'params': {'name': 'asset_validate', 'arguments': args}})
            self.assertTrue(r['result']['isError'])

    def test_stdio_tools_discovery_does_not_launch_engines(self):
        request = '\n'.join(json.dumps(x) for x in [
            {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize'},
            {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
            {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}]) + '\n'
        p = subprocess.run([sys.executable, str(Path(bridge.__file__)), 'serve'], input=request,
                           text=True, capture_output=True, timeout=10)
        self.assertEqual(p.returncode, 0, p.stderr)
        replies = list(map(json.loads, p.stdout.splitlines()))
        self.assertEqual(len(replies), 2)
        self.assertEqual(len(replies[1]['result']['tools']), 21)
        self.assertIn('No watchers', replies[0]['result']['instructions'])

    def test_content_fingerprint_detects_change_with_same_mtime(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'mesh'; path.write_bytes(b'first')
            stamp = path.stat().st_mtime_ns
            before = c.fingerprint([str(path)], {})
            path.write_bytes(b'other'); os.utime(path, ns=(stamp, stamp))
            self.assertNotEqual(before, c.fingerprint([str(path)], {}))

    def test_success_cache_reuses_only_intact_report_and_dependencies(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'ROOT', Path(folder)):
            root = Path(folder); texture = root / 'texture.png'; texture.write_bytes(b'one')
            report = root / 'report.json'; c.write(report, {'ok': True})
            job = {'job_id': 'fixture', 'state': 'succeeded', 'report': str(report)}
            c.write(root / 'jobs/fixture/job.json', job)
            entry = {'job_id': 'fixture', 'report_sha256': c.sha(report), 'dependencies': [str(texture)],
                     'dependency_hash': c.fingerprint([str(texture)], {})}
            c.write(root / 'cache/key.json', entry)
            self.assertTrue(jobs.cached('key', {})['cached'])
            texture.write_bytes(b'two'); self.assertIsNone(jobs.cached('key', {}))
            texture.write_bytes(b'one'); c.write(report, {'ok': False})
            self.assertIsNone(jobs.cached('key', {}))

    def test_failed_check_is_not_cached(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'ROOT', Path(folder)):
            root = Path(folder)
            c.write(root / 'cache/key.json', {'job_id': 'fixture'})
            c.write(root / 'jobs/fixture/job.json', {'state': 'failed'})
            self.assertIsNone(jobs.cached('key', {}))

    def test_automation_zero_tests_or_incomplete_is_not_a_pass(self):
        self.assertFalse(jobs.automation_summary({'succeeded': 0, 'failed': 0})['ok'])
        self.assertFalse(jobs.automation_summary({'succeeded': 2, 'failed': 1})['ok'])
        self.assertFalse(jobs.automation_summary({'succeeded': 2, 'inProcess': 1})['ok'])
        self.assertTrue(jobs.automation_summary({'succeeded': 2, 'failed': 0})['ok'])

    def test_forced_rerun_requires_reason(self):
        with self.assertRaisesRegex(ValueError, 'reason'): jobs.submit('validate', {}, force=True)

    def test_project_identity_mismatch_is_rejected(self):
        client = Mock()
        client.tool.return_value = {'content': [{'type': 'text', 'text': '- PythonTypes.SharedGameDevTools_123: Tools'}]}
        client.invoke.return_value = {'content': [{'type': 'text', 'text': json.dumps({'returnValue': json.dumps({'nonce': 'wrong', 'project': 'other'})})}]}
        with self.assertRaisesRegex(ValueError, 'identity'): ue.verified(client, {'nonce': 'expected', 'project': 'wanted'})

    def test_nested_native_return_value_decoding(self):
        self.assertEqual(ue.decoded({'content': [{'type': 'text', 'text': json.dumps({'returnValue': json.dumps({'ok': True})})}]}), {'ok': True})

    def test_project_lock_conflicts_with_other_agents(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'FAB_ROOT', Path(folder)):
            with c.project_lock(Path('C:/example/Test.uproject')):
                script = 'from pathlib import Path; import common as c; c.project_lock(Path("C:/example/Test.uproject")).__enter__()'
                p = subprocess.run([sys.executable, '-c', script], cwd=Path(c.__file__).parent,
                                   env={**os.environ, 'FAB_BRIDGE_HOME': folder}, capture_output=True, text=True, timeout=10)
                self.assertNotEqual(p.returncode, 0)
                self.assertIn('Another agent', p.stderr)

    def test_relative_source_and_unsafe_import_destination_rejected(self):
        with self.assertRaises(ValueError): c.absolute('relative.blend')
        with tempfile.NamedTemporaryFile(suffix='.fbx') as source:
            with self.assertRaises(ValueError): bridge.unreal_import_fbx('unused', source.name, '/Game/../Other')

    def test_status_does_not_start_work(self):
        with patch.object(subprocess, 'Popen') as launch:
            bridge.status()
            launch.assert_not_called()


if __name__ == '__main__': unittest.main()
