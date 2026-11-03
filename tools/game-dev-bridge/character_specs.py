"""Pure validation for character job specifications; no Blender dependency."""
import math
import re


def name(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]{0,127}', value):
        raise ValueError('Names must be simple identifiers without slashes: ' + repr(value))
    return value


def vector(value):
    if not isinstance(value, list) or len(value) != 3 or any(type(v) not in (int, float) or not math.isfinite(v) for v in value):
        raise ValueError('Expected three finite coordinates')
    return value


def rig_spec(spec):
    if not isinstance(spec, dict): raise ValueError('Rig spec must be an object')
    name(spec.get('armature', 'CharacterRig'))
    bones = spec.get('bones')
    if not isinstance(bones, list) or not bones: raise ValueError('Supply bones with explicit heads, tails and parents')
    names = [name(b['name']) for b in bones]
    if len(set(names)) != len(names): raise ValueError('Duplicate bone names')
    parents = {}
    for b in bones:
        head, tail = vector(b['head']), vector(b['tail'])
        if sum((a - z) ** 2 for a, z in zip(head, tail)) < 1e-12: raise ValueError('Zero-length bone: ' + b['name'])
        parent = b.get('parent')
        if parent is not None and parent not in names: raise ValueError('Unknown parent: ' + str(parent))
        parents[b['name']] = parent
        if type(b.get('deform', True)) is not bool: raise ValueError('deform must be boolean')
        if type(b.get('roll', 0)) not in (int, float) or not math.isfinite(b.get('roll', 0)): raise ValueError('Invalid bone roll')
    for bone in names:
        seen = set(); cursor = bone
        while cursor is not None:
            if cursor in seen: raise ValueError('Cyclic bone hierarchy')
            seen.add(cursor); cursor = parents[cursor]
    if sum(v is None for v in parents.values()) != 1: raise ValueError('Export rig must have exactly one root bone')
    if spec.get('binding', 'automatic') not in ('automatic', 'weights'): raise ValueError('binding must be automatic or weights')
    if not isinstance(spec.get('meshes'), list) or not spec['meshes']: raise ValueError('Select mesh object names explicitly')
    for n in spec['meshes']: name(n)
    max_influences = spec.get('max_influences', 4)
    if type(max_influences) is not int or not 1 <= max_influences <= 12: raise ValueError('max_influences must be 1..12')
    return spec


def normalized_weights(weights, valid_bones, max_influences=4):
    if not isinstance(weights, dict) or not weights: raise ValueError('Every vertex needs positive deform weights')
    pairs = []
    for bone, value in weights.items():
        if bone not in valid_bones: raise ValueError('Unknown/non-deforming weight bone: ' + bone)
        if type(value) not in (int, float) or not math.isfinite(value) or value < 0: raise ValueError('Invalid skin weight')
        if value > 0: pairs.append((bone, value))
    pairs = sorted(pairs, key=lambda x: (-x[1], x[0]))[:max_influences]
    total = sum(w for _, w in pairs)
    if not total: raise ValueError('Every vertex needs positive deform weights')
    return {b: w / total for b, w in pairs}


def clip_spec(spec):
    fps = spec.get('fps', 30)
    if type(fps) is not int or not 1 <= fps <= 240: raise ValueError('fps must be 1..240')
    clips = spec.get('clips')
    if not isinstance(clips, list) or not clips: raise ValueError('Supply named clips with bone tracks')
    names = set()
    for clip in clips:
        n = name(clip['name'])
        if n in names: raise ValueError('Duplicate clip name')
        names.add(n)
        start, end = clip.get('start', 1), clip.get('end')
        if type(start) is not int or type(end) is not int or not 0 <= start < end <= 100000:
            raise ValueError('Clips require a bounded start < end frame range')
        if not isinstance(clip.get('tracks'), dict) or not clip['tracks']: raise ValueError('Clip has no bone tracks')
        for bone, keys in clip['tracks'].items():
            name(bone)
            if not isinstance(keys, list) or not keys: raise ValueError('Track has no keyframes')
            seen = set()
            for key in keys:
                frame = key.get('frame')
                if type(frame) is not int or not start <= frame <= end or frame in seen: raise ValueError('Invalid/duplicate keyframe')
                seen.add(frame)
                if not any(x in key for x in ('location', 'rotation', 'scale')): raise ValueError('Keyframe has no transform')
                for channel in ('location', 'rotation', 'scale'):
                    if channel in key:
                        vector(key[channel])
                        if channel == 'scale' and min(key[channel]) <= 0: raise ValueError('Bone scales must be positive')
    return spec


def validate_spec(operation, spec):
    if not isinstance(spec, dict): raise ValueError('Character specification must be a JSON object')
    if operation == 'rig': return rig_spec(spec)
    if operation == 'animate': return clip_spec(spec)
    if operation not in ('inspect', 'validate', 'export'): raise ValueError('Unknown character operation')
    if spec.get('armature'): name(spec['armature'])
    if 'actions' in spec:
        if not isinstance(spec['actions'], list): raise ValueError('actions must be a list')
        for action in spec['actions']: name(action)
        if len(set(spec['actions'])) != len(spec['actions']): raise ValueError('Duplicate selected actions')
    if operation == 'export' and spec.get('root_motion', 'preserve') not in ('preserve', 'require_in_place'):
        raise ValueError('root_motion must be preserve or require_in_place; translations are never silently removed')
    rules = spec.get('rules', {})
    if set(rules) - {'max_influences', 'require_animation', 'require_motion', 'sample_frames'}: raise ValueError('Unknown character rule')
    if type(rules.get('max_influences', 4)) is not int or not 1 <= rules.get('max_influences', 4) <= 12: raise ValueError('Invalid influence budget')
    for switch in ('require_animation', 'require_motion'):
        if switch in rules and type(rules[switch]) is not bool: raise ValueError('Rule must be boolean')
    if type(rules.get('sample_frames', 5)) is not int or not 2 <= rules.get('sample_frames', 5) <= 30:
        raise ValueError('sample_frames must be 2..30')
    return spec
