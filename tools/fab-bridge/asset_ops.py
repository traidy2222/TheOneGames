"""Verified downloads, conflict-safe project installation and headless Unreal jobs."""
from __future__ import annotations
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import uuid

import bridge as b
from epic_manifest import parse_manifest, decode_chunk, chunk_path, relative_path


def key(value):
    return hashlib.sha256(value.encode()).hexdigest()[:32]


def inside(root, relative):
    root = Path(root).resolve()
    path = root / relative_path(relative)
    resolved = path.resolve()
    if not resolved.is_relative_to(root) or resolved == root:
        raise ValueError('Path escapes the destination')
    # Reject junctions/symlinks even when they currently point inside the root.
    for part in [path, *path.parents]:
        if part == root:
            break
        if part.is_symlink() or (part.exists() and getattr(part.lstat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError('Reparse points are not supported in installation paths')
    return path


@contextmanager
def lock(resource):
    import msvcrt
    folder = b.STATE / 'locks'
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / (key(resource) + '.lock')).open('a+b') as stream:
        if stream.tell() == 0:
            stream.write(b'0')
            stream.flush()
        stream.seek(0)
        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            raise ValueError('Another bridge operation is using this asset or project; retry after it finishes.') from None
        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def selected(id, engine):
    if not re.fullmatch(r'UE_\d+\.\d+', engine):
        raise ValueError('Engine must have the form UE_5.8')
    row = b.get_asset(id)
    if row.get('owned') is not True or row.get('kind') not in ('ASSET_PACK', 'CODE_PLUGIN'):
        raise ValueError('Select an owned Fab asset pack or code plugin')
    versions = [v for v in row['versions'] if engine in v['engine_versions'] and v['artifact_id']]
    if not versions:
        raise ValueError('No exact engine distribution. Older/newer releases are not substituted.')
    if len(versions) != 1:
        raise ValueError('Multiple exact distributions; explicit artifact selection is not yet supported.')
    return row, versions[0]['artifact_id']


def cdn_bytes(url, limit):
    parsed = urllib.parse.urlsplit(url)
    host = (parsed.hostname or '').lower()
    allowed = ('epicgames.com', 'epicgamescdn.com', 'akamaized.net', 'cloudfront.net', 'fastly.net', 'fab.com')
    if parsed.scheme != 'https' or parsed.username or parsed.password or parsed.port not in (None, 443) or not any(host == d or host.endswith('.' + d) for d in allowed):
        raise ValueError('Manifest returned an unsupported CDN host')
    try:
        request = urllib.request.Request(url, headers={'User-Agent': 'FabAgentBridge/0.2'})
        with urllib.request.build_opener(b.NoRedirect()).open(request, timeout=45) as response:
            data = response.read(limit + 1)
        if len(data) > limit:
            raise ValueError('CDN response exceeded its size limit')
        return data
    except (urllib.error.URLError, TimeoutError):
        raise ValueError('CDN request failed or expired; retry the job to obtain fresh download URLs.') from None


def resolve(id, engine):
    row, artifact = selected(id, engine)
    token = b.tokens()
    cloud = b.read_json(b.STATE / 'cloud.json', {})
    if cloud.get('account_key') != hashlib.sha256(token['account_id'].encode()).hexdigest():
        raise ValueError('Sync the current account before downloading')
    payload = json.dumps({'item_id': row['asset_id'], 'namespace': row['namespace'], 'platform': 'Windows'}).encode()
    req = urllib.request.Request('https://www.fab.com/e/artifacts/' + urllib.parse.quote(artifact, safe='') + '/manifest',
                                 data=payload, headers={'Authorization': 'Bearer ' + token['access_token'],
                                 'Content-Type': 'application/json', 'Accept': 'application/json', 'User-Agent': 'FabAgentBridge/0.2'})
    try:
        with urllib.request.build_opener(b.NoRedirect()).open(req, timeout=30) as response:
            info = json.load(response)
    except (urllib.error.URLError, TimeoutError):
        raise ValueError('Fab refused the download manifest or could not be reached. Refresh login/library and retry.') from None
    pointers = []
    for entry in info.get('downloadInfo', []):
        if entry.get('artifactId') not in (None, artifact):
            continue
        bases = entry.get('distributionPointBaseUrls', [])
        for i, point in enumerate(entry.get('distributionPoints', [])):
            if i < len(bases) and point.get('manifestUrl'):
                pointers.append((point['manifestUrl'], bases[i]))
    if not pointers:
        raise ValueError('Fab returned no supported download locations')
    last_failure = 'No supported manifest'
    for url, base in pointers:
        try:
            manifest = parse_manifest(cdn_bytes(url, 64 * 1024 * 1024))
            return row, artifact, manifest, base
        except ValueError as error:
            last_failure = str(error)
            continue
    raise ValueError('No CDN yielded a supported, verified manifest: ' + last_failure)


def plan_download(id, engine='UE_5.8'):
    row, artifact, manifest, _ = resolve(id, engine)
    return summary(row, artifact, manifest, engine)


def summary(row, artifact, manifest, engine):
    return {'id': row['id'], 'title': row['title'], 'kind': row['kind'], 'engine': engine, 'artifact_id': artifact,
            'files': len(manifest['files']), 'download_bytes': sum(c['size'] for c in manifest['chunks'].values()),
            'installed_bytes': sum(f['size'] for f in manifest['files']),
            'manifest_sha256': manifest['manifest_sha256'], 'paths_sample': [f['path'] for f in manifest['files'][:12]],
            'stage': str(stage_path(artifact, engine))}


def stage_path(artifact, engine):
    return b.STATE / 'downloads' / key(engine + ':' + artifact)


def matches(path, entry):
    if not path.is_file() or path.stat().st_size != entry['size']:
        return False
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha1').hexdigest() == entry['sha1']


def update_job(job, **changes):
    path = b.STATE / 'jobs' / (job + '.json')
    value = b.read_json(path, {})
    value.update(changes, updated_at=b.now())
    b.save_json(path, value)


def download(id, engine, max_download_mb, job):
    if type(max_download_mb) is not int or not 1 <= max_download_mb <= 102400:
        raise ValueError('max_download_mb must be 1..102400')
    row, artifact, manifest, base = resolve(id, engine)
    stats = summary(row, artifact, manifest, engine)
    if stats['download_bytes'] > max_download_mb * 1024 * 1024:
        raise ValueError('Download exceeds max_download_mb. Inspect fab_plan_download and pass a suitable explicit limit.')
    stage = stage_path(artifact, engine)
    with lock('download:' + str(stage)):
        stage.mkdir(parents=True, exist_ok=True)
        needed = sum(c['window'] for c in manifest['chunks'].values()) + stats['installed_bytes'] * 2 + 256 * 1024 * 1024
        if shutil.disk_usage(stage).free < needed:
            raise ValueError('Insufficient space for staging and chunk cache')
        receipt_path = stage / 'receipt.json'
        old = b.read_json(receipt_path, {})
        if old and old.get('manifest_sha256') != manifest['manifest_sha256']:
            raise ValueError('This artifact changed upstream. Existing stage retained; use a separate FAB_BRIDGE_HOME for the new revision.')
        payload_root = stage / 'payload'
        payload_root.mkdir(exist_ok=True)
        chunks_root = stage / 'chunks'
        chunks_root.mkdir(exist_ok=True)
        update_job(job, phase='downloading', plan=stats, completed_files=0)
        # Sequential, disk-backed verified cache keeps RAM bounded and supports resume.
        transferred, reused, vault_reused, last_progress = 0, 0, 0, 0.0
        for index, entry in enumerate(manifest['files']):
            target = inside(payload_root, entry['path'])
            if not matches(target, entry):
                for cached_pack in row.get('local_matches', []):
                    if cached_pack.get('artifact_id') != artifact:
                        continue
                    vault = Path(cached_pack['path'])
                    candidates = [inside(vault / 'data', entry['path']), inside(vault, entry['path'])]
                    source = next((p for p in candidates if matches(p, entry)), None)
                    if source:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        fd, name = tempfile.mkstemp(dir=target.parent, prefix='.fab-vault-', suffix='.tmp')
                        try:
                            with os.fdopen(fd, 'wb') as output, source.open('rb') as input_file:
                                shutil.copyfileobj(input_file, output, 1024 * 1024)
                            if not matches(Path(name), entry):
                                raise ValueError('Vault file changed while copying')
                            os.replace(name, target)
                            vault_reused += 1
                            break
                        finally:
                            Path(name).unlink(missing_ok=True)
            if matches(target, entry):
                reused += 1
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                fd, temporary_name = tempfile.mkstemp(dir=target.parent, prefix='.fab-download-', suffix='.tmp')
                os.close(fd)
                temporary = Path(temporary_name)
                try:
                    h = hashlib.sha1()
                    with temporary.open('wb') as output:
                        for part in entry['parts']:
                            chunk = manifest['chunks'][part['guid']]
                            cached = chunks_root / (chunk['sha1'] + '.chunk')
                            expected = {'size': chunk['window'], 'sha1': chunk['sha1']}
                            if not matches(cached, expected):
                                split = urllib.parse.urlsplit(base)
                                url = urllib.parse.urlunsplit((split.scheme, split.netloc, split.path.rstrip('/') + '/' + chunk_path(chunk, manifest['version']), split.query, ''))
                                failure = None
                                for attempt in range(3):
                                    try:
                                        data = decode_chunk(cdn_bytes(url, chunk['size'] + 4096), chunk)
                                        b.atomic_bytes(cached, data)
                                        transferred += chunk['size']
                                        if time.monotonic() - last_progress > 1:
                                            update_job(job, completed_files=index, transferred_bytes=transferred)
                                            last_progress = time.monotonic()
                                        failure = None
                                        break
                                    except ValueError as error:
                                        failure = error
                                        time.sleep(0.3 * (attempt + 1))
                                if failure:
                                    raise failure
                            with cached.open('rb') as source:
                                source.seek(part['offset'])
                                data = source.read(part['size'])
                            if len(data) != part['size']:
                                raise ValueError('Cached chunk slice was truncated')
                            output.write(data)
                            h.update(data)
                    if temporary.stat().st_size != entry['size'] or h.hexdigest() != entry['sha1']:
                        raise ValueError('Reconstructed file failed SHA1 verification')
                    os.replace(temporary, target)
                finally:
                    temporary.unlink(missing_ok=True)
            if index % 10 == 0 or index + 1 == len(manifest['files']):
                update_job(job, completed_files=index + 1, transferred_bytes=transferred)
        receipt = {**stats, 'completed_at': b.now(), 'files': [{k: f[k] for k in ('path', 'size', 'sha1')} for f in manifest['files']],
                   'payload_root': str(payload_root), 'state': 'verified', 'reused_files': reused, 'vault_reused_files': vault_reused}
        b.save_json(receipt_path, receipt)
        return {**stats, 'state': 'verified', 'reused_files': reused, 'vault_reused_files': vault_reused, 'transferred_bytes': transferred}


def project_info(project, engine):
    project = Path(project).resolve(strict=True)
    if project.suffix.lower() != '.uproject' or not project.is_file():
        raise ValueError('Pass an existing .uproject file')
    descriptor = b.read_descriptor(project)
    if descriptor.get('EngineAssociation') not in (engine.removeprefix('UE_'),):
        raise ValueError('Project EngineAssociation must match the requested Launcher engine version')
    installs = b.read_json(b.PROGRAM_DATA / 'Epic/UnrealEngineLauncher/LauncherInstalled.dat', {})
    engines = [Path(e['InstallLocation']) for e in installs.get('InstallationList', []) if e.get('AppName') == engine]
    if not engines or not (engines[0] / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe').is_file():
        raise ValueError('Requested engine is not installed')
    return project, descriptor, engines[0]


def inspect_project(project):
    project = Path(project).resolve(strict=True)
    if project.suffix.lower() != '.uproject' or not project.is_file():
        raise ValueError('Pass an existing .uproject file')
    descriptor = b.read_descriptor(project)
    association = descriptor.get('EngineAssociation', '')
    engine = 'UE_' + association if isinstance(association, str) and re.fullmatch(r'\d+\.\d+', association) else None
    installed = b.read_json(b.PROGRAM_DATA / 'Epic/UnrealEngineLauncher/LauncherInstalled.dat', {})
    engines = [{'engine': e['AppName'], 'path': e['InstallLocation']} for e in installed.get('InstallationList', []) if e.get('AppName', '').startswith('UE_')]
    match = next((e for e in engines if e['engine'] == engine), None)
    ready = bool(match and (Path(match['path']) / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe').is_file())
    return {'project': str(project), 'engine_association': association, 'engine': engine,
            'matching_engine_installed': ready, 'engine_path': match['path'] if match else None,
            'installed_engines': engines, 'plugins': descriptor.get('Plugins', []),
            'content_directory': str(project.parent / 'Content'), 'plugins_directory': str(project.parent / 'Plugins'),
            'scan_root_registration_required': False,
            'next': 'Use this engine for download/import; save and close the target before mutations.' if ready else
                    'No matching Launcher engine. Custom/source-build associations require explicit support; do not rewrite EngineAssociation to bypass the check.'}


def ensure_project_closed(project):
    # Read process metadata only. No UI or desktop interaction.
    command = 'Get-CimInstance Win32_Process -Filter "name = \'UnrealEditor.exe\' OR name = \'UnrealEditor-Cmd.exe\'" | Select-Object -ExpandProperty CommandLine | ConvertTo-Json -Compress'
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command], capture_output=True, text=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise ValueError('Could not verify that the target project is closed')
    value = json.loads(result.stdout) if result.stdout.strip() else []
    lines = value if isinstance(value, list) else [value]
    if any(not isinstance(line, str) or project.stem.casefold() in line.casefold() for line in lines):
        raise ValueError('The target Unreal project is open or an editor process could not be identified. Save and close it before installing/verifying.')


def import_plan(id, project, engine='UE_5.8'):
    row, artifact = selected(id, engine)
    project, descriptor, engine_root = project_info(project, engine)
    stage = stage_path(artifact, engine)
    receipt = b.read_json(stage / 'receipt.json', {})
    if receipt.get('state') != 'verified' or receipt.get('id') != id or receipt.get('engine') != engine:
        raise ValueError('Download this exact asset/engine successfully before importing')
    payload = stage / 'payload'
    entries = receipt['files']
    plugin_name, prefix = None, None
    if row['kind'] == 'CODE_PLUGIN':
        descriptors = [e['path'] for e in entries if e['path'].endswith('.uplugin')]
        if len(descriptors) != 1:
            raise ValueError('Expected one plugin descriptor; multi-plugin packages require a dedicated adapter')
        plugin_file = descriptors[0]
        plugin_name = Path(plugin_file).stem
        relative_path(plugin_name)
        prefix = plugin_file.rsplit('/', 1)[0] + '/' if '/' in plugin_file else ''
    mappings, skipped, conflicts, identical = [], [], [], []
    for entry in entries:
        name = relative_path(entry['path'])
        if row['kind'] == 'CODE_PLUGIN':
            destination = 'Plugins/' + plugin_name + '/' + name[len(prefix):] if name.startswith(prefix) else None
        else:
            # Keep /Game package paths intact; never put the pack beneath a new arbitrary folder.
            parts = name.split('/')
            positions = [i for i, p in enumerate(parts) if p == 'Content']
            destination = '/'.join(parts[positions[0]:]) if len(positions) == 1 else None
        if destination is None:
            skipped.append(name)
            continue
        source, target = inside(payload, name), inside(project.parent, destination)
        if not matches(source, entry):
            raise ValueError('Staged file has changed or is missing; rerun download before import')
        mapped = {**entry, 'destination': destination, 'source': str(source)}
        if target.exists():
            if matches(target, entry):
                identical.append(mapped)
            else:
                conflicts.append(destination)
        else:
            mappings.append(mapped)
    all_mappings = mappings + identical
    if not all_mappings:
        raise ValueError('No supported content files in this package')
    if len({e['destination'].casefold() for e in all_mappings}) != len(all_mappings):
        raise ValueError('Multiple source files map to the same project path')
    missing_plugins = []
    if plugin_name:
        pd = b.read_descriptor(inside(payload, prefix + plugin_name + '.uplugin'))
        for dep in pd.get('Plugins', []):
            if dep.get('Enabled') and not dep.get('Optional'):
                name = dep['Name']
                if not list((engine_root / 'Engine/Plugins').rglob(name + '.uplugin')) and not list((project.parent / 'Plugins').rglob(name + '.uplugin')):
                    missing_plugins.append(name)
        if missing_plugins:
            raise ValueError('Required plugins are missing: ' + ', '.join(missing_plugins))
    return {'id': id, 'title': row['title'], 'kind': row['kind'], 'artifact_id': artifact, 'engine': engine,
            'project': str(project), 'plugin_name': plugin_name, 'files_to_copy': mappings, 'identical_files': identical,
            'conflicts': conflicts, 'skipped_files': skipped, 'installed_bytes': sum(e['size'] for e in mappings),
            'note': 'Config, source and project descriptors from asset packs are not merged. Review skipped_files for required project settings.'}


def plan_import(id, project, engine='UE_5.8'):
    plan = import_plan(id, project, engine)
    return {**{k: v for k, v in plan.items() if k not in ('files_to_copy', 'identical_files')},
            'files_to_copy': len(plan['files_to_copy']), 'identical_files': len(plan['identical_files']),
            'destinations_sample': [e['destination'] for e in plan['files_to_copy'][:20]]}


def install(id, project, engine, job):
    project = Path(project).resolve(strict=True)
    with lock('project:' + str(project).casefold()):
        ensure_project_closed(project)
        plan = import_plan(id, str(project), engine)
        if plan['conflicts']:
            raise ValueError('Import conflicts with existing project files: ' + ', '.join(plan['conflicts'][:8]))
        if shutil.disk_usage(project.parent).free < plan['installed_bytes'] + 256 * 1024 * 1024:
            raise ValueError('Insufficient project disk space')
        receipt_dir = project.parent / 'Saved/FabAgentBridge'
        receipt_dir.mkdir(parents=True, exist_ok=True)
        journal = receipt_dir / (job + '.json')
        original = project.read_bytes()
        backup = receipt_dir / (job + '.uproject.backup')
        b.atomic_bytes(backup, original)
        written = []
        project_updated = None
        try:
            b.save_json(journal, {'state': 'installing', 'plan': plan, 'written': written, 'backup': str(backup)})
            update_job(job, phase='importing', total_files=len(plan['files_to_copy']))
            for index, entry in enumerate(plan['files_to_copy']):
                target = inside(project.parent, entry['destination'])
                target.parent.mkdir(parents=True, exist_ok=True)
                temporary = inside(project.parent, entry['destination'] + '.fab-' + job + '.tmp')
                try:
                    with temporary.open('xb') as dest, Path(entry['source']).open('rb') as source:
                        shutil.copyfileobj(source, dest, 1024 * 1024)
                    if not matches(temporary, entry):
                        raise ValueError('Imported file failed hash verification')
                    # Atomic no-overwrite publish on the same volume. Complete bytes only.
                    os.link(temporary, target)
                    written.append(entry['destination'])
                finally:
                    temporary.unlink(missing_ok=True)
                if index % 10 == 0:
                    update_job(job, completed_files=index + 1)
                    b.save_json(journal, {'state': 'installing', 'plan': plan, 'written': written, 'backup': str(backup)})
            if plan['plugin_name']:
                if project.read_bytes() != original:
                    raise ValueError('Project descriptor changed during import')
                descriptor = b.read_descriptor(project)
                plugins = descriptor.setdefault('Plugins', [])
                entry = next((p for p in plugins if p.get('Name') == plan['plugin_name']), None)
                if entry is None:
                    plugins.append({'Name': plan['plugin_name'], 'Enabled': True})
                else:
                    entry['Enabled'] = True
                project_updated = json.dumps(descriptor, indent=2).encode()
                b.atomic_bytes(project, project_updated)
            result = {'state': 'installed_unverified', 'id': id, 'project': str(project), 'engine': engine,
                      'files_copied': len(written), 'identical_files': len(plan['identical_files']),
                      'plugin_enabled': plan['plugin_name'], 'skipped_files': plan['skipped_files'],
                      'files': [{k: e[k] for k in ('destination', 'size', 'sha1')} for e in plan['files_to_copy'] + plan['identical_files']],
                      'receipt': str(journal), 'backup': str(backup), 'created_at': b.now()}
            b.save_json(journal, result)
            b.save_json(receipt_dir / (key(id) + '.json'), result)
            update_job(job, completed_files=len(written))
            return {k: v for k, v in result.items() if k != 'files'}
        except Exception:
            # Only files created by this operation are candidates for rollback.
            for relative in reversed(written):
                target = inside(project.parent, relative)
                expected = next(e for e in plan['files_to_copy'] if e['destination'] == relative)
                if matches(target, expected):
                    target.unlink()
            if project_updated is not None and project.read_bytes() == project_updated:
                b.atomic_bytes(project, original)
            b.save_json(journal, {'state': 'failed_rollback_attempted', 'written': written, 'backup': str(backup)})
            raise


def verify(id, project, engine, job):
    project, _, engine_root = project_info(project, engine)
    with lock('project:' + str(project).casefold()):
        ensure_project_closed(project)
        receipt_path = project.parent / 'Saved/FabAgentBridge' / (key(id) + '.json')
        receipt = b.read_json(receipt_path, {})
        if receipt.get('id') != id or receipt.get('engine') != engine:
            raise ValueError('No bridge installation receipt for this asset and project')
        for entry in receipt['files']:
            if not matches(inside(project.parent, entry['destination']), entry):
                raise ValueError('Installed files changed; receipt integrity check failed')
        folder = b.STATE / 'jobs' / job
        folder.mkdir(parents=True, exist_ok=True)
        request_path, report = folder / 'verify-request.json', folder / 'verify-report.json'
        b.save_json(request_path, {'files': receipt['files'], 'plugin_name': receipt.get('plugin_enabled'), 'output': str(report)})
        script = folder / 'verify.py'
        source = Path(__file__).with_name('unreal_verify.py').read_text(encoding='utf-8')
        script.write_text('REQUEST_PATH = ' + repr(str(request_path)) + '\n' + source, encoding='utf-8')
        log = folder / 'unreal.log'
        command = [str(engine_root / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(project),
                   '-run=pythonscript', '-script=' + script.as_posix(), '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities',
                   '-unattended', '-nop4', '-NullRHI', '-nosplash', '-nosound', '-stdout', '-UTF8Output', '-abslog=' + str(log)]
        update_job(job, phase='unreal_verification', log=str(log))
        with (folder / 'stdout.log').open('wb') as stream:
            try:
                proc = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                      timeout=900, creationflags=subprocess.CREATE_NO_WINDOW)
            except subprocess.TimeoutExpired:
                raise ValueError('Unreal validation timed out; inspect the job log') from None
        result = b.read_json(report, {})
        if proc.returncode != 0 or not result.get('success'):
            raise ValueError('Unreal could not validate this installation. Inspect job log/report for missing modules, dependencies or load errors.')
        result.pop('registry_assets', None)
        result.update(id=id, project=str(project), log=str(log), report=str(report), verified_at=b.now())
        receipt['verification'] = result
        receipt['state'] = 'verified_in_unreal'
        b.save_json(receipt_path, receipt)
        return result


def project_assets(id, project, query='', limit=30, offset=0):
    if type(limit) is not int or not 1 <= limit <= 100 or type(offset) is not int or offset < 0 or not isinstance(query, str):
        raise ValueError('Invalid query or pagination')
    project = Path(project).resolve(strict=True)
    if project.suffix.lower() != '.uproject':
        raise ValueError('Pass an existing .uproject')
    receipt = b.read_json(project.parent / 'Saved/FabAgentBridge' / (key(id) + '.json'), {})
    if receipt.get('id') != id:
        raise ValueError('No installation receipt for this asset and project')
    verification = receipt.get('verification', {})
    report = b.read_json(verification['report'], {}) if verification.get('report') else {}
    types = {r['package']: r['class'] for r in report.get('registry_assets', report.get('assets_sample', []))}
    results = []
    for entry in receipt['files']:
        name = entry['destination']
        if not name.endswith(('.uasset', '.umap')) or any(w not in name.casefold() for w in query.casefold().split()):
            continue
        plugin = receipt.get('plugin_enabled')
        if name.startswith('Content/'):
            package = '/Game/' + name[len('Content/'):].rsplit('.', 1)[0]
        elif '/Content/' in name and plugin:
            package = '/' + plugin + '/' + name.split('/Content/', 1)[1].rsplit('.', 1)[0]
        else:
            continue
        path = inside(project.parent, name)
        results.append({'package_path': package, 'class': types.get(package), 'file': str(path),
                        'present': path.is_file(), 'bytes': entry['size']})
    return {'id': id, 'project': str(project), 'installation_state': receipt['state'],
            'verified_at': verification.get('verified_at'), 'total': len(results), 'items': results[offset:offset + limit],
            'next_offset': offset + limit if offset + limit < len(results) else None}


def start_job(action, arguments):
    job = uuid.uuid4().hex
    folder = b.STATE / 'jobs'
    folder.mkdir(parents=True, exist_ok=True)
    b.save_json(folder / (job + '.json'), {'job_id': job, 'action': action, 'arguments': arguments,
                                         'state': 'queued', 'created_at': b.now()})
    try:
        process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), 'worker', job],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
    except OSError:
        update_job(job, state='failed', error='Could not launch worker')
        raise ValueError('Could not launch worker') from None
    return {'job_id': job, 'state': 'queued', 'pid': process.pid, 'next': 'Poll fab_job_status with this job_id until succeeded or failed.'}


def download_asset(id, engine='UE_5.8', max_download_mb=1024):
    selected(id, engine)
    return start_job('download', {'id': id, 'engine': engine, 'max_download_mb': max_download_mb})


def import_asset(id, project, engine='UE_5.8'):
    project_info(project, engine)
    return start_job('import', {'id': id, 'project': project, 'engine': engine})


def verify_asset(id, project, engine='UE_5.8'):
    project_info(project, engine)
    return start_job('verify', {'id': id, 'project': project, 'engine': engine})


def job_status(job_id):
    if not re.fullmatch('[0-9a-f]{32}', job_id):
        raise ValueError('Invalid job ID')
    value = b.read_json(b.STATE / 'jobs' / (job_id + '.json'))
    if value is None:
        raise ValueError('Unknown job ID')
    if value.get('state') == 'running' and value.get('pid') and not process_alive(value['pid']):
        # A killed worker cannot leave a permanently "running" job.
        latest = b.read_json(b.STATE / 'jobs' / (job_id + '.json'))
        if latest.get('state') == 'running':
            update_job(job_id, state='failed', error='Worker exited before completing. Download can be retried; inspect any installation journal before retrying import.')
            return b.read_json(b.STATE / 'jobs' / (job_id + '.json'))
        return latest
    return value


def process_alive(pid):
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        return ctypes.get_last_error() == 5  # Access denied means unknown/alive, not dead.
    try:
        code = wintypes.DWORD()
        return not kernel.GetExitCodeProcess(handle, ctypes.byref(code)) or code.value == 259
    finally:
        kernel.CloseHandle(handle)


def worker(job):
    value = job_status(job)
    update_job(job, state='running', pid=os.getpid(), started_at=b.now())
    try:
        fn = {'download': download, 'import': install, 'verify': verify}[value['action']]
        result = fn(**value['arguments'], job=job)
        update_job(job, state='succeeded', result=result, finished_at=b.now())
    except Exception as error:
        # Do not persist exceptions containing URLs, headers, or tokens.
        message = str(error) if isinstance(error, ValueError) and 'https://' not in str(error) and 'Bearer' not in str(error) else type(error).__name__ + ': operation failed'
        update_job(job, state='failed', error=message, finished_at=b.now())


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'worker':
        worker(sys.argv[2])
