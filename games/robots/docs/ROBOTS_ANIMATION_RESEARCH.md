# Rodney animation investigation

Status: all 162 indexed clips can now be sampled and loaded with a candidate
weighted rig. Original blending, layering, events, state selection and visual
fidelity remain incompletely decoded or unverified.

## Reproduce the inventory

Extract the **full** EDB, including the trailing data described in
`ROBOTS_PORT.md`, then run:

```powershell
python tools/inspect_robots_animation.py path/to/p01_rod.edb `
  --hashcodes path/to/hashcodes.h `
  --output build/rodney-animation-inventory.json `
  --skeleton-gltf build/rodney-skeleton-candidate.gltf
```

The tool checks file bounds, version, pointer targets, hierarchy and motion
ranges. It uses field layout from Eurochef's MIT-licensed `anim.rs`, `header.rs`
and `common.rs`; see `licenses/EUROCHEF-MIT.txt`. The glTF has 29 unnamed nodes
and translations only: it has no mesh, skin, weights, animation or verified
bone orientations. It is an investigation artifact, not an animated character.

## Observations from this copy

- The full `p01_rod.edb` has EDB version 248, asset hash `01000002`, seven skin
  records, and 162 animation headers with 1,761,280 bytes in their indexed
  motion ranges. It also has 123 animation-mode records, all named by the
  original `hashcodes.h` in this copy (including Idle, Move, and jump modes).
  The inventory records their offsets and SHA-256; mode/set/script references
  have been traced to candidate clips, though full script behavior is unresolved.
- Each of the seven skins has 29 entries in the candidate hierarchy. The first
  16-bit hierarchy field is `0xffff` for its root and a preceding index for
  every other entry. For each candidate link, the first three components of
  one 4-float vector minus its parent's vector match a second vector to within
  `8.95e-8` in all seven skins. The second 16-bit field also matches the last
  index of each node's contiguous descendants for all seven skins. This supports interpreting them as absolute and
  parent-relative positions, respectively; it does not establish orientations,
  the bone names, or skin binding.
- Static analysis of the supplied x86 executable found a skin initialization
  routine at `0x4fc82c` that reads the bone count at offset `+4`, resolves the
  relative pointer at `+0x48`, iterates eight bytes per bone and copies the first
  16-bit field into its runtime bone entry. REA evidence:
  `ev_8ed62bb01a57f6d55bb3c07250ce90e43bea911c6bd8da0e89bdfe8852db59eb`.
  This corroborates the structure but is static evidence, not observed playback.
- The original frame-cache routine `0x503fde` is referenced by the animation
  sample path `0x50376d`; its callers reach `0x567b30` through `0x503412` and
  related paths. The latter function's decompilation shows a six-coefficient
  polynomial evaluation, selection controlled by the low six bits of 16-bit
  descriptors, and reconstruction of a quaternion component with a square
  root and sign branch. These are static code observations, not a validated
  interpretation of the complete EDB motion layout. IDA function addresses
  are relative to this executable's `0x400000` image base. The input hash is
  `950bc7f0e0feefd92a129deef50123f5619ec36930eceb9707ef02dfcaaa8e13`.
  The native frame routine uses the unsigned 16-bit value at animation-header
  offset `+0x0e` as a frame bound; the 162 supplied records hold values from
  1 through 202. The inventory records these as `frame_count_candidate` and
  preserves neighboring bytes without assigning unverified meanings.
- The game executable embeds vertex-shader source describing four bone indices
  and weights. The skin records align with the 20 split mesh parts, including
  face and hands: each vertex has four index bytes and four float weights, with
  active byte indices divisible by three and indexing a per-part bone palette.
  All 4,506 source vertices match the converter's position order, and weights
  sum to one. The resulting candidate rig reproduces all 25,824 rendered
  rest-pose vertices within `2.4e-7`; rotating one joint moves 1,425 vertices
  while 24,399 remain fixed. This checks consistency, not fidelity to a moving
  original pose or facial morph behavior.

## Motion progress

`tools/robots_motion.py` implements a bounded scalar sampler based on the
original decoder. `tools/verify_robots_motion_native.py` compares it against
the original function at `0x567b30` in Unicorn's isolated x86 emulator. The
emulator maps the supplied executable and EDB into private memory, stubs one
local cleanup routine, and executes neither game startup nor OS calls. It
restores its initial CPU and floating-point state before every sample. An
every-frame audit of all 162 records compared 235,306 track samples and
skipped no frames. 327 components exceeded the comparison tolerance, with a
maximum absolute difference of approximately `0.0279`.
Playback assets therefore use the isolated original decoder instead of
treating the Python approximation as numerically exact.

The 22 formerly unexportable clips use multiple compressed chunks. The native
stream lookup at `0x5563f4` uses metadata selected by `0x504d0b` from the
animation header at `+56`: a padded chunk byte size and frame-end table select
the chunk, its global start frame and its local frame range. Consecutive chunks
include one overlapping sample. Decoding that same overlap frame from either
chunk produced identical native output at all 25 chunk boundaries. This
validates chunk selection and continuity of sampled values, not final pose
composition against original gameplay.

`tools/rig_robots_rodney.py` builds the candidate weighted glTF from Eurochef's
`d000001.gltf` skin export. `tools/export_robots_motion.py --native-exe ... --all`
uses the bounded sampler as a guard and the original isolated decoder for each
integral frame. The current asset exports all 162 indexed clips. The
game uses candidate idle, movement and jump clips traced via mode/set/script
records by `tools/inspect_robots_modes.py`. Its state selection, 30 Hz sampling,
root correction and axis conversion are approximations; the original blend
weights, animation scripts, face morphs and events are not reproduced.
For the physics-driven prototype, `--in-place` fixes the root's horizontal
translation to its bind position, avoiding a jump when the walking clip loops;
vertical motion remains animated. Airborne clips hold their final frame rather
than looping. The prototype's idle and walking clips also interpolate their
final sample back to the first for a smooth loop. These rules are not a
recovered original gameplay controller.

With the extracted EDB, Eurochef skin export, matching inventory, and the
specific executable identified above, the local rig and sampled clips can be
rebuilt with the following commands. The native path requires the Python
packages `pefile` and `unicorn`; outputs must not already exist.

```powershell
python tools/rig_robots_rodney.py path/to/p01_rod.edb path/to/d000001.gltf build/rodney-rig.gltf
python tools/export_robots_motion.py path/to/p01_rod.edb build/rodney-animation-inventory.json build/rodney-rig.gltf build/rodney-motion.gltf --native-exe path/to/Robots.exe --all --in-place
python tools/verify_robots_motion_native.py path/to/Robots.exe path/to/p01_rod.edb build/rodney-animation-inventory.json --all-frames --report-only
```

The exporter reports skipped clips and the reason for each skip. The glTF and
original game files are generated locally and are not included in the source
tree. A successful export does not prove that its poses match original gameplay.

## Engine support added

The glTF loader now imports CUBICSPLINE translation, scale and rotation
channels, evaluates Hermite tangents with the keyframe interval, and normalizes
cubic quaternions. The engine can crossfade translation, scale and quaternion
rotation between two sampled poses, using the bind pose for channels absent
from either clip. Rodney uses an approximate 0.15-second transition; this is
not a recovered original blend rule. Generated glTF tests cover tangent
scaling, quaternion normalization and crossfading against an unanimated pose.
These engine capabilities do not themselves decode the compressed motion.

## Next questions

1. Compare motion samples and skeletal transforms with the original game
   running, not only the isolated decode function; verify track masks.
2. Validate bind orientations, coordinate conversion, face morphs and original
   animated deformations against recorded original poses.
3. Identify remaining named clips, transitions, layered motion and event timing, then
   implement the required runtime controls and validate representative idle,
   movement, jumping and interaction against the original.
