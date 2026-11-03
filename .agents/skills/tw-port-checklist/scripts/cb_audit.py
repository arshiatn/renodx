"""Replaced game shaders against the game's originals: python -I cb_audit.py [game ...] (default twattila).
1. Which constant buffer components the original reads that our build no longer reads, and which it reads that the original does
   not (the decompiler's mix-ups show up as one of each). Names from the original's reflection.
2. Instructions: which constants each instruction of the original reads, component by destination component, and whether our
   build has that instruction (a mix-up hidden by another path of the same shader reading the same constant shows only here).
3. dp2 x, c.cccc, c.cccc = 2 c^2: the decompiler writes dot(c, c) on a scalar, which compiles to c * c.
Part 2 also lists what we changed on purpose: read each line against the source."""
import collections
import ctypes
import pathlib
import re
import sys

compiler = ctypes.WinDLL('d3dcompiler_47.dll')


def disassemble(path):
    data = pathlib.Path(path).read_bytes()
    blob = ctypes.c_void_p()
    result = compiler.D3DDisassemble(data, len(data), 0, None, ctypes.byref(blob))
    assert result == 0, hex(result & 0xFFFFFFFF)
    vtable = ctypes.cast(ctypes.cast(blob, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    get_pointer = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtable[3])
    get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtable[4])
    return ctypes.string_at(get_pointer(blob), get_size(blob)).decode('ascii', 'replace')


def code(text):
    body = text[text.index('\ndcl_') if '\ndcl_' in text else 0:]
    return [line.strip() for line in body.splitlines() if line.strip() and not line.startswith(('dcl_', '//'))]


def reads(text):
    """(cb, register, component) read by the code; only components the instruction really uses is hard, so the whole swizzle."""
    used = collections.Counter()
    for line in code(text):
        for buffer, register, swizzle in re.findall(r'cb(\d+)\[(\d+)\]\.?([xyzw]*)', line):
            for component in set(swizzle or 'xyzw'):
                used[(int(buffer), int(register), component)] += 1
    return used


def names(text):
    """cbN[r].c -> name, from the header's reflection."""
    binds = {name: int(slot) for name, slot in re.findall(r'^// (\S+)\s+cbuffer\s+NA\s+NA\s+cb(\d+)', text, re.M)}
    table = {}
    current = None
    for line in text.splitlines():
        m = re.match(r'// cbuffer (\S+)', line)
        if m:
            current = m[1]
            continue
        m = re.match(r'//\s+(.+?)\s+(\w+)(\[\d+\])?;\s+// Offset:\s+(\d+) Size:\s+(\d+)', line)
        if m and current in binds:
            offset, size = int(m[4]), int(m[5])
            for byte in range(offset, offset + size, 4):
                table[(binds[current], byte // 16, 'xyzw'[(byte % 16) // 4])] = f'{m[2]}+{byte - offset}'
    return table


OPERAND = re.compile(r'(-?)(\|?)cb(\d+)\[(\d+)\]\.([xyzw]{1,4})\|?')


def per_component(text):
    """Instructions reading two different components of a constant, as (opcode, sign, cb, register, components for the
    destination's mask in order), plus each operand pair's order is dropped: fxc swaps commutative operands."""
    found = collections.Counter()
    for line in code(text):
        parts = line.split(None, 1)
        if len(parts) < 2:
            continue
        opcode = parts[0].split('_')[0]
        dest = re.match(r'[ro]\d+\.([xyzw]+),', parts[1])
        if not dest:
            continue
        mask = dest[1]
        for sign, bars, buffer, register, swizzle in OPERAND.findall(parts[1]):
            if len(swizzle) == 1:
                continue
            used = tuple(swizzle['xyzw'.index(c)] for c in mask)
            if len(set(used)) > 1:
                found[(opcode, sign + bars, int(buffer), int(register), ''.join(used))] += 1
    return found


def same_dots(text):
    """dp2 x, c.cccc, c.cccc: (cb, register, component)."""
    return [(int(m[1]), int(m[2]), m[3]) for m in re.finditer(r'dp2\S* \S+, cb(\d+)\[(\d+)\]\.([xyzw])\3{3}, cb\1\[\2\]\.\3{4}', '\n'.join(code(text)))]


ROOT = pathlib.Path(__file__).resolve().parents[4]
DECOMPILER = ROOT / 'bin/cmd_Decompiler-1.3.16'
for game in sys.argv[1:] or ['twattila']:
    dump = DECOMPILER / ('dump_shader_shogun2_pack' if game == 'twshogun2completeedition' else f'dump_shader_{game}_pack') / 'cso'
    embeds = [root / f'{game}.include/embed' for root in (ROOT / 'build32', ROOT / 'build')]
    newest = sorted((e for e in embeds if e.exists()), key=lambda e: max((p.stat().st_mtime for p in e.glob('*.cso')), default=0))
    embed = newest[-1]
    folder = ROOT / 'src/games' / game
    shaders = sorted(folder.glob('shaders/*_0x*.hlsl'))
    print('=====', game, embed)
    clean = 0
    missing = []
    # Generated motion / RT capture shaders: their first line names the game's shader.
    for source in sorted(folder.glob('*.hlsl')):
        with source.open(errors='replace') as file:
            if re.search(r"the game's 0x[0-9A-Fa-f]{8}", file.readline()):
                shaders.append(source)
    for source in shaders:
        crc_in_name = re.search(r'_(0x[0-9A-Fa-f]{8})\.', source.name)
        crc = crc_in_name[1] if crc_in_name else re.search(r"the game's (0x[0-9A-Fa-f]{8})", source.read_text(errors='replace'))[1]
        originals = list(dump.glob(f'{crc}.*.cso'))
        built = embed / f'{crc if crc_in_name else source.name.split(".")[0]}.cso'
        if not originals or not built.exists():
            missing.append(source.name + (' no original' if not originals else ' no build'))
            continue
        original = disassemble(originals[0])
        ours = disassemble(built)
        table = names(original)
        table_ours = names(ours)
        before, after = reads(original), reads(ours)
        lost = sorted(key for key in before if key not in after)
        lost_names = sorted({table.get(key, f'cb{key[0]}[{key[1]}].{key[2]}') for key in lost})
        added = sorted(key for key in after if key not in before)
        added_names = sorted({table_ours.get(key, table.get(key, f'cb{key[0]}[{key[1]}].{key[2]}')) for key in added})
        lost_shapes = sorted((per_component(original) - per_component(ours)).keys())
        lost_dots = [d for d in same_dots(original) if d not in same_dots(ours)]
        if lost or added or lost_shapes or lost_dots:
            print(source.name)
            if lost or added:
                print(f'   lost:  {lost_names}\n   added: {added_names}')
            for shape in lost_shapes:
                print('   instruction:', shape[0], f'{shape[1]}cb{shape[2]}[{shape[3]}]', shape[4], [table.get((shape[2], shape[3], c), '?').split('+')[0] for c in shape[4]])
            for dot in lost_dots:
                print('   dp2 c c lost:', dot, table.get(dot))
        else:
            clean += 1
    print('same:', clean, 'of', len(shaders), '| not compared:', missing)
