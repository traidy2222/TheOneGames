#!/usr/bin/env python3
"""Experimental v248 PC EngineX motion sampler.

Derived from static analysis of the user-supplied Robots executable. This
samples integral frames with every track enabled. Runtime retargeting, clip
state and final joint composition are separate and not yet understood.
"""
import math
import struct
from decimal import Decimal, localcontext


class MotionError(ValueError):
    pass


def chunk_context(edb, record, frame):
    """Resolve the original animation's optional stream-chunk table (header +56)."""
    address, offset, size = record['header_address'], record['motion_address'], record['motion_size']
    count = record['frame_count_candidate']
    if not 0 <= frame < count or address < 0 or address + 60 > len(edb) or offset < 0 or offset + size > len(edb):
        raise MotionError('animation range outside EDB')
    packed = struct.unpack_from('<i', edb, address + 56)[0]
    flags = packed & 0xff
    if not flags & 0x20:
        return {'offset': offset, 'size': size, 'start': 0, 'length': count}
    table = address + 56 + (packed >> 8)
    entry = table + 4 * (flags & 0x1f).bit_count()
    if table < 0 or entry < table or entry + 4 > len(edb):
        raise MotionError('chunk metadata pointer outside EDB')
    pointer = entry + (struct.unpack_from('<i', edb, entry)[0] >> 8)
    if pointer < 0 or pointer + 6 > len(edb):
        raise MotionError('chunk table outside EDB')
    stride = (struct.unpack_from('<I', edb, pointer)[0] + 31) & ~31
    if not stride or stride > size:
        raise MotionError('invalid chunk size')
    start = 0
    for index in range((size + stride - 1) // stride):
        pos = pointer + 4 + index * 2
        if pos + 2 > len(edb):
            raise MotionError('truncated chunk boundaries')
        end = struct.unpack_from('<H', edb, pos)[0]
        if end < start or end >= count:
            raise MotionError('invalid chunk frame boundary')
        if frame <= end:
            # Native streaming includes the next frame in the previous chunk
            # to allow interpolation across chunk boundaries.
            included_end = min(end + 1, count - 1)
            return {'offset': offset + index * stride, 'size': min(stride, size - index * stride),
                    'start': start, 'length': included_end - start + 1}
        start = end + 1
    raise MotionError('frame not covered by chunk table')


def sample(header, motion, frame, chunk_start=0, chunk_length=None):
    if len(header) < 112:
        raise MotionError('truncated animation header')
    frame_count = struct.unpack_from('<H', header, 14)[0]
    tracks = header[20]
    if not 0 < frame_count <= 65535 or not 0 < tracks <= 128:
        raise MotionError('invalid frame or track count')
    frame = min(max(int(frame), 0), frame_count - 1)
    with localcontext() as context:
        context.prec = 21
        normalized = Decimal(frame) / Decimal(frame_count)
        normalized2 = normalized * normalized
        normalized3 = normalized2 * normalized
    def f32_round(value):
        try:
            return struct.unpack('<f', struct.pack('<f', float(value)))[0]
        except OverflowError as e:
            raise MotionError('coefficient evaluation overflow') from e
    # Native code stores powers and each accumulated term as 32-bit floats.
    # Its x87 intermediates can still differ slightly from Python doubles.
    t32 = f32_round(normalized)
    with localcontext() as context:
        context.prec = 21
        powers = [Decimal(1), Decimal.from_float(t32), Decimal.from_float(f32_round(normalized2)), normalized3,
                  normalized3*Decimal.from_float(t32), normalized3*Decimal.from_float(t32)*Decimal.from_float(t32)]

    def u16(offset):
        if offset < 0 or offset + 2 > len(motion):
            raise MotionError(f'metadata outside motion block at {offset}')
        return struct.unpack_from('<H', motion, offset)[0]

    def f32(offset):
        if offset < 0 or offset + 4 > len(motion):
            raise MotionError(f'coefficient outside motion block at {offset}')
        value = struct.unpack_from('<f', motion, offset)[0]
        if not math.isfinite(value):
            raise MotionError('non-finite coefficient')
        return value

    data_offset = 4 * u16(0)
    meta = 2
    if chunk_length is None:
        chunk_length = frame_count
    if not 0 <= chunk_start <= frame < chunk_start + chunk_length <= frame_count:
        raise MotionError('frame outside chunk')
    segment = (frame - chunk_start) >> 7
    last_segment = (chunk_length - 1) >> 7
    translation_mask = struct.unpack_from('<4I', header, 96)
    result = []
    component_index = 0
    for track in range(tracks):
        translated = bool(translation_mask[track // 32] & (1 << (track % 32)))
        values = []
        for _ in range(6 if translated else 3):
            next_data = data_offset + 4 * u16(meta)
            next_meta = meta + 2 * u16(meta + 2)
            if next_meta <= meta or next_meta > len(motion) or next_data > len(motion):
                raise MotionError('invalid channel boundaries')
            coefficient = data_offset
            if segment:
                coefficient += 4 * u16(meta + 2 * (3 * segment - 1))
                current = u16(meta + 2 * (3 * segment))
                descriptor = meta + 2 * u16(meta + 2 * (3 * segment + 1))
            else:
                current = chunk_start
                descriptor = meta + 2 * (3 * last_segment + 2)
            while True:
                if descriptor + 2 > next_meta:
                    raise MotionError(f'frame {frame} is not covered by descriptors for track {track}, channel {len(values)} (metadata {meta:#x}..{next_meta:#x}, descriptor {descriptor:#x})')
                desc = u16(descriptor)
                current += desc >> 6
                flags = desc & 0x3f
                if frame <= current:
                    break
                descriptor += 2
                coefficient += 4 * flags.bit_count()
            if coefficient + 4 * flags.bit_count() > next_data:
                raise MotionError('channel coefficients outside their range')
            coefficients = [0.0]*6
            for degree in range(6):
                if flags & (1 << degree):
                    coefficients[degree] = f32(coefficient)
                    coefficient += 4
            value = coefficients[0]
            for degree in range(1, 6):
                with localcontext() as context:
                    context.prec = 21
                    product = Decimal.from_float(coefficients[degree]) * powers[degree]
                # The original x87 path spills the last two lanes of each
                # four-component batch before adding them to the accumulator.
                if component_index % 4 >= 2:
                    product = Decimal.from_float(f32_round(product))
                value = f32_round(Decimal.from_float(value) + product)
            values.append(value)
            component_index += 1
            data_offset, meta = next_data, next_meta
        position = values[:3] if translated else None
        x, y, encoded_z = values[3:] if translated else values
        z = encoded_z if encoded_z < 2 else encoded_z - 4
        radicand = 1 - x*x - y*y - z*z
        w = (math.sqrt(radicand) if encoded_z < 2 else -math.sqrt(radicand)) if radicand > 0 else radicand
        result.append({'translation': position, 'rotation': [x, y, z, w]})
    return result
