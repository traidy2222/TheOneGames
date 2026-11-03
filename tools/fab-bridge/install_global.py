"""Deploy a self-contained, versioned runtime under the current Windows user.

Does not read/copy login or inventory data or alter project files. MCP registration
is a separate CLI operation so deployment is verifiable before changing config.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

FILES = ['bridge.py', 'asset_ops.py', 'epic_manifest.py', 'unreal_verify.py', 'README.md',
         'Login.ps1', 'test_bridge.py', 'test_asset_ops.py', 'examples/place_meshes.py']

BOOTSTRAP = '''"""Stable user-level entry point for Fab Agent Bridge."""
import json
import os
from pathlib import Path
import re
import runpy
import sys

root = Path(__file__).resolve().parent
release = json.loads((root / 'current.json').read_text(encoding='utf-8'))['release']
if not re.fullmatch('[0-9a-f]{24}', release):
    raise RuntimeError('Invalid Fab bridge release pointer')
directory = (root / 'releases' / release).resolve()
if not directory.is_relative_to((root / 'releases').resolve()):
    raise RuntimeError('Fab bridge release escapes installation')
os.environ.setdefault('FAB_BRIDGE_HOME', str(root.parent))
sys.path.insert(0, str(directory))
runpy.run_path(str(directory / 'bridge.py'), run_name='__main__')
'''


def write_atomic(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(dir=path.parent, prefix=path.name, suffix='.tmp')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(payload)
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def install(destination):
    source = Path(__file__).resolve().parent
    destination = Path(destination).resolve()
    if destination == source or destination.is_relative_to(source):
        raise ValueError('Global installation must be outside the development source folder')
    payloads = {name: (source / name).read_bytes() for name in FILES}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()}
    release = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()[:24]
    target = destination / 'releases' / release
    for name, data in payloads.items():
        path = target / name
        if path.exists():
            if path.read_bytes() != data:
                raise ValueError('Existing release content differs from its identifier')
        else:
            write_atomic(path, data)
    # Verify actual transport from the independent installation before activating it.
    messages = [{'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2025-06-18'}},
                {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
                {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}]
    result = subprocess.run([sys.executable, str(target / 'bridge.py'), 'serve'],
                            input='\n'.join(map(json.dumps, messages)) + '\n', text=True, encoding='utf-8',
                            capture_output=True, timeout=20, cwd=destination,
                            env={**os.environ, 'FAB_BRIDGE_HOME': str(destination.parent)})
    if result.returncode or result.stderr:
        raise ValueError('Installed MCP runtime did not start cleanly; old release retained')
    responses = [json.loads(line) for line in result.stdout.splitlines()]
    if len(responses) != 2 or len(responses[1].get('result', {}).get('tools', [])) != 14:
        raise ValueError('Installed runtime did not advertise all tools; old release retained')
    manifest = {'release': release, 'source': str(source), 'sha256': hashes}
    write_atomic(target / 'release.json', json.dumps(manifest, indent=2).encode())
    write_atomic(destination / 'bridge.py', BOOTSTRAP.encode())
    write_atomic(destination / 'current.json', json.dumps({'release': release}).encode())
    config = {'mcpServers': {'fab-assets': {'command': sys.executable, 'args': [str(destination / 'bridge.py'), 'serve'],
                                         'env': {'FAB_BRIDGE_HOME': str(destination.parent)}}}}
    write_atomic(destination / 'mcp-config.json', json.dumps(config, indent=2).encode())
    return {'installed': True, 'release': release, 'launcher': str(destination / 'bridge.py'),
            'runtime': str(target), 'mcp_config': str(destination / 'mcp-config.json'), 'tools': 14}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', default=str(Path.home() / '.codex/fab-agent-bridge/app'))
    args = parser.parse_args()
    print(json.dumps(install(args.destination), indent=2))
