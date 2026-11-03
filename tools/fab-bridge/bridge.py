"""Local Fab/Unreal inventory and stdio MCP. Python 3.11+, standard library only."""
from __future__ import annotations

import argparse
import base64
import ctypes
from contextlib import contextmanager
import datetime as dt
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

STATE = Path(os.environ.get('FAB_BRIDGE_HOME', str(Path.home() / '.codex/fab-agent-bridge')))
PROGRAM_DATA = Path(os.environ.get('PROGRAMDATA', 'C:/ProgramData'))
CLIENT_ID = '34a02cf8f4414e29b15921876da36f9a'
# Public Launcher client identifier, also used by Legendary/epic-fab. Not a user credential.
CLIENT_KEY = 'daafbccc737745039dffe53d94fc76cf'
TOKEN_URL = 'https://account-public-service-prod03.ol.epicgames.com/account/api/oauth/token'
LOGIN_URL = f'https://www.epicgames.com/id/api/redirect?clientId={CLIENT_ID}&responseType=code'


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def read_json(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding='utf-8-sig'))
    except FileNotFoundError:
        return default


def read_descriptor(path):
    # Unreal descriptors permit comments/trailing commas. Preserve quoted URLs and text.
    text = Path(path).read_text(encoding='utf-8-sig')
    quoted = r'"(?:\\.|[^"\\])*"'
    text = re.sub(quoted + r'|//[^\r\n]*|/\*[\s\S]*?\*/',
                  lambda m: m[0] if m[0].startswith('"') else ' ', text)
    text = re.sub(quoted + r'|,(?=\s*[}\]])',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    return json.loads(text)


def atomic_bytes(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name, suffix='.tmp')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def save_json(path, value):
    atomic_bytes(path, json.dumps(value, ensure_ascii=False, indent=2).encode('utf-8'))


def protect(data, decrypt=False):
    """Windows user-bound DPAPI; never persist plaintext tokens."""
    if os.name != 'nt':
        raise ValueError('Persistent login currently requires Windows DPAPI.')
    from ctypes import wintypes

    class Blob(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('data', ctypes.POINTER(ctypes.c_ubyte))]

    buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    source, dest = Blob(len(data), buffer), Blob()
    crypt = ctypes.WinDLL('crypt32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    fn = crypt.CryptUnprotectData if decrypt else crypt.CryptProtectData
    fn.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    fn.restype = wintypes.BOOL
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    if not fn(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(dest)):
        raise ValueError('Windows could not protect/unprotect the login for this user.')
    try:
        return ctypes.string_at(dest.data, dest.size)
    finally:
        kernel.LocalFree(dest.data)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None  # Never forward account credentials to a redirected host.


def request_json(url, headers=None, form=None):
    request = urllib.request.Request(url, headers={'Accept': 'application/json', 'User-Agent': 'FabAgentBridge/0.1', **(headers or {})},
                                     data=urllib.parse.urlencode(form).encode() if form is not None else None)
    try:
        with urllib.request.build_opener(NoRedirect()).open(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        # Response bodies can contain tokens or login details; do not return them to agents.
        raise ValueError(f'Account service returned HTTP {error.code}. Login may need renewal; no catalogue changes were committed.') from None
    except urllib.error.URLError:
        raise ValueError('Account service could not be reached; existing catalogue retained.') from None


def exchange(form):
    auth = base64.b64encode(f'{CLIENT_ID}:{CLIENT_KEY}'.encode()).decode()
    token = request_json(TOKEN_URL, {'Authorization': 'Basic ' + auth}, {**form, 'token_type': 'eg1'})
    if not all(isinstance(token.get(k), str) and token[k] for k in ('access_token', 'refresh_token', 'account_id', 'expires_at')):
        raise ValueError('Unexpected Epic authentication response; login was not stored.')
    atomic_bytes(STATE / 'login.dpapi', protect(json.dumps(token).encode()))
    return token


@contextmanager
def auth_lock():
    """Serialize shared credential rotation across independent agent processes."""
    import msvcrt
    folder = STATE / 'locks'
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / 'authentication.lock').open('a+b') as stream:
        if stream.tell() == 0:
            stream.write(b'0')
            stream.flush()
        deadline = time.monotonic() + 60
        while True:
            stream.seek(0)
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise ValueError('Another agent is refreshing the shared login; retry shortly.') from None
                time.sleep(0.1)
        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def read_tokens():
    path = STATE / 'login.dpapi'
    if not path.exists():
        raise ValueError('Epic login required. Run bridge.py login in your own terminal; do not paste credentials into chat.')
    return json.loads(protect(path.read_bytes(), decrypt=True))


def token_expiring(token):
    expiry = dt.datetime.fromisoformat(token['expires_at'].replace('Z', '+00:00'))
    return expiry <= dt.datetime.now(dt.timezone.utc) + dt.timedelta(minutes=2)


def tokens():
    token = read_tokens()
    if token_expiring(token):
        with auth_lock():
            # Another agent may already have rotated the refresh token while we waited.
            token = read_tokens()
            if token_expiring(token):
                token = exchange({'grant_type': 'refresh_token', 'refresh_token': token['refresh_token']})
    return token


def login():
    print('Open this Epic URL yourself, sign in, and copy the authorizationCode value:')
    print(LOGIN_URL)
    code = getpass.getpass('Authorization code (hidden; stays on this machine): ').strip()
    if not code:
        raise ValueError('No authorization code provided.')
    with auth_lock():
        token = exchange({'grant_type': 'authorization_code', 'code': code})
        previous = read_json(STATE / 'cloud.json', {})
        if previous and previous.get('account_key') != hashlib.sha256(token['account_id'].encode()).hexdigest():
            (STATE / 'cloud.json').unlink(missing_ok=True)
    return sync_cloud()


def fetch_library(token, fetch=request_json):
    account = token['account_id']
    if not re.fullmatch(r'[A-Za-z0-9_-]+', account):
        raise ValueError('Unexpected account identifier.')
    records, seen, cursor = {}, set(), None
    for _ in range(10000):
        query = {'count': 100}
        if cursor:
            query['cursor'] = cursor
        page = fetch(f'https://www.fab.com/e/accounts/{account}/ue/library?' + urllib.parse.urlencode(query),
                     {'Authorization': 'Bearer ' + token['access_token']})
        if not isinstance(page, dict) or not isinstance(page.get('results'), list) or not isinstance(page.get('cursors'), dict) or 'next' not in page['cursors']:
            raise ValueError('Fab library schema changed; refusing to replace the last catalogue.')
        for record in page['results']:
            if not isinstance(record, dict) or not isinstance(record.get('assetId'), str) or not record['assetId']:
                raise ValueError('Unrecognized library item; refusing partial sync.')
            versions = record.get('projectVersions', [])
            if not isinstance(versions, list) or any(not isinstance(v, dict) for v in versions):
                raise ValueError('Unrecognized project versions; refusing partial sync.')
            if any(not isinstance(v.get('artifactId', ''), str) or not isinstance(v.get('engineVersions', []), list)
                   or any(not isinstance(e, str) for e in v.get('engineVersions', [])) for v in versions):
                raise ValueError('Unrecognized engine version metadata; refusing partial sync.')
            uid = record['assetId']
            # Explicit allowlist avoids persisting auth fields or signed download URLs.
            records[uid] = {
                'id': 'fab:' + uid, 'asset_id': uid,
                'title': record.get('title') or record.get('name') or uid,
                'description': record.get('description', ''),
                'kind': record.get('distributionMethod', 'unknown'),
                'namespace': record.get('assetNamespace', ''),
                'versions': [{'artifact_id': v.get('artifactId', ''), 'engine_versions': v.get('engineVersions', [])} for v in versions],
                'owned': True, 'ownership_source': 'authenticated_fab_ue_library',
            }
        cursor = page['cursors']['next']
        if cursor is None or cursor == '':
            return list(records.values())
        if not isinstance(cursor, str) or cursor in seen:
            raise ValueError('Fab pagination repeated or returned an invalid cursor; last catalogue retained.')
        seen.add(cursor)
    raise ValueError('Fab pagination limit reached; last catalogue retained.')


def sync_cloud():
    token = tokens()
    records = fetch_library(token)
    save_json(STATE / 'cloud.json', {'synced_at': now(), 'scope': 'Fab Unreal Engine library only',
                                  'account_key': hashlib.sha256(token['account_id'].encode()).hexdigest(),
                                  'complete_for_endpoint': True, 'records': records})
    return {'synced': len(records), 'scope': 'Fab Unreal Engine library only', 'synced_at': now()}


def config():
    return read_json(STATE / 'config.json', {})


def scan_local():
    cfg = config()
    records, warnings, engines = [], [], []
    installed = read_json(PROGRAM_DATA / 'Epic/UnrealEngineLauncher/LauncherInstalled.dat', {})
    for engine in installed.get('InstallationList', []):
        if engine.get('AppName', '').startswith('UE_'):
            engines.append({'version': engine['AppName'], 'path': engine['InstallLocation']})
    roots = [(Path(e['path']) / 'Engine/Plugins', 'engine', e['version']) for e in engines]
    project_roots = [Path(p) for p in cfg.get('project_roots', [str(Path.home() / 'Documents/Unreal Projects')])]
    projects = []
    for root in project_roots:
        if not root.exists():
            warnings.append(f'Project root missing: {root}')
            continue
        for path in root.rglob('*.uproject'):
            projects.append(str(path))
            roots.append((path.parent / 'Plugins', 'project', str(path)))
            content = path.parent / 'Content'
            if content.exists():
                for asset in content.rglob('*'):
                    if asset.suffix.lower() in ('.uasset', '.umap'):
                        records.append({'id': 'file:' + str(asset), 'title': asset.stem, 'kind': 'unreal_package',
                                        'path': str(asset), 'project': str(path), 'owned': None, 'local_state': 'project_file_present',
                                        'object_path': '/Game/' + asset.relative_to(content).with_suffix('').as_posix(),
                                        'note': 'Presence only; asset type, loadability and license are not inferred.'})
    for root, scope, version in roots:
        if not root.exists():
            continue
        for path in root.rglob('*.uplugin'):
            try:
                d = read_descriptor(path)
                records.append({'id': 'plugin:' + str(path), 'title': d.get('FriendlyName', path.stem),
                                'description': d.get('Description', ''), 'kind': 'plugin', 'path': str(path),
                                'scope': scope, 'host': version, 'engine_version': d.get('EngineVersion'),
                                'marketplace_url': d.get('MarketplaceURL', ''), 'owned': None,
                                'local_state': 'plugin_descriptor_present', 'note': 'Enablement/build compatibility not verified.'})
            except (ValueError, OSError, TypeError):
                warnings.append(f'Could not read plugin descriptor: {path}')
    vaults = [Path(p) for p in cfg.get('vault_roots', [str(PROGRAM_DATA / 'Epic/EpicGamesLauncher/VaultCache')])]
    for vault in vaults:
        if not vault.exists():
            warnings.append(f'Vault root missing: {vault}')
            continue
        for folder in vault.iterdir():
            if folder.is_dir() and folder.name != 'FabLibrary':
                records.append({'id': 'vault:' + str(folder), 'title': folder.name, 'kind': 'vault_cache',
                                'artifact_id': folder.name, 'path': str(folder), 'owned': None,
                                'local_state': 'vault_directory_present', 'note': 'Cache completeness not verified.'})
        db = vault / 'FabLibrary/listings_v1.db'
        if db.exists():
            try:
                connection = sqlite3.connect(db.resolve().as_uri() + '?mode=ro', uri=True)
                connection.row_factory = sqlite3.Row
                try:
                    for row in connection.execute('SELECT listing_uid,format,quality,path FROM download_meta'):
                        item = dict(row)
                        records.append({'id': 'download:' + str(db) + ':' + str(item), 'title': item['listing_uid'],
                                        'kind': 'fab_download', 'listing_uid': item['listing_uid'], 'path': item['path'],
                                        'format': item['format'], 'quality': item['quality'], 'owned': None,
                                        'local_state': 'download_path_present' if item['path'] and Path(item['path']).exists() else 'download_record_missing_path'})
                finally:
                    connection.close()
            except sqlite3.Error:
                warnings.append(f'Fab database unavailable or schema changed: {db}')
    result = {'scanned_at': now(), 'engines': engines, 'projects': projects, 'warnings': warnings, 'records': records}
    save_json(STATE / 'local.json', result)
    return {'local_records': len(records), 'engines': engines, 'projects': projects, 'warnings': warnings}


def catalogue():
    cloud = read_json(STATE / 'cloud.json', {})
    local = read_json(STATE / 'local.json', {})
    rows = []
    by_artifact = {r['artifact_id']: r for r in local.get('records', []) if r.get('artifact_id')}
    staged = {}
    for path in (STATE / 'downloads').glob('*/receipt.json'):
        receipt = read_json(path, {})
        if receipt.get('state') == 'verified':
            staged.setdefault(receipt['id'], []).append({'artifact_id': receipt['artifact_id'], 'engine': receipt['engine'],
                                                        'path': receipt['stage'], 'state': 'download_verified', 'completed_at': receipt['completed_at']})
    for record in cloud.get('records', []):
        row = dict(record)
        row['local_matches'] = [by_artifact[v['artifact_id']] for v in row['versions'] if v['artifact_id'] in by_artifact]
        row['local_state'] = 'vault_directory_present' if row['local_matches'] else 'not_found_in_scanned_roots'
        row['bridge_downloads'] = staged.get(row['id'], [])
        if row['bridge_downloads']:
            row['local_state'] = 'bridge_download_verified'
        rows.append(row)
    rows.extend(local.get('records', []))
    return rows


def status():
    cloud, local = read_json(STATE / 'cloud.json', {}), read_json(STATE / 'local.json', {})
    return {'runtime_path': str(Path(__file__).resolve()), 'state_directory': str(STATE.resolve()),
            'login_stored': (STATE / 'login.dpapi').exists(), 'cloud_synced_at': cloud.get('synced_at'),
            'cloud_records': len(cloud.get('records', [])), 'cloud_scope': cloud.get('scope'),
            'local_scanned_at': local.get('scanned_at'), 'local_records': len(local.get('records', [])),
            'engines': local.get('engines', []), 'projects': local.get('projects', []), 'warnings': local.get('warnings', []),
            'limits': ['Cloud sync covers UE assets/plugins; non-UE-only Fab formats are not covered.',
                       'Not found locally is not proof that an asset is undownloaded on every drive.',
                       'Local files do not prove account ownership, compatibility, or project enablement.',
                       'Downloads/imports use background jobs. Poll fab_job_status; queued does not mean completed.',
                       'Exact engine versions only. Import preserves package paths and refuses conflicting files.',
                       'Unreal verification checks registration and sample loads, not rendering or gameplay.']}


def search(query='', source='all', limit=30, offset=0):
    if source not in ('all', 'owned', 'local') or type(limit) is not int or not 1 <= limit <= 100 or type(offset) is not int or offset < 0 or not isinstance(query, str):
        raise ValueError('Expected source all/owned/local, string query, limit 1..100 and nonnegative offset.')
    words = query.casefold().split()
    rows = [r for r in catalogue() if (source == 'all' or (r.get('owned') is True) == (source == 'owned'))
            and all(w in ' '.join(str(r.get(k, '')) for k in ('title', 'description', 'kind', 'path')).casefold() for w in words)]
    return {'total': len(rows), 'offset': offset, 'items': rows[offset:offset+limit],
            'next_offset': offset+limit if offset+limit < len(rows) else None,
            'cloud_synced_at': read_json(STATE / 'cloud.json', {}).get('synced_at')}


def get_asset(id):
    for row in catalogue():
        if row['id'] == id:
            return row
    raise ValueError('Asset ID not found. Search first and use the exact returned ID.')


def prepare(id, engine='UE_5.8'):
    row = get_asset(id)
    exact = [v for v in row.get('versions', []) if engine in v['engine_versions']]
    return {'asset': row, 'requested_engine': engine, 'exact_distributions': exact,
            'compatibility': 'listed_for_engine' if exact else 'not_established',
            'action': 'Use fab_plan_download, fab_download_asset, fab_plan_import, fab_import_asset and fab_verify_asset. Poll fab_job_status after background operations.',
            'executed': False}


def ops(name, **args):
    import asset_ops
    return getattr(asset_ops, name)(**args)


TOOLS = {
    'fab_status': (status, {}, []),
    'fab_search': (search, {'query': {'type': 'string'}, 'source': {'type': 'string', 'enum': ['all', 'owned', 'local']},
                          'limit': {'type': 'integer', 'minimum': 1, 'maximum': 100}, 'offset': {'type': 'integer', 'minimum': 0}}, []),
    'fab_get_asset': (get_asset, {'id': {'type': 'string'}}, ['id']),
    'fab_prepare_asset': (prepare, {'id': {'type': 'string'}, 'engine': {'type': 'string'}}, ['id']),
    'fab_scan_local': (scan_local, {}, []),
    'fab_sync_owned': (sync_cloud, {}, []),
    'fab_plan_download': (lambda **a: ops('plan_download', **a), {'id': {'type': 'string'}, 'engine': {'type': 'string'}}, ['id']),
    'fab_download_asset': (lambda **a: ops('download_asset', **a), {'id': {'type': 'string'}, 'engine': {'type': 'string'}, 'max_download_mb': {'type': 'integer', 'minimum': 1, 'maximum': 102400}}, ['id']),
    'fab_plan_import': (lambda **a: ops('plan_import', **a), {'id': {'type': 'string'}, 'project': {'type': 'string'}, 'engine': {'type': 'string'}}, ['id', 'project']),
    'fab_import_asset': (lambda **a: ops('import_asset', **a), {'id': {'type': 'string'}, 'project': {'type': 'string'}, 'engine': {'type': 'string'}}, ['id', 'project']),
    'fab_verify_asset': (lambda **a: ops('verify_asset', **a), {'id': {'type': 'string'}, 'project': {'type': 'string'}, 'engine': {'type': 'string'}}, ['id', 'project']),
    'fab_job_status': (lambda **a: ops('job_status', **a), {'job_id': {'type': 'string'}}, ['job_id']),
    'fab_project_assets': (lambda **a: ops('project_assets', **a), {'id': {'type': 'string'}, 'project': {'type': 'string'}, 'query': {'type': 'string'}, 'limit': {'type': 'integer', 'minimum': 1, 'maximum': 100}, 'offset': {'type': 'integer', 'minimum': 0}}, ['id', 'project']),
    'fab_inspect_project': (lambda **a: ops('inspect_project', **a), {'project': {'type': 'string'}}, ['project']),
}


def rpc(message):
    if not isinstance(message, dict) or message.get('jsonrpc') != '2.0' or not isinstance(message.get('method'), str):
        return {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32600, 'message': 'Invalid request'}}
    if 'id' not in message:
        return None
    response = {'jsonrpc': '2.0', 'id': message['id']}
    method, params = message['method'], message.get('params', {})
    if method == 'initialize':
        requested = params.get('protocolVersion') if isinstance(params, dict) else None
        response['result'] = {'protocolVersion': requested if requested in ('2024-11-05', '2025-03-26', '2025-06-18', '2025-11-25') else '2025-06-18',
                              'capabilities': {'tools': {}}, 'serverInfo': {'name': 'fab-agent-bridge', 'version': '0.3.0'}}
    elif method == 'ping':
        response['result'] = {}
    elif method == 'tools/list':
        response['result'] = {'tools': [{'name': name, 'description': {
            'fab_status': 'Report inventory coverage, freshness and account-link status.',
            'fab_search': 'Search owned UE Fab assets including cloud-only items, and scanned local files/plugins.',
            'fab_get_asset': 'Read one exact catalogue record.', 'fab_prepare_asset': 'Return exact engine distributions and installation guidance. Does not install.',
            'fab_scan_local': 'Refresh local inventory from configured roots. Writes bridge cache only.',
            'fab_sync_owned': 'Refresh owned UE library through Epic login, including undownloaded items. Requires terminal login first.',
            'fab_plan_download': 'Resolve an owned exact-engine distribution and return verified manifest sizes before downloading.',
            'fab_download_asset': 'Start a verified, resumable download into the local bridge staging cache. Poll fab_job_status.',
            'fab_plan_import': 'Check a downloaded asset against an existing .uproject; report file conflicts and skipped configuration.',
            'fab_import_asset': 'Start import into a closed .uproject. Preserves package paths; refuses overwrite conflicts. Code plugins are enabled in the project. Poll fab_job_status.',
            'fab_verify_asset': 'Start headless Unreal validation of a bridge installation. Target project must be closed. Poll fab_job_status.',
            'fab_job_status': 'Read download/import/verification progress and result. Only state succeeded means the operation completed.',
            'fab_project_assets': 'Search an imported pack in a target project and return Unreal package paths, known asset classes, file presence and verification freshness for agent scripts.',
            'fab_inspect_project': 'Inspect any explicit .uproject path, including outside indexed roots, to determine its engine version and whether the matching engine is installed.'}[name],
            'inputSchema': {'type': 'object', 'properties': props, 'required': required, 'additionalProperties': False},
            'annotations': {'readOnlyHint': name not in ('fab_scan_local', 'fab_sync_owned', 'fab_download_asset', 'fab_import_asset', 'fab_verify_asset'), 'destructiveHint': False}}
            for name, (_, props, required) in TOOLS.items()]}
    elif method == 'tools/call':
        name = params.get('name') if isinstance(params, dict) else None
        args = params.get('arguments', {}) if isinstance(params, dict) else None
        if name not in TOOLS or not isinstance(args, dict):
            response['error'] = {'code': -32602, 'message': 'Unknown tool or invalid arguments'}
        else:
            fn, props, required = TOOLS[name]
            if set(args) - set(props) or set(required) - set(args) or any(
                (props[k]['type'] == 'string' and not isinstance(v, str)) or
                (props[k]['type'] == 'integer' and type(v) is not int) for k, v in args.items() if k in props):
                response['error'] = {'code': -32602, 'message': 'Invalid tool arguments'}
            else:
                try:
                    result = fn(**args)
                    response['result'] = {'content': [{'type': 'text', 'text': json.dumps(result, ensure_ascii=False)}], 'isError': False}
                except (ValueError, OSError, KeyError, TypeError) as error:
                    message = str(error) if isinstance(error, ValueError) and 'https://' not in str(error) and 'Bearer' not in str(error) else 'Operation failed. Check fab_status or the job log.'
                    response['result'] = {'content': [{'type': 'text', 'text': message}], 'isError': True}
    else:
        response['error'] = {'code': -32601, 'message': 'Method not found'}
    return response


def serve():
    for line in sys.stdin:
        try:
            response = rpc(json.loads(line))
        except (ValueError, TypeError):
            response = {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32700, 'message': 'Parse error'}}
        if response is not None:
            print(json.dumps(response, ensure_ascii=False), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['status', 'scan', 'login', 'logout', 'sync', 'search', 'get', 'prepare', 'serve', 'plan-download', 'download', 'plan-import', 'import', 'verify', 'job', 'assets', 'project'])
    parser.add_argument('value', nargs='?', default='')
    parser.add_argument('--source', choices=['all', 'owned', 'local'], default='all')
    parser.add_argument('--limit', type=int, default=30)
    parser.add_argument('--offset', type=int, default=0)
    parser.add_argument('--engine', default='UE_5.8')
    parser.add_argument('--project')
    parser.add_argument('--query', default='')
    parser.add_argument('--max-download-mb', type=int, default=1024)
    args = parser.parse_args()
    if args.command == 'serve':
        return serve()
    try:
        if args.command == 'logout':
            with auth_lock():
                (STATE / 'login.dpapi').unlink(missing_ok=True)
                (STATE / 'cloud.json').unlink(missing_ok=True)
            result = {'logged_out': True, 'cloud_cache_removed': True}
        else:
            result = {'status': status, 'scan': scan_local, 'login': login, 'sync': sync_cloud,
                      'search': lambda: search(args.value, args.source, args.limit, args.offset),
                      'get': lambda: get_asset(args.value), 'prepare': lambda: prepare(args.value, args.engine),
                      'plan-download': lambda: ops('plan_download', id=args.value, engine=args.engine),
                      'download': lambda: ops('download_asset', id=args.value, engine=args.engine, max_download_mb=args.max_download_mb),
                      'plan-import': lambda: ops('plan_import', id=args.value, project=args.project, engine=args.engine),
                      'import': lambda: ops('import_asset', id=args.value, project=args.project, engine=args.engine),
                      'verify': lambda: ops('verify_asset', id=args.value, project=args.project, engine=args.engine),
                      'job': lambda: ops('job_status', job_id=args.value),
                      'assets': lambda: ops('project_assets', id=args.value, project=args.project, query=args.query, limit=args.limit, offset=args.offset),
                      'project': lambda: ops('inspect_project', project=args.value)}[args.command]()
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({'error': str(error)}, ensure_ascii=False), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
    sys.exit(main())
