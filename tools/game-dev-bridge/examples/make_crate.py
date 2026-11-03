"""Example blender_run job: UV-mapped two-metre crate with a material and FBX export."""
from pathlib import Path
import bpy

size = float(PARAMETERS.get('size_metres', 2.0))
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = 1.0
bpy.ops.mesh.primitive_cube_add(size=size, location=(0, 0, size / 2))
obj = bpy.context.object; obj.name = 'SM_SharedCrate'
material = bpy.data.materials.new('M_SharedCrate'); material.use_nodes = True
node = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
node.inputs['Base Color'].default_value = (0.24, 0.1, 0.035, 1)
node.inputs['Roughness'].default_value = .75
obj.data.materials.append(material)
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT_DIR / 'SharedCrate.blend'))
bpy.ops.export_scene.fbx(filepath=str(OUTPUT_DIR / 'SharedCrate.fbx'), use_selection=True,
                         object_types={'MESH'}, apply_unit_scale=True, axis_forward='-Y', axis_up='Z',
                         bake_anim=False, add_leaf_bones=False)
result = {'blend': str(OUTPUT_DIR / 'SharedCrate.blend'), 'fbx': str(OUTPUT_DIR / 'SharedCrate.fbx'),
          'size_metres': size, 'expected_triangles': 12}
