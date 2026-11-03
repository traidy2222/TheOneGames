#!/usr/bin/env python3
"""Trace candidate Rodney mode -> set -> script -> motion references.

These are raw local-index links, not a recreation of script playback or blend
rules. The script command scanner only reports 0x83-prefixed local references.
"""
import argparse
import json
from pathlib import Path
from inspect_robots_animation import Reader, inspect


def trace(path, hashcodes):
    inventory = inspect(path, hashcodes)
    data = path.read_bytes()
    r = Reader(data)
    modes, sets, scripts = r.table(0x78, 20), r.table(0x80, 20), r.table(0x68, 16)
    default = next((m for m in inventory['animation_modes'] if m['hash'] == '09000002'), None)
    if not default:
        raise ValueError('default animation mode not found')
    base, count = default['address'], default['entry_count_raw']
    r.unpack(f'<{count*2}I', base)
    links = []
    for i in range(count):
        entry = base + i*8
        mode_index = r.unpack('<I', entry)[0]
        if mode_index >= len(modes):
            raise ValueError('mode index outside table')
        group = r.rel(entry+4, 8)
        group_count = r.unpack('<I', group)[0]
        records = r.rel(group+4, group_count*8)
        if group_count > len(sets):
            raise ValueError('unreasonable mode group count')
        for k in range(group_count):
            file_ref, set_index = r.unpack('<II', records+k*8)
            if set_index >= len(sets):
                raise ValueError('animation set index outside table')
            set_address = r.unpack('<I', sets[set_index]+8)[0]
            set_count = r.unpack('<I', sets[set_index]+16)[0]
            if set_count > 1024:
                raise ValueError('unreasonable animation set size')
            script_links = []
            for n in range(set_count):
                script_ref = r.unpack('<I', set_address+n*20+8)[0]
                script_index = script_ref & 0xffffff
                if script_ref >> 24 != 0x84 or script_index >= len(scripts):
                    script_links.append({'reference': f'{script_ref:08x}', 'unresolved': True})
                    continue
                script_addr = r.unpack('<I', scripts[script_index]+8)[0]
                if r.unpack('<I', script_addr)[0] != 0x300:
                    raise ValueError('unexpected animation script type')
                cursor = script_addr + 0x44
                refs = []
                for _ in range(1024):
                    command, size = r.unpack('<BB', cursor)
                    if size == 0:
                        break
                    if size < 4 or cursor + size > len(data):
                        raise ValueError('invalid animation script command')
                    payload = cursor + (4 if command == 0x12 else 12)
                    if payload > cursor + size:
                        raise ValueError('truncated animation script command')
                    for pos in range(payload, cursor+size-3, 4):
                        value = r.unpack('<I', pos)[0]
                        if value >> 24 == 0x83 and (value & 0xffffff) < len(inventory['animations']):
                            refs.append({'index': value & 0xffffff, 'reference': f'{value:08x}',
                                         'command': command, 'offset': pos})
                    cursor += size
                else:
                    raise ValueError('animation script command limit exceeded')
                script_links.append({'reference': f'{script_ref:08x}', 'index': script_index,
                                     'address': script_addr, 'candidate_motions': refs})
            links.append({'mode_index': mode_index, 'mode': inventory['animation_modes'][mode_index],
                          'file_reference': f'{file_ref:08x}', 'set_index': set_index,
                          'scripts': script_links})
    return {'source_sha256': inventory['sha256'], 'links': links,
            'note': 'Candidate local motion references; blending, events, and activation are not decoded.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edb', type=Path)
    parser.add_argument('hashcodes', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = trace(args.edb, args.hashcodes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"Found {len(result['links'])} candidate mode-to-set links")


if __name__ == '__main__':
    main()
