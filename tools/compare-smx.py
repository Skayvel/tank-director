#!/usr/bin/env python
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tank Director contributors

"""Compare two SourcePawn .smx files section by section.

Usage:
    compare-smx.py <a.smx> <b.smx> [--json]
    compare-smx.py --sections <a.smx>

Why this exists
---------------
Two builds of the same source can differ in size and hash: spcomp's stack/heap
estimate is not stable. Comparing whole-file hashes therefore proves nothing
about whether a shipped binary matches a source tree. Comparing the emitted
payload sections (.code, .data, ...) does.

Format notes
------------
The SMX header layout differs between SourcePawn versions, and modern builds
store all sections inside one zlib stream. This parser therefore:

* locates the string table by finding the literal '.code\\0';
* locates the section table by validating candidate 12-byte entries whose
  name offsets resolve to NUL-terminated names starting with '.';
* decides between stored-plain and compressed layouts by checking whether
  section ranges fit inside the file, and if not, decompresses the payload
  stream that starts at the first section offset.

Section `dataoffs` values are offsets in the *uncompressed* image, so a
section's bytes are image[dataoffs - image_start : ... + size].
"""
import hashlib
import json
import sys
import zlib

SMX_MAGIC = 0x53504646  # 'SPFF'
HEADER_PROBE = 4096
MAX_SECTIONS = 32


def _u32(blob, off):
    return int.from_bytes(blob[off:off + 4], 'little')


def _cstr(blob, off):
    end = blob.find(b'\x00', off)
    if end < 0:
        return None
    raw = blob[off:end]
    if not raw:
        return ''
    try:
        return raw.decode('ascii')
    except UnicodeDecodeError:
        return None


def find_string_table(blob):
    idx = blob.find(b'.code\x00')
    if idx < 0:
        raise ValueError('cannot locate section name string table')
    return idx


def discover_table(blob):
    strtab = find_string_table(blob)
    best = None
    for start in range(16, min(HEADER_PROBE, strtab)):
        entries = []
        pos = start
        for _ in range(MAX_SECTIONS):
            if pos + 12 > strtab:
                break
            nameoffs = _u32(blob, pos)
            dataoffs = _u32(blob, pos + 4)
            size = _u32(blob, pos + 8)
            # A valid entry has a short name offset that resolves to a NUL
            # terminated name beginning with '.'. Anything else ends the run;
            # the entries collected so far may still be a valid table.
            if nameoffs > 0x10000:
                break
            name = _cstr(blob, strtab + nameoffs)
            if not name or not name.startswith('.'):
                break
            if size == 0 and dataoffs == 0:
                break
            entries.append({'name': name, 'dataoffs': dataoffs, 'size': size})
            pos += 12
        if len(entries) < 3:
            continue
        names = [e['name'] for e in entries]
        if '.code' not in names or '.data' not in names:
            continue
        offs = [e['dataoffs'] for e in entries]
        if offs != sorted(offs):
            continue
        if best is None or len(entries) > len(best[1]):
            best = (start, entries)
    if best is None:
        raise ValueError('cannot locate a plausible section table')
    return best


def load(path):
    with open(path, 'rb') as fh:
        blob = fh.read()
    if len(blob) < 32:
        raise ValueError('%s: too small to be an SMX' % path)
    if _u32(blob, 0) != SMX_MAGIC:
        raise ValueError('%s: bad magic' % path)

    table_off, entries = discover_table(blob)
    plain = all(e['dataoffs'] + e['size'] <= len(blob) for e in entries)

    if plain:
        image = blob
        image_start = 0
        compression = 'none'
    else:
        image_start = min(e['dataoffs'] for e in entries)
        try:
            image = zlib.decompress(blob[image_start:])
        except zlib.error as exc:
            raise ValueError('%s: payload stream at 0x%X is not zlib: %s' % (path, image_start, exc))
        compression = 'zlib'
        for e in entries:
            if e['dataoffs'] - image_start + e['size'] > len(image):
                raise ValueError('%s: section %s exceeds decompressed image' % (path, e['name']))

    sections = []
    for e in entries:
        start = e['dataoffs'] - image_start
        data = image[start:start + e['size']]
        sections.append({
            'name': e['name'],
            'size': len(data),
            'sha256': hashlib.sha256(data).hexdigest(),
            'dataoffs': e['dataoffs'],
            'data': data,
        })

    return {
        'path': path,
        'file_size': len(blob),
        'sha256': hashlib.sha256(blob).hexdigest(),
        'version': int.from_bytes(blob[4:6], 'little'),
        'table_offset': table_off,
        'image_start': image_start,
        'compression': compression,
        'image_size': len(image),
        'sections': sections,
    }


def print_sections(info):
    print('%s' % info['path'])
    print('  file %d bytes, sha256 %s' % (info['file_size'], info['sha256']))
    print('  version 0x%04X, section table 0x%X, payload %s, image %d bytes'
          % (info['version'], info['table_offset'], info['compression'], info['image_size']))
    w = max(len(s['name']) for s in info['sections'])
    print('  %-*s  %10s  %s' % (w, 'section', 'bytes', 'sha256'))
    for s in info['sections']:
        print('  %-*s  %10d  %s' % (w, s['name'], s['size'], s['sha256']))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if argv[1] == '--sections':
        print_sections(load(argv[2]))
        return 0
    as_json = '--json' in argv
    a = load(argv[1])
    b = load(argv[2])

    print('A: %s' % a['path'])
    print('   file %d, sha256 %s' % (a['file_size'], a['sha256']))
    print('B: %s' % b['path'])
    print('   file %d, sha256 %s' % (b['file_size'], b['sha256']))
    print('   payload: A=%s B=%s' % (a['compression'], b['compression']))
    print('')

    by_a = {s['name']: s for s in a['sections']}
    by_b = {s['name']: s for s in b['sections']}
    order = [s['name'] for s in a['sections']]
    order += [n for n in by_b if n not in order]

    rows = []
    all_equal = True
    for name in order:
        sa, sb = by_a.get(name), by_b.get(name)
        if sa is None or sb is None:
            all_equal = False
            rows.append((name, sa['size'] if sa else 0, sb['size'] if sb else 0,
                         sa['sha256'] if sa else '-', sb['sha256'] if sb else '-', False))
            continue
        equal = sa['data'] == sb['data']
        if not equal:
            all_equal = False
        rows.append((name, sa['size'], sb['size'], sa['sha256'], sb['sha256'], equal))

    w = max(len(r[0]) for r in rows)
    print('%-*s  %10s  %10s  %5s  %s' % (w, 'section', 'A bytes', 'B bytes', 'same', 'sha256(A)'))
    print('-' * (w + 2 + 10 + 2 + 10 + 2 + 5 + 2 + 64))
    for name, sa, sb, ha, hb, equal in rows:
        print('%-*s  %10d  %10d  %5s  %s' % (w, name, sa, sb, 'YES' if equal else 'no', ha))

    payload_equal = all(
        by_a[n]['data'] == by_b[n]['data']
        for n in ('.code', '.data')
        if n in by_a and n in by_b
    )

    print('')
    print('all sections identical  : %s' % ('YES' if all_equal else 'NO'))
    print('.code + .data identical : %s' % ('YES' if payload_equal else 'NO'))
    for name, sa, sb, ha, hb, equal in rows:
        if not equal and ha != '-' and hb != '-':
            print('  differing %s: %s vs %s' % (name, ha[:16], hb[:16]))

    if as_json:
        print('')
        print(json.dumps({
            'a': {'path': a['path'], 'size': a['file_size'], 'sha256': a['sha256']},
            'b': {'path': b['path'], 'size': b['file_size'], 'sha256': b['sha256']},
            'sections': [
                {'name': n, 'a_size': x, 'b_size': y, 'a_sha256': p, 'b_sha256': q, 'equal': e}
                for n, x, y, p, q, e in rows
            ],
            'all_sections_equal': all_equal,
            'code_and_data_equal': payload_equal,
        }, indent=2))

    return 0 if payload_equal else 1


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv))
    except (ValueError, OSError) as exc:
        print('error: %s' % exc, file=sys.stderr)
        sys.exit(3)
