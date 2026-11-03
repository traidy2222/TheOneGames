"""Bounded, offline emulation of one function from the supplied Robots PE."""
import hashlib
import math
import struct
from pathlib import Path

import pefile
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP


class NativeSampler:
    def __init__(self, exe: Path, edb: bytes):
        if hashlib.sha256(exe.read_bytes()).hexdigest() != '950bc7f0e0feefd92a129deef50123f5619ec36930eceb9707ef02dfcaaa8e13':
            raise ValueError('native decoder addresses are not verified for this executable')
        pe = pefile.PE(str(exe))
        if pe.OPTIONAL_HEADER.ImageBase != 0x400000:
            raise ValueError('unexpected image base')
        self.edb = edb
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 0xfff) & ~0xfff)
        self.uc.mem_write(0x400000, pe.get_memory_mapped_image())
        self.uc.mem_map(0x10000000, (len(edb) + 0xfff) & ~0xfff)
        self.uc.mem_write(0x10000000, edb)
        self.uc.mem_map(0x20000000, 0x200000)
        self.uc.mem_map(0x30000000, 0x10000)
        # Stubs the decoder's local cleanup in emulated memory only.
        self.uc.mem_write(0x556c53, b'\xc3')
        self.initial_context = self.uc.context_save()

    def sample(self, record, frame, safe_shape, chunk=None):
        uc, base, stack = self.uc, 0x20001000, 0x201ff000
        uc.context_restore(self.initial_context)
        address, offset = record['header_address'], record['motion_address']
        header = self.edb[address:address+160]
        count, tracks = record['frame_count_candidate'], header[20]
        if len(safe_shape) != tracks or not 0 <= frame < count:
            raise ValueError('invalid prevalidated sample')
        if chunk is None:
            chunk = {'offset': offset, 'start': 0, 'length': count}
        offset = chunk['offset']
        start, length = chunk['start'], chunk['length']
        if not 0 <= start <= frame < start + length <= count:
            raise ValueError('invalid prevalidated chunk')
        uc.mem_write(base, b'\xff'*16)
        uc.mem_write(base+0x20, header[96:112])
        uc.mem_write(base+0x40, struct.pack('<7I', frame, frame, count, 0, start, length, frame-start))
        uc.mem_write(base+0x100, b'\0'*4096)
        uc.mem_write(base+0x1100, b'\0'*4096)
        args = [0x10000000+offset, tracks, frame, base, base+0x20,
                base+0x100, base+0x1100, base+0x40, 1]
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.mem_write(stack, struct.pack('<10I', 0x30000000, *args))
        uc.reg_write(UC_X86_REG_ECX, 0x10000000+address)
        try:
            uc.emu_start(0x567b30, 0x30000000, count=3000000)
        except UcError as e:
            raise ValueError(f'isolated native decoder failed at {uc.reg_read(UC_X86_REG_EIP):#x}') from e
        if uc.reg_read(UC_X86_REG_EIP) != 0x30000000:
            raise ValueError('native decoder exceeded instruction limit')
        result, translated = [], 0
        for track, shape in enumerate(safe_shape):
            q = list(struct.unpack('<4f', uc.mem_read(base+0x1100+track*16, 16)))
            position = None
            if shape['translation'] is not None:
                position = list(struct.unpack('<3f', uc.mem_read(base+0x100+translated*16, 12)))
                translated += 1
            if not all(math.isfinite(x) for x in q + (position or [])):
                raise ValueError('native decoder returned non-finite values')
            result.append({'translation': position, 'rotation': q})
        return result
