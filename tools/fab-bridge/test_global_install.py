import contextlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import install_global


class GlobalInstallTests(unittest.TestCase):
    def test_installed_runtime_works_without_source_from_unrelated_cwd(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            result = install_global.install(root / 'state/app')
            cwd = root / 'Unrelated Project'
            cwd.mkdir()
            # A workspace module with the same name must not shadow the installed bridge.
            (cwd / 'bridge.py').write_text('raise RuntimeError("wrong bridge")')
            env = {k: v for k, v in os.environ.items() if k not in ('FAB_BRIDGE_HOME', 'PYTHONPATH')}
            proc = subprocess.run([sys.executable, result['launcher'], 'status'], cwd=cwd, env=env,
                                  text=True, encoding='utf-8', capture_output=True, timeout=15)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            status = json.loads(proc.stdout)
            self.assertEqual(Path(status['state_directory']), root / 'state')
            self.assertTrue(Path(status['runtime_path']).is_relative_to(root / 'state/app/releases'))
            repeated = install_global.install(root / 'state/app')
            self.assertEqual(repeated['release'], result['release'])
            self.assertEqual(len(list((root / 'state/app/releases').iterdir())), 1)


if __name__ == '__main__':
    unittest.main()
