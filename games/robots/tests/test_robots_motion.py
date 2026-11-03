import math
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from robots_motion import MotionError, sample, chunk_context
from export_robots_motion import in_place_position


class MotionTests(unittest.TestCase):
    def make_motion(self):
        header = bytearray(160)
        struct.pack_into('<H', header, 14, 3)
        header[20] = 1
        # Three constant quaternion components; offsets are in halfwords/floats.
        descriptors = [1, 3, (2 << 6) | 1] * 3
        motion = struct.pack('<H9H3f', 5, *descriptors, .1, .2, .3)
        return header, motion

    def test_constant_quaternion_and_clamping(self):
        header, motion = self.make_motion()
        for frame in [0, 1, 2, 99]:
            result = sample(header, motion, frame)[0]
            self.assertIsNone(result['translation'])
            for actual, expected in zip(result['rotation'], [.1, .2, .3, math.sqrt(.86)]):
                self.assertAlmostEqual(actual, expected, places=6)

    def test_truncated_stream_rejected(self):
        header, motion = self.make_motion()
        with self.assertRaises(MotionError):
            sample(header, motion[:-2], 0)

    def test_in_place_uses_bind_horizontal_and_preserves_vertical(self):
        self.assertEqual(in_place_position((4, 2, -3), (.25, 8, -.5)), (.25, 2, -.5))

    def test_chunk_boundary_and_interpolation_overlap(self):
        edb = bytearray(192)
        struct.pack_into('<i', edb, 56, (8 << 8) | 0x20)
        struct.pack_into('<i', edb, 64, 16 << 8)
        struct.pack_into('<IHH', edb, 80, 32, 2, 5)
        record = {'header_address': 0, 'motion_address': 128, 'motion_size': 64, 'frame_count_candidate': 6}
        self.assertEqual(chunk_context(edb, record, 2), {'offset': 128, 'size': 32, 'start': 0, 'length': 4})
        self.assertEqual(chunk_context(edb, record, 3), {'offset': 160, 'size': 32, 'start': 3, 'length': 3})
        with self.assertRaises(MotionError):
            chunk_context(edb, record, 6)


if __name__ == '__main__':
    unittest.main()
