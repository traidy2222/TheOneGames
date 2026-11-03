"""Project-routed native Unreal MCP. Each call uses a fresh verified HTTP session."""
import json
import os
import re
from pathlib import Path
import socket
import subprocess
import sys
import time
import urllib.request
import uuid
import common as c

PLUGINS = ['ModelContextProtocol', 'PythonScriptPlugin', 'EditorScriptingUtilities',
           'EditorToolset', 'AutomationTestToolset']
OPTIONAL = {'PCGToolset', 'NiagaraToolsets', 'AnimationAssistantToolset', 'GASToolsets',
            'PhysicsToolsets', 'StateTreeToolset', 'AIModuleToolset', 'GameplayTagsToolset', 'IKRig', 'ControlRig'}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('Local MCP redirects are not allowed')


class NativeClient:
    def __init__(self, port, timeout=45):
        if type(port) is not int or not 1024 <= port <= 65535:
            raise ValueError('Invalid local port')
        self.url = f'http://127.0.0.1:{port}/mcp'
        self.session = None
        self.counter = 0
        self.timeout = timeout
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    def rpc(self, method, params=None, notification=False):
        self.counter += 1
        payload = {'jsonrpc': '2.0', 'method': method, 'params': params or {}}
        if not notification: payload['id'] = self.counter
        headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
                   'MCP-Protocol-Version': '2025-06-18'}
        if self.session: headers['Mcp-Session-Id'] = self.session
        req = urllib.request.Request(self.url, json.dumps(payload).encode(), headers)
        with self.opener.open(req, timeout=self.timeout) as response:
            self.session = response.headers.get('Mcp-Session-Id', self.session)
            data = response.read().decode()
        if notification or not data: return None
        if data.startswith('event:') or data.startswith('data:'):
            messages = [json.loads(line[5:].strip()) for line in data.splitlines() if line.startswith('data:')]
            value = next(v for v in messages if v.get('id') == self.counter)
        else: value = json.loads(data)
        if value.get('error'): raise ValueError('Unreal MCP: ' + json.dumps(value['error']))
        return value['result']

    def __enter__(self):
        self.rpc('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
                               'clientInfo': {'name': 'shared-game-dev', 'version': '1.0'}})
        self.rpc('notifications/initialized', notification=True)
        return self

    def __exit__(self, *args):
        if self.session:
            try:
                req = urllib.request.Request(self.url, method='DELETE', headers={'Mcp-Session-Id': self.session})
                with self.opener.open(req, timeout=5): pass
            except Exception: pass

    def tool(self, name, arguments=None):
        result = self.rpc('tools/call', {'name': name, 'arguments': arguments or {}})
        if result.get('isError'): raise ValueError('Unreal tool failed: ' + json.dumps(result))
        return result

    def invoke(self, toolset, tool, arguments=None):
        return self.tool('call_tool', {'toolset_name': toolset, 'tool_name': tool, 'arguments': arguments or {}})


def decoded(result):
    value = result.get('structuredContent')
    if value is None:
        texts = [x['text'] for x in result.get('content', []) if x.get('type') == 'text']
        value = json.loads(texts[0]) if texts else None
    for _ in range(4):
        if isinstance(value, dict) and set(value) in ({'result'}, {'returnValue'}): value = next(iter(value.values()))
        elif isinstance(value, str):
            try: value = json.loads(value)
            except ValueError: break
        else: break
    return value


def session_file(project):
    return c.ROOT / 'sessions' / (c.key(str(project).casefold()) + '.json')


def session_status(project):
    project = c.absolute(project, {'.uproject'})
    row = c.read(session_file(project), {'project': str(project), 'state': 'stopped'})
    if row.get('state') in ('starting', 'ready', 'stopping') and not c.alive(row.get('supervisor_pid')):
        row['state'] = 'interrupted'
    return row


def verified(client, row):
    listing = client.tool('list_toolsets')
    text = '\n'.join(block.get('text', '') for block in listing.get('content', []))
    names = re.findall(r'^- ([^:\n]*SharedGameDevTools[^:\n]*):', text, re.M)
    if len(names) != 1: raise ValueError('Shared project identity tool is not ready')
    client.shared_toolset = names[0]
    identity = decoded(client.invoke(client.shared_toolset, 'identity'))
    if not isinstance(identity, dict) or identity.get('nonce') != row['nonce'] or \
            Path(identity.get('project', '')).resolve() != Path(row['project']):
        raise ValueError('Unreal endpoint identity does not match the requested project/session')


def start(project, engine_path=None, extra_plugins=None):
    info = c.project_info(project, engine_path)
    if not info['native_mcp_available']: raise ValueError('This engine does not include native Unreal MCP')
    extra_plugins = extra_plugins or []
    if set(extra_plugins) - OPTIONAL: raise ValueError('Unsupported optional toolset plugin')
    project = Path(info['project'])
    with c.lock('session-start:' + str(project).casefold()):
        old = session_status(str(project))
        if old['state'] in ('starting', 'ready'):
            return old
        with c.project_lock(project):
            c.check_editor_closed(project)
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
            row = {**info, 'state': 'starting', 'port': port, 'nonce': uuid.uuid4().hex,
                   'plugins': PLUGINS + extra_plugins, 'created_at': time.time()}
            c.write(session_file(project), row)
        process = subprocess.Popen([sys.executable, str(c.HERE / 'unreal_link.py'), 'host', str(project)],
                                   cwd=c.HERE, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, creationflags=c.HIDDEN,
                                   env={**os.environ, 'GAME_DEV_HOME': str(c.ROOT), 'FAB_BRIDGE_HOME': str(c.FAB_ROOT)})
        row['supervisor_pid'] = process.pid; c.write(session_file(project), row)
        return {'project': str(project), 'state': 'starting', 'supervisor_pid': process.pid,
                'next': 'Call unreal_session_status until ready or failed; this does not run tests.'}


def host(project):
    project = Path(project)
    path = session_file(project)
    row = c.read(path)
    deadline = time.monotonic() + 10
    while row.get('supervisor_pid') != os.getpid():
        if time.monotonic() >= deadline: return
        time.sleep(.05); row = c.read(path)
    row['supervisor_pid'] = os.getpid(); c.write(path, row)
    process = None
    try:
        with c.project_lock(project):
            c.check_editor_closed(project)
            folder = c.ROOT / 'sessions' / row['nonce']; folder.mkdir(parents=True, exist_ok=True)
            log = folder / 'editor.log'
            args = [row['editor_cmd'], str(project), '-unattended', '-nop4', '-nosplash', '-NullRHI', '-nosound', '-RenderOffscreen',
                    '-EnablePlugins=' + ','.join(row['plugins']), '-ModelContextProtocolStartServer',
                    '-ModelContextProtocolPort=' + str(row['port']),
                    '-ExecutePythonScript=' + (c.HERE / 'unreal_bootstrap.py').as_posix(), '-abslog=' + str(log)]
            with (folder / 'process.log').open('wb') as output:
                process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                                           creationflags=c.HIDDEN, env={**os.environ, 'GAME_DEV_SESSION_NONCE': row['nonce']})
                row.update(pid=process.pid, log=str(log)); c.write(path, row)
                deadline = time.monotonic() + 180
                last_error = ''
                while process.poll() is None and time.monotonic() < deadline:
                    try:
                        with NativeClient(row['port'], timeout=3) as client: verified(client, row)
                        break
                    except Exception as error:
                        last_error = str(error); time.sleep(1)
                else:
                    raise ValueError('Unreal MCP startup failed: ' + last_error + '; inspect ' + str(log))
                row['state'] = 'ready'; c.write(path, row)
                process.wait()
                row.update(state='stopped', exit_code=process.returncode); c.write(path, row)
    except Exception as error:
        if process and process.poll() is None:
            process.terminate()
            try: process.wait(timeout=15)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        row.update(state='failed', error=str(error)); c.write(path, row)


def call(project, action, toolset=None, tool=None, arguments=None, code=None):
    row = session_status(project)
    if row['state'] != 'ready': raise ValueError('Start the requested project session and wait until ready')
    with c.lock('rpc:' + row['project'].casefold(), timeout=45):
        with NativeClient(row['port']) as client:
            verified(client, row)
            if action == 'list': return client.tool('list_toolsets')
            if action == 'describe': return client.tool('describe_toolset', {'toolset_name': toolset})
            if action == 'python':
                result = decoded(client.invoke(client.shared_toolset, 'execute_python', {'code': code}))
                if not result.get('ok'): raise ValueError(result.get('error', 'Unreal Python failed'))
                return result
            if action == 'stop': return decoded(client.invoke(client.shared_toolset, 'shutdown'))
            # Block desktop/UI and nested dispatch routes. Native asset/scene API tools remain available.
            if not toolset or not tool or any(x in toolset.casefold() for x in ['slate', 'mcpclient']) or \
                    tool.casefold() in {'captureeditorimage', 'openeditorforasset'}:
                raise ValueError('Desktop/UI automation is not part of this code-only bridge')
            return client.invoke(toolset, tool, arguments)


if __name__ == '__main__':
    host(sys.argv[2])
