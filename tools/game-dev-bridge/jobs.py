"""On-demand jobs. Validation/test caches use content hashes, never a timer or watcher."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
import common as c


def signature(kind, arguments):
    inputs = arguments.get('inputs', [])[:]
    if arguments.get('source'): inputs.append(arguments['source'])
    if arguments.get('script'): inputs.append(arguments['script'])
    if arguments.get('spec_file'): inputs.append(arguments['spec_file'])
    if kind.startswith('unreal'):
        project = Path(arguments['project'])
        inputs.append(str(project))
        # Explicit dependency scopes are available for large projects. Defaults include project inputs.
        if not arguments.get('inputs'):
            inputs += [str(project.parent / n) for n in ['Config', 'Source', 'Content', 'Plugins'] if (project.parent / n).exists()]
        inputs.append(str(Path(arguments['engine_path']) / 'Engine/Build/Build.version'))
    tool = Path(c.blender_path()) if kind in ('blender', 'validate', 'character') else Path(arguments['editor_cmd'])
    options = {**arguments, 'kind': kind, 'tool': str(tool), 'tool_size': tool.stat().st_size,
               'tool_mtime': tool.stat().st_mtime_ns, 'runner': c.sha(c.HERE / 'blender_runner.py'),
               'jobs': c.sha(Path(__file__))}
    if kind == 'character':
        options['character_code'] = [c.sha(c.HERE / n) for n in ('character_blender.py', 'character_specs.py')]
    return c.fingerprint(inputs, options)


def cached(cache_key, arguments):
    entry = c.read(c.ROOT / 'cache' / (cache_key + '.json'))
    if not entry: return None
    job = c.read(c.ROOT / 'jobs' / entry['job_id'] / 'job.json')
    if not job or job['state'] != 'succeeded': return None
    try:
        if c.sha(job['report']) != entry['report_sha256']: return None
        if c.fingerprint(entry['dependencies'], {}) != entry['dependency_hash']: return None
    except (OSError, ValueError): return None
    return {**job, 'cached': True, 'reuse_reason': 'Same declared input contents, tool version and validation/test settings.'}


def submit(kind, arguments, force=False, reason=None):
    if force and not reason: raise ValueError('A forced rerun needs a concrete reason')
    args = dict(arguments)
    timeout = args.setdefault('timeout_seconds', 600)
    if type(timeout) is not int or not 10 <= timeout <= 7200: raise ValueError('Timeout must be 10..7200 seconds')
    for name in ('script', 'source', 'spec_file'):
        if args.get(name): args[name] = str(c.absolute(args[name]))
    args['inputs'] = [str(c.absolute(p)) for p in args.get('inputs', [])]
    if kind == 'blender' and not args.get('script'): raise ValueError('A modelling script is required')
    if kind == 'character':
        from character_specs import validate_spec
        c.absolute(args['source'], {'.blend', '.fbx', '.glb'})
        spec = c.read(args['spec_file']) if args.get('spec_file') else {}
        validate_spec(args['operation'], spec)
    if kind == 'validate':
        if not args.get('source'): raise ValueError('A source asset is required')
        c.absolute(args['source'], {'.blend', '.fbx', '.glb', '.gltf', '.obj'})
        rules = args.setdefault('rules', {})
        if set(rules) - {'require_uv', 'require_material', 'require_manifold', 'max_triangles'}:
            raise ValueError('Unknown validation rule')
        for name, value in rules.items():
            if name == 'max_triangles':
                if type(value) is not int or value < 1: raise ValueError('max_triangles must be positive')
            elif type(value) is not bool: raise ValueError('Validation switches must be booleans')
    if kind.startswith('unreal'):
        args.update(c.project_info(args['project'], args.get('engine_path')))
        if any(not re.fullmatch('[A-Za-z0-9_]+', p) for p in args.get('extra_plugins', [])):
            raise ValueError('Invalid extra plugin name')
        if kind == 'unreal_tests' and not re.fullmatch(r'[A-Za-z0-9_. :/-]+', args.get('test_filter', '')):
            raise ValueError('An explicit automation test name/prefix is required; console command separators are forbidden')
        if kind == 'unreal_python_test' and not args.get('script'): raise ValueError('A test script is required')
    should_cache = kind != 'blender' and (kind != 'character' or args['operation'] in ('inspect', 'validate'))
    cache_key = signature(kind, args) if should_cache else None
    if cache_key and not force:
        previous = cached(cache_key, args)
        if previous: return previous
    with c.lock('submit:' + (cache_key or uuid.uuid4().hex)):
        # Reuse an already running identical check instead of launching duplicate engines.
        if cache_key:
            for path in (c.ROOT / 'jobs').glob('*/job.json'):
                job = c.read(path)
                if job.get('cache_key') == cache_key and job['state'] in ('queued', 'running') and c.alive(job.get('pid')):
                    return {**job, 'already_running': True}
        job_id = uuid.uuid4().hex
        folder = c.ROOT / 'jobs' / job_id; folder.mkdir(parents=True)
        job = {'job_id': job_id, 'kind': kind, 'arguments': args, 'state': 'queued',
               'cache_key': cache_key, 'cached': False, 'reason': reason, 'created_at': time.time(),
               'log': str(folder / 'process.log'), 'report': str(folder / 'report.json'),
               'output': str(folder / 'output')}
        c.write(folder / 'job.json', job)
        with (folder / 'worker.log').open('wb') as worker_log:
            process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), job_id], cwd=c.HERE,
                                       stdin=subprocess.DEVNULL, stdout=worker_log, stderr=subprocess.STDOUT,
                                       creationflags=c.HIDDEN,
                                       env={**os.environ, 'GAME_DEV_HOME': str(c.ROOT), 'FAB_BRIDGE_HOME': str(c.FAB_ROOT)})
        job['pid'] = process.pid; c.write(folder / 'job.json', job)
        return {'job_id': job_id, 'state': 'queued', 'pid': process.pid,
                'next': 'Poll dev_job_status; this only reads progress and never reruns the work.'}


def run_process(args, job):
    with Path(job['log']).open('wb') as log:
        proc = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                creationflags=c.HIDDEN)
        try: code = proc.wait(timeout=job['arguments']['timeout_seconds'])
        except subprocess.TimeoutExpired:
            proc.terminate()
            try: proc.wait(timeout=10)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()
            raise ValueError('Job exceeded its timeout; inspect the saved log') from None
    if code: raise ValueError(f'Process exited with code {code}; inspect the saved log')


def automation_summary(report):
    tests = report.get('tests', [])
    completed = sum(str(t.get('state', '')).casefold() in ('success', 'successwithwarnings') for t in tests)
    failures = sum(str(t.get('state', '')).casefold() not in ('success', 'successwithwarnings') for t in tests)
    # Unreal's export includes explicit counters; a green exit code and zero matching tests is not a pass.
    count = int(report.get('succeeded', completed)) + int(report.get('succeededWithWarnings', 0))
    failed = int(report.get('failed', failures))
    incomplete = int(report.get('notRun', 0)) + int(report.get('inProcess', 0))
    return {'ok': count > 0 and failed == 0 and incomplete == 0, 'passed': count,
            'failed': failed, 'incomplete': incomplete, 'tests': tests}


def execute(job):
    args = job['arguments']; folder = Path(job['report']).parent
    if job['kind'] in ('blender', 'validate', 'character'):
        request = {**args, 'kind': job['kind'], 'output': job['output'], 'report': job['report']}
        c.write(folder / 'request.json', request)
        run_process([c.blender_path(), '--background', '--factory-startup', '--disable-autoexec',
                     '--python-exit-code', '1', '--python', str(c.HERE / 'blender_runner.py'),
                     '--', str(folder / 'request.json')], job)
    else:
        project = Path(args['project'])
        with c.project_lock(project):
            c.check_editor_closed(project)
            cmd = [args['editor_cmd'], str(project), '-unattended', '-nop4', '-nosplash', '-NullRHI', '-nosound', '-RenderOffscreen',
                   '-abslog=' + str(folder / 'unreal.log')]
            plugins = args.get('extra_plugins', []) + (['PythonScriptPlugin', 'EditorScriptingUtilities'] if job['kind'] == 'unreal_python_test' else [])
            if plugins: cmd.append('-EnablePlugins=' + ','.join(sorted(set(plugins))))
            if job['kind'] == 'unreal_python_test':
                wrapper = folder / 'test_runner.py'
                wrapper.write_text('import runpy, json, traceback\nfrom pathlib import Path\n'
                                   'try:\n'
                                   f'    scope = runpy.run_path({args["script"]!r}, run_name="__main__")\n'
                                   '    result = {"ok": True, "result": scope.get("result")}\n'
                                   'except BaseException:\n'
                                   '    result = {"ok": False, "error": traceback.format_exc()}\n'
                                   f'Path({job["report"]!r}).write_text(json.dumps(result, indent=2), encoding="utf-8")\n', encoding='utf-8')
                cmd += ['-run=pythonscript', '-script=' + wrapper.as_posix()]
            else:
                export = folder / 'automation'
                cmd += ['-ExecCmds=Automation RunTests ' + args['test_filter'],
                        '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(export)]
            run_process(cmd, job)
            if job['kind'] == 'unreal_tests':
                raw = c.read(export / 'index.json')
                if raw is None: raise ValueError('Unreal did not produce an automation report')
                c.write(job['report'], automation_summary(raw))
    result = c.read(job['report'])
    if result is None: raise ValueError('Process produced no result report')
    job['result'] = result
    if not result.get('ok'): raise ValueError('Validation/test failed; inspect result and report')
    if Path(job['output']).exists():
        job['artifacts'] = [str(p) for p in Path(job['output']).rglob('*') if p.is_file()]
    return result


def worker(job_id):
    folder = c.ROOT / 'jobs' / job_id; path = folder / 'job.json'
    job = c.read(path)
    deadline = time.monotonic() + 10
    while job.get('pid') != os.getpid():
        if time.monotonic() >= deadline: return
        time.sleep(.05); job = c.read(path)
    job.update(state='running', started_at=time.time()); c.write(path, job)
    try:
        result = execute(job)
        job['state'] = 'succeeded'
        if job['cache_key'] and result.get('cacheable', True):
            dependencies = result.get('dependencies', [])
            if signature(job['kind'], job['arguments']) == job['cache_key']:
                c.write(c.ROOT / 'cache' / (job['cache_key'] + '.json'), {
                    'job_id': job_id, 'report_sha256': c.sha(job['report']), 'dependencies': dependencies,
                    'dependency_hash': c.fingerprint(dependencies, {})})
            else: job['cache_note'] = 'Inputs changed during execution; result will not be reused.'
    except Exception as error:
        job.update(state='failed', error=str(error))
        if Path(job['report']).exists(): job['result'] = c.read(job['report'])
    job['finished_at'] = time.time(); c.write(path, job)


def status(job_id):
    if not re.fullmatch('[0-9a-f]{32}', job_id): raise ValueError('Invalid job ID')
    job = c.read(c.ROOT / 'jobs' / job_id / 'job.json')
    if not job: raise ValueError('Unknown job')
    if job['state'] in ('queued', 'running') and job.get('pid') and not c.alive(job.get('pid')):
        job = c.read(c.ROOT / 'jobs' / job_id / 'job.json')
        if job['state'] in ('queued', 'running'):
            job.update(state='interrupted', error='Worker exited without a completion receipt; inspect report.json and worker.log')
    return job


if __name__ == '__main__': worker(sys.argv[1])
