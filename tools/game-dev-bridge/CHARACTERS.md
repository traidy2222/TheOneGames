# Shared character rigging and animation

Seven character tools extend the user-level `game-dev` server. All jobs are code-only,
use explicit paths and isolated background Blender processes, and preserve source files.
Read job results with `dev_job_status`. A queued job is not a completed operation.

## Rig and animate

`character_inspect(source, spec_file?)` lists bones, bound meshes, available actions and
binding problems. An inspection can succeed while reporting problems; `character_validate`
enforces them. Select `armature` by name if a file has several rigs.

`character_rig(source, spec_file)` creates a new rig with an explicit JSON specification:

```json
{"armature":"CharacterRig","meshes":["Body"],"binding":"automatic","max_influences":4,
 "bones":[{"name":"root","head":[0,0,0],"tail":[0,0,0.2]},
          {"name":"spine","parent":"root","head":[0,0,0.2],"tail":[0,0,1.2]}]}
```

Bone coordinates use the scene's world coordinate space; the new armature has an identity
transform. Roll is radians; `deform` defaults true. Exactly one root is required. This is
an explicit skeleton builder, not automatic anatomical fitting. Fit joint positions to the
actual character before binding. Existing armature modifiers are never replaced implicitly.
Automatic binding uses Blender bone heat weights, then prunes/normalizes influences. Unweighted
vertices fail; there is no arbitrary fallback bone. For deterministic/custom weighting use
`binding:"weights"` and `weights:{"Body":{"0":{"spine":1},"1":{"root":0.4,"spine":0.6}}}`
covering every vertex index. The result is a new `character_rigged.blend`.

`character_animate(source, spec_file)` authors named actions on an existing rig:

```json
{"armature":"CharacterRig","fps":30,"clips":[{"name":"Bend","start":1,"end":31,
 "tracks":{"spine":[{"frame":1,"rotation":[0,0,0]},
                     {"frame":16,"rotation":[0.4,0,0]},
                     {"frame":31,"rotation":[0,0,0]}]}}]}
```

Transforms are bone-local `location`, XYZ Euler `rotation` in radians, and positive `scale`.
New rigs use XYZ bones. Rotation authoring on an existing quaternion/other-mode bone is rejected:
explicitly convert/bake its existing actions first, since rotation mode is shared across actions.
Keys interpolate linearly. Existing action names fail instead of overwriting. For advanced
IK, constraints, mocap cleanup, or procedural motion use `blender_run` with task-specific
Python, then bake/isolate the intended single-character action. NLA and multi-object action
slots need that explicit bake; the simple tools do not guess their intended composition.

## Validate and export

`character_validate(source, spec_file?, inputs?, force?, reason?)` checks the single root,
bound meshes, finite normalized skin weights and influence budget, and samples deformation
over selected clips. Example spec:
`{"actions":["Bend"],"rules":{"require_animation":true,"require_motion":true,"max_influences":4,"sample_frames":5}}`.
Samples report vertex displacement and root movement. They do not establish attractive
deformation, correct foot contact, facial expression quality, or gameplay readiness.
Pass linked dependencies in `inputs`; detected linked Blender libraries are tracked too.
Passing unchanged checks are reused. No tests run on discovery, status or a timer.

`character_export(source, spec_file)` writes `Character.fbx`, one FBX per selected action,
and `character-manifest.json`. Example: `{"actions":["Bend","Walk"],"root_motion":"preserve"}`.
Without `actions`, only the current active action is used; other actions never export implicitly.
Neutral mesh and clips share the same full skeleton; no leaf bones are added. Clips bake
selected actions with NLA/all-actions disabled. `require_in_place` rejects any root transform
change at exported integer frames, including motion that returns to its starting point.
It never silently removes root motion. Non-armature mesh modifiers must be baked explicitly.
The export copy converts mesh/bone coordinates and translation keys to centimetres, keeping
unit root scale in Unreal. Source files remain untouched. Apply armature object scale and bake
constraints, drivers, external parents and location-curve modifiers before unit conversion.
Verify scale at first engine delivery. The exporter uses
the conventional `Armature` object name for Unreal's FBX root handling.

## Unreal delivery and retargeting

Start the task's explicit project with `unreal_start`. Use `unreal_import_character`:

- Mesh: `project`, `source` (Character.fbx), `destination` (fresh `/Game/...` folder).
- Each clip: its FBX `source`, a fresh `destination`, the returned `skeleton` path,
  `animation_only:true`, and optionally `enable_root_motion:true`.

Imports verify asset classes, Skeleton identity and nonzero animation duration. No existing
assets are overwritten and no physics asset is generated. Set up collision, physics, animation
blueprints, IK/gameplay behavior and facial controls according to the target project.

`unreal_retarget_animations(project, animations, source_mesh, target_mesh, retargeter, destination)`
uses an existing configured IK Retargeter and a fresh output folder. Start the session with
`extra_plugins:["IKRig"]`. Chain mappings and retarget poses must already match those meshes.
Outputs must use the target skeleton and have positive duration; these checks cannot prove
retargeting quality. This tool does not automatically construct a humanoid retargeter.

After meaningful delivery, save and stop the managed session before `unreal_test` offline
checks or Fab operations. `examples/make_character_fixture.py` creates a small reproducible
rig/clip fixture through `blender_run`; `examples/test_character_unreal.py` checks its saved
Skeleton bindings, timing, root-motion settings and centimetre scale. Run focused checks
after relevant changes/failures/checkpoints, and stop when those checks pass.

Implementation references: [Blender FBX API](https://docs.blender.org/api/4.5/bpy.ops.export_scene.html)
and [Unreal IK retarget batch API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/IKRetargetBatchOperation).
