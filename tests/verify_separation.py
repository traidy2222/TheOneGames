"""Check the dependency boundary and migrated active content references."""
from pathlib import Path
import json
import sys

root = Path(__file__).resolve().parents[1]
engine = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root.parent / 'TheOne'
for name in ('application.cpp', 'application.hpp', 'game.hpp', 'sandbox_game.cpp', 'main.cpp'):
    assert not (engine / 'src' / name).exists(), f'Game source remains in engine: {name}'
assert not (engine / 'assets').exists(), 'Runtime game assets remain in engine'
cmake = (engine / 'CMakeLists.txt').read_text()
assert 'Games 2' not in cmake and 'robots' not in cmake and 'sandbox' not in cmake
assert 'TheOne::Renderer' in cmake and 'TheOne::Content' in cmake
for name in ('robots-village.one', 'robots-city.one'):
    scene = root / 'games/robots/build' / name
    if not scene.exists():
        continue
    for entity in json.loads(scene.read_text())['entities']:
        if not entity.get('asset'): continue
        asset = Path(entity['asset'])
        assert asset.is_file(), f'Missing relocated asset: {asset}'
        assert not asset.is_relative_to(engine), f'Asset still depends on engine data: {asset}'
        if asset.suffix == '.gltf':
            data = json.loads(asset.read_text())
            for ref in data.get('buffers', []) + data.get('images', []):
                uri = ref.get('uri', '')
                if uri and not uri.startswith('data:'):
                    assert (asset.parent / uri).is_file(), f'Missing glTF resource: {asset}: {uri}'
print('PASS: engine/game dependency boundary and active relocated scene resources')

# Shared engine implementations must not reappear in a game host.
for name in ('editor_history.cpp', 'editor_history.hpp', 'scene_asset_reuse.cpp', 'scene_asset_reuse.hpp'):
    assert not (root / 'shared/import-host' / name).exists(), f'Duplicated engine implementation: {name}'
for name in ('include/theone/scene/editor_history.hpp', 'include/theone/physics/scene_reuse.hpp'):
    assert (engine / name).is_file(), f'Missing public engine API: {name}'
for cmake in [root / 'CMakeLists.txt', *root.glob('games/*/CMakeLists.txt')]:
    text = cmake.read_text()
    assert '${THEONE_ROOT}/src' not in text, f'Private engine include path: {cmake}'
print('PASS: promoted services are engine-owned and game targets use public engine APIs')
