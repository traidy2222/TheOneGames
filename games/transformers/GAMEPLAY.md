# Optimus gameplay reconstruction

## Implemented corrections

The engine camera uses a right-handed view. At yaw zero it looks along +Z and screen-right is -X. The playable host now uses that right vector for D movement, steering and mouse yaw. The imported character faces +Z: source muzzle nodes sit 2.71 m ahead of the right-hand mount in Weapon_Idle. Rotating the character 180 degrees would introduce a new mismatch.

Gameplay clip changes now blend local translation/scale and quaternion rotation, then rebuild the hierarchy. The source clip crossfade overrides the animation-rules default of 0.1 seconds. Takeoff is held for its source duration and landing has a short transition. Firing while moving selects directional Weapon_Run clips. The aim module applies source aim_up/down offsets, at full weight at 65 degrees, after base pose blending. Browser timeline sampling remains deterministic.

The loadout resolves PrimeLaserRifle, PrimeLaserRifleLeftHand, ShellCannon, ShellCannonL and OptimusVehicleWeapon definitions. Lasers alternate hands, shell weapons fire together on release with source charge-damage parameters, and truck fire emits from both source barrel nodes. Right and left hand mounts and the vehicle AirFilter_R attachment resolve through the same Player::placement matrix for rendering and muzzle calculation. FireA/B, idle, range, damage, sound IDs and fire intervals come from the exported sidecar. Camera sight rays select a target point; shots trace from the physical muzzle toward it, with the source aim-assist threshold. Source heat capacities, increments, cooling delays/rates and overheat periods drive a bounded heat state.

The truck weapon object is a muzzle carrier: its source GLB has no animations and only a 2.37 cm placeholder mesh. It does not contain separate visible cannon barrel geometry. Both source fire nodes are retained; this is not evidence of missing deployment animation.

Audio no longer destroys one-shot tails when animations change or loop. Sample decoding caches within 32 MiB; sustained samples feed through an SDL stream callback. Source sample choices, gain, pitch and fade metadata are used. Footstep events resolve the yard's default concrete surface sound. Mounted weapon fire and truck engine events are triggered by gameplay. Unbound/null sample entries are skipped.

## Evidence and remaining work

Primary asset evidence: `build/exported/optimus/optimus.json`, its supplemental sidecar and referenced GLBs. Source format/application evidence is in the pinned importer under `build/third-party/rotf-robots`, including its weapon-mode notes. Native executable identity and bounded tracing are recorded in `build/inventory/gameplay-native-notes.md`. The REA CLI overview rejected IDA's unsupported list_documents operation; direct IDA opened the supplied executable and inspected native strings/references. No verified original gameplay pseudocode was recovered in that bounded trace.

This is an incomplete gameplay reconstruction. The full native locomotion/combat state graph, melee branches/hit-window rules, climbing, dodge, vehicle suspension/drift, damage/status systems, original projectile/particle programs and full FEV RPM/spatial/parameter mixer remain unfinished. Heat recovery currently requires both the lock period and full cooling; that policy, shell hold/release timing and camera/physics behavior are reconstructed rather than verified against original execution. Charged heat modifiers and exact native weapon-animation fire event timing remain incomplete. Prototype target crates count hits rather than reproduce original enemy health and damage reactions. Audio stream playback is exercised; listening quality has not been verified.

## Focused validation

Only the playable targets and focused check executable are built. Checks cover camera-relative right movement, local-pose blend continuity, additive aim direction, forward weapon pose at FireA/B times 0/0.05/0.15 s, exact shot/muzzle origins, alternating/paired mounts and truck barrel emission, along with the existing arena movement/collision checks. Vulkan captures exercise robot weapons, transformation/driving and truck weapons. Logs and images are under `build/inventory/`. No engine-wide or other-game suites are required for these changes.

Final focused check passed. Robot weapon and truck weapon captures each reported zero Vulkan validation errors, starting 16 and 10 audio voices respectively with playback enabled. Stream creation/playback is observed; that does not establish listening quality or original mixer parity.
