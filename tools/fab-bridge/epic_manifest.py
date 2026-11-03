"""Bounded reader for Epic BuildPatch binary manifests and chunk payloads.

Format cross-checked against installed UE BuildPatchServices Data serialization and
https://github.com/StarksLabs/epic-fab/blob/master/src/manifestParser.ts.
Unsupported/encrypted formats fail closed rather than installing unchecked bytes.
"""
import hashlib
import json
import re
import struct
import zlib


def relative_path(name):
    if not isinstance(name, str) or not name or name.startswith(('/', '\\')):
        raise ValueError('Invalid manifest path')
    parts = name.replace('\\', '/').split('/')
    for p in parts:
        if (p in ('', '.', '..') or p.endswith((' ', '.')) or any(ord(c) < 32 or c in '<>:"|?*' for c in p)
                or re.fullmatch(r'(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\..*)?', p, re.I)):
            raise ValueError('Unsafe manifest path')
    return '/'.join(parts)


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, size):
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError('Truncated manifest or chunk')
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def number(self, fmt='I'):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))[0]

    def guid(self):
        return ''.join(f'{self.number():08X}' for _ in range(4))

    def string(self):
        size = self.number('i')
        if abs(size) > 1024 * 1024:
            raise ValueError('Oversized manifest string')
        if size == 0:
            return ''
        data = self.take(abs(size) * (2 if size < 0 else 1))
        null = b'\0\0' if size < 0 else b'\0'
        if not data.endswith(null):
            raise ValueError('Unterminated manifest string')
        return data[:-len(null)].decode('utf-16-le' if size < 0 else 'utf-8')

    def section(self):
        size = self.number()
        if size < 5:
            raise ValueError('Invalid manifest section')
        return Reader(self.take(size - 4))

    def count(self):
        value = self.number()
        if value > 1000000:
            raise ValueError('Manifest entry count too large')
        return value


def inflate(data, size):
    if not 0 <= size <= 256 * 1024 * 1024:
        raise ValueError('Unsupported decompressed size')
    decoder = zlib.decompressobj()
    try:
        result = decoder.decompress(data, size + 1)
    except zlib.error:
        raise ValueError('Invalid compressed payload') from None
    if len(result) != size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError('Compressed size mismatch')
    return result


def parse_manifest(data):
    if data.lstrip().startswith(b'{'):
        return parse_json_manifest(data)
    r = Reader(data)
    if r.number() != 0x44BEC00C:
        raise ValueError('Only Epic binary manifests are currently supported')
    header, raw_size, stored_size = r.number(), r.number(), r.number()
    digest, flags, version = r.take(20), r.number('B'), r.number()
    if not 14 <= version <= 24 or flags not in (0, 1) or not r.pos <= header <= len(data):
        raise ValueError('Unsupported or encrypted manifest')
    body = data[header:]
    if len(body) != stored_size:
        raise ValueError('Manifest stored size mismatch')
    body = inflate(body, raw_size) if flags else body
    if len(body) != raw_size or hashlib.sha1(body).digest() != digest:
        raise ValueError('Manifest SHA1 mismatch')
    r = Reader(body)
    meta = r.section()
    meta_version, feature = meta.number('B'), meta.number()
    if meta.number('B'):
        raise ValueError('File-data manifests are unsupported')
    meta.number()
    app, build = meta.string(), meta.string()
    c = r.section()
    c.number('B')
    chunks = [{'guid': c.guid()} for _ in range(c.count())]
    for field, read in [('hash', lambda: f'{c.number("Q"):016X}'), ('sha1', lambda: c.take(20).hex()),
                        ('group', lambda: c.number('B')), ('window', lambda: c.number()), ('size', lambda: c.number('q'))]:
        for chunk in chunks:
            chunk[field] = read()
    if feature >= 22:
        secrets = [c.take(16) for _ in chunks]
        for _ in chunks:
            c.number()
        tags = [c.take(16) for _ in chunks]
        if any(any(s) for s in secrets + tags):
            raise ValueError('Encrypted chunk metadata is unsupported')
    if any(not 0 < x['window'] <= 64 * 1024 * 1024 or not 0 < x['size'] <= 64 * 1024 * 1024 for x in chunks):
        raise ValueError('Invalid chunk size')
    chunk_map = {x['guid']: x for x in chunks}
    if len(chunk_map) != len(chunks):
        raise ValueError('Duplicate chunks')
    f = r.section()
    fversion, count = f.number('B'), f.count()
    if fversion > 2:
        raise ValueError('Unsupported file manifest version')
    files = [{'path': relative_path(f.string())} for _ in range(count)]
    if len({x['path'].casefold() for x in files}) != count:
        raise ValueError('Duplicate case-insensitive file paths')
    for _ in files:
        if f.string():
            raise ValueError('Manifest symlinks are unsupported')
    for entry in files:
        entry['sha1'] = f.take(20).hex()
    for _ in files:
        f.number('B')
    for _ in files:
        for _ in range(f.count()):
            f.string()
    for entry in files:
        parts = []
        for _ in range(f.count()):
            size = f.number()
            if size < 28:
                raise ValueError('Invalid chunk part')
            p = Reader(f.take(size - 4))
            guid, offset, size = p.guid(), p.number(), p.number()
            if guid not in chunk_map or offset + size > chunk_map[guid]['window']:
                raise ValueError('Chunk part out of bounds')
            parts.append({'guid': guid, 'offset': offset, 'size': size})
        entry['parts'], entry['size'] = parts, sum(p['size'] for p in parts)
    if fversion >= 1:
        for _ in files:
            if f.number():
                f.take(16)
        for _ in files:
            f.string()
    if fversion >= 2:
        for entry in files:
            entry['sha256'] = f.take(32).hex()
    if not files:
        raise ValueError('Empty manifest')
    return {'version': feature, 'app': app, 'build': build, 'chunks': chunk_map, 'files': files,
            'manifest_sha256': hashlib.sha256(data).hexdigest()}


def blob(value):
    if not isinstance(value, str) or not value or len(value) % 3 or not value.isascii() or not value.isdigit():
        raise ValueError('Invalid decimal byte encoding')
    try:
        return bytes(int(value[i:i + 3]) for i in range(0, len(value), 3))
    except ValueError:
        raise ValueError('Invalid encoded byte') from None


def blob_int(value):
    return int.from_bytes(blob(value), 'little')


def parse_json_manifest(data):
    value = json.loads(data)
    version = blob_int(value.get('ManifestFileVersion', '013000000000'))
    if value.get('bIsFileData') or not 6 <= version <= 14:
        raise ValueError('Unsupported JSON manifest feature level')
    chunks = {}
    for guid, size in value['ChunkFilesizeList'].items():
        if not re.fullmatch('[0-9a-fA-F]{32}', guid):
            raise ValueError('Invalid JSON chunk GUID')
        sha1 = value['ChunkShaList'][guid].lower()
        chunk = {'guid': guid.upper(), 'size': blob_int(size), 'hash': f"{blob_int(value['ChunkHashList'][guid]):016X}",
                 'sha1': sha1, 'group': blob_int(value['DataGroupList'][guid]), 'window': 1024 * 1024}
        if not re.fullmatch('[0-9a-f]{40}', sha1) or not 0 < chunk['size'] < 64 * 1024 * 1024:
            raise ValueError('Invalid JSON chunk metadata')
        chunks[guid.upper()] = chunk
    files = []
    for entry in value['FileManifestList']:
        if entry.get('SymlinkTarget'):
            raise ValueError('Symlinks unsupported')
        digest = blob(entry['FileHash'])
        if len(digest) != 20:
            raise ValueError('Invalid file SHA1')
        parts = []
        for p in entry['FileChunkParts']:
            guid, offset, size = p['Guid'].upper(), blob_int(p['Offset']), blob_int(p['Size'])
            if guid not in chunks or offset + size > chunks[guid]['window']:
                raise ValueError('JSON chunk part out of bounds')
            parts.append({'guid': guid, 'offset': offset, 'size': size})
        files.append({'path': relative_path(entry['Filename']), 'sha1': digest.hex(), 'parts': parts, 'size': sum(p['size'] for p in parts)})
    if not files or len({f['path'].casefold() for f in files}) != len(files):
        raise ValueError('Empty or duplicate JSON file list')
    return {'version': version, 'app': value.get('AppNameString', ''), 'build': value.get('BuildVersionString', ''),
            'files': files, 'chunks': chunks, 'manifest_sha256': hashlib.sha256(data).hexdigest()}


def chunk_path(chunk, version):
    if version >= 23:
        raise ValueError('Secret-addressed chunks are unsupported')
    folder = 'ChunksV5' if version >= 22 else 'ChunksV4' if version >= 15 else 'ChunksV3'
    return f"{folder}/{chunk['group']:02}/{chunk['hash']}_{chunk['guid']}.chunk"


def decode_chunk(data, chunk):
    r = Reader(data)
    if r.number() != 0xB1FE3AA2:
        raise ValueError('Invalid chunk magic')
    version, header, size = r.number(), r.number(), r.number()
    guid, rolling, flags = r.guid(), f'{r.number("Q"):016X}', r.number('B')
    if version not in (1, 2, 3, 4) or flags not in (0, 1) or guid != chunk['guid'] or rolling != chunk['hash']:
        raise ValueError('Unexpected or encrypted chunk')
    if not r.pos <= header <= len(data) or len(data) != header + size:
        raise ValueError('Chunk size mismatch')
    body = data[header:]
    body = inflate(body, chunk['window']) if flags else body
    if len(body) != chunk['window'] or hashlib.sha1(body).hexdigest() != chunk['sha1']:
        raise ValueError('Chunk SHA1 mismatch')
    return body
