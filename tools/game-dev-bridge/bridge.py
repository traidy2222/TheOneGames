"""Shared Unreal MCP, Blender pipeline and demand-driven validation/test tools."""
import argparse
import inspect
import json
import re
from pathlib import Path
import sys
import common as c
import jobs
import unreal_link as ue


def status():
    return {'runtime': str(c.HERE), 'state_directory': str(c.ROOT), 'blender': c.blender_path(),
            'testing_policy': 'On demand only. Reuse successful unchanged checks. No watcher, scheduler or startup test.',
            'sessions': [{k: v for k, v in c.read(p).items() if k != 'nonce'} for p in (c.ROOT / 'sessions').glob('*.json')]}


def unreal_project(project, engine_path=None): return c.project_info(project, engine_path)
def unreal_start(project, engine_path=None, extra_plugins=None): return ue.start(project, engine_path, extra_plugins)
def unreal_session_status(project): return {k: v for k, v in ue.session_status(project).items() if k != 'nonce'}
def unreal_stop(project): return ue.call(project, 'stop')
def unreal_toolsets(project): return ue.call(project, 'list')
def unreal_describe(project, toolset): return ue.call(project, 'describe', toolset=toolset)
def unreal_call(project, toolset, tool, arguments=None): return ue.call(project, 'call', toolset, tool, arguments)
def unreal_python(project, code): return ue.call(project, 'python', code=code)


def unreal_import_fbx(project, source, destination):
    """Import into a new dedicated /Game folder; refuse existing assets."""
    source = str(c.absolute(source, {'.fbx'}))
    if not re.fullmatch(r'/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+', destination):
        raise ValueError('Use a dedicated /Game/Folder with alphanumeric/underscore segments')
    request = {'source': source, 'destination': destination}
    code = ('import json\n'
            f'request = json.loads({json.dumps(request)!r})\n'
            'library = unreal.EditorAssetLibrary\n'
            'existing = library.list_assets(request["destination"], recursive=True, include_folder=False)\n'
            'assert not existing, "Destination already contains assets; choose a new folder or explicitly edit existing assets"\n'
            'task = unreal.AssetImportTask()\n'
            'task.filename = request["source"]\n'
            'task.destination_path = request["destination"]\n'
            'task.automated = True\n'
            'task.replace_existing = False\n'
            'task.save = True\n'
            'options = unreal.FbxImportUI()\n'
            'options.import_mesh = True\n'
            'options.import_as_skeletal = False\n'
            'options.automated_import_should_detect_type = False\n'
            'options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH\n'
            'task.options = options\n'
            'unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])\n'
            'paths = list(task.imported_object_paths)\n'
            'assert paths, "Importer returned no asset paths"\n'
            'assets = [library.load_asset(path) for path in paths]\n'
            'assert all(assets), "Imported asset failed to load"\n'
            'result = {"source": request["source"], "assets": [{"path": a.get_path_name(), "class": a.get_class().get_name()} for a in assets]}\n')
    return ue.call(project, 'python', code=code)


def blender_run(script, source=None, parameters=None, inputs=None, timeout_seconds=600):
    return jobs.submit('blender', dict(script=script, source=source, parameters=parameters or {}, inputs=inputs or [], timeout_seconds=timeout_seconds))


def asset_validate(source, rules=None, inputs=None, force=False, reason=None, timeout_seconds=600):
    return jobs.submit('validate', dict(source=source, rules=rules or {}, inputs=inputs or [], timeout_seconds=timeout_seconds), force, reason)


def unreal_test(project, test_filter=None, script=None, inputs=None, engine_path=None, extra_plugins=None, force=False, reason=None, timeout_seconds=600):
    if bool(test_filter) == bool(script): raise ValueError('Choose one explicit automation filter or Python assertion script')
    return jobs.submit('unreal_python_test' if script else 'unreal_tests',
                       dict(project=project, test_filter=test_filter, script=script, inputs=inputs or [],
                            engine_path=engine_path, extra_plugins=extra_plugins or [], timeout_seconds=timeout_seconds), force, reason)


def dev_job_status(job_id): return jobs.status(job_id)


def character_job(source, operation, spec_file=None, inputs=None, force=False, reason=None, timeout_seconds=600):
    return jobs.submit('character', dict(source=source, operation=operation, spec_file=spec_file,
                        inputs=inputs or [], timeout_seconds=timeout_seconds), force, reason)


def character_inspect(source, spec_file=None, inputs=None):
    return character_job(source, 'inspect', spec_file, inputs)


def character_rig(source, spec_file, timeout_seconds=600):
    return character_job(source, 'rig', spec_file, timeout_seconds=timeout_seconds)


def character_animate(source, spec_file, timeout_seconds=600):
    return character_job(source, 'animate', spec_file, timeout_seconds=timeout_seconds)


def character_validate(source, spec_file=None, inputs=None, force=False, reason=None, timeout_seconds=600):
    return character_job(source, 'validate', spec_file, inputs, force, reason, timeout_seconds)


def character_export(source, spec_file, timeout_seconds=600):
    return character_job(source, 'export', spec_file, timeout_seconds=timeout_seconds)


def character_engine_call(project, request):
    if not re.fullmatch(r'/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+', request['destination']):
        raise ValueError('Use a dedicated /Game folder with simple names')
    code = (c.HERE / 'character_unreal.py').read_text(encoding='utf-8')
    code += '\nimport json\nresult = run(json.loads(' + repr(json.dumps(request)) + '))\n'
    return ue.call(project, 'python', code=code)


def unreal_import_character(project, source, destination, skeleton=None, animation_only=False, enable_root_motion=False):
    source = str(c.absolute(source, {'.fbx'}))
    if animation_only and not skeleton: raise ValueError('Animation-only import requires a target skeleton')
    return character_engine_call(project, dict(operation='import', source=source, destination=destination,
                                  skeleton=skeleton, animation_only=animation_only, enable_root_motion=enable_root_motion))


def unreal_retarget_animations(project, animations, source_mesh, target_mesh, retargeter, destination):
    if not animations: raise ValueError('Select explicit source animation paths')
    return character_engine_call(project, dict(operation='retarget', animations=animations, source_mesh=source_mesh,
                                  target_mesh=target_mesh, retargeter=retargeter, destination=destination))


DESCRIPTIONS = {
    'dev_status': 'Inspect shared runtime and sessions. Never starts an engine or test.',
    'unreal_project': 'Inspect an explicit Unreal project and resolve its matching engine; no registration required.',
    'unreal_start': 'Start or reuse a hidden, code-only native MCP session for a closed Unreal project. Poll session status.',
    'unreal_session_status': 'Read the project session state. Does not launch anything or test.',
    'unreal_stop': 'Close the bridge-owned hidden editor. Save intended asset/level changes before stopping.',
    'unreal_toolsets': 'Discover native engine toolsets for the explicit project.',
    'unreal_describe': 'Read exact native tool schemas before calling them.',
    'unreal_call': 'Invoke a discovered native Unreal asset/scene tool. Calls are serialized per project. No desktop automation.',
    'unreal_python': 'Run trusted task-scoped Unreal Python in the explicit project. Assign result for structured output; save changes explicitly.',
    'unreal_import_fbx': 'Import a static-mesh FBX into a new dedicated /Game folder, save and load-check it. Refuses existing destination assets; skeletal imports need explicit Unreal Python options.',
    'blender_run': 'Run a modelling/export Python file in isolated headless Blender. OUTPUT_DIR, INPUT_FILE, PARAMETERS are injected. Returns job ID.',
    'asset_validate': 'On-demand structural asset checks. Reuses a successful identical check; changed files/settings rerun. Returns job ID or cached report.',
    'unreal_test': 'On-demand targeted automation or Python assertion tests. Reuses unchanged passes. Requires project closed, explicit test selection; never continuously tests.',
    'dev_job_status': 'Read job progress/results; polling does not rerun a job.',
    'character_inspect': 'Inspect a specific Blender/FBX/GLB character: skeleton, binding problems and available actions. Returns a job/cached report.',
    'character_rig': 'Build an explicit single-root skeleton and bind meshes using a JSON bone/weight spec. Saves a new blend; never replaces the input rig.',
    'character_animate': 'Author named bone-transform clips from a JSON spec in an existing rig. Saves a new blend; existing actions are not overwritten.',
    'character_validate': 'On-demand skin weights, root hierarchy and sampled finite deformation checks. Reuses unchanged passes; no visual-quality guarantee.',
    'character_export': 'Export a neutral skeletal FBX plus separately selected baked animation FBXs and a manifest. Uses explicit action names and preserves root motion.',
    'unreal_import_character': 'Import a skeletal FBX or animation-only FBX to a fresh folder, verify Skeleton binding and duration. Animation-only requires a Skeleton path.',
    'unreal_retarget_animations': 'Duplicate selected clips using an existing configured Unreal IK Retargeter and explicit source/target meshes. Does not infer chain mappings.',
}
FUNCTIONS = {name: globals()[name] for name in DESCRIPTIONS if name != 'dev_status'}
FUNCTIONS['dev_status'] = status
OBJECT_ARGS = {'parameters', 'rules', 'arguments'}
ARRAY_ARGS = {'inputs', 'extra_plugins', 'animations'}
BOOL_ARGS = {'force', 'animation_only', 'enable_root_motion'}


def schemas():
    result = []
    for name, function in FUNCTIONS.items():
        props, required = {}, []
        for arg, param in inspect.signature(function).parameters.items():
            kind = 'object' if arg in OBJECT_ARGS else 'array' if arg in ARRAY_ARGS else 'boolean' if arg in BOOL_ARGS else 'integer' if arg == 'timeout_seconds' else 'string'
            props[arg] = {'type': kind}
            if kind == 'array': props[arg]['items'] = {'type': 'string'}
            if param.default is inspect.Parameter.empty: required.append(arg)
        result.append({'name': name, 'description': DESCRIPTIONS[name],
                       'inputSchema': {'type': 'object', 'properties': props, 'required': required, 'additionalProperties': False}})
    return result


def dispatch(name, arguments):
    if name not in FUNCTIONS: raise ValueError('Unknown tool')
    if not isinstance(arguments, dict): raise ValueError('Arguments must be an object')
    schema = next(s['inputSchema'] for s in schemas() if s['name'] == name)
    if set(arguments) - set(schema['properties']): raise ValueError('Unknown argument')
    if set(schema['required']) - set(arguments): raise ValueError('Missing required argument')
    types = {'string': str, 'object': dict, 'array': list, 'boolean': bool, 'integer': int}
    for k, v in arguments.items():
        t = schema['properties'][k]['type']
        if type(v) is not types[t]: raise ValueError('Wrong argument type: ' + k)
        if t == 'array' and not all(isinstance(x, str) for x in v): raise ValueError('Expected string array')
    return FUNCTIONS[name](**arguments)


def rpc(message):
    if not isinstance(message, dict) or message.get('jsonrpc') != '2.0' or not isinstance(message.get('method'), str):
        return {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32600, 'message': 'Invalid request'}}
    if 'id' not in message: return None
    result = {'jsonrpc': '2.0', 'id': message['id']}
    method = message['method']
    if method == 'initialize':
        result['result'] = {'protocolVersion': '2025-06-18', 'capabilities': {'tools': {}},
                            'serverInfo': {'name': 'game-dev', 'version': '1.1.0'},
                            'instructions': 'Code-only, explicit project/asset paths. Discover native schemas before use. Poll jobs until complete. Tests and validation run only when needed; reuse unchanged successful checks. No watchers or scheduled testing. Save before closing owned Unreal sessions. Use fab-assets for owned Fab downloads.'}
    elif method == 'ping': result['result'] = {}
    elif method == 'tools/list': result['result'] = {'tools': schemas()}
    elif method == 'tools/call':
        try:
            params = message.get('params', {})
            data = dispatch(params.get('name'), params.get('arguments', {}))
            result['result'] = {'content': [{'type': 'text', 'text': json.dumps(data, ensure_ascii=False)}]}
        except Exception as error:
            result['result'] = {'isError': True, 'content': [{'type': 'text', 'text': str(error)}]}
    else: result['error'] = {'code': -32601, 'message': 'Unknown method'}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['serve', 'status', 'tools', 'call'])
    parser.add_argument('tool', nargs='?')
    parser.add_argument('--args', default='{}', help='JSON arguments (prefer --args-file for paths/code)')
    parser.add_argument('--args-file')
    args = parser.parse_args()
    if args.command == 'serve':
        for line in sys.stdin:
            try: value = rpc(json.loads(line))
            except Exception: value = {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32700, 'message': 'Invalid JSON'}}
            if value is not None: print(json.dumps(value, ensure_ascii=False), flush=True)
    else:
        try:
            value = status() if args.command == 'status' else schemas() if args.command == 'tools' else dispatch(
                args.tool, c.read(args.args_file) if args.args_file else json.loads(args.args))
            print(json.dumps(value, indent=2, ensure_ascii=False))
        except Exception as error:
            print(json.dumps({'error': str(error)}), file=sys.stderr); return 1
    return 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8'); sys.stderr.reconfigure(encoding='utf-8')
    sys.exit(main())
