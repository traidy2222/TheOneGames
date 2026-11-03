#!/usr/bin/env python3
"""Inspect and extract entries from the supplied Robots PC v7 archives.

Output names use the entry number and resource hash rather than archive paths.
Filename decoding follows eurotools/eurochef's MIT-licensed path.rs (v7).
"""
import argparse
import json
import struct
from pathlib import Path


def inventory(descriptor):
    data = descriptor.read_bytes()
    if len(data) < 20:
        raise ValueError('truncated header')
    version, size, count, packed, relative = struct.unpack_from('<5I', data)
    if version != 7 or size != len(data) or packed != 1:
        raise ValueError('expected a packed version 7 descriptor with matching size')
    table_end = 16 + relative
    if not 20 <= table_end <= len(data) or count > (table_end - 20) // 28:
        raise ValueError('invalid entry count or table boundary')
    entries, pos = [], 20
    for index in range(count):
        if pos + 20 > table_end:
            raise ValueError('truncated entry')
        length, resource_hash, version, flags, copies = struct.unpack_from('<5I', data, pos)
        pos += 20
        if not copies or copies > (table_end - pos) // 8:
            raise ValueError('invalid copy count')
        locations = []
        for _ in range(copies):
            offset, container = struct.unpack_from('<2I', data, pos)
            pos += 8
            if container > 999:
                raise ValueError('invalid container number')
            path = descriptor.with_suffix(f'.{container:03d}')
            if not path.is_file() or offset + length > path.stat().st_size:
                raise ValueError(f'entry {index} points outside {path}')
            locations.append({'container': container, 'offset': offset})
        container_path = descriptor.with_suffix(f'.{locations[0]["container"]:03d}')
        with container_path.open('rb') as source:
            source.seek(locations[0]['offset'])
            magic = source.read(min(length, 4)).hex()
            stored_length = length
            if magic == '4d4f4547' and length >= 28:
                source.seek(locations[0]['offset'] + 20)
                full_length, base_length = struct.unpack('<2I', source.read(8))
                if base_length == length and full_length >= length and locations[0]['offset'] + full_length <= container_path.stat().st_size:
                    stored_length = full_length
        pointer = table_end + index * 4
        if pointer + 4 > len(data):
            raise ValueError('truncated filename pointer table')
        start = pointer + struct.unpack_from('<I', data, pointer)[0]
        if start < table_end + count * 4 or start >= len(data):
            raise ValueError('invalid filename pointer')
        decoded = bytearray()
        for i, byte in enumerate(data[start:]):
            value = (byte + 0x16 - index - i) & 0xff
            if value == 0:
                break
            decoded.append(value)
        else:
            raise ValueError('unterminated filename')
        name = decoded.decode('ascii')
        entries.append({'index': index, 'name': name, 'hash': f'{resource_hash:08x}', 'length': length, 'edb_full_length_candidate': stored_length,
                        'version': version, 'flags': f'{flags:08x}', 'magic_hex': magic,
                        'locations': locations})
    if pos != table_end or table_end + count * 4 > len(data):
        raise ValueError('entry or filename pointer table boundary mismatch')
    for entry in entries:
        loc = entry['locations'][0]
        later = [other['locations'][0]['offset'] for other in entries
                 if other['locations'][0]['container'] == loc['container']
                 and other['locations'][0]['offset'] > loc['offset']]
        if later and loc['offset'] + entry['edb_full_length_candidate'] > min(later):
            raise ValueError(f'entry {entry["index"]} full EDB range overlaps the next indexed resource')
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('descriptor', type=Path)
    parser.add_argument('--extract', type=Path, help='new or empty destination directory')
    parser.add_argument('--full-edb', action='store_true', help='include EDB trailing data indicated by its header')
    args = parser.parse_args()
    entries = inventory(args.descriptor)
    if args.extract:
        args.extract.mkdir(parents=True, exist_ok=True)
        if any(args.extract.iterdir()):
            raise ValueError('extraction destination must be empty')
        for entry in entries:
            loc = entry['locations'][0]
            src = args.descriptor.with_suffix(f'.{loc["container"]:03d}')
            suffix = {'4d4f4547': 'geom', '4d555358': 'musx'}.get(entry['magic_hex'], 'bin')
            out = args.extract / f'{entry["index"]:04d}_{entry["hash"]}.{suffix}'
            with src.open('rb') as source, out.open('xb') as target:
                source.seek(loc['offset'])
                remaining = entry['edb_full_length_candidate'] if args.full_edb else entry['length']
                while remaining:
                    chunk = source.read(min(remaining, 1024 * 1024))
                    if not chunk:
                        raise ValueError('unexpected end of container')
                    target.write(chunk)
                    remaining -= len(chunk)
    print(json.dumps({'descriptor': str(args.descriptor), 'entries': entries}, indent=2))


if __name__ == '__main__':
    main()
