"""Run inside a separate Blender process. Never touches a user's live scene."""
import json
import math
from pathlib import Path
import runpy
import sys
import traceback
import shlex
import urllib.parse
import bpy
import bmesh
sys.path.insert(0, str(Path(__file__).resolve().parent))


def load(path):
    path = Path(path)
    ext = path.suffix.lower()
    if ext == '.blend':
        bpy.ops.wm.open_mainfile(filepath=str(path), load_ui=False, use_scripts=False)
    elif ext == '.fbx': bpy.ops.import_scene.fbx(filepath=str(path))
    elif ext in ('.gltf', '.glb'): bpy.ops.import_scene.gltf(filepath=str(path))
    elif ext == '.obj': bpy.ops.wm.obj_import(filepath=str(path))
    else: raise ValueError('Unsupported source format: ' + ext)


def validate(source, rules):
    load(source)
    errors, warnings, rows, dependencies = [], [], [], [str(Path(source).resolve())]
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    if not meshes: errors.append('No mesh objects in the scene')
    total = 0
    graph = bpy.context.evaluated_depsgraph_get()
    for obj in meshes:
        evaluated = obj.evaluated_get(graph)
        mesh = evaluated.to_mesh()
        try:
            mesh.calc_loop_triangles()
            tris = len(mesh.loop_triangles); total += tris
            bm = bmesh.new(); bm.from_mesh(mesh)
            non_manifold = sum(not edge.is_manifold for edge in bm.edges)
            loose_vertices = sum(not vert.link_edges for vert in bm.verts)
            bm.free()
            dimensions = list(obj.dimensions)
            invalid = sum(not all(math.isfinite(float(v)) for v in vert.co) for vert in mesh.vertices)
            degenerate = sum(poly.area <= 1e-12 for poly in mesh.polygons)
            material_slots = len(obj.material_slots)
            row = {'name': obj.name, 'vertices': len(mesh.vertices), 'triangles': tris,
                   'dimensions_scene_units': dimensions, 'scale': list(obj.scale),
                   'uv_layers': len(mesh.uv_layers), 'material_slots': material_slots,
                   'non_manifold_edges': non_manifold, 'loose_vertices': loose_vertices,
                   'degenerate_faces': degenerate, 'invalid_vertices': invalid,
                   'armatures': [m.object.name for m in obj.modifiers if m.type == 'ARMATURE' and m.object]}
            rows.append(row)
            if invalid: errors.append(obj.name + ': non-finite vertex coordinates')
            if rules.get('require_uv', True) and not mesh.uv_layers: errors.append(obj.name + ': missing UVs')
            if rules.get('require_material', False) and (not material_slots or any(s.material is None for s in obj.material_slots)):
                errors.append(obj.name + ': missing material')
            if rules.get('require_manifold', False) and non_manifold: errors.append(obj.name + ': non-manifold edges')
            elif non_manifold: warnings.append(obj.name + ': open/non-manifold geometry; may be intentional')
            if degenerate: warnings.append(obj.name + ': degenerate faces')
            if loose_vertices: warnings.append(obj.name + ': loose vertices')
            if any(abs(s - 1) > 1e-5 for s in obj.scale): warnings.append(obj.name + ': unapplied object scale')
            if max(dimensions, default=0) <= 0: errors.append(obj.name + ': zero-sized geometry')
        finally: evaluated.to_mesh_clear()
    if rules.get('max_triangles') is not None and total > rules['max_triangles']:
        errors.append(f'Triangle budget exceeded: {total} > {rules["max_triangles"]}')
    cacheable = True
    for library in bpy.data.libraries:
        if library.filepath:
            path = Path(bpy.path.abspath(library.filepath)).resolve()
            if path.is_file(): dependencies.append(str(path))
            else: errors.append('Missing linked library: ' + str(path))
    source_path = Path(source)
    sidecars = []
    if source_path.suffix.lower() == '.gltf':
        document = json.loads(source_path.read_text(encoding='utf-8-sig'))
        sidecars = [x['uri'] for field in ('buffers', 'images') for x in document.get(field, [])
                    if x.get('uri') and not x['uri'].startswith('data:')]
        sidecars = [urllib.parse.unquote(name) for name in sidecars]
    elif source_path.suffix.lower() == '.obj':
        for line in source_path.read_text(errors='replace').splitlines():
            if line.strip().startswith('mtllib '): sidecars += shlex.split(line.strip()[7:])
    for name in sidecars:
        path = (source_path.parent / name).resolve()
        if path.is_file(): dependencies.append(str(path))
        else: errors.append('Missing asset sidecar: ' + str(path))
    for img in bpy.data.images:
        if img.source == 'FILE' and not img.packed_file and img.filepath:
            path = Path(bpy.path.abspath(img.filepath)).resolve()
            if path.is_file(): dependencies.append(str(path))
            else: errors.append('Missing texture: ' + str(path))
        elif img.source in ('TILED', 'SEQUENCE', 'MOVIE') and not img.packed_file:
            cacheable = False
            warnings.append('Dynamic/tiled image dependency: validation results will not be cached: ' + img.name)
    armatures = [{'name': o.name, 'bones': len(o.data.bones)} for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    return {'ok': not errors, 'source': source, 'blender_version': bpy.app.version_string,
            'triangles': total, 'objects': rows, 'armatures': armatures,
            'unit_system': bpy.context.scene.unit_settings.system,
            'unit_scale': bpy.context.scene.unit_settings.scale_length,
            'errors': errors, 'warnings': warnings, 'dependencies': sorted(set(dependencies)), 'cacheable': cacheable,
            'limits': 'Structural checks only; visual quality, collision suitability and deformation require task-specific review.'}


def main(request_path):
    request = json.loads(Path(request_path).read_text(encoding='utf-8'))
    output = Path(request['output']); output.mkdir(parents=True, exist_ok=True)
    # --factory-startup is independent from the live Blender. Remove only this fresh scene's objects.
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    try:
        if request['kind'] == 'character':
            import character_blender
            result = character_blender.run(request)
        elif request['kind'] == 'validate':
            result = validate(request['source'], request.get('rules', {}))
        else:
            if request.get('source'): load(request['source'])
            namespace = runpy.run_path(request['script'], init_globals={
                'OUTPUT_DIR': output, 'INPUT_FILE': request.get('source'),
                'PARAMETERS': request.get('parameters', {}), 'bpy': bpy})
            result = {'ok': True, 'result': namespace.get('result'), 'blender_version': bpy.app.version_string}
        Path(request['report']).write_text(json.dumps(result, indent=2), encoding='utf-8')
    except BaseException:
        Path(request['report']).write_text(json.dumps({'ok': False, 'error': traceback.format_exc()}), encoding='utf-8')
        raise


if __name__ == '__main__': main(sys.argv[sys.argv.index('--') + 1])
