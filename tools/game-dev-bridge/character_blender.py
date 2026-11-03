"""Character operations in isolated Blender; explicit specifications, no live UI."""
import json
import math
from pathlib import Path
import bpy
from mathutils import Vector, Matrix
from character_specs import validate_spec, normalized_weights, name


def rig_object(spec):
    objects = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    if spec.get('armature'): objects = [o for o in objects if o.name == spec['armature']]
    if len(objects) != 1: raise ValueError('Select exactly one armature by name; found ' + str([o.name for o in objects]))
    return objects[0]


def bound_meshes(arm):
    return [o for o in bpy.context.scene.objects if o.type == 'MESH' and
            any(m.type == 'ARMATURE' and m.object == arm for m in o.modifiers)]


def select(objects, active):
    if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects: obj.hide_set(False); obj.select_set(True)
    bpy.context.view_layer.objects.active = active


def assign_action(arm, action):
    arm.animation_data_create(); arm.animation_data.action = action
    if action and hasattr(action, 'slots') and len(action.slots):
        if len(action.slots) != 1: raise ValueError('Multi-object action needs an explicit per-character bake: ' + action.name)
        arm.animation_data.action_slot = action.slots[0]


def actions_for(arm, spec):
    if 'actions' in spec:
        result = []
        for n in spec['actions']:
            action = bpy.data.actions.get(n)
            if action is None: raise ValueError('Missing action: ' + n)
            result.append(action)
        return result
    if arm.animation_data and arm.animation_data.action: return [arm.animation_data.action]
    return []


def rest_pose(arm):
    for b in arm.pose.bones:
        b.location = (0, 0, 0); b.rotation_euler = (0, 0, 0)
        b.rotation_quaternion = (1, 0, 0, 0); b.rotation_axis_angle = (0, 0, 1, 0); b.scale = (1, 1, 1)


def weights_report(arm, meshes, maximum):
    deform = {b.name for b in arm.data.bones if b.use_deform}
    rows = []
    for obj in meshes:
        groups = {g.index: g.name for g in obj.vertex_groups}
        unweighted, unnormalized, over, invalid = [], [], [], []
        for vertex in obj.data.vertices:
            raw = [g.weight for g in vertex.groups if groups.get(g.group) in deform]
            if any(not math.isfinite(w) or w < 0 for w in raw): invalid.append(vertex.index)
            values = [w for w in raw if w > 0]
            if not values: unweighted.append(vertex.index)
            elif abs(sum(values) - 1) > 1e-3: unnormalized.append(vertex.index)
            if len(values) > maximum: over.append(vertex.index)
        rows.append({'mesh': obj.name, 'vertices': len(obj.data.vertices),
                     'unweighted': len(unweighted), 'unnormalized': len(unnormalized),
                     'over_influence_budget': len(over), 'invalid_weights': len(invalid),
                     'problem_vertices_sample': sorted(set(unweighted + unnormalized + over + invalid))[:25]})
    return rows


def create_rig(spec, output):
    if bpy.data.objects.get(spec.get('armature', 'CharacterRig')): raise ValueError('Rig name already exists; existing rigs are not replaced')
    meshes = []
    for n in spec['meshes']:
        obj = bpy.data.objects.get(n)
        if obj is None or obj.type != 'MESH': raise ValueError('Missing mesh: ' + n)
        if any(m.type == 'ARMATURE' for m in obj.modifiers): raise ValueError('Mesh is already rigged; refusing to replace its binding')
        meshes.append(obj)
    data = bpy.data.armatures.new(spec.get('armature', 'CharacterRig'))
    arm = bpy.data.objects.new(data.name, data); bpy.context.collection.objects.link(arm)
    select([arm], arm); bpy.ops.object.mode_set(mode='EDIT')
    for entry in spec['bones']:
        bone = data.edit_bones.new(entry['name']); bone.head = entry['head']; bone.tail = entry['tail']
        bone.roll = entry.get('roll', 0); bone.use_deform = entry.get('deform', True)
    for entry in spec['bones']:
        if entry.get('parent'): data.edit_bones[entry['name']].parent = data.edit_bones[entry['parent']]
    bpy.ops.object.mode_set(mode='OBJECT')
    for bone in arm.pose.bones: bone.rotation_mode = 'XYZ'
    maximum = spec.get('max_influences', 4)
    deform = {b.name for b in data.bones if b.use_deform}
    for obj in meshes:
        for group in list(obj.vertex_groups):
            if group.name in deform: obj.vertex_groups.remove(group)
    if spec.get('binding', 'automatic') == 'automatic':
        select(meshes + [arm], arm); bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    else:
        for obj in meshes:
            mod = obj.modifiers.new('CharacterSkin', 'ARMATURE'); mod.object = arm
            world = obj.matrix_world.copy(); obj.parent = arm; obj.matrix_world = world
            supplied = spec.get('weights', {}).get(obj.name, {})
            if set(supplied) != {str(v.index) for v in obj.data.vertices}: raise ValueError('Supply weights for every vertex: ' + obj.name)
            for v in obj.data.vertices:
                for bone, weight in normalized_weights(supplied[str(v.index)], deform, maximum).items():
                    group = obj.vertex_groups.get(bone) or obj.vertex_groups.new(name=bone)
                    group.add([v.index], weight, 'REPLACE')
    # Normalize and limit actual deformation groups only; never assign an arbitrary fallback bone.
    for obj in meshes:
        groups = {g.index: g.name for g in obj.vertex_groups}
        for vertex in obj.data.vertices:
            current = {groups[g.group]: g.weight for g in vertex.groups if groups[g.group] in deform and g.weight > 0}
            normalized = normalized_weights(current, deform, maximum)
            for group in obj.vertex_groups:
                if group.name in deform: group.remove([vertex.index])
            for bone, weight in normalized.items(): obj.vertex_groups[bone].add([vertex.index], weight, 'REPLACE')
    report = weights_report(arm, meshes, maximum)
    path = output / 'character_rigged.blend'; bpy.ops.wm.save_as_mainfile(filepath=str(path))
    return {'ok': True, 'blend': str(path), 'armature': arm.name, 'bones': len(data.bones), 'weights': report}


def make_clips(spec, output):
    arm = rig_object(spec)
    if arm.animation_data and arm.animation_data.nla_tracks: raise ValueError('Bake or isolate NLA tracks before authoring clips')
    for clip in spec['clips']:
        for bone_name, keys in clip['tracks'].items():
            bone = arm.pose.bones.get(bone_name)
            if bone and any('rotation' in key for key in keys) and bone.rotation_mode != 'XYZ':
                # Rotation modes belong to the rig, not each Action. Do not break existing quaternion clips.
                raise ValueError('Rotation authoring requires XYZ bone mode; explicitly convert/bake existing actions first: ' + bone_name)
    for clip in spec['clips']:
        if bpy.data.actions.get(clip['name']): raise ValueError('Action already exists; refusing to overwrite: ' + clip['name'])
        for bone in clip['tracks']:
            if bone not in arm.pose.bones: raise ValueError('Unknown animation bone: ' + bone)
    bpy.context.scene.render.fps = spec.get('fps', 30); bpy.context.scene.render.fps_base = 1
    created = []
    for clip in spec['clips']:
        assign_action(arm, None); rest_pose(arm)
        action = bpy.data.actions.new(clip['name']); action.use_fake_user = True
        assign_action(arm, action)
        for bone_name, keys in clip['tracks'].items():
            bone = arm.pose.bones[bone_name]
            for key in keys:
                for field, prop in [('location', 'location'), ('rotation', 'rotation_euler'), ('scale', 'scale')]:
                    if field in key:
                        setattr(bone, prop, key[field]); bone.keyframe_insert(data_path=prop, frame=key['frame'], group=bone_name)
        action.use_frame_range = True; action.frame_start = clip.get('start', 1); action.frame_end = clip['end']
        if hasattr(action, 'layers') and action.layers:
            curves = [fc for layer in action.layers for strip in layer.strips
                      for fc in strip.channelbag(arm.animation_data.action_slot).fcurves]
        else: curves = list(action.fcurves)
        for curve in curves:
            for key in curve.keyframe_points: key.interpolation = 'LINEAR'
        created.append(action)
    assign_action(arm, created[0]); bpy.context.scene.frame_set(int(created[0].frame_range[0]))
    path = output / 'character_animated.blend'; bpy.ops.wm.save_as_mainfile(filepath=str(path))
    return {'ok': True, 'blend': str(path), 'armature': arm.name, 'fps': bpy.context.scene.render.fps,
            'actions': [{'name': a.name, 'frames': list(a.frame_range)} for a in created]}


def evaluated_positions(meshes):
    graph = bpy.context.evaluated_depsgraph_get(); positions = []
    for obj in meshes:
        evaluated = obj.evaluated_get(graph); mesh = evaluated.to_mesh()
        try: positions.extend(tuple(evaluated.matrix_world @ v.co) for v in mesh.vertices)
        finally: evaluated.to_mesh_clear()
    return positions


def inspect_character(spec, sample=False):
    arm = rig_object(spec); meshes = bound_meshes(arm)
    roots = [b.name for b in arm.data.bones if b.parent is None]
    rules = spec.get('rules', {}); errors, warnings = [], []
    if len(roots) != 1: errors.append('Expected exactly one skeleton root')
    if not meshes: errors.append('No meshes bound to this armature')
    rows = weights_report(arm, meshes, rules.get('max_influences', 4))
    for row in rows:
        if any(row[k] for k in ('unweighted', 'unnormalized', 'over_influence_budget', 'invalid_weights')):
            errors.append('Skin weight problems on ' + row['mesh'])
    if any(abs(s - 1) > 1e-5 for s in arm.scale): warnings.append('Armature scale is not applied; verify engine scale before reuse')
    actions = actions_for(arm, spec)
    if rules.get('require_animation') and not actions: errors.append('No selected animation clips')
    if arm.animation_data and arm.animation_data.nla_tracks: errors.append('Active NLA requires an explicit bake/isolation before this pipeline')
    clips = []
    for action in actions:
        row = {'name': action.name, 'frames': list(action.frame_range), 'slots': len(action.slots) if hasattr(action, 'slots') else 0}
        if sample and len(roots) == 1:
            assign_action(arm, None); rest_pose(arm); assign_action(arm, action)
            start, end = action.frame_range
            n = rules.get('sample_frames', 5)
            frames = sorted(set(round(start + (end-start) * i/(n-1), 4) for i in range(n)))
            baseline, max_motion, first_root, last_root, root_motion = None, 0.0, None, None, 0.0
            for frame in frames:
                whole = math.floor(frame); bpy.context.scene.frame_set(whole, subframe=frame-whole)
                bpy.context.view_layer.update()
                positions = evaluated_positions(meshes)
                if any(not math.isfinite(v) for point in positions for v in point): errors.append(action.name + ': non-finite deformed geometry')
                if baseline is None: baseline = positions
                elif len(positions) != len(baseline): errors.append(action.name + ': animated topology changes')
                else: max_motion = max(max_motion, max(((Vector(a)-Vector(b)).length for a,b in zip(positions,baseline)), default=0))
                last_root = arm.matrix_world @ arm.pose.bones[roots[0]].head
                if first_root is None: first_root = last_root.copy()
                root_motion = max(root_motion, (last_root-first_root).length)
            row.update(sampled_frames=frames, max_vertex_displacement=max_motion, root_delta=list(last_root-first_root), max_root_displacement=root_motion)
            if rules.get('require_motion') and max_motion < 1e-6: errors.append(action.name + ': no sampled skin deformation')
        clips.append(row)
    dependencies = []
    for lib in bpy.data.libraries:
        path = Path(bpy.path.abspath(lib.filepath)).resolve()
        if path.is_file(): dependencies.append(str(path))
        else: errors.append('Missing linked library: ' + str(path))
    return {'ok': not errors, 'armature': arm.name, 'roots': roots, 'meshes': rows,
            'bones': [{'name': b.name, 'parent': b.parent.name if b.parent else None, 'deform': b.use_deform,
                       'head': list(b.head_local), 'tail': list(b.tail_local)} for b in arm.data.bones],
            'available_actions': [a.name for a in bpy.data.actions], 'clips': clips, 'fps': bpy.context.scene.render.fps,
            'errors': errors, 'warnings': warnings, 'dependencies': dependencies,
            'limits': 'Structural/finite deformation samples, not a visual quality or gameplay assessment.'}


def export_character(spec, output):
    arm = rig_object(spec); meshes = bound_meshes(arm)
    if not meshes: raise ValueError('No bound character meshes')
    if len([b for b in arm.data.bones if b.parent is None]) != 1: raise ValueError('One root bone is required for skeletal FBX')
    if any(m.type != 'ARMATURE' and m.show_viewport for o in meshes for m in o.modifiers):
        raise ValueError('Bake non-armature mesh modifiers explicitly before exporting a deform rig')
    if arm.animation_data and arm.animation_data.nla_tracks: raise ValueError('Isolate/bake NLA tracks before export')
    actions = actions_for(arm, spec)
    # Export centimetre coordinates with unit object scales. Leaving metres to the FBX
    # importer produces a scale-100 root in Unreal even when the visible mesh size is right.
    factor = bpy.context.scene.unit_settings.scale_length * 100.0
    if any(abs(s-1) > 1e-5 for s in arm.scale):
        raise ValueError('Apply/bake armature object scale explicitly before skeletal export')
    if arm.parent or any(o.parent not in (None, arm) for o in meshes):
        raise ValueError('Bake external parent transforms before character export')
    if abs(factor-1) > 1e-6:
        if any(b.constraints for b in arm.pose.bones) or arm.constraints or any(o.constraints for o in meshes):
            raise ValueError('Bake constrained motion before converting export coordinates to centimetres')
        if any(o.animation_data and o.animation_data.drivers for o in meshes + [arm]):
            raise ValueError('Bake animation drivers before centimetre export')
        scaled = set()
        for obj in [arm] + meshes:
            if obj.data not in scaled:
                if obj.type == 'MESH': obj.data.transform(Matrix.Scale(factor,4), shape_keys=True)
                else: obj.data.transform(Matrix.Scale(factor,4))
                scaled.add(obj.data)
            obj.location *= factor
            inverse = obj.matrix_parent_inverse.copy(); inverse.translation *= factor; obj.matrix_parent_inverse = inverse
        for action in actions:
            if hasattr(action, 'layers') and action.layers:
                if len(action.slots) != 1: raise ValueError('Bake multi-object actions before export')
                curves = [fc for layer in action.layers for strip in layer.strips for fc in strip.channelbag(action.slots[0]).fcurves]
            else: curves = list(action.fcurves)
            for curve in curves:
                if curve.data_path == 'location' or curve.data_path.endswith('.location'):
                    if curve.modifiers: raise ValueError('Bake location curve modifiers before centimetre export')
                    for key in curve.keyframe_points:
                        key.co.y *= factor; key.handle_left.y *= factor; key.handle_right.y *= factor
        bpy.context.scene.unit_settings.scale_length = .01
        bpy.context.view_layer.update()
    if spec.get('root_motion') == 'require_in_place':
        root = next(b.name for b in arm.data.bones if b.parent is None)
        for action in actions:
            assign_action(arm, None); rest_pose(arm); assign_action(arm, action)
            first = None
            for frame in range(math.floor(action.frame_range[0]), math.ceil(action.frame_range[1]) + 1):
                bpy.context.scene.frame_set(frame); bpy.context.view_layer.update()
                matrix = arm.matrix_world @ arm.pose.bones[root].matrix
                if first is None: first = matrix.copy()
                if any(abs(matrix[i][j] - first[i][j]) > 1e-5 for i in range(4) for j in range(4)):
                    raise ValueError('Selected clip has root movement; root motion is never silently removed')
    if bpy.data.objects.get('Armature') not in (None, arm): raise ValueError('Another object owns export name Armature')
    original_name = arm.name; arm.name = 'Armature'
    select(meshes + [arm], arm)
    settings = dict(use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                    use_armature_deform_only=False, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
                    bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                    bake_anim_simplify_factor=0.0, path_mode='COPY', embed_textures=True)
    assign_action(arm, None); rest_pose(arm); arm.data.pose_position = 'REST'
    mesh_path = output / 'Character.fbx'
    bpy.ops.export_scene.fbx(filepath=str(mesh_path), bake_anim=False, **settings)
    arm.data.pose_position = 'POSE'; exports = []
    for action in actions:
        name(action.name)
        assign_action(arm, None); rest_pose(arm); assign_action(arm, action)
        start, end = action.frame_range
        bpy.context.scene.frame_start = math.floor(start); bpy.context.scene.frame_end = math.ceil(end)
        bpy.context.scene.frame_set(math.floor(start))
        path = output / (action.name + '.fbx')
        if path == mesh_path: raise ValueError('Action name Character conflicts with mesh export')
        bpy.ops.export_scene.fbx(filepath=str(path), bake_anim=True, **settings)
        exports.append({'action': action.name, 'fbx': str(path), 'frames': [start,end],
                        'duration_seconds': (end-start)/(bpy.context.scene.render.fps/bpy.context.scene.render.fps_base)})
    manifest = {'ok': True, 'armature': original_name, 'mesh_fbx': str(mesh_path), 'clips': exports,
                'export_unit': 'centimetres', 'coordinate_scale_applied': factor,
                'root_motion': spec.get('root_motion', 'preserve'), 'leaf_bones_added': False,
                'skeleton_bones': [b.name for b in arm.data.bones]}
    (output / 'character-manifest.json').write_text(json.dumps(manifest,indent=2), encoding='utf-8')
    return manifest


def run(request):
    from blender_runner import load
    spec = json.loads(Path(request['spec_file']).read_text(encoding='utf-8')) if request.get('spec_file') else {}
    operation = request['operation']; validate_spec(operation, spec)
    load(request['source']); output = Path(request['output'])
    if operation == 'rig': return create_rig(spec, output)
    if operation == 'animate': return make_clips(spec, output)
    if operation == 'export': return export_character(spec, output)
    report = inspect_character(spec, sample=operation == 'validate')
    if operation == 'inspect': report['ok'] = True  # Inspection reports problems; validation enforces them.
    return report
