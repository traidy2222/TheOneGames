# Executed by UnrealEditor-Cmd's Python commandlet. REQUEST_PATH is prepended by the bridge.
import json
from pathlib import Path
import unreal

request = json.loads(Path(REQUEST_PATH).read_text(encoding='utf-8'))
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(synchronous_search=True)
packages = []
for entry in request['files']:
    name = entry['destination'].replace('\\', '/')
    if not name.endswith(('.uasset', '.umap')):
        continue
    if name.startswith('Content/'):
        package = '/Game/' + name[len('Content/'):].rsplit('.', 1)[0]
    elif '/Content/' in name and request.get('plugin_name'):
        package = '/' + request['plugin_name'] + '/' + name.split('/Content/', 1)[1].rsplit('.', 1)[0]
    else:
        continue
    packages.append((package, name.endswith('.umap')))
found, missing, loaded, failed = [], [], [], []
for package, is_map in packages:
    assets = registry.get_assets_by_package_name(package)
    if not assets:
        missing.append(package)
    else:
        found.append({'package': package, 'class': str(assets[0].asset_class_path.asset_name)})
        if not is_map and len(loaded) + len(failed) < 25:
            try:
                asset = assets[0].get_asset()
                (loaded if asset is not None else failed).append(package)
            except Exception:
                failed.append(package)
plugin_verified = False
if request.get('plugin_name'):
    # Reaching this commandlet means enabled precompiled plugin modules passed editor startup.
    plugin_verified = request['plugin_name'] in unreal.PluginBlueprintLibrary.get_enabled_plugin_names()
result = {'success': bool(packages or plugin_verified) and not missing and not failed and (not request.get('plugin_name') or plugin_verified),
          'registry_packages': len(found), 'load_sample_count': len(loaded), 'loaded_sample': loaded,
          'missing_packages': missing, 'failed_loads': failed, 'assets_sample': found[:25],
          'plugin_enabled_in_unreal': plugin_verified, 'scope': 'All package registrations, up to 25 non-map asset loads; no render or gameplay test.'}
Path(request['output']).write_text(json.dumps({**result, 'registry_assets': found}, indent=2), encoding='utf-8')
if not result['success']:
    raise RuntimeError('Fab bridge verification failed; see JSON report')
