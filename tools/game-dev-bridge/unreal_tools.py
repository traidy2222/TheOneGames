"""Loaded in a hidden Unreal editor; registers code-only tools on Epic's MCP."""
import contextlib
import io
import json
import os
from pathlib import Path
import traceback
import unreal
from toolset_registry import tool_call


@unreal.uclass()
class SharedGameDevTools(unreal.ToolsetDefinition):
    """Project identity and trusted Unreal Python execution for the shared bridge."""

    @tool_call
    @staticmethod
    def identity() -> str:
        return json.dumps({'project': str(Path(unreal.Paths.get_project_file_path()).resolve()),
                           'nonce': os.environ['GAME_DEV_SESSION_NONCE'], 'pid': os.getpid()})

    @tool_call
    @staticmethod
    def execute_python(code: str) -> str:
        """Run trusted task-scoped Python with Unreal APIs. Assign JSON-serializable result."""
        namespace = {'unreal': unreal, '__name__': '__agent_script__'}
        output = io.StringIO()
        try:
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                exec(compile(code, '<shared-game-dev>', 'exec'), namespace)
            return json.dumps({'ok': True, 'result': namespace.get('result'), 'stdout': output.getvalue()[-16000:]})
        except Exception:
            return json.dumps({'ok': False, 'error': traceback.format_exc(), 'stdout': output.getvalue()[-16000:]})

    @tool_call
    @staticmethod
    def shutdown() -> str:
        """Close this bridge-owned editor. Save intended changes explicitly first."""
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        return json.dumps({'stopping': True})


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.ToolsetRegistry.register_toolset_class(SharedGameDevTools)
unreal.SystemLibrary.execute_console_command(None, 'ModelContextProtocol.RefreshTools')
print('GAME_DEV_BOOTSTRAP_READY')
