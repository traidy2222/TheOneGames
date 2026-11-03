"""Run inside UnrealEditor-Cmd, after importing a pack and querying its package paths.

FAB_MESH_PATHS: JSON array of exact StaticMesh package paths.
FAB_MAP_PATH: new /Game/... map path (existing maps are refused).
FAB_USAGE_REPORT: absolute local JSON report path.
"""
import json
import os
from pathlib import Path
import unreal

mesh_paths = json.loads(os.environ['FAB_MESH_PATHS'])
map_path = os.environ['FAB_MAP_PATH']
if not map_path.startswith('/Game/') or '..' in map_path or not 1 <= len(mesh_paths) <= 10:
    raise RuntimeError('Use a new /Game map and 1..10 meshes')
if unreal.EditorAssetLibrary.does_asset_exist(map_path):
    raise RuntimeError('Example refuses to overwrite an existing map')
meshes = []
for package in mesh_paths:
    mesh = unreal.load_asset(package)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError('The requested package is not a loadable StaticMesh: ' + package)
    meshes.append(mesh)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for index, mesh in enumerate(meshes):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(index * 400.0, 0.0, 0.0))
    if actor is None or not actor.static_mesh_component.set_static_mesh(mesh):
        raise RuntimeError('Could not create mesh actor')
    actor.set_actor_label('FabBridge_' + str(index))
if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
    raise RuntimeError('Could not save demonstration map')
if not unreal.EditorLoadingAndSavingUtils.load_map(map_path):
    raise RuntimeError('Could not reload demonstration map')
placed = []
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.StaticMeshActor) and actor.get_actor_label().startswith('FabBridge_'):
        mesh = actor.static_mesh_component.static_mesh
        if mesh is None:
            raise RuntimeError('Mesh reference did not survive map reload')
        placed.append({'actor': actor.get_actor_label(), 'mesh': mesh.get_path_name()})
expected = sorted(p + '.' + p.rsplit('/', 1)[1] for p in mesh_paths)
if sorted(p['mesh'] for p in placed) != expected:
    raise RuntimeError('Reloaded actors do not match requested meshes')
report = {'success': True, 'map': map_path, 'actors': placed,
          'scope': 'Mesh loads, actor placement, level save and reload. No visual or gameplay assessment.'}
Path(os.environ['FAB_USAGE_REPORT']).write_text(json.dumps(report, indent=2), encoding='utf-8')
