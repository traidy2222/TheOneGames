Optimus source export extensions (MIT)

Upstream: https://github.com/HUNK2/rotf-robots
Pinned commit: dd97296c9a618d06cc2e01bf38c6221b057f6eba
Upstream copyright (c) 2026 HUNK2. See rotf-robots-MIT-LICENSE.txt.
These local helper/test additions are also supplied under the MIT License.
No game data is included in these sources.

Copy optimus_supplement.rs, optimus_source_audit.rs and optimus_wheel_audit.rs into upstream export/examples/.
Copy optimus_supplement_test.rs into upstream export/tests/optimus_supplement.rs.
The test imports upstream export/tests/common/mod.rs.

From games/transformers/build/third-party/rotf-robots:
  cargo run --release -p rotf-export --example optimus_source_audit -- ../../source-assets ../../exported ../../inventory/optimus-source-animation-audit.json
  cargo run --release -p rotf-export --example optimus_supplement -- ../../source-assets ../../exported ../../inventory/optimus-supplement-export.json

For the focused test, set TF2_GAME_DIR to the absolute staged source-assets folder,
and ROTF_SUPPLEMENT_EXPORT_DIR to the absolute build/exported folder, then:
  cargo test --release -p rotf-export --test optimus_supplement -- --nocapture

The original rotf-export must run for each variant before the audit/supplement helpers.
The helpers read only the staged data and add files under exported and inventory.
They do not change the game executable, installer, ISO or engine code.

Outputs:
- IntroPlayer on the native robot hierarchy; separate IntroCamera motion carrier.
- Standard/gold: 50 loose source-coordinate meshes with no guessed attachments.
- Standard/gold: two FaceSet clips on explicitly unbound independent motion carriers.
- Every original texture under source-textures/<id>.dds; all mip/face pixel bytes retained;
  flat textures also have PNGs. Cubemaps stay DDS.
- Named unbound motion channels, uninterpreted property streams and interaction-event fields
  are preserved as JSON, with no invented runtime meanings.
- <pack>_supplemental.json provides files/clips/render_sets/materials/collision for catalog merge.

Limits:
The loose mesh and motion-carrier exports do not reconstruct missing original hierarchy or
bind FaceSet to those meshes. Camera projection parameter units are not established here.
All 2280 decoded source meshes now have exported geometry: lower-detail and ancillary meshes
are clearly labelled loose collections. The 23 skinned LOD parts retain their native palettes,
inverse binds and normalized weights via independent native bind frames. These carriers do not
claim a recovered original animation parent hierarchy. This is not a fully reconstructed game.

Focused coverage result: 3 tests passed: all 2280 mesh IDs / 359419 source vertices and indices,
23 skinned LOD parts, 350 original DDS pixel payloads, 1800 unbound animation samples,
and 972 native intro node poses.

Wheel probe (TF2_GAME_DIR set to staged data):
  cargo run --release -p rotf-export --example optimus_wheel_audit
Clean REST -> Trans_R2V_A -> Trans_R2V_B -> V_Idle keeps tyre local translation at zero.
Legacy full Trans_R2V keys Tire_Front_L to (-1.334961,0.048706,-0.871094), duplicating its
TireGOAL offset; later A/B clips leave that unkeyed tyre local state held. Seed from clean rest
using the actual A/B sequence. Keep legacy clips accessible as source data, not initialization.
