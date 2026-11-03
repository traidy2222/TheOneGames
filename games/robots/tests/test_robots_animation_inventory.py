import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


spec = importlib.util.spec_from_file_location('robots_anim', Path(__file__).resolve().parents[1] / 'tools/inspect_robots_animation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    data = bytearray(1024)
    struct.pack_into('<7I', data, 0, 0x47454f4d, 0x01000002, 248, 0, 0, len(data), len(data))
    struct.pack_into('<hhi', data, 0x58, 1, 0, 0x100 - 0x5c)
    struct.pack_into('<hhi', data, 0x60, 1, 0, 0x120 - 0x64)
    struct.pack_into('<IHH6I', data, 0x100, 0x03000001, 0, 0, 0x300, 0, 0x320, 0, 16, 0x0d000001)
    struct.pack_into('<IHH7I', data, 0x120, 0x0d000001, 0, 0, 0x180, 0, 0, 0, 0, 0, 0)
    struct.pack_into('<II', data, 0x180, 0x200, 2)
    for field, dest in [(0x40, 0x240), (0x44, 0x260), (0x48, 0x280)]:
        struct.pack_into('<i', data, 0x180 + field, dest - (0x180 + field))
    struct.pack_into('<8f', data, 0x240, 0, 1, 0, 0, 0, 3, 0, 0)
    struct.pack_into('<8f', data, 0x260, 0, 1, 0, 1, 0, 2, 0, 1)
    struct.pack_into('<8H', data, 0x280, 0xffff, 1, 0, 0, 0, 1, 0, 0)
    return data


class InventoryTests(unittest.TestCase):
    def test_hierarchy_and_motion_ranges(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'fixture.edb'
            path.write_bytes(fixture())
            result = module.inspect(path)
            self.assertEqual(result['skins'][0]['bone_count'], 2)
            self.assertEqual(result['skins'][0]['bones'][1]['parent_candidate'], 0)
            self.assertEqual(result['skins'][0]['max_parent_difference_error'], 0)
            self.assertTrue(result['skins'][0]['subtree_ranges_consistent'])
            self.assertEqual(result['animations'][0]['motion_size'], 16)

    def test_invalid_pointer_rejected(self):
        data = fixture()
        struct.pack_into('<i', data, 0x180 + 0x48, 0x7fffffff)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'bad.edb'
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                module.inspect(path)


if __name__ == '__main__':
    unittest.main()
