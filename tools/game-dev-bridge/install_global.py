"""Install an immutable user-level release, verifying MCP before switching."""
import hashlib
import json
from pathlib import Path
import os
import subprocess
import sys
import common as c

FILES = ['bridge.py', 'common.py', 'jobs.py', 'unreal_link.py', 'unreal_bootstrap.py',
         'unreal_tools.py', 'blender_runner.py', 'README.md', 'test_bridge.py',
         'examples/make_crate.py', 'examples/test_crate_unreal.py', 'character_specs.py',
         'character_blender.py', 'character_unreal.py', 'CHARACTERS.md', 'test_character.py',
         'examples/make_character_fixture.py', 'examples/test_character_unreal.py']
BOOTSTRAP = '''from pathlib import Path
import json, os, re, runpy, sys
root = Path(__file__).resolve().parent
release = json.loads((root / 'current.json').read_text())['release']
if not re.fullmatch('[0-9a-f]{24}', release): raise ValueError('Invalid release pointer')
directory = (root / 'releases' / release).resolve()
if not directory.is_relative_to((root / 'releases').resolve()): raise ValueError('Invalid release directory')
os.environ.setdefault('GAME_DEV_HOME', str(root.parent))
sys.path.insert(0, str(directory))
runpy.run_path(str(directory / 'bridge.py'), run_name='__main__')
'''


def install(destination=None):
    source = Path(__file__).resolve().parent
    destination = Path(destination or c.ROOT / 'app').resolve()
    if destination == source or destination.is_relative_to(source): raise ValueError('Use an independent install directory')
    contents = {name: (source / name).read_bytes() for name in FILES}
    hashes = {name: hashlib.sha256(value).hexdigest() for name, value in contents.items()}
    release = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()[:24]
    target = destination / 'releases' / release
    for name, value in contents.items():
        path = target / name; path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists() and path.read_bytes() != value: raise ValueError('Release hash collision or modified installation')
        if not path.exists(): path.write_bytes(value)
    requests = [{'jsonrpc': '2.0', 'id': 1, 'method': 'initialize'}, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}]
    process = subprocess.run([sys.executable, str(target / 'bridge.py'), 'serve'],
                             input='\n'.join(map(json.dumps, requests)) + '\n', capture_output=True, text=True,
                             timeout=15, cwd=destination, env={**os.environ, 'GAME_DEV_HOME': str(destination.parent)})
    responses = list(map(json.loads, process.stdout.splitlines()))
    if process.returncode or process.stderr or len(responses) != 2 or len(responses[1]['result']['tools']) != 21:
        raise ValueError('Installed MCP transport check failed; prior active release retained')
    c.write(target / 'manifest.json', {'release': release, 'sha256': hashes})
    launcher = destination / 'bridge.py'
    temporary = destination / 'bridge.py.tmp'; temporary.write_text(BOOTSTRAP); os.replace(temporary, launcher)
    c.write(destination / 'current.json', {'release': release})
    config = {'mcpServers': {'game-dev': {'command': sys.executable, 'args': [str(launcher), 'serve'],
                                        'env': {'GAME_DEV_HOME': str(destination.parent)}}}}
    c.write(destination / 'mcp-config.json', config)
    return {'launcher': str(launcher), 'release': release, 'tools': 21, 'config': str(destination / 'mcp-config.json')}


if __name__ == '__main__': print(json.dumps(install(), indent=2))
