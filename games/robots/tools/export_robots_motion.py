#!/usr/bin/env python3
"""Attach sampled candidate v248 clips to the local Rodney candidate rig.

Clip identities, runtime state selection, coordinate/bind conventions, and
some malformed or incompletely understood streams remain unverified. A clip
is exported only if every requested integral frame passes bounded decoding.
"""
import argparse
import base64
import hashlib
import json
import math
import struct
from pathlib import Path

from robots_motion import MotionError, sample, chunk_context


def append_accessor(gltf, values, kind, count, minimum=None, maximum=None):
    data = struct.pack('<' + 'f'*len(values), *values)
    b = len(gltf['buffers'])
    gltf['buffers'].append({'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,'+base64.b64encode(data).decode('ascii')})
    view = len(gltf['bufferViews'])
    gltf['bufferViews'].append({'buffer': b, 'byteOffset': 0, 'byteLength': len(data)})
    a = {'bufferView': view, 'componentType': 5126, 'count': count, 'type': kind}
    if minimum is not None:
        a['min'], a['max'] = [minimum], [maximum]
    index = len(gltf['accessors'])
    gltf['accessors'].append(a)
    return index


def in_place_position(position, root_bind):
    """Keep vertical animation while the engine controls horizontal movement."""
    return (root_bind[0], position[1], root_bind[2])


def export(edb, inventory_path, rig_path, output, indices, native_exe=None, in_place=False):
    data = edb.read_bytes()
    inventory = json.loads(inventory_path.read_text())
    gltf = json.loads(rig_path.read_text())
    digest = hashlib.sha256(data).hexdigest()
    if inventory['sha256'] != digest or gltf.get('extras', {}).get('robots_rig', {}).get('source_sha256') != digest:
        raise ValueError('EDB, motion inventory, and rig source hashes must match')
    native = None
    if native_exe:
        from robots_native_sampler import NativeSampler
        native = NativeSampler(native_exe, data)
    if len(gltf.get('skins', [])) != 1 or len(gltf['skins'][0]['joints']) != 29:
        raise ValueError('expected a 29-joint candidate rig')
    joints = gltf['skins'][0]['joints']
    gltf['animations'] = []
    exported, skipped = [], []
    for index in indices:
        if not 0 <= index < len(inventory['animations']):
            raise ValueError(f'clip index {index} out of range')
        record = inventory['animations'][index]
        address = record['header_address']
        header = data[address:address+160]
        frames, fps = record['frame_count_candidate'], header[12]
        if header[20] != len(joints) or not 1 <= fps <= 240:
            skipped.append({'index': index, 'reason': 'unsupported track count or candidate sample rate'})
            continue
        try:
            poses = []
            for i in range(frames):
                chunk = chunk_context(data, record, i)
                chunk_data = data[chunk['offset']:chunk['offset']+chunk['size']]
                safe = sample(header, chunk_data, i, chunk['start'], chunk['length'])
                poses.append(native.sample(record, i, safe, chunk) if native else safe)
            if any(not all(math.isfinite(x) for x in p['rotation']) or
                   abs(sum(x*x for x in p['rotation']) - 1) > .02
                   for pose in poses for p in pose):
                raise MotionError('invalid sampled quaternion')
        except (MotionError, ValueError) as e:
            skipped.append({'index': index, 'reason': str(e)})
            continue
        root_offset = struct.unpack_from('<3f', header, 140)
        # These two clips are used as looping locomotion states in the
        # prototype. Interpolate the last sample back to the first instead of
        # snapping at the loop boundary. Other clips retain their authored end.
        loop_seam = in_place and index in (0, 123) and frames > 1
        if loop_seam:
            poses.append(poses[0])
        key_count = len(poses)
        times = [i / fps for i in range(key_count)]
        t = append_accessor(gltf, times, 'SCALAR', key_count, 0, times[-1])
        animation = {'name': f'original_{record["hash"]}', 'samplers': [], 'channels': []}
        for track, node in enumerate(joints):
            rotations = []
            for pose in poses:
                x,y,z,w = pose[track]['rotation']
                rotations.extend([x,-y,-z,w])
            r = append_accessor(gltf, rotations, 'VEC4', key_count)
            sampler_index = len(animation['samplers'])
            animation['samplers'].append({'input': t, 'output': r, 'interpolation': 'LINEAR'})
            animation['channels'].append({'sampler': sampler_index, 'target': {'node': node, 'path': 'rotation'}})
            if poses[0][track]['translation'] is not None:
                positions = []
                for pose in poses:
                    x,y,z = pose[track]['translation']
                    if track == 0:
                        x,y,z = x+root_offset[0],y+root_offset[1],z+root_offset[2]
                    value = (-x,y,z)
                    if track == 0 and in_place:
                        value = in_place_position(value, gltf['nodes'][node].get('translation', [0,0,0]))
                    positions.extend(value)
                p = append_accessor(gltf, positions, 'VEC3', key_count)
                sampler_index = len(animation['samplers'])
                animation['samplers'].append({'input': t, 'output': p, 'interpolation': 'LINEAR'})
                animation['channels'].append({'sampler': sampler_index, 'target': {'node': node, 'path': 'translation'}})
        gltf['animations'].append(animation)
        exported.append({'index': index, 'hash': record['hash'], 'frames': frames, 'fps_candidate': fps, 'loop_seam': loop_seam})
    if not exported:
        raise ValueError(f'no clips could be exported: {skipped}')
    gltf.setdefault('extras', {})['robots_motion'] = {'exported': exported, 'skipped': skipped,
        'sampler': 'isolated original x86 decoder' if native else 'experimental Python decoder',
        'in_place': in_place,
        'status': 'sampled candidate; mode mapping and original pose composition not validated'}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as f:
        json.dump(gltf, f, separators=(',', ':'), allow_nan=False)
    return {'exported': exported, 'skipped': skipped}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('edb', type=Path)
    p.add_argument('inventory', type=Path)
    p.add_argument('rig', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--native-exe', type=Path, help='use bounded emulation of the supplied original decoder for sample values')
    p.add_argument('--in-place', action='store_true', help='remove root horizontal motion for physics-driven characters')
    selection = p.add_mutually_exclusive_group(required=True)
    selection.add_argument('--clip', type=int, action='append', help='zero-based animation table index; repeatable')
    selection.add_argument('--all', action='store_true', help='try every indexed clip; skip unsafe streams')
    args = p.parse_args()
    indices = range(len(json.loads(args.inventory.read_text())['animations'])) if args.all else args.clip
    print(json.dumps(export(args.edb, args.inventory, args.rig, args.output, indices, args.native_exe, args.in_place), indent=2))


if __name__ == '__main__':
    main()
