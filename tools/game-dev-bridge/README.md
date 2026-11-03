# Shared game development bridge

User-level stdio MCP and equivalent CLI for Unreal's native MCP, isolated Blender jobs,
asset validation and targeted Unreal tests. Python 3.11 standard library, Blender and Unreal;
no downloaded dependencies, desktop/browser automation, watchers or scheduled tests.

## Shared installation

Run `python install_global.py` from this source directory. Immutable releases are stored in
`C:/Users/traid/.codex/game-dev-bridge/app/releases`; `app/bridge.py` is the stable launcher.
`app/mcp-config.json` is usable by other local MCP clients. The user-level Codex server is
`game-dev`; skill `unreal-blender-pipeline` explains task-scoped use. Existing chats can use
the CLI immediately and reconnect for tools. Other machines/accounts need their own setup.
No project-local registration is necessary. Native MCP requires an engine that includes it
(tested engine: UE 5.8.4); a custom/source engine can be specified with `engine_path`.

```powershell
& 'C:/Program Files/Python311/python.exe' 'C:/Users/traid/.codex/game-dev-bridge/app/bridge.py' status
& 'C:/Program Files/Python311/python.exe' 'C:/Users/traid/.codex/game-dev-bridge/app/bridge.py' tools
& 'C:/Program Files/Python311/python.exe' 'C:/Users/traid/.codex/game-dev-bridge/app/bridge.py' call unreal_project --args-file 'C:/absolute/request.json'
```

`request.json` is the tool's argument object, e.g. `{"project":"C:/MyGame/MyGame.uproject"}`.
Prefer argument files for scripts and Windows paths instead of shell-escaped JSON.
Tool schemas are returned by `tools`; all project and asset file paths must be absolute.
Set `blender` in the shared `config.json` to pin a particular Blender executable.

## Unreal

1. `unreal_project(project, engine_path?)` resolves the actual project and engine.
2. Save/close this project in any other Unreal process. `unreal_start(project, extra_plugins?)`
   launches an offscreen NullRHI editor and enables native MCP, Python and core editor/testing
   tools for that process. It does not rewrite the project descriptor or global engine settings.
3. Poll `unreal_session_status(project)` until `ready`. Startup can take several minutes.
4. `unreal_toolsets(project)` then `unreal_describe(project, toolset)` discovers exact native
   schemas; `unreal_call(project, toolset, tool, arguments)` invokes them. Optional extra plugins:
   PCGToolset, NiagaraToolsets, AnimationAssistantToolset, GASToolsets, PhysicsToolsets,
   StateTreeToolset, AIModuleToolset, GameplayTagsToolset. Restart a session to change its plugins.
5. `unreal_python(project, code)` supports task-scoped trusted scripts. `unreal` is provided;
   assign JSON-serializable `result`. Save intended assets/maps explicitly.
6. `unreal_import_fbx(project, source, destination)` imports a static mesh into a new dedicated
   `/Game/Folder`, saves and load-checks it. Existing destination assets cause an error.
   Skeletal meshes, animation, re-import and other formats use explicit native APIs/Python.
7. Save changes, then `unreal_stop(project)` and wait for `stopped` before offline imports/tests.

Each session has a unique local port and verified project path/nonce. Native calls are serialized.
The session holds the same project lock used by Fab import/verification, preventing concurrent
on-disk mutations. Sessions are bridge-owned and persistent until explicitly stopped; this is not
a background test loop. Do not automate UI/Slate, take desktop screenshots, or drive mouse/keyboard
through scripts/native tools. Offscreen/NullRHI operation does not validate visual rendering.

## Blender and asset checks

`blender_run(script, source?, parameters?, inputs?, timeout_seconds?)` runs a trusted Python file
in a separate `--background --factory-startup --disable-autoexec` process. It cannot modify the
live Blender scene. Scripts receive `OUTPUT_DIR` (a new job directory), `INPUT_FILE`, `PARAMETERS`
and `bpy`; assign JSON-serializable `result`. Imported source formats: blend, FBX, GLB/glTF, OBJ.
Auto-run scripts embedded in blend files are disabled; explicitly submitted Python is trusted code,
not a filesystem sandbox. Write outputs under OUTPUT_DIR and keep sources unchanged unless asked.

`examples/make_crate.py` makes a UV-mapped material-bearing 2 m cube and exports blend/FBX.
This is a minimal reproducible pipeline example, not a finished art asset.

`asset_validate(source, rules?, inputs?, force?, reason?)` validates evaluated mesh geometry,
dimensions, finite coordinates, UV presence, materials, topology diagnostics and missing images.
Optional rules: `require_uv` (default true), `require_material`, `require_manifold`, `max_triangles`.
Topology warnings may be intentional (foliage/open surfaces); do not auto-repair or impose universal
triangle budgets. Structural passes do not prove visual quality, proper skin weights or good deformation.
Successful checks track discovered file images, linked blend libraries and glTF/OBJ sidecars.
Declare additional external dependencies in inputs. Tiled/movie/sequence image dependencies disable
cache reuse because a complete dependency set cannot be inferred reliably.

## Tests only when needed

`unreal_test(project, test_filter=..., extra_plugins=...)` runs a named/prefix-selected Unreal Automation suite and
requires a nonempty successful export report. `unreal_test(project, script=...)` instead runs a
Python assertion script in a commandlet. The script must raise on failure; a returned result alone
does not define pass/fail. These are editor/integration tests, not a universal gameplay playtester.
Use native automation toolsets or project-specific functional tests for gameplay requirements.

Validation and tests run only when requested for a relevant change, unresolved failure, release
checkpoint, or explicit user request. Successful identical checks are reused by SHA-256 over inputs,
settings and runner/tool identity, plus discovered texture dependencies. Nothing runs on status,
discovery, reconnect or an idle timer. Polling reads state and never repeats the job.

Unreal test inputs default to the descriptor plus Config, Source, Content and Plugins (excluding
Saved/Intermediate/.git). An explicit `inputs` list narrows the dependency scope for large projects;
include every dependency relevant to the test, including externally shared content. Caches describe
saved files only; save changed editor state first. Input changes during execution prevent cache reuse.
Failed/incomplete checks are not cached. `force=true` requires a concrete `reason`.
Examples: validate a changed mesh once; test changed gameplay once; skip tests for prose-only edits.
Do not run a whole project suite after every small edit or repeatedly rerun a passing unchanged check.

All asynchronous operations return job IDs. `dev_job_status(job_id)` provides terminal state,
report, log and artifact paths. A queued/running job is not a success. Per-job deadlines stop hung
owned processes. Preserve reports from failures and investigate before retrying.

## Verification and sources

Run the focused bridge checks with `python -m unittest discover -s . -p test_bridge.py -v` after
bridge implementation changes. Live validation uses an isolated Unreal project and Blender fixture.
Research references: [Epic native MCP](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor),
[Unreal Python](https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python),
[Codex MCP](https://learn.chatgpt.com/docs/extend/mcp). Native APIs are experimental; engine versions
must be inspected, not inferred from a different project's successful run.
# Character pipeline

The shared server also provides character inspection, skeleton construction, skin binding,
animation authoring, deformation validation, skeletal/clip FBX export, Unreal skeletal imports
and configured IK retargeting. See [CHARACTERS.md](CHARACTERS.md) for JSON specifications and
the explicit limits of automated checks. There are 21 tools in this release.

