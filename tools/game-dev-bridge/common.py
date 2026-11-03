"""Shared paths, receipts and interprocess coordination (standard library only)."""
from contextlib import contextmanager
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(os.environ.get('GAME_DEV_HOME', str(Path.home() / '.codex/game-dev-bridge')))
FAB_ROOT = Path(os.environ.get('FAB_BRIDGE_HOME', str(Path.home() / '.codex/fab-agent-bridge')))
HERE = Path(__file__).resolve().parent
HIDDEN = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def key(value):
    return hashlib.sha256(str(value).encode()).hexdigest()[:32]


def read(path, default=None):
    path = Path(path)
    return json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else default


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, suffix='.tmp')
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as out:
            json.dump(value, out, indent=2, ensure_ascii=False)
        # Windows readers/antivirus can briefly hold a destination without delete sharing.
        for attempt in range(40):
            try:
                os.replace(tmp, path)
                break
            except PermissionError:
                if attempt == 39: raise
                time.sleep(.025)
    finally:
        Path(tmp).unlink(missing_ok=True)


def absolute(value, suffix=None):
    path = Path(value)
    if not path.is_absolute():
        raise ValueError('An absolute path is required')
    path = path.resolve(strict=True)
    if suffix and path.suffix.lower() not in suffix:
        raise ValueError('Unsupported file extension: ' + path.suffix)
    return path


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def fingerprint(paths, options):
    files = {}
    for name in paths:
        path = absolute(name)
        if path.is_dir():
            for base, dirs, names in os.walk(path, followlinks=False):
                dirs[:] = sorted(d for d in dirs if d not in {'Saved', 'Intermediate', '.git', '__pycache__'}
                                 and not (Path(base) / d).is_symlink())
                for n in sorted(names):
                    f = Path(base) / n
                    if not f.is_symlink():
                        files[str(f)] = sha(f)
        else:
            files[str(path)] = sha(path)
    return hashlib.sha256(json.dumps({'files': files, 'options': options}, sort_keys=True).encode()).hexdigest()


def alive(pid):
    if not pid:
        return False
    kernel = ctypes.windll.kernel32
    kernel.OpenProcess.restype = ctypes.c_void_p
    handle = kernel.OpenProcess(0x1000, False, int(pid))
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        return bool(kernel.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code))) and code.value == 259
    finally:
        kernel.CloseHandle(ctypes.c_void_p(handle))


@contextmanager
def lock(resource, timeout=0, fab=False):
    import msvcrt
    folder = (FAB_ROOT if fab else ROOT) / 'locks'
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / (key(resource) + '.lock')).open('a+b') as stream:
        if stream.tell() == 0:
            stream.write(b'0'); stream.flush()
        deadline = time.monotonic() + timeout
        while True:
            stream.seek(0)
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise ValueError('Another agent owns this resource; retry after its operation finishes.') from None
                time.sleep(.1)
        try:
            yield
        finally:
            stream.seek(0); msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def project_lock(project):
    # Same lock as Fab download/import bridge: no simultaneous on-disk project mutations.
    return lock('project:' + str(project).casefold(), fab=True)


def project_info(project, engine_path=None):
    project = absolute(project, {'.uproject'})
    descriptor = read(project)
    engine = Path(engine_path).resolve() if engine_path else None
    if not engine:
        installed = read(Path(os.environ.get('PROGRAMDATA', 'C:/ProgramData')) /
                         'Epic/UnrealEngineLauncher/LauncherInstalled.dat', {})
        expected = 'UE_' + descriptor.get('EngineAssociation', '')
        engine = next((Path(e['InstallLocation']) for e in installed.get('InstallationList', [])
                       if e.get('AppName') == expected), None)
    if not engine or not (engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe').exists():
        raise ValueError('Matching Unreal engine not found; provide engine_path for a custom/source build.')
    plugin = engine / 'Engine/Plugins/Experimental/ModelContextProtocol/ModelContextProtocol.uplugin'
    return {'project': str(project), 'engine_path': str(engine),
            'engine_association': descriptor.get('EngineAssociation'), 'native_mcp_available': plugin.exists(),
            'engine_version': read(engine / 'Engine/Build/Build.version', {}),
            'editor_cmd': str(engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')}


def blender_path():
    config = read(ROOT / 'config.json', {})
    candidates = [Path(config['blender'])] if config.get('blender') else list(
        Path('C:/Program Files/Blender Foundation').glob('Blender */blender.exe'))
    if not candidates:
        raise ValueError('Set the Blender executable in shared config.json')
    return str(sorted(candidates)[-1].resolve(strict=True))


def check_editor_closed(project):
    command = "Get-CimInstance Win32_Process -Filter \"Name LIKE 'UnrealEditor%'\" | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"
    proc = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command],
                          capture_output=True, text=True, creationflags=HIDDEN, timeout=20)
    if proc.returncode:
        raise ValueError('Could not check whether this project is already open')
    rows = json.loads(proc.stdout) if proc.stdout.strip() else []
    if isinstance(rows, dict): rows = [rows]
    for row in rows:
        cmd = (row.get('CommandLine') or '').replace('/', '\\').casefold()
        if str(project).replace('/', '\\').casefold() in cmd:
            raise ValueError('Project is already open in Unreal; close it before starting a managed session or offline test.')
