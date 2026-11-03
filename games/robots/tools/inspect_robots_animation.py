#!/usr/bin/env python3
"""Inspect v248 EngineX character animation tables without guessing motion codecs.

Table and structure layout informed by eurotools/eurochef (MIT; see
docs/licenses/EUROCHEF-MIT.txt). All offsets refer to the complete EDB, not
the base-length-only archive extraction.
"""
import argparse
import hashlib
import json
import math
import re
import struct
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data = data

    def unpack(self, fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(self.data):
            raise ValueError(f'range outside EDB: 0x{offset:x} + 0x{size:x}')
        return struct.unpack_from(fmt, self.data, offset)

    def rel(self, offset, size=0):
        target = offset + self.unpack('<i', offset)[0]
        if target < 0 or target + size > len(self.data):
            raise ValueError(f'invalid relative pointer at 0x{offset:x}')
        return target

    def table(self, offset, stride):
        count, _, relative = self.unpack('<hhi', offset)
        if count < 0:
            raise ValueError('negative table count')
        address = offset + 4 + relative
        if count and (address < 0 or address + count * stride > len(self.data)):
            raise ValueError('table outside EDB')
        return [address + stride * i for i in range(count)]


def inspect(path, hashcodes=None):
    data = path.read_bytes()
    r = Reader(data)
    magic, asset_hash, version, _, _, full_size, base_size = r.unpack('<7I', 0)
    if magic != 0x47454f4d or version != 248 or full_size != len(data):
        raise ValueError('expected a complete PC v248 EDB (including trailing motion data)')
    skins = []
    for entry in r.table(0x60, 36):
        skin_hash, _, _, address = r.unpack('<IHHI', entry)
        obj_type, count = r.unpack('<II', address)
        if obj_type != 0x200 or not 0 < count <= 1024:
            raise ValueError('unexpected skin type or bone count')
        glob = r.rel(address + 0x40, count * 16)
        local = r.rel(address + 0x44, count * 16)
        hier = r.rel(address + 0x48, count * 8)
        bones = []
        max_error = 0.0
        for i in range(count):
            parent, end, flags, _ = r.unpack('<4H', hier + i*8)
            world = r.unpack('<4f', glob + i*16)
            rel = r.unpack('<4f', local + i*16)
            if not all(math.isfinite(v) for v in world + rel):
                raise ValueError('non-finite bone vector')
            if parent != 0xffff and parent >= i:
                raise ValueError('unexpected hierarchy link')
            if not i <= end < count:
                raise ValueError('unexpected hierarchy range')
            parent_pos = r.unpack('<4f', glob + parent*16) if parent != 0xffff else (0, 0, 0, 0)
            error = max(abs(world[j] - parent_pos[j] - rel[j]) for j in range(3))
            max_error = max(max_error, error)
            bones.append({'index': i, 'parent_candidate': None if parent == 0xffff else parent,
                          'subtree_end_candidate': end, 'flags': flags,
                          'absolute_vector': world, 'relative_vector': rel})
        ranges_consistent = True
        for bone in bones:
            index, end = bone['index'], bone['subtree_end_candidate']
            descendants = []
            for child in range(index + 1, count):
                parent = bones[child]['parent_candidate']
                while parent is not None and parent != index:
                    parent = bones[parent]['parent_candidate']
                if parent == index:
                    descendants.append(child)
            if descendants != list(range(index + 1, end + 1)):
                ranges_consistent = False
        skins.append({'hash': f'{skin_hash:08x}', 'address': address, 'bone_count': count,
                      'absolute_vectors_address': glob, 'relative_vectors_address': local,
                      'hierarchy_address': hier, 'max_parent_difference_error': max_error,
                      'subtree_ranges_consistent': ranges_consistent, 'bones': bones})
    animations = []
    for entry in r.table(0x58, 32):
        h, section, debug, address, _, motion_address, _, motion_size, skin = r.unpack('<IHH6I', entry)
        if address + 24 > len(data) or motion_address + motion_size > len(data):
            raise ValueError('animation points outside EDB')
        frame_count = r.unpack('<H', address + 14)[0]
        animations.append({'hash': f'{h:08x}', 'section': section, 'debug': debug,
                           'header_address': address, 'motion_address': motion_address,
                           'motion_size': motion_size, 'skin_reference_raw': f'{skin:08x}',
                           'frame_count_candidate': frame_count,
                           'header_bytes_12_23': data[address+12:address+24].hex()})
    names = {}
    if hashcodes:
        for name, value in re.findall(r'^\s*#define\s+(HT_\w+)\s+0x([0-9a-fA-F]{8})\b',
                                      hashcodes.read_text(encoding='ascii', errors='replace'), re.MULTILINE):
            names[int(value, 16)] = name
    modes = []
    for entry in r.table(0x78, 20):
        code, section, debug, address, _, count = r.unpack('<IHHIII', entry)
        if count and address >= len(data):
            raise ValueError('animation mode points outside EDB')
        modes.append({'hash': f'{code:08x}', 'name': names.get(code),
                      'section': section, 'debug': debug, 'address': address, 'entry_count_raw': count})
    return {'source': str(path), 'sha256': hashlib.sha256(data).hexdigest(),
            'asset_hash': f'{asset_hash:08x}', 'version': version, 'base_size': base_size,
            'full_size': full_size, 'skins': skins, 'animations': animations, 'animation_modes': modes,
            'note': 'Hierarchy links and position interpretation are inferred from the vectors; motion and weights are not decoded.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edb', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--hashcodes', type=Path, help='optional original game hashcodes.h for mode names')
    parser.add_argument('--skeleton-gltf', type=Path,
                        help='export first candidate hierarchy and translations; no skin weights or clips')
    args = parser.parse_args()
    inventory = inspect(args.edb, args.hashcodes)
    if args.skeleton_gltf:
        if not inventory['skins']:
            raise ValueError('no skeleton candidates')
        bones = inventory['skins'][0]['bones']
        nodes = []
        roots = []
        for bone in bones:
            x, y, z, _ = bone['relative_vector']
            nodes.append({'name': f'joint_{bone["index"]:02d}', 'translation': [-x, y, z]})
        for bone in bones:
            parent = bone['parent_candidate']
            if parent is None:
                roots.append(bone['index'])
            else:
                nodes[parent].setdefault('children', []).append(bone['index'])
        output = {'asset': {'version': '2.0', 'generator': 'The One Robots skeleton investigation'},
                  'scene': 0, 'scenes': [{'nodes': roots}], 'nodes': nodes,
                  'extras': {'provenance': inventory['sha256'], 'skin_hash': inventory['skins'][0]['hash'],
                             'limitations': 'Inferred translations and hierarchy only; no skin weights or animations.'}}
        args.skeleton_gltf.parent.mkdir(parents=True, exist_ok=True)
        args.skeleton_gltf.write_text(json.dumps(output, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    result = json.dumps(inventory, indent=2, allow_nan=False)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result + '\n', encoding='utf-8')
    else:
        print(result)


if __name__ == '__main__':
    main()
