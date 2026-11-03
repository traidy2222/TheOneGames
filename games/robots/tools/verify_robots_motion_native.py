#!/usr/bin/env python3
"""Compare the Python sampler with the original x86 decoder in Unicorn.

Runs only the function at 0x567b30, not the game executable or its startup.
Requires local unicorn and pefile Python modules and the user's own game files.
The native cleanup helper is stubbed; no process, filesystem or network APIs
are emulated. This is a controlled decoder comparison, not original gameplay.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

from robots_motion import MotionError, sample, chunk_context
from robots_native_sampler import NativeSampler


def verify(exe, edb, inventory, all_clips=False, all_frames=False, report_only=False):
    raw = edb.read_bytes()
    info = json.loads(inventory.read_text())
    if hashlib.sha256(exe.read_bytes()).hexdigest() != '950bc7f0e0feefd92a129deef50123f5619ec36930eceb9707ef02dfcaaa8e13':
        raise ValueError('native function addresses are only verified for the supplied executable')
    if hashlib.sha256(raw).hexdigest() != info['sha256']:
        raise ValueError('inventory and EDB do not match')
    native = NativeSampler(exe, raw)
    comparisons = skipped = mismatches = 0
    overlap_checks = 0
    max_overlap_error = 0.0
    first_mismatch = None
    first_sample = None
    max_error = 0.0
    indices = range(len(info['animations'])) if all_clips else [0, 9, 10, 20, 30, 40, 50, 75, 100, 125, 150]
    for index in indices:
        item = info['animations'][index]
        address = item['header_address']
        header = raw[address:address+160]
        count = item['frame_count_candidate']
        for frame in (range(count) if all_frames else sorted({0, count // 2, count - 1})):
            try:
                chunk = chunk_context(raw, item, frame)
                actual = sample(header, raw[chunk['offset']:chunk['offset']+chunk['size']], frame, chunk['start'], chunk['length'])
            except MotionError:
                skipped += 1
                continue
            native_values = native.sample(item, frame, actual, chunk)
            if all_frames and frame:
                previous_chunk = chunk_context(raw, item, frame - 1)
                if previous_chunk['offset'] != chunk['offset']:
                    previous_shape = sample(header, raw[previous_chunk['offset']:previous_chunk['offset']+previous_chunk['size']], frame, previous_chunk['start'], previous_chunk['length'])
                    overlap = native.sample(item, frame, previous_shape, previous_chunk)
                    for left, right in zip(overlap, native_values):
                        for key in ('translation', 'rotation'):
                            if left[key] is not None:
                                max_overlap_error = max(max_overlap_error, *(abs(x-y) for x,y in zip(left[key], right[key])))
                    overlap_checks += 1
            if comparisons == 0 and native.sample(item, frame, actual, chunk) != native_values:
                raise ValueError('native sampler is not deterministic on repeated input')
            if first_sample is None:
                first_sample = (item, frame, actual, chunk, native_values)
            for track, (value, reference) in enumerate(zip(actual, native_values)):
                native_q = reference['rotation']
                comparisons += 1
                for x, y in zip(value['rotation'], native_q):
                    if not math.isfinite(x) or not math.isfinite(y):
                        raise ValueError('non-finite native or decoded quaternion')
                    max_error = max(max_error, abs(x-y))
                    if not math.isclose(x, y, rel_tol=1e-4, abs_tol=.003):
                        message = f'quaternion mismatch clip {index} frame {frame} track {track}: {x} vs {y}'
                        if not report_only: raise ValueError(message)
                        mismatches += 1
                        if first_mismatch is None: first_mismatch = message
                if value['translation'] is not None:
                    native_p = reference['translation']
                    for x, y in zip(value['translation'], native_p):
                        max_error = max(max_error, abs(x-y))
                        if not math.isclose(x, y, rel_tol=1e-4, abs_tol=.003):
                            message = f'translation mismatch clip {index} frame {frame} track {track}: {x} vs {y}'
                            if not report_only: raise ValueError(message)
                            mismatches += 1
                            if first_mismatch is None: first_mismatch = message
    if first_sample and native.sample(*first_sample[:4]) != first_sample[4]:
        raise ValueError('native sampler changed after decoding other frames')
    return {'compared_tracks': comparisons, 'skipped_unsafe_frames': skipped, 'mismatched_components': mismatches,
            'first_mismatch': first_mismatch, 'max_absolute_error': max_error,
            'chunk_overlap_checks': overlap_checks, 'max_chunk_overlap_error': max_overlap_error}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('edb', type=Path)
    parser.add_argument('inventory', type=Path)
    parser.add_argument('--all', action='store_true', help='sample beginning, middle and end of every clip')
    parser.add_argument('--all-frames', action='store_true', help='check every integral frame of every clip')
    parser.add_argument('--report-only', action='store_true', help='report numerical mismatches instead of stopping at the first')
    args = parser.parse_args()
    print(json.dumps(verify(args.exe, args.edb, args.inventory, args.all or args.all_frames, args.all_frames, args.report_only), indent=2))


if __name__ == '__main__':
    main()
