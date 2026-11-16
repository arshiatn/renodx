"""Ryse's byte-pattern hooks against the game's decrypted image (engine_memory.hpp's FindPattern, offline).

python -I patterns.py <repository> <Bin64\\ryse_image.bin>

Every `constexpr std::string_view NAME = "XX ?? ..."` of src/games/rysesonofrome (the attack patterns, the particle
counts, the graphics hooks) that a FindPattern call reads must match exactly once in the image's executable sections,
as the add-on needs at run time (else it leaves the hook out). The image is the DLSS add-on's debug dump (file offset =
RVA), saved while the add-on ran: its own changes are in it, so a site also counts with them (a detour at the
pattern's start: E9 rel32 and int3 padding; the CodePatches of graphics.hpp, PATCHED below). CONTROL: at each found
site, the pattern's last fixed byte outside those changes altered in a copy of the image: no site is found (the search
sees every byte it checks). The image is only read.
"""

import pathlib
import re
import struct
import sys

LITERAL = re.compile(r'constexpr std::string_view (\w+) =((?:\s*"[^"]*")+)\s*;')
PIECE = re.compile(r'"([^"]*)"')
HEX = re.compile(r'^(?:(?:[0-9A-F]{2}|\?\?) )*(?:[0-9A-F]{2}|\?\?)$')
# graphics.hpp's CodePatches inside a pattern (offset, replacement): LOD_LIST_JUMP's je -> jmp, PAUSE_FLAG_CHECK's
# "and cl, al" -> two nops.
PATCHED = {'LOD_LIST_TEST': (26, 'EB'), 'JITTER_APPLY': (84, '90 90')}
DETOUR_PADDING = 12  # int3s after a detour's jmp, at most


def patterns(folder):
    found = {}
    sources = {path.name: path.read_text(encoding='utf-8') for path in sorted(folder.glob('*.hpp'))}
    every = '\n'.join(sources.values())
    for name, text in sources.items():
        for match in LITERAL.finditer(text):
            value = ''.join(PIECE.findall(match.group(2)))
            if HEX.match(value) and re.search(r'FindPattern\((?:internal::)?' + match.group(1) + r'\b', every):
                found[match.group(1)] = (name, value.split(' '))
    return found


def executable_sections(image):
    header = struct.unpack_from('<I', image, 0x3C)[0]
    sections = struct.unpack_from('<H', image, header + 6)[0]
    optional = struct.unpack_from('<H', image, header + 20)[0]
    first = header + 24 + optional
    ranges = []
    for index in range(sections):
        entry = first + 40 * index
        virtual_size, address, raw_size = struct.unpack_from('<III', image, entry + 8)
        characteristics = struct.unpack_from('<I', image, entry + 36)[0]
        if characteristics & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            ranges.append((address, address + (virtual_size or raw_size)))
    return ranges


# The pattern as the game ships it, then as the add-on may have left it in the image; with each variant the offsets
# the add-on changed.
def variants(name, pieces):
    result = [(pieces, set())]
    if name in PATCHED:
        offset, replacement = PATCHED[name]
        bytes_ = replacement.split(' ')
        result.append((pieces[:offset] + bytes_ + pieces[offset + len(bytes_):], set(range(offset, offset + len(bytes_)))))
    for padding in range(DETOUR_PADDING + 1):
        changed = 5 + padding
        if changed < len(pieces):
            result.append((['E9', '??', '??', '??', '??'] + ['CC'] * padding + pieces[changed:], set(range(changed))))
    return result


def compiled(pieces):
    parts = [b'.' if byte == '??' else re.escape(bytes([int(byte, 16)])) for byte in pieces]
    return re.compile(b'(?=' + b''.join(parts) + b')', re.DOTALL)


# Every site of any variant inside one section (overlapping ones too, as FindPattern counts them; the lookahead stops at
# the section's end): {start: the offsets the add-on changed there}.
def sites(expressions, image, ranges):
    found = {}
    for expression, changed in expressions:
        for begin, end in ranges:
            for hit in expression.finditer(image, begin, end):
                found.setdefault(hit.start(), changed)
    return found


def main():
    repository = pathlib.Path(sys.argv[1])
    image = bytearray(pathlib.Path(sys.argv[2]).read_bytes())
    ranges = executable_sections(image)
    found = patterns(repository / 'src' / 'games' / 'rysesonofrome')
    failures = 0
    print(f'{len(found)} patterns, {len(ranges)} executable sections ({sum(end - begin for begin, end in ranges) >> 20} MB)')
    for name, (source, pieces) in sorted(found.items()):
        expressions = [(compiled(variant), changed) for variant, changed in variants(name, pieces)]
        hits = sites(expressions, image, ranges)
        control = 'not run'
        how = ''
        if len(hits) == 1:
            (site, changed), = hits.items()
            how = ' as shipped' if not changed else f' with the add-on\'s change over bytes {min(changed)}-{max(changed)}'
            fixed = max(index for index, byte in enumerate(pieces) if byte != '??' and index not in changed)
            image[site + fixed] ^= 0xFF
            after = len(sites(expressions, image, ranges))
            image[site + fixed] ^= 0xFF
            control = f'{after} with byte {fixed} altered'
            if after != 0:
                failures += 1
            how = f' at +{site:#x}{how}'
        else:
            failures += 1
        print(f'  {name} ({source}, {len(pieces)} bytes): {len(hits)} site{"s" if len(hits) != 1 else ""}{how}; CONTROL {control}')
    if len(found) == 0:
        failures += 1
    print(f'{"PASS" if failures == 0 else "FAIL"} ({failures} failures)')
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
