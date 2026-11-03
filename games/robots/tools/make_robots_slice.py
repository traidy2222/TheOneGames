#!/usr/bin/env python3
"""Assemble a local exploration slice from Eurochef's extracted village."""
import argparse
import json
import math
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('village', type=Path, help='folder with the village ECM and glTFs')
    ap.add_argument('rodney', type=Path, help='converted Rodney glTF')
    ap.add_argument('output', type=Path, help='scene file to generate')
    ap.add_argument('--pickup', type=Path, required=True, help='converted original scrap pickup glTF')
    args = ap.parse_args()
    village, rodney = args.village.resolve(), args.rodney.resolve()
    data = json.loads((village / '85000000.ecm').read_text())
    entities, missing = [], []
    for zone in data['mapzone_entities']:
        path = village / f'ref_{zone["entity_refptr"]}.gltf'
        if not path.is_file():
            raise FileNotFoundError(path)
        entities.append({'id': len(entities)+1, 'name': f'Village zone {zone["entity_refptr"]}',
                         'asset': str(path), 'body': 'static'})
    for placement in data['placements']:
        path = village / f'{placement["object_ref"]:x}.gltf'
        if not path.is_file():
            missing.append(f'{placement["object_ref"]:x}')
            continue
        x, y, z = placement['position']
        rx, ry, rz = placement['rotation']
        if abs(rx) > 1e-5 or abs(rz) > 1e-5:
            raise ValueError('placement uses an unimplemented rotation axis')
        angle = -ry / 2
        entities.append({'id': len(entities)+1, 'name': f'Village object {placement["object_ref"]:x}',
                         'asset': str(path), 'position': [-x, y, z],
                         'rotation': [0, math.sin(angle), 0, math.cos(angle)],
                         'scale': placement['scale']})
    if not rodney.is_file():
        raise FileNotFoundError(rodney)
    entities.append({'id': 1000, 'name': 'Rodney', 'asset': str(rodney), 'position': [0, 1.2, 10]})
    marker = args.pickup.resolve()
    if not marker.is_file():
        raise FileNotFoundError(marker)
    # Type 25 is identified as XTrigger_Pickup in Eurochef's Robots trigger map.
    # The precise subtype and activation logic remain unknown: use the game's
    # silver bolt model as a provisional visual for these real positions.
    pickups = [t for t in data['triggers'] if t['ttype'] == 'Trig_25']
    for i, trigger in enumerate(pickups):
        x, y, z = trigger['position']
        entities.append({'id': 1100+i, 'name': f'Collectible {i+1}', 'asset': str(marker),
                         'position': [-x, y+.4, z], 'behavior': 'spin', 'speed': 1.4})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'version': 2, 'entities': entities}, indent=2))
    print(f'Wrote {args.output} with {len(entities)} entities and {len(pickups)} pickup triggers; missing placement models: {missing}')


if __name__ == '__main__':
    main()
