#!/usr/bin/env python3
"""Build a candidate skinned glTF from the local Robots PC Rodney EDB.

Requires Eurochef's full-body glTF export. Vertex index interpretation is based
on the shader's 3-register matrices and EDB palette consistency; pose playback
has not yet been compared against the original executable.
"""
import argparse
import base64
import json
import math
import struct
from pathlib import Path

from inspect_robots_animation import Reader, inspect


def add_buffer(gltf, payload, target=None):
    index = len(gltf['buffers'])
    gltf['buffers'].append({'byteLength': len(payload),
                            'uri': 'data:application/octet-stream;base64,' + base64.b64encode(payload).decode('ascii')})
    view = {'buffer': index, 'byteOffset': 0, 'byteLength': len(payload)}
    if target is not None:
        view['target'] = target
    result = len(gltf['bufferViews'])
    gltf['bufferViews'].append(view)
    return result


def rig(edb, model, output):
    inventory = inspect(edb)
    skin = next((s for s in inventory['skins'] if s['hash'] == '0d000001'), None)
    if not skin or not skin['subtree_ranges_consistent'] or skin['max_parent_difference_error'] > 1e-5:
        raise ValueError('expected validated Rodney base hierarchy')
    data = edb.read_bytes()
    reader = Reader(data)
    gltf = json.loads(model.read_text(encoding='utf-8'))
    if len(gltf['meshes']) != 1 or len(gltf['nodes']) != 1 or gltf.get('skins'):
        raise ValueError('expected an unskinned single-node Eurochef body')
    primitives = gltf['meshes'][0]['primitives']
    position_ids = list(dict.fromkeys(p['attributes']['POSITION'] for p in primitives))
    entries = reader.table(0x50, 20)
    base = skin['address']
    skin_entries = []
    for field in (0x68, 0x70):
        count = reader.unpack('<I', base + field)[0]
        if count > 256:
            raise ValueError('unreasonable skin entity count')
        start = reader.rel(base + field + 4, count*20)
        skin_entries.extend(start + i*20 for i in range(count))
    if len(position_ids) not in (1, len(skin_entries)):
        raise ValueError('Eurochef position streams do not match skin entities')
    parts, total_vertices = [], 0
    for group, (position_id, entry) in enumerate(zip(position_ids, skin_entries)):
        position_accessor = gltf['accessors'][position_id]
        vertex_count = position_accessor['count']
        view = gltf['bufferViews'][position_accessor['bufferView']]
        uri = gltf['buffers'][view['buffer']]['uri']
        if not uri.startswith('data:') or ';base64,' not in uri:
            raise ValueError('expected embedded Eurochef vertex buffer')
        position_data = base64.b64decode(uri.split(',', 1)[1], validate=True)
        position_start = view.get('byteOffset', 0) + position_accessor.get('byteOffset', 0)
        position_stride = view.get('byteStride', 12)
        palette_ptr = reader.rel(entry)
        part_count, _, entity_id, _ = reader.unpack('<IIIi', entry + 4)
        entity_index = entity_id & 0xffffff
        if entity_index >= len(entries) or not 0 < part_count <= 256:
            raise ValueError('invalid skin entity or part count')
        _, _, _, entity = reader.unpack('<IHHI', entries[entity_index])
        kind = reader.unpack('<I', entity)[0]
        if kind == 0x603:
            if part_count != reader.unpack('<I', entity + 0x54)[0]:
                raise ValueError('split mesh and skin part counts differ')
            mesh_parts = [reader.rel(entity + 0x5c + p*4, 0x84) for p in range(part_count)]
        elif kind == 0x601 and part_count == 1:
            mesh_parts = [entity]
        else:
            raise ValueError('unsupported skin mesh structure')
        joints, weights = bytearray(), bytearray()
        for part, mesh_addr in enumerate(mesh_parts):
            part_data = reader.rel(palette_ptr + part*4, 12)
            palette_count = reader.unpack('<I', part_data)[0]
            if not 0 < palette_count <= 29:
                raise ValueError('invalid bone palette size')
            palette_addr = reader.rel(part_data + 4, palette_count)
            palette = data[palette_addr:palette_addr + palette_count]
            if any(j >= skin['bone_count'] for j in palette) or reader.unpack('<I', mesh_addr)[0] != 0x601:
                raise ValueError('invalid bone palette or mesh')
            count = reader.unpack('<I', mesh_addr + 0x78)[0]
            source_vertices = reader.rel(mesh_addr + 0x5c, count * 32)
            vertex_data = reader.rel(part_data + 8, count * 20)
            multi = 0
            for i in range(count):
                src = reader.unpack('<3f', source_vertices + i*32)
                pos_offset = position_start + (len(joints)//4) * position_stride
                if pos_offset + 12 > len(position_data):
                    raise ValueError('position accessor exceeds buffer')
                target = struct.unpack_from('<3f', position_data, pos_offset)
                if any(abs(a-b)>1e-5 for a,b in zip((-src[0],src[1],src[2]), target)):
                    raise ValueError('Eurochef vertex order does not match EDB skin records')
                offset = vertex_data + i * 20
                ids = data[offset:offset+4]
                ws = reader.unpack('<4f', offset + 4)
                if not all(math.isfinite(w) and 0 <= w <= 1 for w in ws) or abs(sum(ws)-1) > 1e-5:
                    raise ValueError('invalid vertex weights')
                mapped = []
                for local, weight in zip(ids, ws):
                    if weight > 1e-7 and (local % 3 or local // 3 >= palette_count):
                        raise ValueError('active influence outside part palette')
                    mapped.append(palette[local // 3] if local % 3 == 0 and local // 3 < palette_count else 0)
                joints.extend(mapped)
                weights.extend(struct.pack('<4f', *ws))
                multi += sum(w > 1e-7 for w in ws) > 1
            parts.append({'group': group, 'part': part, 'vertices': count, 'palette': list(palette), 'multi_weight_vertices': multi})
        if len(joints) != vertex_count * 4:
            raise ValueError(f'weights cover {len(joints)//4} vertices, model group has {vertex_count}')
        total_vertices += vertex_count
        joint_view = add_buffer(gltf, joints, 34962)
        weight_view = add_buffer(gltf, weights, 34962)
        joint_accessor = len(gltf['accessors'])
        gltf['accessors'].append({'bufferView': joint_view, 'componentType': 5121, 'count': vertex_count, 'type': 'VEC4'})
        weight_accessor = len(gltf['accessors'])
        gltf['accessors'].append({'bufferView': weight_view, 'componentType': 5126, 'count': vertex_count, 'type': 'VEC4'})
        for primitive in primitives:
            if primitive['attributes']['POSITION'] == position_id:
                primitive['attributes']['JOINTS_0'] = joint_accessor
                primitive['attributes']['WEIGHTS_0'] = weight_accessor
    bones = skin['bones']
    first_node = len(gltf['nodes'])
    for bone in bones:
        x, y, z, _ = bone['relative_vector']
        gltf['nodes'].append({'name': f'joint_{bone["index"]:02d}', 'translation': [-x, y, z]})
    roots = []
    for bone in bones:
        node = first_node + bone['index']
        parent = bone['parent_candidate']
        if parent is None:
            roots.append(node)
        else:
            gltf['nodes'][first_node + parent].setdefault('children', []).append(node)
    inverse = bytearray()
    for bone in bones:
        x, y, z, _ = bone['absolute_vector']
        inverse.extend(struct.pack('<16f', 1,0,0,0, 0,1,0,0, 0,0,1,0, x,-y,-z,1))
    ibm_view = add_buffer(gltf, inverse)
    ibm_accessor = len(gltf['accessors'])
    gltf['accessors'].append({'bufferView': ibm_view, 'componentType': 5126, 'count': len(bones), 'type': 'MAT4'})
    gltf['skins'] = [{'inverseBindMatrices': ibm_accessor, 'joints': list(range(first_node, first_node+len(bones))),
                      'skeleton': roots[0]}]
    gltf['nodes'][0]['skin'] = 0
    gltf['scenes'][gltf.get('scene', 0)]['nodes'].extend(roots)
    gltf.setdefault('extras', {})['robots_rig'] = {'source_sha256': inventory['sha256'], 'skin': skin['hash'],
        'status': 'candidate weights and bind translations; original motion not yet decoded', 'parts': parts}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as f:
        json.dump(gltf, f, separators=(',', ':'), allow_nan=False)
    return {'vertices': total_vertices, 'groups': len(position_ids), 'parts': len(parts), 'joints': len(bones),
            'multi_weight_vertices': sum(p['multi_weight_vertices'] for p in parts)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('edb', type=Path)
    p.add_argument('model', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    print(json.dumps(rig(args.edb, args.model, args.output), indent=2))


if __name__ == '__main__':
    main()
