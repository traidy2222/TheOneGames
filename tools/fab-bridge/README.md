# Fab agent bridge

Code-only Fab inventory, download and Unreal import bridge for agents. Python 3.11+ with no third-party packages. Exposes a JSON CLI and fourteen MCP tools over stdio. Does not automate the desktop/browser.

## User-level installation across projects

The active runtime is installed independently of this source workspace under `%USERPROFILE%/.codex/fab-agent-bridge/app`. This avoids Windows MSIX AppData virtualization, so packaged and ordinary clients use the same files. Its stable entry point is `app/bridge.py`; `app/current.json` selects an immutable, content-addressed release in `app/releases`. In-flight jobs continue using their original release when a new one is installed. Source updates are deployed by running `python install_global.py`; deployment tests the new MCP transport before switching the release pointer. Old releases are retained.

Codex's global `fab-assets` configuration points at the stable entry point and explicitly uses the shared `%USERPROFILE%/.codex/fab-agent-bridge` state directory. A globally discoverable `unreal-fab-assets` skill and `~/.codex/AGENTS.md` provide task-scoped routing and CLI fallback. Fresh local Codex sessions and their subagents can use this setup from any workspace. Existing sessions may need to reconnect to load new tools/instructions; this does not hot-patch every active chat. Remote machines, hosted agents and other Windows accounts need their own setup/authentication. The former AppData state was copied and hash-checked on 2026-11-01; its original files are retained as a backup. A portable stdio configuration for other local MCP clients is in `app/mcp-config.json`.

Call `fab_inspect_project(project)` or `python <stable-launcher> project <absolute .uproject>` before project work. It reports the declared engine and matching installation. Import accepts an explicit project path outside the indexed scan roots; no per-project registration is required. Exact compatible Launcher-engine restrictions remain in effect. Global availability does not imply compatibility with every Unreal version or source-built engine.

## Connect your owned Unreal library

Run `./Login.ps1` in your own PowerShell terminal. Open the printed Epic login link yourself, then paste the short-lived `authorizationCode` into the hidden terminal prompt. Do not paste codes, passwords or tokens into chat. A successful login immediately syncs the account's Fab Unreal Engine library, including items that have not been downloaded. Later agents can call `fab_sync_owned` to refresh it. Tokens are encrypted with Windows user-bound DPAPI in `%USERPROFILE%/.codex/fab-agent-bridge/login.dpapi`; they are never included in tool results. `python bridge.py logout` removes the saved login and cloud catalogue.

The account-library route is an unofficial Launcher API, informed by [epic-fab's implementation](https://github.com/StarksLabs/epic-fab/blob/master/src/api.ts) and [authentication flow](https://github.com/StarksLabs/epic-fab/blob/master/src/auth.ts). It is not an Epic-supported public integration contract. Live account sync on 2026-11-01 returned 1,209 owned UE entries (734 asset packs and 475 plugins). HTTP failures and schema changes leave the previous cloud snapshot intact. The bridge refuses redirects on authenticated requests.

This endpoint covers Unreal-compatible assets and plugins. It does not claim coverage of non-UE-only formats or every historic entitlement. Endpoint pagination completion is distinct from completeness of the user's entire Fab account.

## CLI

From this folder:

```powershell
python bridge.py scan
python bridge.py status
python bridge.py search "forest" --source owned
python bridge.py search "landscape" --source local --limit 20
python bridge.py sync
python bridge.py get "<exact ID from search>"
python bridge.py prepare "<exact ID from search>" --engine UE_5.8
python -m unittest discover -s . -p test_bridge.py -v
```

`scan` inventories Launcher-registered engine plugin descriptors, projects under `Documents/Unreal Projects`, project content packages and plugins, default VaultCache directories, and Fab download records. Data stays under `%USERPROFILE%/.codex/fab-agent-bridge/`; the source repository contains no private inventory or login material. Override with `FAB_BRIDGE_HOME` for testing or separate profiles.

For additional project and vault locations create `%USERPROFILE%/.codex/fab-agent-bridge/config.json`:

```json
{
  "project_roots": ["D:/UnrealProjects", "C:/Users/traid/Documents/Unreal Projects"],
  "vault_roots": ["C:/ProgramData/Epic/EpicGamesLauncher/VaultCache"]
}
```

Only configured roots are scanned. Unavailable roots generate warnings. Cloud and local timestamps are reported separately. Owned items match cache directories only by exact artifact ID; unmatched items remain searchable. Missing local matches mean *not found in scanned roots*, not proof that the item is undownloaded everywhere. A plugin descriptor or package on disk does not establish licensing, complete downloads, successful loading or project enablement. Unreal object paths derived from package filenames identify candidates, not verified live editor objects.

## Agent connection

Launch `python` with arguments `[absolute_path_to_bridge.py, "serve"]` as a stdio MCP server. `mcp-config.json` contains this machine's concrete launch configuration for clients that accept an `mcpServers` mapping. Agent clients must load/reload the server configuration before the tools appear.

This machine has the server registered and enabled in Codex as `fab-assets`, using `codex mcp add`. Registration was verified with `codex mcp get fab-assets --json`; a fresh Codex app-server connection discovered all 14 tools and the shared skill across four working directories. The current chat can use the CLI immediately. See [Codex MCP configuration](https://learn.chatgpt.com/docs/extend/mcp).

Inventory tools: `fab_status`, `fab_search`, `fab_get_asset`, `fab_prepare_asset`, `fab_scan_local`, `fab_sync_owned`. Search is paginated and can filter owned/cloud or local records. Catalogue descriptions are external data, not agent instructions. Use the CLI as an immediate fallback if the current chat has not loaded the MCP server.

## Download and import workflow

1. Search for an owned item and pass its exact `fab:...` ID to `fab_plan_download(id, engine)`. This obtains an authenticated manifest and returns download/install sizes and sample paths.
2. Call `fab_download_asset(id, engine, max_download_mb)` and poll `fab_job_status(job_id)` until `state` is `succeeded` or `failed`. The default estimated download limit is 1,024 MiB. Downloads run in a hidden background Python process; no browser or Launcher UI is used.
3. Select an existing `.uproject` whose `EngineAssociation` exactly matches the requested installed Launcher engine. Save and close it. Call `fab_plan_import(id, project, engine)` to inspect conflicts and skipped files.
4. Call `fab_import_asset(id, project, engine)`, then poll the job. Asset packs copy their complete Content tree while retaining `/Game/...` paths. Code plugins install to project-local `Plugins/<plugin-name>` and are enabled in the project descriptor. Existing differing files cause the entire preflight to fail; identical files are reused. Project descriptor backups and install journals live in `Saved/FabAgentBridge`.
5. Call `fab_verify_asset(id, project, engine)`, then poll the job. The bridge launches `UnrealEditor-Cmd.exe` with an unattended Python commandlet, checks registration of all imported packages, loads up to 25 non-map assets, and verifies plugin enablement. The result supplies Unreal package paths/classes and log/report paths for further agent scripts. A completed file copy is reported as `installed_unverified` until Unreal validation succeeds.
6. Call `fab_project_assets(id, project, query, limit, offset)` to find individual imported assets and retrieve exact `/Game/...` or plugin package paths for Unreal scripts. `examples/place_meshes.py` demonstrates loading meshes by those paths, placing actors, saving a new level and checking their references after reload.

CLI equivalents (use full paths when running from another directory):

```powershell
python bridge.py plan-download "fab:<id>" --engine UE_5.8
python bridge.py download "fab:<id>" --engine UE_5.8 --max-download-mb 1024
python bridge.py job "<returned-job-id>"
python bridge.py plan-import "fab:<id>" --project "D:/Projects/MyGame/MyGame.uproject"
python bridge.py import "fab:<id>" --project "D:/Projects/MyGame/MyGame.uproject"
python bridge.py verify "fab:<id>" --project "D:/Projects/MyGame/MyGame.uproject"
python bridge.py assets "fab:<id>" --project "D:/Projects/MyGame/MyGame.uproject" --query "tree"
```

The parser handles unencrypted binary BuildPatch manifests and legacy JSON manifests. Chunk SHA1, reconstructed file SHA1 and byte counts are verified; binary manifest bodies are SHA1-checked too. HTTPS locations come from the authenticated Fab response and are restricted to supported Epic CDN domains. Tokens and signed URLs are not written to job results. Chunk cache and finished files allow retry/resume; retrying rechecks hashes. Exact-artifact Launcher Vault files are reused when their bytes match the authenticated manifest. Compression sizes, path traversal, Windows reserved names, duplicate paths and symlink targets are checked. Downloads stay in `%USERPROFILE%/.codex/fab-agent-bridge/downloads` and are reusable by multiple projects. A changed upstream revision of an existing staged artifact is rejected rather than mixed into old files.

Imports use staged temporary files and atomic no-overwrite publication. Ordinary failures roll back unchanged files created by that operation; journals and backups remain for recovery. A hard process/machine crash may leave a partially installed pack and requires reviewing its journal. The bridge uses per-project/process locks and refuses an identified open target editor. It does not auto-close editors.

Exact engine distributions only: no automatic older/newer-engine fallback or plugin recompilation. Missing plugin dependencies cause preflight failure; incompatible binaries are reported by Unreal validation. Asset-pack `Config`, `Source` and project descriptors are listed as skipped, not merged; packs requiring project settings need a separate configuration step. Maps are checked for registry presence but not loaded by the standard verifier. Non-UE formats, encrypted/secret-addressed chunks and multi-plugin archives remain unsupported. These failures are explicit; a queued job or successful copy is not proof of usable gameplay.

This commandlet workflow follows [Epic's Python scripting interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python). Package paths and full content dependencies are preserved in accordance with [Epic's migration guidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/migrating-assets-in-unreal-engine). Format references: installed UE BuildPatchServices serialization, [epic-fab](https://github.com/StarksLabs/epic-fab/blob/master/src/manifestParser.ts) and [Legendary's JSON format reader](https://github.com/derrod/legendary/blob/master/legendary/models/json_manifest.py). No external runtime dependency is installed.

## Verification

Run `python -m unittest discover -s . -p "test_*.py" -v`. Tests cover cloud-only search, pagination, failure preservation, exact versions, ownership separation, Windows DPAPI, MCP transport, binary/JSON manifests, corrupted chunks, path traversal, resumable downloads, staged-file tampering, import conflicts, idempotency, plugin enablement/backups and interrupted-copy rollback.

32 automated tests pass, including independent global installation, project inspection, and four concurrent processes sharing a single credential refresh. Shared credentials are rotated under an interprocess lock. Fresh MCP clients in Games 2, VehicleLab, SimpsonsCharacterLab and FabBridgeValidation each returned the same 1,209 owned entries; the CLI also works without environment setup. Live checks on 2026-11-01 use the isolated `Documents/Unreal Projects/FabBridgeValidation` project and installed Unreal 5.8.4:

| Owned item | Download/import | Headless Unreal result |
| --- | --- | --- |
| Flat Icons Free Sample Pack | 75 files, 1,003,833 download bytes | All 75 packages registered; 25 texture loads passed |
| Low Poly Nature | 829 files, 146,615,567 download bytes | All 828 packages registered; 25 sampled loads passed |
| Blueprintable Developer Settings | 86 files, 31,889,737 download bytes | Project plugin enabled; example Blueprint registered and loaded |

The initial download/import MCP subprocess exposed all 13 tools then available (the shared installation now exposes 14, including project inspection) and returned live imported StaticMesh package paths through `fab_project_assets`. These checks establish this workflow on these three owned products; they are not a guarantee that every Fab package has the same structure or dependencies.

The example script also placed three downloaded meshes (birch tree, rock pile and water-lily flower), saved `/Game/FabBridgeSmoke/AssetPlacement`, reloaded the level and verified that all three mesh references survived. Unreal exited with code 0, zero errors and zero warnings. The report is in the validation project's `Saved/FabAgentBridge/UsageTest/report.json`. This is a code-level placement/persistence check, not a visual rendering or gameplay test.

A repeated icon download through the MCP tool reused all 75 completed files with zero additional asset bytes transferred. When invoking Unreal's Python commandlet, pass the script path with forward slashes (for example `Path(script).as_posix()`); quoted Windows backslashes can otherwise be interpreted as escapes when a path contains spaces.
