"""Focused Unreal assertion test for the example pipeline's imported crate."""
import unreal

mesh = unreal.load_asset('/Game/SharedPipelineSmoke/SharedCrate')
assert isinstance(mesh, unreal.StaticMesh), 'Imported static mesh missing'
bounds = mesh.get_bounds()
assert abs(bounds.box_extent.x * 2 - 200) < 1, 'Expected two-metre FBX to import at 200 cm'
subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
assert world is not None, 'Failed to create transient test world'
actor = subsystem.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
assert actor is not None, 'Failed to spawn imported mesh'
try:
    assert actor.static_mesh_component.set_static_mesh(mesh)
    assert actor.static_mesh_component.static_mesh == mesh
finally:
    subsystem.destroy_actor(actor)
result = {'asset': mesh.get_path_name(), 'size_x_cm': bounds.box_extent.x * 2, 'spawn_and_reference': 'passed'}
