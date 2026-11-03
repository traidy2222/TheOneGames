"""Small deterministic deformation fixture, executed by blender_run."""
import json
import math
from pathlib import Path
import bpy

output = Path(OUTPUT_DIR)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
vertices = [(0.2*math.cos(i*math.tau/8), 0.2*math.sin(i*math.tau/8), z)
            for z in (0, .5, 1, 1.5, 2) for i in range(8)]
faces = [tuple(reversed(range(8))), tuple(range(32,40))]
faces += [(r*8+i, r*8+(i+1)%8, (r+1)*8+(i+1)%8, (r+1)*8+i) for r in range(4) for i in range(8)]
mesh = bpy.data.meshes.new('Body'); mesh.from_pydata(vertices, [], faces); mesh.update()
body = bpy.data.objects.new('Body', mesh); bpy.context.collection.objects.link(body)
bpy.context.view_layer.objects.active = body; body.select_set(True)
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.uv.smart_project(); bpy.ops.object.mode_set(mode='OBJECT')
material = bpy.data.materials.new('FixtureMaterial'); material.diffuse_color = (.12,.35,.8,1); body.data.materials.append(material)
bpy.context.scene.unit_settings.system = 'METRIC'; bpy.context.scene.unit_settings.scale_length = 1.0
bpy.ops.wm.save_as_mainfile(filepath=str(output/'character_source.blend'))
weights = {}
for i, (_,_,z) in enumerate(vertices):
    upper = min(1, max(0, (z-.5)))
    weights[str(i)] = {'lower': 1-upper, 'upper': upper}
rig = {'armature':'CharacterRig', 'meshes':['Body'], 'binding':'weights', 'max_influences':4,
       'bones':[{'name':'root','head':[0,0,0],'tail':[0,0,.2]},
                {'name':'lower','parent':'root','head':[0,0,0],'tail':[0,0,1]},
                {'name':'upper','parent':'lower','head':[0,0,1],'tail':[0,0,2]}],
       'weights': {'Body':weights}}
clips = {'armature':'CharacterRig','fps':30,'clips':[
    {'name':'Bend','start':1,'end':31,'tracks':{'upper':[
        {'frame':1,'rotation':[0,0,0]}, {'frame':16,'rotation':[.6,0,0]}, {'frame':31,'rotation':[0,0,0]}]}},
    {'name':'Step','start':1,'end':31,'tracks':{'root':[
        {'frame':1,'location':[0,0,0]}, {'frame':31,'location':[.5,0,0]}]}}]}
specs = {'rig.json':rig, 'clips.json':clips,
         'validate.json':{'actions':['Bend','Step'], 'rules':{'require_animation':True,'require_motion':True}},
         'export.json':{'actions':['Bend','Step'], 'root_motion':'preserve'}}
for filename, spec in specs.items(): (output/filename).write_text(json.dumps(spec,indent=2),encoding='utf-8')
result = {'source':str(output/'character_source.blend'), 'specs':{n:str(output/n) for n in specs}}
