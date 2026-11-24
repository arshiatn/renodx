"""Every-shader audit (G24): one ledger row per shader of a game's whole census, three verdicts per row (motion vectors,
render scale, RT shadow casters), each proven by evidence this checker re-reads on every run. "Done" for any of the three
axes = `--final` exits 0.

The census, the tables, the trace folders and the engine knowledge come from audit_rules/games.py (checker-owned,
reviewed); rules from the game's pack (audit_rules/<pack>.py). Facts come from the .cso bytes (D3DDisassemble of
d3dcompiler_47.dll), never from decompiled .hlsl (--verify-hlsl only compares the decompiler's output with them).
Procedure and the evidence rules: tools/tw/round_notes/handoff_plan/g24_shader_audit_design.md.

usage:
  python -I -B shader_audit.py --game <key> --tools <tools root> [--ledger <tools>/audit/<game>.tsv] [--tree <repo>]
         [--bin <bin>] [--handoff <HANDOFF.md>] [--jobs N]
         [--census-only] [--verify-hlsl]       # census, file identity, facts coverage; no ledger
         [--seed] [--apply-rules]              # fill the ledger in place (<ledger>.bak first)
         [--family GLOB] [--only HASH ...]     # part checks: never exit 0 or 4, the summary says PART
         [--sample --nonce TEXT] [--review FILE] [--final]
  python -I -B shader_audit.py --selftest <fixture folder> --tools <tools root> [--tree <repo>]
Exit: 0 complete; 1 problems; 2 census short or broken; 3 a rule control failed or is missing; 4 complete except gap
boxes still pending (--final: a failure).

Frozen core (this file and audit_rules/games.py): no change may loosen a check or alter an existing claim. Implementing a
claim that fails closed today, or adding a matrix line, is a core extension: it goes through the coordinator and the
same reviewer, brings at least one must-fail fixture case, and the hand-back carries the new hashes.
"""
import argparse
import concurrent.futures
import ctypes
import collections
import fnmatch
import glob
import hashlib
import importlib.util
import json
import os
import random
import re
import shutil
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
RULES_DIR = os.path.join(HERE, 'audit_rules')
EXTRACTOR = 'g24-facts-6'  # bump when the extracted facts change: the cache key holds it
NAME = re.compile(r'^0x([0-9A-Fa-f]{8})\.([a-z]+)_(\w+)\.cso$')
STAGES = ('vs', 'ps', 'cs', 'gs', 'hs', 'ds', 'lib')
IN_TRACE = re.compile(r'\b(vs|ps|cs|gs|hs|ds) ([0-9A-Fa-f]{8})\b')
AXES = ('mv', 'scale', 'rt')
COLUMNS = ('hash', 'stage', 'mv', 'mv_ev', 'scale', 'scale_ev', 'rt', 'rt_ev', 'note')
GAPS = {'GAP', 'BUG', 'MISSING'}
COMPONENTS = 'xyzw'

# ------------------------------------------------------------------------------------------- evidence matrix (M2, R3)
# (axis, verdict, reason) -> {token kind: claim}. A cell not here can't be settled; a kind not listed is not primary.
_NA_COMMON = {'no-geometry': {'facts': 'no-geometry'}, 'dead': {'facts': 'dead'}, 'ours': {'ours': 'file'}}
MATRIX = {
    ('mv', 'OWN', ''): {'table': 'motion', 'rule': 'rule', 'trace': 'writes-velocity', 'twin': 'OWN'},
    ('mv', 'OWN', 'experimental'): {'table': 'motion-gated'},
    ('mv', 'CAMERA', ''): {'facts': 'static-chain', 'rule': 'rule', 'pair': 'static-rows'},
    ('mv', 'GAP', ''): {'trace': 'drawn', 'rule': 'rule', 'facts': 'bones|vtf'},
    ('mv', 'N/A', 'depth-only'): {'trace': 'depth-only', 'rule': 'rule'},
    ('mv', 'N/A', 'position-only'): {'trace': 'no-velocity', 'rule': 'rule'},
    ('mv', 'N/A', 'shadow-only'): {'trace': 'shadow-only', 'rule': 'rule'},
    ('mv', 'N/A', 'post'): {'trace': 'fullscreen', 'rule': 'rule'},
    ('mv', 'N/A', 'ui'): {'trace': 'after-ui', 'rule': 'rule'},
    ('mv', 'N/A', 'after-upscale'): {'trace': 'after-upscale'},
    ('mv', 'N/A', 'offscreen-view'): {'trace': 'offscreen-view'},
    ('scale', 'SCALED', ''): {'src': 'code', 'facts': 'roots-pixel|roots-view', 'rule': 'rule', 'out': 'tool',
                              'test': 'tool'},
    ('scale', 'FIXED', 'after-upscale'): {'trace': 'after-upscale'},
    ('scale', 'FIXED', 'fixed-target'): {'trace': 'fixed-target', 'rule': 'rule'},
    ('scale', 'BUG', ''): {'trace': 'drawn', 'rule': 'rule', 'out': 'tool'},
    ('scale', 'ACCEPTED', ''): {'decision': 'atn', 'trace': 'drawn', 'out': 'tool', 'rule': 'rule'},
    ('scale', 'N/A', 'no-screen-read'): {'facts': 'roots-world', 'rule': 'rule', 'out': 'tool'},
    ('scale', 'N/A', 'mesh-uv-only'): {'trace': 'mesh-uv', 'facts': 'roots-world', 'rule': 'rule', 'out': 'tool'},
    ('rt', 'CAPTURED', ''): {'table': 'capture', 'rule': 'rule', 'trace': 'host-state'},
    ('rt', 'CAPTURED', 'experimental'): {'table': 'capture-gated'},
    ('rt', 'SKIPPED', 'opacity-never'): {'table': 'capture'},
    ('rt', 'SKIPPED', 'twin'): {'twin': 'CAPTURED'},
    ('rt', 'SKIPPED', 'game-no-shadow'): {'trace': 'game-no-shadow', 'rule': 'rule'},
    ('rt', 'MISSING', ''): {'out': 'tool', 'trace': 'drawn', 'rule': 'rule'},
    ('rt', 'N/A', 'keyed-elsewhere'): {'table': 'key'},
    ('rt', 'N/A', 'no-rt'): {'manifest': 'no-rt'},
    # step 1: the screen-pass half of S0-S4 for the VS the facts can't call no-geometry (DrawRectangle, Slate), by trace
    ('rt', 'N/A', 'post'): {'trace': 'fullscreen', 'rule': 'rule'},
    ('rt', 'N/A', 'ui'): {'trace': 'after-ui', 'rule': 'rule'},
}
for _reason in ('water', 'decal', 'proxy', 'sky', 'particle', 'transparent', 'editor', 'debug', 'ui3d'):
    MATRIX[('rt', 'SKIPPED', _reason)] = {'rule': 'rule', 'src': 'code', 'facts': 'proxy'}
for _axis in AXES:  # no-geometry is a VS fact (S0-M1): a PS / CS row never shows it
    for _reason, _accepted in _NA_COMMON.items():
        MATRIX[(_axis, 'N/A', _reason)] = _accepted
NEVER_PRIMARY = {'note', 'box', 'override'}
# Trace claims the checker evaluates; any other trace claim fails closed (S0-S2 lists the lines left without a route).
TRACE_CLAIMS = {'drawn', 'depth-only', 'writes-velocity', 'no-velocity', 'fixed-target', 'mesh-uv', 'fullscreen', 'after-ui',
                'after-upscale'}
# a rule cell with one of these fails where traces disagree
CROSS_CHECKED = ('depth-only', 'writes-velocity', 'no-velocity', 'fullscreen', 'after-ui', 'after-upscale')
SCREEN_COUNTS = (3, 4, 6)  # fullscreen: one or two triangles in all (a trace's count is vertices or indices x instances)
DRAW_LINE = re.compile(r'^(?:draw|indexed) (\d+) vs ([0-9A-Fa-f]{8})\b')
WRITTEN = re.compile(r'\b(?:rt\d+|u\d+) (0x[0-9a-f]+) (\d+x\d+)')
# Verdicts the tripwires guard, and the fact kinds they read (R3): missing or partial facts fail these cells.
GUARDED = {('mv', 'CAMERA', ''): {'signature', 'dataflow', 'view'},
           ('mv', 'N/A', 'position-only'): {'signature'},
           ('scale', 'N/A', 'no-screen-read'): {'signature', 'dataflow', 'view'},
           ('scale', 'N/A', 'mesh-uv-only'): {'signature', 'dataflow', 'view'}} | \
          {(axis, 'N/A', 'no-geometry'): {'signature', 'dataflow'} for axis in AXES} | \
          {(axis, 'N/A', reason): {'signature', 'dataflow'} for axis in ('mv', 'rt') for reason in ('post', 'ui')}
# The fact kinds and the keys each one holds (n-r3-1: a rule sees only the keys of the kinds it declares).
KINDS = {
    'signature': ('stage', 'model', 'inputs', 'outputs', 'input_semantics', 'output_semantics', 'position_only', 'family'),
    'resources': ('constant_buffers', 'resources', 'uavs'),
    'dataflow': ('position_sources', 'position_input', 'bone_like', 'indexed_position', 'vtf', 'sv_position_used',
                 'reads', 'output_sources', 'screen_space', 'no_geometry', 'discards', 'primitive_data'),
    'view': ('view_slot', 'view_rows_in_position', 'previous_rows_in_outputs', 'projected_output', 'static_chain',
             'read_roots', 'roots_world', 'roots_pixel', 'roots_view'),
}
ALL_KINDS = frozenset(KINDS)
FACT_CLAIMS = {  # facts:<claim> -> (fact key, kinds it needs)
    'static-chain': ('static_chain', {'signature', 'dataflow', 'view'}),
    'roots-world': ('roots_world', {'signature', 'dataflow', 'view'}),
    'roots-pixel': ('roots_pixel', {'signature', 'dataflow', 'view'}),
    'roots-view': ('roots_view', {'signature', 'dataflow', 'view'}),
    'no-geometry': ('no_geometry', {'signature', 'dataflow'}),
    'bones': ('bone_like', {'signature', 'dataflow'}),
    'vtf': ('vtf', {'signature', 'dataflow'}),
}


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    """SHA-256 prefix of a file with LF line ends (git's CRLF checkouts give the same hash)."""
    return hashlib.sha256(open(path, 'rb').read().replace(b'\r\n', b'\n')).hexdigest()[:12]


def literal(value, in_name=False):
    """A C++ hash literal: 0x<hash> with any u / l suffix, never part of a longer hex number (S-r1); never inside an
    identifier (__0x7A1E0D01, the embedded bytes) except in a file name (ao06_0x506D32BE.ps_5_0.hlsl)."""
    return re.compile(('(?<![0-9A-Za-z])' if in_name else r'\b') + rf'0x{value}(?![0-9A-Fa-f])', re.I)


def split_cell(cell):
    """'N/A/ours:src/x.hlsl' -> ('N/A', 'ours', 'ours:src/x.hlsl'); 'CAPTURED' -> ('CAPTURED', '', '')."""
    verdict, reason = ('N/A', cell[4:]) if cell.startswith('N/A') else cell.partition('/')[::2]
    return verdict, reason.partition(':')[0], reason


# ------------------------------------------------------------------------------------------------ disassembly (facts)
class Disassembler:
    """D3DDisassemble from d3dcompiler_47.dll (in process: no fxc per shader)."""

    def __init__(self):
        self.lib = ctypes.WinDLL('d3dcompiler_47.dll')
        self.lib.D3DDisassemble.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_char_p,
                                            ctypes.POINTER(ctypes.c_void_p))
        self.lib.D3DDisassemble.restype = ctypes.c_long

    def __call__(self, data):
        blob = ctypes.c_void_p()
        if self.lib.D3DDisassemble(data, len(data), 0, None, ctypes.byref(blob)) != 0 or not blob:
            return None
        table = ctypes.cast(ctypes.cast(blob, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        pointer = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(table[3])(blob)
        size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(table[4])(blob)
        text = ctypes.string_at(pointer, size).decode('ascii', 'replace')
        ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(table[2])(blob)
        return text


PER_COMPONENT = {
    'mov', 'movc', 'add', 'mul', 'mad', 'div', 'min', 'max', 'sqrt', 'rsq', 'rcp', 'exp', 'log', 'frc', 'round_ne',
    'round_ni', 'round_pi', 'round_z', 'ftoi', 'ftou', 'itof', 'utof', 'and', 'or', 'xor', 'not', 'iadd', 'imad', 'umad',
    'ishl', 'ishr', 'ushr', 'ineg', 'imin', 'imax', 'umin', 'umax', 'eq', 'ne', 'lt', 'ge', 'ieq', 'ine', 'ilt', 'ige',
    'ult', 'uge', 'f16tof32', 'f32tof16', 'bfi', 'ubfe', 'ibfe', 'bfrev', 'countbits', 'firstbit_hi', 'firstbit_lo',
    'firstbit_shi', 'deriv_rtx', 'deriv_rty', 'deriv_rtx_coarse', 'deriv_rtx_fine', 'deriv_rty_coarse',
    'deriv_rty_fine', 'sincos', 'imul', 'umul', 'udiv', 'uaddc', 'usubb', 'swapc', 'msad', 'nop'}
TWO_DESTINATIONS = {'sincos', 'imul', 'umul', 'udiv', 'uaddc', 'usubb', 'swapc'}
DOTS = {'dp2': 2, 'dp3': 3, 'dp4': 4}
FLOW = {'ret', 'retc_nz', 'retc_z', 'label', 'call', 'callc_nz', 'callc_z', 'emit', 'cut', 'emit_stream', 'cut_stream',
        'emitthencut', 'emitthencut_stream', 'nop', 'hs_decls', 'hs_control_point_phase', 'hs_fork_phase',
        'hs_join_phase', 'abort', 'debug_break', 'discard_nz', 'discard_z', 'break', 'continue', 'case', 'default'}
OPERAND = re.compile(r'^([a-zA-Z_]+?)(\d*)(?:\[([^\]]*)\])?(?:\[([^\]]*)\])?(?:\.([xyzw]{1,4}))?$')


def clean(operand):
    """An operand without spaces, its [precise(...)] mark and its {min16f...} precision note."""
    return re.sub(r'\{[^}]*\}', '', re.sub(r'^\[precise[^\]]*\]', '', operand.strip().replace(' ', '')))


def split_operands(text):
    out, depth, current = [], 0, ''
    for char in text:
        if char in '([':
            depth += 1
        elif char in ')]':
            depth -= 1
        if char == ',' and depth == 0:
            out.append(current.strip())
            current = ''
        else:
            current += char
    if current.strip():
        out.append(current.strip())
    return out


def parse_signature(lines, start):
    rows = []
    for line in lines[start + 4:]:
        parts = line[2:].split() if line.startswith('//') else []
        if len(parts) < 6:
            break
        mask_index = 2 if re.fullmatch(r'[xyzw]+|N/A', parts[2]) else None
        if mask_index is None:  # no mask column (e.g. SV_Depth): name index register sysvalue format used
            parts.insert(2, '')
        rows.append([parts[0].upper(), int(parts[1]), parts[2], parts[3], parts[4], parts[5],
                     parts[6] if len(parts) > 6 else ''])
    return rows


def extract(asm):
    """Base facts of one shader from its disassembly: signatures, declarations, and a conservative data flow (every
    register component carries the set of roots it may come from; control flow merges branches and taints writes with
    their conditions; loops run twice)."""
    lines = asm.splitlines()
    inputs, outputs, patch = [], [], []
    for index, line in enumerate(lines):
        if line.startswith('// Input signature:'):
            inputs = parse_signature(lines, index)
        elif line.startswith('// Output signature:'):
            outputs = parse_signature(lines, index)
        elif line.startswith('// Patch Constant signature:'):
            patch = parse_signature(lines, index)
    code = [line.partition('//')[0].strip() for line in lines]  # debug listings end lines with '// r0.x <- name'
    code = [line for line in code if line]
    models = [i for i, line in enumerate(code) if re.fullmatch(r'(vs|ps|cs|gs|hs|ds)_\d_\d', line)]
    if not models:
        return {'error': 'no shader model line'}
    code = code[models[-1]:]  # a 4_0_level_9_x listing starts with its level-9 code: the SM4 program comes last
    model = code[0]
    stage = model[:2]
    constant_buffers, resources, uavs, specials = {}, {}, {}, {}
    instructions, skipping = [], 0
    for line in code[1:]:
        if skipping:
            skipping += line.count('{') - line.count('}')
            continue
        if line.startswith('dcl_immediateConstantBuffer'):
            skipping = line.count('{') - line.count('}')
            continue
        if line.startswith('dcl_'):
            found = re.match(r'dcl_constantbuffer [Cc][Bb](\d+)\[(\d+)\]', line)
            if found:
                constant_buffers[found.group(1)] = int(found.group(2))
            found = re.match(r'dcl_resource_(\w+)(?:\(\d+\))?(?: \([^)]*\))? t(\d+)', line)  # texture2dms(4) (float,..) t0
            if found:
                resources['t' + found.group(2)] = found.group(1)
            found = re.match(r'dcl_uav_(\w+)(?: \([^)]*\))? u(\d+)', line)
            if found:
                uavs['u' + found.group(2)] = found.group(1)
            found = re.match(r'dcl_input(?:_ps)?_s[gi]v(?: \w+)* v(\d+)\.\w+, (\w+)', line)
            if found:
                specials['v' + found.group(1)] = found.group(2)
            continue
        # 'op [precise(x)](structured_buffer, stride=4)(mixed,...) r0.x, ...': the mark sits before the op's types
        found = re.match(r'^(\w+(?:\([^)]*\))*\w*)\s*(.*)$', re.sub(r'^(\w+) \[precise[^\]]*\]', r'\1', line))
        if found:
            instructions.append((found.group(1), split_operands(found.group(2))))
    sv_of_register = {}
    for row in inputs:
        if row[3].isdigit():
            label = 'sv:position' if row[0] == 'SV_POSITION' else \
                'sv:vertexid' if row[0] == 'SV_VERTEXID' else 'sv:instanceid' if row[0] == 'SV_INSTANCEID' else \
                f'in:{row[0]}{row[1]}'
            sv_of_register.setdefault(row[3], set()).add(label)
    for register, value in specials.items():
        sv_of_register.setdefault(register[1:], set()).add(
            {'vertex_id': 'sv:vertexid', 'instance_id': 'sv:instanceid', 'position': 'sv:position'}.get(value, f'sv:{value}'))
    outputs_of_register = {}
    for row in outputs:
        outputs_of_register.setdefault(row[3], []).append(f'{row[0]}{row[1]}')

    state, taint_stack = {}, []
    writes = collections.defaultdict(lambda: [frozenset()] * 4)
    reads, unknown, used = [], set(), set()

    def taint():
        out = set()
        for frame in taint_stack:
            out |= frame[-1]
        return frozenset(out)

    def read(operand):
        """4 frozensets (per destination component position) of the roots an operand may carry."""
        text = re.sub(r'^[-|]+', '', clean(operand)).rstrip('|')
        if text.startswith(('l(', 'd(')):
            return [frozenset()] * 4
        match = OPERAND.match(text)
        if not match:
            unknown.add('operand ' + text)
            return [frozenset({'unknown'})] * 4
        kind, number, index1, index2, swizzle = match.groups()
        index_roots = frozenset()
        for index in (index1, index2):
            if index and not index.isdigit():
                for part in re.findall(r'[a-zA-Z]+\d*(?:\.[xyzw])?', index):
                    index_roots |= frozenset().union(*read(part))
        if kind == 'r':
            components = state.get('r' + number, [frozenset()] * 4)
        elif kind == 'x':
            components = state.get('x' + number, [frozenset()] * 4)
            components = [c | index_roots for c in components]
        elif kind == 'v':
            register = number or next((i for i in (index2, index1) if i and i.isdigit()), '?')
            labels = sv_of_register.get(register, {f'in:v{register}'})
            used.update(labels)
            components = [frozenset(labels) | index_roots] * 4
        elif kind == 'cb':
            if index1 and index1.isdigit():
                components = [frozenset({f'cb{number}[{index1}]'})] * 4
            else:
                dynamic = frozenset({f'cb{number}[dyn]'} | {'idx:' + r for r in index_roots if not r.startswith('idx:')})
                components = [dynamic | index_roots] * 4
        elif kind == 'icb':
            components = [frozenset({'icb'} | {'idx:' + r for r in index_roots if not r.startswith('idx:')}) | index_roots] * 4
        elif kind.startswith('vThread') or kind in ('vCoverage', 'vPrim', 'vGSInstanceID', 'vOutputControlPointID',
                                                     'vDomain', 'vicp', 'vpc', 'vocp', 'vForkInstanceID', 'vJoinInstanceID'):
            name = 'sv:threadid' if kind.startswith('vThread') else 'in:' + kind
            components = [frozenset({name}) | index_roots] * 4
        elif kind == 'g':
            components = [frozenset({'tgsm:g' + number}) | index_roots] * 4
        elif kind in ('t', 'u', 's', 'null', 'o', 'oDepth', 'oMask'):
            components = [frozenset()] * 4
        else:
            unknown.add('operand ' + kind)
            components = [frozenset({'unknown'})] * 4
        if swizzle:
            picks = swizzle if len(swizzle) == 4 else (swizzle * 4)[:4] if len(swizzle) == 1 else swizzle + swizzle[-1] * (4 - len(swizzle))
            components = [components[COMPONENTS.index(c)] for c in picks]
        return components

    def write(operand, values):
        text = clean(operand)
        if text == 'null':
            return
        match = OPERAND.match(text)
        if not match:
            unknown.add('destination ' + text)
            return
        kind, number, mask = match.group(1), match.group(2), match.group(5) or COMPONENTS
        extra = taint()
        if kind in ('r', 'x'):
            key = kind + number
            current = list(state.get(key, [frozenset()] * 4))
            for c in mask:
                position = COMPONENTS.index(c)
                current[position] = (values[position] | extra) if kind == 'r' else (current[position] | values[position] | extra)
            state[key] = current
        elif kind in ('o', 'oDepth', 'oMask', 'oDepthGE', 'oDepthLE', 'oStencilRef'):
            key = 'o' + number if kind == 'o' else kind
            current = list(writes[key])
            for c in mask:
                position = COMPONENTS.index(c)
                current[position] = current[position] | values[position] | extra
            writes[key] = current

    def union_states(a, b):
        out = {}
        for key in set(a) | set(b):
            left, right = a.get(key, [frozenset()] * 4), b.get(key, [frozenset()] * 4)
            out[key] = [left[i] | right[i] for i in range(4)]
        return out

    position, passes = 0, {}
    while position < len(instructions):
        token, operands = instructions[position]
        op = re.sub(r'\([^)]*\)', '', token)
        op = re.sub(r'_sat$', '', op)
        base = op.replace('_indexable', '').replace('_aoffimmi', '')
        if base in ('if_nz', 'if_z'):
            taint_stack.append(['if', {k: list(v) for k, v in state.items()}, None, frozenset().union(*read(operands[0]))])
        elif base == 'else':
            frame = taint_stack[-1]
            frame[2] = state
            state = {k: list(v) for k, v in frame[1].items()}
        elif base == 'endif':
            frame = taint_stack.pop()
            state = union_states(state, frame[2] if frame[2] is not None else frame[1])
        elif base == 'loop':
            if passes.get(position) is None:
                taint_stack.append(['loop', position, {k: list(v) for k, v in state.items()}, frozenset()])
                passes[position] = 0
        elif base in ('breakc_nz', 'breakc_z', 'continuec_nz', 'continuec_z', 'retc_nz', 'retc_z'):
            for frame in reversed(taint_stack):
                if frame[0] == 'loop':
                    frame[-1] = frame[-1] | frozenset().union(*read(operands[0]))
                    break
        elif base == 'endloop':
            frame = next(f for f in reversed(taint_stack) if f[0] == 'loop')
            start = frame[1]
            state = union_states(state, frame[2])
            if passes[start] == 0:
                passes[start] = 1
                position = start + 1
                continue
            taint_stack.remove(frame)
            passes[start] = None
        elif base == 'switch':
            taint_stack.append(['switch', {k: list(v) for k, v in state.items()}, frozenset().union(*read(operands[0]))])
        elif base in ('case', 'default'):
            frame = next(f for f in reversed(taint_stack) if f[0] == 'switch')
            state = union_states(state, frame[1])
        elif base == 'endswitch':
            frame = next(f for f in reversed(taint_stack) if f[0] == 'switch')
            taint_stack.remove(frame)
            state = union_states(state, frame[1])
        elif base in FLOW or base.startswith('sync') or base.startswith('dcl'):
            pass
        elif base.startswith(('store_', 'atomic_')):
            target = operands[0].split('.')[0] if operands else '?'
            value = frozenset().union(*[frozenset().union(*read(o)) for o in operands[1:]])
            writes['uav:' + target] = [writes['uav:' + target][0] | value | taint()] * 4
        elif base.startswith(('sample', 'gather4', 'ld', 'lod', 'resinfo', 'bufinfo', 'sampleinfo', 'samplepos',
                              'imm_atomic', 'eval')):
            destination, sources = (operands[0], operands[1:]) if operands else ('null', [])
            resource = next((o.split('.')[0].lstrip('-|') for o in sources if re.match(r'^[tug]\d+', o.lstrip('-|'))), '?')
            address = frozenset().union(*read(sources[0])) if sources else frozenset()
            others = frozenset().union(*[frozenset().union(*read(o)) for o in sources[1:]])
            if base.startswith(('resinfo', 'bufinfo', 'sampleinfo', 'samplepos')):
                result = frozenset({'size:' + resource}) | others
            elif base.startswith('eval'):
                result = address | others
            else:
                kind = resources.get(resource) or uavs.get(resource) or ('tgsm' if resource.startswith('g') else '?')
                is_buffer = kind in ('buffer', 'structured', 'raw') or 'structured' in base or 'raw' in base or resource.startswith('g')
                label = ('buf:' if is_buffer else 'tex:') + resource
                indexed = frozenset('idx:' + r for r in address if not r.startswith('idx:')) if is_buffer else frozenset()
                result = frozenset({label}) | indexed | address | others
                reads.append([resource, kind, sorted(address)])
            write(destination, [result] * 4)
        elif base in DOTS:
            count = DOTS[base]
            values = frozenset()
            for operand in operands[1:]:
                values |= frozenset().union(*read(operand)[:count])
            write(operands[0], [values] * 4)
        else:
            if base not in PER_COMPONENT:
                unknown.add(base)
            destinations = 2 if base in TWO_DESTINATIONS else 1
            sources = [read(o) for o in operands[destinations:]]
            if base in PER_COMPONENT:
                values = [frozenset().union(*[s[i] for s in sources]) if sources else frozenset() for i in range(4)]
                if base in ('div', 'rcp') and sources:  # a value over an interpolant: a projected coordinate
                    divisor = frozenset().union(*sources[-1])
                    if any(r.startswith(('in:', 'sv:position')) for r in divisor):
                        values = [v | {'proj'} for v in values]
            else:
                everything = frozenset().union(*[frozenset().union(*s) for s in sources]) if sources else frozenset()
                values = [everything] * 4
            for operand in operands[:destinations]:
                write(operand, values)
        position += 1
    output_sources = {}
    for register, values in writes.items():
        labels = sorted(frozenset().union(*values))
        names = outputs_of_register.get(register[1:], [register]) if register.startswith('o') and register[1:].isdigit() \
            else [register]
        for name in names:
            output_sources[name] = sorted(set(output_sources.get(name, [])) | set(labels))
    # S0-S5: a pixel shader that can change a caster's shape: it discards or writes a coverage mask
    discards = any(token in ('discard_nz', 'discard_z') for token, _ in instructions) or 'oMask' in writes or \
        any(row[0] == 'SV_COVERAGE' for row in outputs)
    return {'model': model, 'stage': stage, 'inputs': inputs, 'outputs': outputs, 'patch': patch,
            'constant_buffers': constant_buffers, 'resources': resources, 'uavs': uavs,
            'output_sources': output_sources, 'reads': reads, 'used': sorted(used), 'unknown': sorted(unknown),
            'discards': discards}


def family_of(base, position_only):
    """The checker's family of a shader (UE has no names in its bytecode; S0-M6): a VS / HS / DS / GS by its input
    layout (the vertex factory) and whether it is position-only; a PS by its input semantic names (no indices: not every
    material's interpolant set) and its output layout (the pass's targets); a CS by its resource layout."""
    if base['stage'] == 'cs':
        key = '|'.join(f'{k}:{v}' for k, v in sorted(base['resources'].items()) + sorted(base['uavs'].items()))
    elif base['stage'] == 'ps':
        key = '|'.join(sorted({r[0] for r in base['inputs']})) + '>' + '|'.join(f'{r[0]}{r[1]}' for r in base['outputs'])
    else:
        key = '|'.join(f'{r[0]}{r[1]}:{r[5]}' for r in base['inputs']) + ('>position-only' if position_only else '')
    return f'{base["stage"]}-{zlib.crc32(key.encode()):08x}'


def derive(base, engine):
    """All facts of a shader from its cached base facts and the engine's knowledge; returns (facts, kinds present)."""
    if base is None or 'error' in base:
        return {}, frozenset()
    output_semantics = [f'{r[0]}{r[1]}' for r in base['outputs']]
    position_only = output_semantics == ['SV_POSITION0']
    facts = {'stage': base['stage'], 'model': base['model'], 'inputs': base['inputs'], 'outputs': base['outputs'],
             'input_semantics': [f'{r[0]}{r[1]}' for r in base['inputs']], 'output_semantics': output_semantics,
             'position_only': position_only, 'family': family_of(base, position_only),
             'constant_buffers': {int(k): v for k, v in base['constant_buffers'].items()},
             'resources': base['resources'], 'uavs': base['uavs']}
    kinds = {'signature', 'resources'}
    if base['unknown']:
        facts['unknown'] = base['unknown']
    position = set(base['output_sources'].get('SV_POSITION0', []))
    uint_inputs = {f'in:{r[0]}{r[1]}' for r in base['inputs'] if r[5] in ('uint', 'int') or r[0] == 'BLENDINDICES'}
    texture_kinds = {k for k, v in base['resources'].items() if v not in ('buffer', 'structured', 'raw')}
    facts['position_sources'] = sorted(position)
    facts['position_input'] = any(r.startswith('in:') for r in position)
    # S0-S4 (narrow): a VS whose clip position is made from its vertex inputs, vertex / instance id and literals only:
    # no constant (no camera, no light matrix, no object transform), no buffer or texture read (no vertex pulling).
    # A DS / GS position from its inputs is the earlier stage's geometry, so only a VS is screen-space. r2-S2: that
    # form means a screen pass only in an engine whose pack was reviewed for it (manifest screen_space).
    facts['screen_space'] = base['stage'] == 'vs' and 'SV_POSITION0' in output_semantics and all(
        r.startswith(('in:', 'idx:in:', 'idx:sv:vertexid', 'idx:sv:instanceid')) or r in ('sv:vertexid', 'sv:instanceid', 'icb')
        for r in position)
    facts['no_geometry'] = facts['screen_space'] and bool(engine.get('screen_space'))  # S0-M1: a VS only
    facts['discards'] = base['discards']
    # r2-M1: Unreal 4.22+ GPU Scene. The primitive id input indexes the primitive's own data (LocalToWorld) in a
    # structured buffer: a position buffer read only ever by that id alone is per-draw data, not bones or vertex
    # pulling. Any other buffer read with the id in its index, a dynamic constant or an icb in the position: none is.
    pid = f'in:{engine["primitive_id"]}' if engine.get('primitive_id') else None
    primitive = set()
    if pid in uint_inputs and not any(r == 'icb' or r.endswith('[dyn]') for r in position):
        reads_of = collections.defaultdict(list)
        for resource, kind, address in base['reads']:
            reads_of[resource].append((kind, address))
        buffers = {r[4:] for r in position if r.startswith('buf:')}
        primitive = {b for b in buffers if all(kind == 'structured' and address == [pid] for kind, address in reads_of[b])}
        if any(pid in address for b in buffers - primitive for _, address in reads_of[b]):
            primitive = set()
    facts['primitive_data'] = sorted(primitive)
    rest = position - ({f'buf:{b}' for b in primitive} | {pid, f'idx:{pid}'}) if primitive else position
    facts['bone_like'] = any(r[4:] in uint_inputs for r in rest if r.startswith('idx:'))
    facts['indexed_position'] = any(r.startswith('idx:') or r.endswith('[dyn]') for r in rest)
    facts['vtf'] = any(r.startswith('tex:') and r[4:] in texture_kinds for r in position)
    facts['sv_position_used'] = 'sv:position' in base['used']  # read by any instruction (a dither's discard too)
    facts['reads'] = base['reads']
    facts['output_sources'] = base['output_sources']
    if not base['unknown']:
        kinds.add('dataflow')
    view = engine.get('view')
    if view is None or 'dataflow' not in kinds:
        return facts, frozenset(kinds)
    sizes = facts['constant_buffers']
    if base['stage'] in ('vs', 'ds', 'gs'):
        candidates = [s for s, rows in sizes.items() if rows >= view['min_rows']
                      and all(f'cb{s}[{r}]' in position for r in range(4))]
        known = bool(candidates) or not any(r.startswith('cb') for r in position)
    else:
        candidates = [s for s, rows in sizes.items() if rows >= view['min_rows']]
        known = True
    if len(candidates) > 1 or not known:
        return facts, frozenset(kinds)  # View not identified: the 'view' kind stays missing (fail closed)
    slot = candidates[0] if candidates else None
    allowed = set(view['projection']) | set(view['translation'])
    rows = sorted({int(r.split('[')[1][:-1]) for r in position if slot is not None and r.startswith(f'cb{slot}[')
                   and r[-5:] != '[dyn]'} - allowed)
    facts['view_slot'] = slot
    facts['view_rows_in_position'] = rows + (['dyn'] if f'cb{slot}[dyn]' in position else [])
    others = set().union(*[set(v) for k, v in base['output_sources'].items() if k != 'SV_POSITION0']) \
        if base['output_sources'] else set()
    facts['previous_rows_in_outputs'] = sorted(int(r.split('[')[1][:-1]) for r in others if slot is not None
                                               and r.startswith(f'cb{slot}[') and r[-5:] != '[dyn]'
                                               and int(r.split('[')[1][:-1]) in set(view['previous']))
    facts['projected_output'] = base['stage'] == 'vs' and slot is not None and \
        any(f'cb{slot}[{r}]' in others for r in view['projection'])
    # Static in world space given its per-draw constants: vertex inputs, one per-draw buffer (more than one non-View
    # buffer can be a material's or a parameter collection's offset: world position offset, wind) and only the View's
    # projection and translation rows. The View must project it (S0-M2: a screen quad has no View slot). r2-M1: the
    # GPU Scene primitive's data counts as that one per-draw source (a Material buffer beside it is refused).
    per_draw = {r.split('[')[0] for r in rest if r.startswith('cb') and not r.startswith(f'cb{slot}[')} | primitive
    facts['static_chain'] = slot is not None and any(r.startswith('in:') for r in rest) and not facts['bone_like'] and \
        not facts['indexed_position'] and not facts['vtf'] and not facts['view_rows_in_position'] and \
        len(per_draw) <= 1 and \
        not any(r.startswith(('tex:', 'buf:', 'tgsm:', 'uav:', 'icb', 'unknown', 'sv:', 'proj')) for r in rest)

    def roots(labels):
        out = set()
        for label in labels:
            if label in ('sv:position', 'sv:threadid'):
                out.add('pixel')
            elif slot is not None and label.startswith(f'cb{slot}['):
                out.add('view')
            elif label == 'proj':
                out.add('proj')
            elif label.startswith('size:'):
                out.add('size')
            elif label.startswith('in:'):
                out.add('input')  # an interpolant: a mesh uv or a screen position the VS made (design 3.7)
            elif label.startswith(('tex:', 'buf:', 'tgsm', 'uav')):
                out.add('tex')  # a dependent read: the coordinate comes from another read
            elif label == 'unknown':
                out.add('unknown')
        return out or {'world'}

    facts['read_roots'] = [[resource, kind, sorted(roots(labels))] for resource, kind, labels in base['reads']]
    screen = {'pixel', 'view', 'proj', 'size', 'unknown'}
    all_roots = [set(r[2]) for r in facts['read_roots']]
    facts['roots_world'] = not facts['sv_position_used'] and all(r == {'world'} for r in all_roots)
    screen_reads = [r for r in all_roots if r & screen]
    facts['roots_pixel'] = bool(screen_reads) and all(r <= {'pixel', 'world'} for r in all_roots)
    facts['roots_view'] = bool(screen_reads) and all(r <= {'pixel', 'view', 'world'} for r in all_roots) and \
        any('view' in r for r in all_roots)
    kinds.add('view')
    return facts, frozenset(kinds)


class FactsView(dict):
    """A rule's strict view of the facts (n-r3-1): only the keys of the kinds it declares, and only when they were
    extracted; any other key raises."""

    def __init__(self, facts, kinds, needs):
        missing = set(needs) - set(kinds)
        if missing:
            raise KeyError('facts missing: ' + ', '.join(sorted(missing)))
        super().__init__({k: facts[k] for kind in needs for k in KINDS[kind] if k in facts})

    def get(self, key, default=None):
        return self[key]

    def __missing__(self, key):
        raise KeyError(f'fact {key} is not in the rule\'s declared needs')


def bytecode_stage(data):
    """The stage the program chunk of a container declares (SHDR / SHEX / DXIL version token: kind in bits 16-31; DXIL
    kinds 6-12 are ray-tracing libraries), a D3D9 token stream's version token (0xFFFE vs / 0xFFFF ps, models 1-3),
    '?' for anything else."""
    if len(data) >= 8 and data[2:4] in (b'\xfe\xff', b'\xff\xff') and 1 <= data[1] <= 3:
        return 'vs' if data[2] == 0xfe else 'ps'
    if data[:4] != b'DXBC' or len(data) < 32:
        return '?'
    count = int.from_bytes(data[28:32], 'little')
    for index in range(min(count, 64)):
        offset = int.from_bytes(data[32 + 4 * index:36 + 4 * index], 'little')
        if data[offset:offset + 4] in (b'SHDR', b'SHEX', b'DXIL'):
            kind = int.from_bytes(data[offset + 8:offset + 12], 'little') >> 16
            return ('ps', 'vs', 'gs', 'hs', 'ds', 'cs', *['lib'] * 7, '?', '?')[kind] if kind < 15 else '?'
    return '?'


def facts_worker(item):
    path, sha = item
    disassemble = facts_worker.disassembler = getattr(facts_worker, 'disassembler', None) or Disassembler()
    data = open(path, 'rb').read()
    if data[:4] != b'DXBC':
        return sha, {'error': 'D3D9 bytecode: not analysed (the facts read SM4 / SM5 DXBC)' if bytecode_stage(data) != '?'
                     else 'not DXBC (DXIL or unknown container)'}
    asm = disassemble(data)
    if asm is None:
        return sha, {'error': 'D3DDisassemble failed'}
    try:
        return sha, extract(asm)
    except Exception as error:  # a listing the parser can't read: no facts (fail closed), listed by hash
        return sha, {'error': f'parse: {type(error).__name__}: {error}'}


# ------------------------------------------------------------------------------------------------------ context
class Context:
    def __init__(self, args):
        self.args = args
        manifest_path = os.path.join(RULES_DIR, 'games.py')
        self.manifest_path = manifest_path
        manifest = load(manifest_path, 'audit_manifest')
        if args.game not in manifest.GAMES:
            sys.exit(f'unknown game {args.game} (games.py: {", ".join(manifest.GAMES)})')
        self.game = manifest.GAMES[args.game]
        self.engine = manifest.ENGINES[self.game['engine']]
        self.pack_path = os.path.join(RULES_DIR, self.game['pack'] + '.py') if self.game['pack'] else None
        self.pack = load(self.pack_path, 'audit_pack') if self.pack_path else None
        self.rules = getattr(self.pack, 'RULES', {}) if self.pack else {}
        self.roots = {'tools': args.tools, 'bin': args.bin or os.path.join(args.tree, 'bin'), 'repo': args.tree}
        self.files, self.problems, self.notes, self.excluded, self.marks = {}, [], [], {}, {}
        self.cache_dir = os.path.join(args.tools, 'audit', 'cache', args.game)

    def path(self, spec):
        root, _, rest = spec.partition(':')
        return os.path.join(self.roots[root], rest) if root in self.roots else None

    def text(self, path, strip_comments=False):
        key = (path, strip_comments)
        if key not in self.files:
            raw = open(path, encoding='utf-8', errors='replace').read() if path and os.path.isfile(path) else None
            if raw is not None and strip_comments:
                raw = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), raw, flags=re.S)
                raw = re.sub(r'//[^\n]*', '', raw)
            self.files[key] = raw
        return self.files[key]

    def repo_files(self, patterns):
        out = []
        for pattern in patterns:
            out += [os.path.relpath(p, self.args.tree).replace('\\', '/')
                    for p in glob.glob(os.path.join(self.args.tree, pattern), recursive=True)]
        return out


# ------------------------------------------------------------------------------------------------- census (M1, R2)
def census(ctx, header, final=False):
    """{(hash, stage): {'tiers', 'file'}}; problems for floors, file identity (n-r3-2) and source+ (R2)."""
    sources = list(ctx.game['sources'])
    for spec in header.get('source+', []):  # "<S|R> <root>:<path> <glob>"
        parts = spec.split()
        if not parts or parts[0] not in ('S', 'R'):
            ctx.problems.append(f'source+ "{spec}": only tiers S and R may be added; level A comes only from the '
                                'manifest\'s traces (R2)')
            continue
        root, _, path = parts[1].partition(':') if len(parts) == 3 else ('', '', '')
        if root not in ctx.roots or not path:
            ctx.problems.append(f'source+ "{spec}": use "<S|R> <root>:<path> <glob>"')
            continue
        sources.append((parts[0], root, path, parts[2], 1))
    os.makedirs(ctx.cache_dir, exist_ok=True)
    cache_path = os.path.join(ctx.cache_dir, 'files.json')
    try:
        cache = {} if final else json.load(open(cache_path, encoding='utf-8'))
    except (OSError, ValueError):
        cache = {}
    found, fresh, broken = {}, {}, False
    for tier, root, path, pattern, floor in sources:
        unique, folder = set(), os.path.join(ctx.roots[root], path)
        for name in sorted(glob.glob(os.path.join(folder, pattern), recursive=True)):
            match = NAME.match(os.path.basename(name))
            if not match:
                ctx.problems.append(f'census source {path}: {os.path.basename(name)} is not named 0x<HASH>.<stage>.cso')
                continue
            stat = os.stat(name)
            entry = cache.get(name)
            if not entry or 'stage' not in entry or entry['size'] != stat.st_size or entry['mtime'] != stat.st_mtime_ns:
                data = open(name, 'rb').read()
                entry = {'size': stat.st_size, 'mtime': stat.st_mtime_ns, 'crc': f'{zlib.crc32(data):08X}',
                         'sha': hashlib.sha256(data).hexdigest(), 'stage': bytecode_stage(data)}
            fresh[name] = entry
            key = (match.group(1).upper(), entry['stage'])
            if entry['crc'] != key[0]:  # n-r3-2: the name hash is the CRC32 of the bytes
                ctx.problems.append(f'census {name}: CRC32 of its bytes is {entry["crc"]}, not its name\'s {key[0]}')
                continue
            if entry['stage'] not in STAGES:
                ctx.problems.append(f'census {name}: no shader program in its bytes')
                continue
            if match.group(2) != entry['stage']:  # the dumper's name lost the stage (Rome Remastered's .xx_5_0)
                ctx.notes.append(f'{key[0]}: named {match.group(2)}, its bytecode is a {entry["stage"]}')
            if key in ctx.game['not_game']:  # an injected DLL's shader in the dump, not the game's
                ctx.excluded[key] = ctx.game['not_game'][key]
                continue
            unique.add(key)
            if key in found:
                if found[key]['sha'] != entry['sha']:  # n-r3-2: the same (hash, stage) with other bytes
                    ctx.problems.append(f'census {key[0]} {key[1]}: {name} has other bytes than {found[key]["file"]}')
                found[key]['tiers'].add(tier)
            else:
                found[key] = {'tiers': {tier}, 'file': name, 'sha': entry['sha']}
        if len(unique) < int(floor):
            ctx.problems.append(f'census source {path}: {len(unique)} unique (hash, stage), floor {floor}')
            broken = True
    json.dump(fresh, open(cache_path, 'w', encoding='utf-8'))
    traces, listing = [], []
    for folder in ctx.game['trace_folders']:
        for root, _, names in os.walk(ctx.path(folder)):
            for name in sorted(n for n in names if 'frame_trace' in n and n.endswith('.txt')):
                path = os.path.join(root, name)
                traces.append(os.path.normcase(os.path.abspath(path)))
                listing.append(f'{folder}/{os.path.relpath(path, ctx.path(folder))} {os.path.getsize(path)}'.replace('\\', '/'))
    ctx.trace_digest = hashlib.sha256('\n'.join(sorted(listing)).encode()).hexdigest()[:12]  # S0-M4: pins the folders
    if len(traces) < ctx.game['trace_files']:
        ctx.problems.append(f'traces: {len(traces)} trace files in the manifest folders, floor {ctx.game["trace_files"]}')
        broken = True
    traced = set()
    for trace in traces:
        for stage, value in IN_TRACE.findall(ctx.text(trace) or ''):
            key = (value.upper(), stage)
            if key in ctx.game['not_game']:  # n-r2-1: a traced injected shader is not the game's either
                ctx.excluded[key] = ctx.game['not_game'][key]
            elif value != '00000000':
                traced.add(key)
    for key in traced:
        found.setdefault(key, {'tiers': set(), 'file': None, 'sha': None})['tiers'].add('A')
    if len(traced) < ctx.game['trace_floor']:
        ctx.problems.append(f'traces: {len(traced)} unique (hash, stage), floor {ctx.game["trace_floor"]}')
        broken = True
    ctx.stages = {key[1] for key in found}  # fullscreen: the traces log no GS / HS / DS (step 1)
    return found, traces, broken


def trace_scale(ctx, path):
    """Render scale read from the trace itself (view width / the widest colour target), not typed in the ledger."""
    text = ctx.text(path) or ''
    view = re.search(r'\bview (\d+)x\d+', text)
    widest = max((int(w) for w in re.findall(r'\brt\d+ 0x[0-9a-f]+ (\d+)x', text)), default=0)
    return round(int(view.group(1)) / widest, 3) if view and widest else None


def load_facts(ctx, found, keys, jobs, force=()):
    """Base facts per census file, cached by SHA-256 + extractor version (N3); 'force' re-extracts (S-r3)."""
    cache_path = os.path.join(ctx.cache_dir, 'facts.json')
    try:
        cache = json.load(open(cache_path, encoding='utf-8'))
    except (OSError, ValueError):
        cache = {}
    if cache.get('version') != EXTRACTOR:
        cache = {'version': EXTRACTOR, 'facts': {}}
    todo = sorted({(found[k]['file'], found[k]['sha']) for k in keys if k in found and found[k]['file']
                   and (found[k]['sha'] not in cache['facts'] or found[k]['sha'] in force)})
    fresh = {}
    if todo:
        if jobs > 1 and len(todo) > 64:
            with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
                fresh = dict(pool.map(facts_worker, todo, chunksize=32))
        else:
            fresh = dict(map(facts_worker, todo))
    changed = []
    for sha, base in fresh.items():
        if sha in force and sha in cache['facts'] and cache['facts'][sha] != base:
            changed.append(sha)
        cache['facts'][sha] = base
    if fresh:
        json.dump(cache, open(cache_path, 'w', encoding='utf-8'))
    facts = {}
    for key in keys:
        entry = found.get(key)
        base = cache['facts'].get(entry['sha']) if entry and entry['sha'] else None
        facts[key] = derive(base, ctx.engine) + (base.get('error') if base else 'no .cso in the census',)
    return facts, changed


def verify_hlsl(ctx, found, facts):
    """cmd_Decompiler's .hlsl beside each census .cso against the bytecode's own signature and textures (ATN: the
    decompiler is sometimes broken). Rows never read .hlsl; this only reports where the decompiled file disagrees."""
    out, checked = [], 0
    for key, entry in sorted(found.items()):
        if not entry['file']:
            continue
        hlsl = ctx.text(entry['file'][:-4] + '.hlsl')
        fact, kinds, _ = facts.get(key, ({}, frozenset(), None))
        if hlsl is None or 'signature' not in kinds:
            continue
        checked += 1
        head, _, body = hlsl.partition('void main(')
        signature = body.partition(')')[0] if body else ''
        semantics = sorted(s.upper() for s in re.findall(r':\s*(\w+)\s*(?:,|$)', signature, re.M))
        # sets: an input SV_Coverage (vCoverage) is in no DXBC signature, so 'in SV_Coverage, out SV_Coverage' is one
        semantics = sorted({re.sub(r'(\D)$', r'\g<1>0', s) for s in semantics})
        want = sorted(set(fact['input_semantics'] + fact['output_semantics']))
        textures = sorted(set(re.findall(r'register\((t\d+)\)', head)))
        why = []
        if semantics != want:
            why.append(f'signature {semantics} vs bytecode {want}')
        if textures != sorted(fact['resources']):
            why.append(f'resources {textures} vs bytecode {sorted(fact["resources"])}')
        if why:
            out.append(f'{key[0]}\t{key[1]}\t' + '; '.join(why))
    return checked, out


# ------------------------------------------------------------------------------------------------ the ledger
def read_ledger(ctx, path):
    header, rows = collections.defaultdict(list), []
    if not os.path.isfile(path):
        return header, rows
    for number, line in enumerate(open(path, encoding='utf-8'), 1):
        if line.startswith('#!'):
            key, _, value = line[2:].partition(':')
            key = key.strip()
            if key not in ('game', 'tree', 'trace', 'source+', 'packs', 'note'):
                ctx.problems.append(f'ledger line {number}: "#! {key}" is the manifest\'s, not the ledger\'s (M1)')
            header[key].append(value.strip())
        elif line.strip() and not line.startswith(('#', 'hash\t')):
            cells = (line.rstrip('\n').split('\t') + [''] * 9)[:9]
            rows.append(dict(zip(COLUMNS, [c.strip() for c in cells])) | {'line': number})
    return header, rows


def write_ledger(path, header_lines, rows):
    if os.path.isfile(path):
        shutil.copyfile(path, path + '.bak')  # N4
    with open(path, 'w', encoding='utf-8', newline='\n') as out:
        out.writelines(header_lines)
        out.write('\t'.join(('hash', 'stage', 'mv', 'mv_evidence', 'scale', 'scale_evidence', 'rt', 'rt_evidence', 'note')) + '\n')
        for row in rows:
            out.write('\t'.join(row[c] for c in COLUMNS) + '\n')


def applies(ctx, axis, key, fact, kinds):
    """Whether an axis applies to a stage (S4): the manifest's stages, plus a VS with a projected output for scale, a PS
    a trace shows writing velocity for mv, and with the manifest's rt_ps a PS that can change a caster's shape (S0-S5:
    discard, coverage or depth output; missing facts count as can)."""
    stage = key[1]
    if stage in ctx.game['axes'][axis]:
        return True
    if axis == 'scale' and stage == 'vs':
        return 'view' in kinds and fact.get('projected_output')
    if axis == 'mv' and stage == 'ps':
        return key in ctx.velocity_ps
    if axis == 'rt' and stage == 'ps' and ctx.game['rt_ps']:
        return 'dataflow' not in kinds or fact['discards'] or any(s.startswith('SV_DEPTH') for s in fact['output_semantics'])
    return False


# ------------------------------------------------------------------------------------------------ tables (M6)
def table_entry(ctx, path, symbol, value):
    body = ctx.text(os.path.join(ctx.args.tree, path), strip_comments=True)
    start = re.search(rf'\b{re.escape(symbol)}\s*=\s*\{{', body or '')
    if not start:
        return None
    depth, end = 0, start.end() - 1
    for end in range(start.end() - 1, len(body)):
        depth += {'{': 1, '}': -1}.get(body[end], 0)
        if depth == 0:
            break
    inner = body[start.end():end]
    hit = literal(value).search(inner)
    if not hit:
        return None
    open_at, close_at = inner.rfind('{', 0, hit.start()), inner.find('}', hit.end())
    if open_at != -1 and inner.rfind('}', 0, hit.start()) < open_at:
        return inner[open_at:close_at + 1]
    return hit.group(0)


def table_verdicts(ctx, value):
    """{axis: (verdict cell, 'path:SYMBOL')} from the manifest's tables (the contradiction checks read the same)."""
    said = {}
    for axis, tables, default in (('rt', ctx.game['capture_tables'], 'CAPTURED'), ('mv', ctx.game['motion_tables'], 'OWN')):
        for (path, symbol), field in tables.items():
            entry = table_entry(ctx, path, symbol, value)
            if entry is not None:
                said[axis] = ((field[0] if field and re.search(field[1], entry) else default), f'{path}:{symbol}')
    for path, symbol in ctx.game['key_tables']:
        if table_entry(ctx, path, symbol, value) is not None:
            said.setdefault('rt', ('N/A/keyed-elsewhere', f'{path}:{symbol}'))
    return said


# ------------------------------------------------------------------------------------------------ evidence (M2, R1, R3)
def trace_lines(ctx, value, stage):
    """{trace: its lines that draw / dispatch the (hash, stage)}, over the manifest's traces (indexed once per run)."""
    if not hasattr(ctx, 'trace_index'):
        ctx.trace_index = collections.defaultdict(lambda: collections.defaultdict(list))
        for path in ctx.traces:
            for text in (ctx.text(path) or '').splitlines():
                for hit in {(v.upper(), s) for s, v in IN_TRACE.findall(text)}:
                    ctx.trace_index[hit][path].append(text)
    return ctx.trace_index.get((value, stage), {})


def velocity_target(ctx, text, slot=r'\d+'):
    """A trace line binds one of the engine's velocity formats (in render target slot 'slot')."""
    return any(re.search(rf'\brt{slot} 0x[0-9a-f]+ \d+x\d+ {f}\b', text) for f in ctx.engine['velocity_formats'])


def mesh_uv(ctx, interpolants, text):
    """Why one trace line that draws a pixel shader reading by 'interpolants' does not show mesh-uv-only, or '' (S0-S3):
    its VS must be View-projected (a mesh, not a screen quad) and make each of them from per-vertex or per-draw data
    only: vertex inputs, vertex / instance id, buffers (manual vertex fetch, GPU Scene data; open question 1) indexed by
    those or by non-View constant rows, and non-View constant rows. Never a texture, a View row, a dynamic index, a
    projection or a dependent index."""
    vs = next((v.upper() for s, v in IN_TRACE.findall(text) if s == 'vs'), None)
    fact, kinds, _ = ctx.facts.get((vs, 'vs'), ({}, frozenset(), None))
    if not {'dataflow', 'view'} <= kinds or fact['view_slot'] is None:
        return f'its VS {vs} has no View-projected position in the facts'
    slot = fact['view_slot']
    for interpolant in interpolants:
        sources = fact['output_sources'].get(interpolant)
        if sources is None:
            return f'its VS {vs} writes no {interpolant}'
        bad = []
        for label in sources:
            plain = label[4:] if label.startswith('idx:') else label
            constant = re.fullmatch(r'cb(\d+)\[\d+\]', plain)
            if not (plain.startswith('in:') or plain in ('sv:vertexid', 'sv:instanceid') or label.startswith('buf:')
                    or constant and int(constant.group(1)) != slot):
                bad.append(label)
        if bad:
            return f'its VS {vs} makes {interpolant} from {sorted(bad)[:3]} (textures, View rows, system values, ' \
                   'projection, dynamic or dependent indices)'
    return ''


def written(text):
    """[(address, WxH)] a draw, dispatch or copy line writes (rt* / u*, a copy's destination); a clear marks no pass."""
    if text.startswith('copy '):
        return re.findall(r'-> (0x[0-9a-f]+) (\d+x\d+)', text)
    return WRITTEN.findall(text) if text.startswith(('draw ', 'indexed ', 'dispatch ')) else []


def frame_marks(ctx, path):
    """A trace's marks, read from its own lines (the traces log no engine markers; step 1): the output size (the widest
    colour target); the line of the composite, the first write of the final target (rt0 of the trace's last draw), None
    unless that target is output-size and a fullscreen draw, a dispatch or a copy writes it first; the first line that
    writes an output-size target (the upscale below 100 %); the view size (the first view line, as trace_scale) and its
    downsampled chain (halved up to 7 times, rounded either way: bloom, AO, exposure); and each line's first index
    (identical lines count from the earliest)."""
    if path not in ctx.marks:
        lines = (ctx.text(path) or '').splitlines()
        sizes = [(int(w), int(h)) for text in lines for w, h in re.findall(r'\brt\d+ 0x[0-9a-f]+ (\d+)x(\d+)', text)]
        output = '%dx%d' % max(sizes) if sizes else None
        last = next((t for t in reversed(lines) if t.startswith(('draw ', 'indexed ')) and re.search(r'\brt0 0x', t)), '')
        final = re.search(r'\brt0 (0x[0-9a-f]+) (\d+x\d+)', last)
        first = next((i for i, t in enumerate(lines) if final and final.group(1) in (a for a, _ in written(t))), None)
        draw = DRAW_LINE.match(lines[first]) if first is not None else None
        screen = draw and int(draw.group(1)) in SCREEN_COUNTS or first is not None and lines[first].startswith(('dispatch ', 'copy '))
        view = re.search(r'\bview (\d+)x(\d+)', ctx.text(path) or '')
        width, height = (int(view.group(1)), int(view.group(2))) if view else (0, 0)
        ctx.marks[path] = {'output': output, 'composite': first if screen and final.group(2) == output else None,
                           'upscale': next((i for i, t in enumerate(lines) if any(s == output for _, s in written(t))), None),
                           'chain': {f'{w}x{h}' for k in range(8) for w in (width >> k, -(-width >> k))
                                     for h in (height >> k, -(-height >> k))} if view else set(),
                           'index': {text: i for i, text in reversed(list(enumerate(lines)))}}
    return ctx.marks[path]


def view_free(ctx, vs):
    """Why a VS's clip position may come from the View, or '' (fullscreen, after-ui; step 1): it needs dataflow facts
    and an engine View layout, and no constant buffer of the View's size (min_rows) may feed the position. A View read
    shorter than min_rows reads neither PreViewTranslation, which every Unreal mesh VS reads, nor the size rows."""
    fact, kinds, _ = ctx.facts.get((vs, 'vs'), ({}, frozenset(), None))
    if 'dataflow' not in kinds or ctx.engine['view'] is None:
        return f'its VS {vs} has no dataflow facts, or the engine no View layout'
    big = sorted({f'cb{hit.group(1)}' for hit in (re.match(r'(?:idx:)?cb(\d+)\[', r) for r in fact['position_sources'])
                  if hit and fact['constant_buffers'].get(int(hit.group(1)), 0) >= ctx.engine['view']['min_rows']})
    return f'its VS {vs} reads the View-sized {", ".join(big)} in its position' if big else ''


def trace_claim(ctx, claim, value, stage, token):
    """A trace claim over every line of the (hash, stage) in EVERY trace of the manifest's folders (R1)."""
    lines = trace_lines(ctx, value, stage)
    every = [t for group in lines.values() for t in group]
    if claim == 'host-state':
        claim = ctx.game['host_state'] or 'none'
    if claim not in TRACE_CLAIMS:
        return f'{token}: the trace claim "{claim}" needs engine markers the checker does not read yet (fails closed)'
    if not every:
        return f'{token}: {value} {stage} is in no manifest trace'
    if claim == 'depth-only':
        bad = [t for t in every if re.search(r'\brt\d+ 0x', t) or not re.search(r'\bds 0x', t)]
        return f'{token}: {len(bad)} of {len(every)} lines of {value} in the manifest\'s traces draw colour or no depth' if bad else ''
    if claim in ('writes-velocity', 'no-velocity'):
        if not ctx.engine['velocity_formats']:  # S0-M5: never passes for an engine without known velocity formats
            return f'{token}: the manifest knows no velocity format of this engine: {claim} fails closed'
        if claim == 'writes-velocity':
            bad = [t for t in every if not velocity_target(ctx, t, '0')]
            return f'{token}: {len(bad)} of {len(every)} lines of {value} write no velocity target' if bad else ''
        bad = [t for t in every if velocity_target(ctx, t)]
        return f'{token}: {len(bad)} of {len(every)} lines of {value} bind a velocity target' if bad else ''
    if claim == 'fixed-target':  # S0-M3: unreadable scales never count, every line needs its target size
        sizes, skipped, unparsed = {}, 0, 0
        for path, group in lines.items():
            if ctx.scale[path] is None:
                skipped += 1
                continue
            for text in group:
                target = re.search(r'\b(?:u0|rt0) 0x[0-9a-f]+ (\d+x\d+)', text)
                unparsed += target is None
                if target:
                    sizes.setdefault(ctx.scale[path], set()).add(target.group(1))
        skipped = f' ({skipped} traces with an unreadable render scale skipped)' if skipped else ''
        if unparsed:
            return f'{token}: {unparsed} lines of {value} have no rt0 / u0 target size{skipped}'
        if len(sizes) < 2:
            return f'{token}: fixed-target needs the hash in traces at two render scales (has {sorted(sizes)}){skipped}'
        if len({size for group in sizes.values() for size in group}) != 1:
            return f'{token}: target size follows the scale: ' + ', '.join(f'{s}: {sorted(v)}' for s, v in sorted(sizes.items()))
        return ''
    if claim == 'mesh-uv':
        fact, kinds, _ = ctx.facts.get((value, stage), ({}, frozenset(), None))
        if stage != 'ps' or not {'dataflow', 'view'} <= kinds:
            return f'{token}: mesh-uv needs a pixel shader with dataflow and View facts'
        roots = [r for r in fact['read_roots'] if not set(r[2]) <= {'world', 'input'}]
        if fact['sv_position_used'] or roots:
            return f'{token}: {value} uses SV_Position or reads by screen, size or dependent roots {roots[:3]}'
        interpolants = sorted({r[3:] for _, _, address in fact['reads'] for r in address if r.startswith('in:')})
        bad = [why for why in (mesh_uv(ctx, interpolants, t) for t in every) if why]
        return f'{token}: {len(bad)} of {len(every)} lines of {value}: {bad[0]}' if bad else ''
    if claim == 'fullscreen' and ctx.stages & {'gs', 'hs', 'ds'}:
        return f'{token}: the census has {"/".join(sorted(ctx.stages & {"gs", "hs", "ds"}))} shaders, which the traces do ' \
               'not log (one or two triangles could become geometry): fullscreen fails closed'
    if claim in ('fullscreen', 'after-ui', 'after-upscale'):  # step 1: from the frame's own lines (frame_marks)
        fact, kinds, _ = ctx.facts.get((value, stage), ({}, frozenset(), None))
        bad, below = [], 0
        for path, group in lines.items():
            marks = frame_marks(ctx, path)
            for text in group:
                draw, at = DRAW_LINE.match(text), marks['index'][text]
                if claim == 'fullscreen':  # every line one or two triangles by a VS the View can't project
                    bad.append('not a draw with a logged count' if not draw else
                               f'draws {draw.group(1)} vertices or indices' if int(draw.group(1)) not in SCREEN_COUNTS
                               else view_free(ctx, draw.group(2).upper()))
                elif claim == 'after-ui':  # every line after the composite, by a VS the View can't project
                    bad.append('its trace\'s final target is not output-size or not first written by a fullscreen draw, '
                               'a dispatch or a copy' if marks['composite'] is None else
                               'not after the final target\'s first write (the composite)' if at <= marks['composite']
                               else 'not a draw with a logged count' if not draw else view_free(ctx, draw.group(2).upper()))
                else:  # after-upscale: output-size targets only; below 100 % after the upscale, reading no render size
                    targets = sorted({size for _, size in written(text)})
                    if targets != [marks['output']]:
                        bad.append(f'writes {targets}, not only the output size {marks["output"]}')
                    elif ctx.scale[path] is not None and ctx.scale[path] < 1:
                        below += 1
                        bound = dict(re.findall(r'\b(t\d+) 0x[0-9a-f]+ (\d+x\d+)', text))
                        if marks['upscale'] is None or at <= marks['upscale']:
                            bad.append('not after its trace\'s first output-size write (the upscale)')
                        elif stage in ('ps', 'cs') and 'resources' not in kinds:
                            bad.append(f'{value} has no resource facts')
                        elif stage in ('ps', 'cs') and any(bound.get(s) in marks['chain'] for s in fact['resources']):
                            read = sorted(s for s in fact['resources'] if bound.get(s) in marks['chain'])
                            bad.append(f'reads {", ".join(read)} at the view size or its downsampled chain')
        bad = [why for why in bad if why]
        if bad:
            return f'{token}: {len(bad)} of {len(every)} lines of {value}: {bad[0]}'
        return f'{token}: {value} is in no trace below 100 % (after-upscale is proven there)' \
            if claim == 'after-upscale' and not below else ''
    return '' if claim == 'drawn' else f'{token}: no check for the trace claim {claim}'


def check_token(ctx, row, axis, cell, token, cited, said, fact, kinds):
    verdict, reason, full_reason = split_cell(cell)
    kind, _, rest = token.partition(':')
    accepted = MATRIX.get((axis, verdict, reason), {})
    value = row['hash']
    if kind in NEVER_PRIMARY:
        return None
    if kind not in accepted:
        return f'{token}: a {kind}: token is not evidence for {cell} (accepted: {", ".join(sorted(accepted)) or "nothing"})'
    claim = accepted[kind]
    if kind == 'table':
        path, _, symbol = rest.rpartition(':')
        tables = {'motion': ctx.game['motion_tables'], 'motion-gated': ctx.game['motion_tables'],
                  'capture': ctx.game['capture_tables'], 'capture-gated': ctx.game['capture_tables'],
                  'key': ctx.game['key_tables']}[claim]
        if (path, symbol) not in tables or table_entry(ctx, path, symbol, value) is None:  # S-r4
            return f'{token}: {value} is not in that table, or it is not a {claim} table of the manifest'
        axis_said = said.get(axis)
        if axis_said and axis_said[1] == f'{path}:{symbol}' and axis_said[0] == cell:
            return ''
        if claim.endswith('-gated') and axis_said and axis_said[0].startswith(cell.partition(':')[0]):
            return ''
        return f'{token}: that table says {axis_said[0] if axis_said else "nothing"} for {value}, not {cell}'
    if kind == 'src':
        path, _, line = rest.rpartition(':')
        allowed = ctx.repo_files(ctx.game['src_files'].get(axis, []))
        if path not in allowed or not line.isdigit():
            return f'{token}: {path} is not a {axis} file of the manifest (or no line)'
        code = (ctx.text(os.path.join(ctx.args.tree, path), strip_comments=True) or '').splitlines()
        near = code[max(0, int(line) - 4):int(line) + 3]
        in_name = literal(value, in_name=True).search(os.path.basename(path)) is not None and path.endswith('.hlsl')
        return '' if in_name or any(literal(value).search(text) for text in near) else \
            f'{token}: {value} is not in code there (comments do not count)'
    if kind == 'trace':
        trace_id, _, line = rest.partition(':')
        if trace_id not in cited:
            return f'{token}: trace id not in the header'
        own = (ctx.text(cited[trace_id]) or '').splitlines()
        if not line.isdigit() or not (0 < int(line) <= len(own)) or \
                (row['stage'], value) not in {(s, v.upper()) for s, v in IN_TRACE.findall(own[int(line) - 1])}:
            return f'{token}: {value} not on that line as a {row["stage"]}'
        return trace_claim(ctx, claim, value, row['stage'], token)
    if kind == 'rule':
        rule = ctx.rules.get(rest)
        if rule is None:
            return f'{token}: unknown rule'
        if (rule['axis'], rule['verdict'], rule['reason']) != (axis, verdict, full_reason):
            return f'{token}: the rule gives {rule["axis"]} {rule["verdict"]}/{rule["reason"]}, not {axis} {cell} (M3)'
        state = fires(ctx, rule, row, fact, kinds)
        if state is None:
            return f'{token}: facts of {value} missing or partial for the rule (have {", ".join(sorted(kinds)) or "none"}) (R3)'
        if state == 'scope':
            return f'{token}: {value} ({fact.get("family", "?")}) is outside the rule\'s scope'
        return '' if state else f'{token}: the rule does not fire on {value}'
    if kind == 'facts':
        if rest not in claim.split('|'):
            return f'{token}: facts:{rest} does not show {cell} (accepted: facts:{claim})'
        if rest == 'dead':
            return '' if value in ctx.game['dead'] else f'{token}: {value} is not in the manifest\'s dead list'
        if rest == 'proxy' or rest not in FACT_CLAIMS:
            return f'{token}: facts:{rest} is not extracted by the step-0 checker (fails closed)'
        key, needs = FACT_CLAIMS[rest]
        missing = needs - kinds
        if missing:
            return f'{token}: facts of {value} partial (no {", ".join(sorted(missing))}): fails closed (R3)'
        return '' if fact.get(key) else f'{token}: the facts do not show {rest}'
    if kind == 'ours':
        files = ctx.repo_files(ctx.game['ours'])
        return '' if rest in files and literal(value, in_name=True).search(os.path.basename(rest)) else \
            f'{token}: {rest} is not one of our shader files named 0x{value}'
    if kind == 'manifest':
        return '' if rest == 'no-rt' and ctx.game['no_rt'] else f'{token}: the manifest does not say {rest}'
    if kind == 'decision':
        path, _, line = rest.rpartition(':')
        file = ctx.args.handoff if os.path.basename(path) == 'HANDOFF.md' and ctx.args.handoff else \
            ctx.path(path) if ':' in path else os.path.join(ctx.args.tree, path)
        lines = (ctx.text(file) or '').splitlines()
        if not line.isdigit() or not (0 < int(line) <= len(lines)):
            return f'{token}: no such line'
        text = lines[int(line) - 1]
        if 'my pick' in text.lower() or 'pick:' in text.lower():
            return f'{token}: a pick is not ATN\'s answer'
        decided = 'Already decided' in '\n'.join(lines[:int(line)]).rpartition('\n## ')[2]
        return '' if re.search(r'\bATN\b[^\n]*\b(20\d\d-)?\d\d-\d\d\b', text) or decided else \
            f'{token}: not a line ATN answered (no "ATN <date>", not under "Already decided")'
    if kind in ('out', 'test'):
        tool_id, _, line = rest.partition(':')
        tool = ctx.game['tools'].get(tool_id)
        if tool is None:
            return f'{token}: not a registered tool of the manifest'
        lines = (ctx.text(ctx.path(tool['output'])) or '').splitlines()
        if not line.isdigit() or not (0 < int(line) <= len(lines)):
            return f'{token}: no such line in the tool\'s output'
        cells = lines[int(line) - 1].split('\t')
        if len(cells) <= max(tool['hash_column'], tool['verdict_column']) or cells[tool['hash_column']].upper() != value:
            return f'{token}: {value} not on that line'
        mapped = tool['map'].get(cells[tool['verdict_column']])
        return '' if mapped == verdict else f'{token}: the tool says {cells[tool["verdict_column"]]}, not {verdict}'
    if kind in ('twin', 'pair'):
        return f'{token}: per-draw / per-mesh claims need first index and buffers in the traces (not logged yet): fails closed'
    return f'{token}: no check for kind {kind}'


def fires(ctx, rule, row, fact, kinds):
    """True / False; 'scope' outside its scope; None when the facts the rule declares are missing (n-r3-1: the predicate
    gets a strict view of only those kinds, so reading anything else is the same as missing facts)."""
    if row['stage'] not in rule['stages']:
        return False
    try:
        signature = FactsView(fact, kinds, {'signature'})
        if not rule['scope'](signature):
            return 'scope'
        return bool(rule['predicate'](FactsView(fact, kinds, rule['needs'])))
    except KeyError:
        return None


def box_state(ctx, tag, axis, family):
    """('open' / 'ticked' / 'pending', '') or (None, why): the tag must be this row's [audit <game> <axis> <family>]."""
    want = f'[audit {ctx.args.game} {axis} {family}]'
    if tag != want:
        return None, f'needs box:{want} (has {tag or "no box"})'
    boxes = [line for line in (ctx.text(ctx.args.handoff) or '').splitlines() if re.match(r'\s*- \[[ xX]\]', line) and tag in line]
    if len(boxes) == 1:
        return ('open' if boxes[0].lstrip().startswith('- [ ]') else 'ticked'), ''
    pending = ctx.text(ctx.path(ctx.game['box_pending'])) or ''
    if not boxes and any(line.split('\t')[0] == tag for line in pending.splitlines()):
        return 'pending', ''
    return None, f'{tag} is not one HANDOFF box nor a line of the pending file'


def check_cell(ctx, row, axis, cited, key):
    cell, tokens = row[axis], [t.strip() for t in row[axis + '_ev'].split(';') if t.strip()]
    subject = f'{row["hash"]} {row["stage"]} {axis}'
    fact, kinds, error = ctx.facts.get(key, ({}, frozenset(), 'no facts'))
    if not applies(ctx, axis, key, fact, kinds):
        return ([f'{subject}: "{cell}" on a stage this axis does not apply to'] if cell not in ('', '-') else []), None
    if cell in ('', '-', '?'):
        return [f'{subject}: no verdict'], None
    verdict, reason, full_reason = split_cell(cell)
    if (axis, verdict, reason) not in MATRIX:
        return [f'{subject}: {cell} is not in the evidence matrix'], None
    said = table_verdicts(ctx, row['hash'])
    problems = []
    if axis in said and said[axis][0] != cell and not (said[axis][0] == 'CAPTURED' and cell.startswith('CAPTURED/experimental')):
        problems.append(f'{subject}: {said[axis][1]} says {said[axis][0]}, the row says {cell} (contradiction)')
    if axis == 'scale' and row['hash'] in ctx.replaced and not any(ctx.replaced[row['hash']] in t for t in tokens):
        problems.append(f'{subject}: we replace this shader ({ctx.replaced[row["hash"]]}): the verdict must cite it')
    results = [(token, check_token(ctx, row, axis, cell, token, cited, said, fact, kinds)) for token in tokens]
    problems += [f'{subject}: {why}' for _, why in results if why]
    primary = [t for t, why in results if why == '']
    if verdict == 'ACCEPTED':
        primary = [t for t in primary if not t.startswith('decision:')] if any(t.startswith('decision:') for t in primary) else []
    if not primary:
        problems.append(f'{subject}: {cell} without primary evidence that resolves')
    claim = MATRIX[(axis, verdict, reason)].get('trace')
    if claim in CROSS_CHECKED and any(t.startswith('rule:') for t in primary) and \
            trace_lines(ctx, row['hash'], row['stage']):  # a rule never outvotes the traces
        why = trace_claim(ctx, claim, row['hash'], row['stage'], 'the traces')
        if why:
            problems.append(f'{subject}: {cell} by a rule, but {why}')
    missing = GUARDED.get((axis, verdict, reason), set()) - kinds
    if missing:  # R3
        problems.append(f'{subject}: {cell} is guarded by tripwires, facts missing or partial (no {", ".join(sorted(missing))}'
                        f'{"; " + error if error else ""}): fails closed (R3)')
    problems += [f'{subject}: {why}' for why in tripwires(axis, verdict, reason, fact, kinds, primary)]
    for name, rule in ctx.rules.items():  # overrides (M3)
        if rule['axis'] == axis and (rule['verdict'], rule['reason']) != (verdict, full_reason) and \
                fires(ctx, rule, row, fact, kinds) is True and f'override:{name}' not in tokens:
            problems.append(f'{subject}: rule {name} fires here with {rule["verdict"]}/{rule["reason"]}: cite override:{name} '
                            'with primary evidence')
    state = None
    if verdict in GAPS:
        tags = [t[4:] for t in tokens if t.startswith('box:')]
        state, why = box_state(ctx, tags[0] if tags else '', axis, fact.get('family', 'unparsed'))
        if why:
            problems.append(f'{subject}: {cell} {why} (M7, S-r2)')
        elif state == 'ticked':
            problems.append(f'{subject}: box ticked, verdict still {cell}: re-audit the row')
    kind = 'rule:' + next(t[5:] for t in primary if t.startswith('rule:')) if any(t.startswith('rule:') for t in primary) else 'hand'
    return problems, (kind, state, any(t.startswith('override:') for t in tokens))


def tripwires(axis, verdict, reason, fact, kinds, primary):
    """Fixed checks over the facts, on every row whoever settled it (M4)."""
    out = []
    if axis == 'mv' and verdict == 'CAMERA':
        for key, why in (('bone_like', 'bone-like inputs (skinned)'), ('vtf', 'a texture read in the position (vertex texture fetch)'),
                         ('indexed_position', 'a buffer or dynamic-indexed read in the position')):
            if fact.get(key):
                out.append(f'CAMERA on a VS with {why}')
        if fact.get('view_rows_in_position'):
            out.append(f'CAMERA on a VS whose position reads View rows beyond the projection {fact["view_rows_in_position"]} '
                       '(camera-facing, time or view data)')
        if 'view' in kinds and fact.get('view_slot') is None:  # r2-S1
            out.append('CAMERA on a VS the View does not project (no View slot: a screen pass or its own matrices)')
    if axis == 'scale' and verdict == 'N/A' and reason in ('no-screen-read', 'mesh-uv-only'):
        if fact.get('sv_position_used'):
            out.append(f'N/A/{reason} on a shader that uses SV_Position')
        # an interpolant may be a VS-made screen position: only mesh-uv-only (a rule's claim) may read by one
        screen = {'pixel', 'view', 'proj', 'size', 'unknown', 'tex'} | ({'input'} if reason == 'no-screen-read' else set())
        screen = [r for r in fact.get('read_roots', []) if set(r[2]) & screen]
        if screen:
            out.append(f'N/A/{reason} with screen-space, interpolant or dependent reads {screen[:3]}')
    if axis == 'scale' and verdict == 'SCALED' and any(t.startswith('rule:') for t in primary):
        risky = [r for r in fact.get('read_roots', []) if set(r[2]) & {'proj', 'size', 'input'}]
        if risky:
            out.append(f'SCALED by a rule with projected, texture-size or interpolant reads {risky[:3]}')
    if verdict == 'N/A' and reason == 'no-geometry' and fact:
        if fact['stage'] not in ('vs', 'ds', 'gs'):
            out.append(f'no-geometry on a {fact["stage"]}: only a stage with an SV_Position output draws geometry')
        elif not fact.get('screen_space'):
            out.append('no-geometry on a shader whose position reads constants, buffers or textures, or comes from '
                       'an earlier stage')
        elif not fact.get('no_geometry'):  # r2-S2
            out.append('no-geometry in an engine whose screen-space form is not reviewed (manifest screen_space)')
    if axis == 'mv' and verdict == 'N/A' and reason == 'position-only' and fact and not fact['position_only']:
        out.append('N/A/position-only on a VS whose outputs are not SV_Position only')
    if verdict == 'N/A' and reason in ('post', 'ui') and fact.get('stage') == 'vs' and fact.get('view_slot') is not None:
        out.append(f'N/A/{reason} on a VS the View projects (a mesh, not a screen pass or UI)')
    return out


def controls(ctx, rows, found):
    """At least one positive and one negative hand-row control per rule (M3, R3); a positive one is level A (S0-S7)."""
    problems, by_key = [], {(r['hash'], r['stage']): r for r in rows}
    for name, rule in ctx.rules.items():
        own = rule.get('controls', {}).get(ctx.args.game, {'positive': [], 'negative': []})
        if not own['positive'] or not own['negative']:
            problems.append(f'rule {name}: needs at least one positive and one negative control for {ctx.args.game} '
                            f'(has {len(own["positive"])} / {len(own["negative"])}) (R3)')
        for value, want in [(v, True) for v in own['positive']] + [(v, False) for v in own['negative']]:
            row = next((by_key[(value, s)] for s in rule['stages'] if (value, s) in by_key), None)  # n-s0-2
            if row is None:
                problems.append(f'rule {name}: control {value} has no {"/".join(rule["stages"])} row')
                continue
            key = (row['hash'], row['stage'])
            if want and 'A' not in found.get(key, {'tiers': ()})['tiers']:
                problems.append(f'rule {name}: positive control {value} is not level A (in no manifest trace)')
            fact, kinds, _ = ctx.facts.get(key, ({}, frozenset(), None))
            state = fires(ctx, rule, row, fact, kinds)
            same = split_cell(row[rule['axis']])[0::2] == (rule['verdict'], rule['reason'])
            hand = bool(row[rule['axis'] + '_ev']) and 'rule:' not in row[rule['axis'] + '_ev']
            settled = not any(p.startswith(f'{row["hash"]} {row["stage"]} {rule["axis"]}:') for p in ctx.cell_problems)
            if state not in (True, False) or state != want or same != want or not hand or not settled:
                problems.append(f'rule {name}: control {value} ({"positive" if want else "negative"}) fails: fires {state}, '
                                f'verdict {row[rule["axis"]]}, settled without rules {hand}, proven {settled}')
    return problems


# ------------------------------------------------------------------------------------------------ fill (--seed, --apply-rules)
def seed(ctx, header_lines, rows, found):
    have = {(r['hash'], r['stage']) for r in rows}
    added = 0
    for key in sorted(found, key=lambda k: (k[1], k[0])):
        if key in have:
            continue
        fact, kinds, _ = ctx.facts.get(key, ({}, frozenset(), None))
        row = {c: '' for c in COLUMNS} | {'hash': key[0], 'stage': key[1]}
        for axis in AXES:
            row[axis] = '?' if applies(ctx, axis, key, fact, kinds) else '-'
        rows.append(row)
        added += 1
    return added


def apply_rules(ctx, rows):
    filled, conflicts, gaps = collections.Counter(), [], collections.defaultdict(list)
    for row in rows:
        key = (row['hash'], row['stage'])
        fact, kinds, _ = ctx.facts.get(key, ({}, frozenset(), None))
        for axis in AXES:
            if row[axis] != '?':
                continue
            firing = [(name, rule) for name, rule in ctx.rules.items() if rule['axis'] == axis and fires(ctx, rule, row, fact, kinds) is True]
            verdicts = {(r['verdict'], r['reason']) for _, r in firing}
            if len(verdicts) > 1:
                conflicts.append(f'{row["hash"]} {row["stage"]} {axis}: rules disagree: {", ".join(n for n, _ in firing)}')
                continue
            if not firing:
                continue
            name, rule = firing[0]
            claim = MATRIX[(axis, rule['verdict'], rule['reason'].partition(':')[0])].get('trace')
            if claim in CROSS_CHECKED and trace_lines(ctx, row['hash'], row['stage']) and \
                    trace_claim(ctx, claim, row['hash'], row['stage'], 'the traces'):
                conflicts.append(f'{row["hash"]} {row["stage"]} {axis}: {name} fires, the traces disagree ({claim}): left open')
                continue
            row[axis] = rule['verdict'] + (('/' + rule['reason']) if rule['reason'] else '')
            row[axis + '_ev'] = f'rule:{name}'
            if rule['verdict'] in GAPS:
                tag = f'[audit {ctx.args.game} {axis} {fact.get("family", "unparsed")}]'
                row[axis + '_ev'] += f';box:{tag}'
                gaps[tag].append(row['hash'])
            filled[name] += 1
    if gaps:  # M7: one pending line per gap family, written by this run
        path = ctx.path(ctx.game['box_pending'])
        existing = (ctx.text(path) or '').splitlines()
        known = {line.split('\t')[0] for line in existing}
        with open(path, 'a', encoding='utf-8', newline='\n') as out:
            for tag, hashes in sorted(gaps.items()):
                if tag not in known:
                    out.write(f'{tag}\t{ctx.args.game}\t{len(hashes)} rows: {",".join(hashes[:12])}\tcoordinator\n')
    return filled, conflicts


# ------------------------------------------------------------------------------------------------ summary
def census_holes(ctx):
    """What 'complete' does not cover, printed in every summary (n-s0-4, S0-S6, S0-S2)."""
    tables = {'motion': ctx.game['motion_tables'], 'motion-gated': ctx.game['motion_tables'], 'key': ctx.game['key_tables'],
              'capture': ctx.game['capture_tables'], 'capture-gated': ctx.game['capture_tables']}

    def works(axis, kind, claim):
        if kind == 'trace':
            claim = (ctx.game['host_state'] or 'none') if claim == 'host-state' else claim
            return bool(ctx.game['trace_folders']) and claim in TRACE_CLAIMS and \
                (claim not in ('writes-velocity', 'no-velocity') or bool(ctx.engine['velocity_formats'])) and \
                (claim not in ('fullscreen', 'after-ui') or ctx.engine['view'] is not None) and \
                (claim != 'fullscreen' or not ctx.stages & {'gs', 'hs', 'ds'}) and \
                (claim != 'after-upscale' or any(s is not None and s < 1 for s in ctx.scale.values()))
        if kind == 'facts':
            return any(c == 'dead' and ctx.game['dead'] or c in FACT_CLAIMS and
                       ('view' not in FACT_CLAIMS[c][1] or ctx.engine['view'] is not None) and
                       (c != 'no-geometry' or bool(ctx.engine.get('screen_space'))) for c in claim.split('|'))
        return {'table': bool(tables.get(claim)), 'src': bool(ctx.game['src_files'].get(axis)), 'ours': bool(ctx.game['ours']),
                'manifest': ctx.game['no_rt'], 'decision': True, 'out': bool(ctx.game['tools']),
                'test': bool(ctx.game['tools'])}.get(kind, False)  # twin / pair / facts:proxy: not read yet

    stuck = [f'{axis} {verdict}' + (f'/{reason}' if reason else '') for (axis, verdict, reason), accepted in MATRIX.items()
             if not any(works(axis, kind, claim) for kind, claim in accepted.items() if kind != 'rule')]
    out = [f'holes: {"; ".join(ctx.game["holes"]) or "none"}']
    if ctx.excluded:
        out.append(f'not game: {len(ctx.excluded)} (hash, stage) excluded ({", ".join(sorted(set(ctx.excluded.values())))})')
    if ctx.engine['view'] is not None:
        blind = sum(1 for (_, stage), (_, kinds, _) in ctx.facts.items() if stage == 'vs' and 'dataflow' in kinds
                    and 'view' not in kinds)
        out.append(f'VS with dataflow but no View identified: {blind} (scale "-" without a verdict: their own matrices, '
                   'shadow and capture passes)')
    out.append(f'lines without a working non-rule kind: {", ".join(stuck) or "none"}')
    return out


# ------------------------------------------------------------------------------------------------ main
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game')
    parser.add_argument('--ledger')
    parser.add_argument('--tools', required=True)
    parser.add_argument('--tree', default=os.path.abspath(os.path.join(HERE, '..', '..', '..', '..')))
    parser.add_argument('--bin')
    parser.add_argument('--handoff')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--census-only', action='store_true')
    parser.add_argument('--verify-hlsl', action='store_true')
    parser.add_argument('--seed', action='store_true')
    parser.add_argument('--apply-rules', action='store_true')
    parser.add_argument('--family')
    parser.add_argument('--only', nargs='*')
    parser.add_argument('--sample', action='store_true')
    parser.add_argument('--nonce')
    parser.add_argument('--review')
    parser.add_argument('--final', action='store_true')
    parser.add_argument('--selftest')
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest(args)
    if not args.game:
        parser.error('--game is required')
    args.handoff = args.handoff or os.path.join(args.tree, 'HANDOFF.md')
    args.ledger = args.ledger or os.path.join(args.tools, 'audit', f'{args.game}.tsv')
    ctx = Context(args)
    header, rows = ({}, []) if args.census_only else read_ledger(ctx, args.ledger)
    found, traces, broken = census(ctx, header, final=args.final)
    ctx.traces = traces
    ctx.scale = {path: trace_scale(ctx, path) for path in traces}
    ctx.velocity_ps = {(v.upper(), 'ps') for path in traces for line in (ctx.text(path) or '').splitlines()
                       if velocity_target(ctx, line, '0') for v in re.findall(r'\bps ([0-9A-Fa-f]{8})\b', line) if v != '00000000'}
    ctx.replaced = {}
    for path in ctx.repo_files(ctx.game['ours']):
        match = re.search(r'0x([0-9A-Fa-f]{8})', os.path.basename(path))
        if match and (match.group(1).upper(), os.path.basename(path).split('.')[-2][:2]) in found:
            ctx.replaced[match.group(1).upper()] = path
    tiers = collections.Counter('A' if 'A' in v['tiers'] else 'R' if 'R' in v['tiers'] else 'S' for v in found.values())
    census_line = f'census {len(found)} ({", ".join(f"{k} {v}" for k, v in sorted(tiers.items()))})'
    if broken:
        print(f'shader_audit {args.game}: {census_line}, exit 2; checker {digest(__file__)} manifest '
              f'{digest(ctx.manifest_path)} traces {ctx.trace_digest} ({len(traces)} files)')
        print('\n'.join(f'  PROBLEM\t{p}' for p in ctx.problems))
        return 2
    keys = set(found) if args.census_only or args.seed else {(r['hash'], r['stage']) for r in rows} | set()
    ctx.facts, _ = load_facts(ctx, found, keys, args.jobs)
    if args.census_only:
        return census_report(ctx, found, census_line, args)
    header_lines = [line for line in open(args.ledger, encoding='utf-8') if line.startswith('#')] \
        if os.path.isfile(args.ledger) else [f'#! game: {args.game}\n']
    if args.seed or args.apply_rules:
        added = seed(ctx, header_lines, rows, found) if args.seed else 0
        filled, conflicts = apply_rules(ctx, rows) if args.apply_rules else ({}, [])
        write_ledger(args.ledger, header_lines, rows)
        print(f'shader_audit {args.game}: seeded {added} rows, rules filled {sum(filled.values())} cells '
              f'({", ".join(f"{k} {v}" for k, v in sorted(filled.items())) or "none"}); ledger {args.ledger} (.bak kept)')
        for conflict in conflicts[:50]:
            print(f'  CONFLICT\t{conflict}')
        header, rows = read_ledger(ctx, args.ledger)
        ctx.facts, _ = load_facts(ctx, found, {(r['hash'], r['stage']) for r in rows}, args.jobs)
    pack_now = digest(ctx.pack_path) if ctx.pack_path else 'none'
    for spec in header.get('packs', []):  # S7
        recorded = dict(item.partition('=')[::2] for item in spec.split())
        if recorded.get(ctx.game['pack']) != pack_now:
            ctx.problems.append(f'#! packs: reviewed with {ctx.game["pack"]}={recorded.get(ctx.game["pack"])}, the pack is now '
                                f'{pack_now}: re-run and re-sample the changed rules (S7)')
    if args.final and not header.get('packs'):
        ctx.problems.append('#! packs: missing (--final needs the pack hash the ledger was reviewed with)')
    cited = {}  # R1: a header trace id only names a trace a token cites
    for spec in header.get('trace', []):
        trace_id, _, path = spec.partition(' ')
        full = os.path.normcase(os.path.abspath(ctx.path(path.strip()) or ''))
        if full not in ctx.scale:
            ctx.problems.append(f'trace {trace_id}: not a trace of the manifest\'s folders (M1)')
            continue
        cited[trace_id] = full
    only = {v.upper() for v in args.only} if args.only is not None else None
    in_part = (lambda key: (only is None or key[0] in only) and
               (args.family is None or fnmatch.fnmatch(ctx.facts.get(key, ({},))[0].get('family', 'unparsed'), args.family)))
    seen = collections.Counter((r['hash'], r['stage']) for r in rows)
    ctx.problems += [f'{h} {s}: {n} rows' for (h, s), n in seen.items() if n > 1]
    missing = sorted(k for k in found if k not in seen and in_part(k))
    ctx.problems += [f'{h} {s} ({",".join(sorted(found[(h, s)]["tiers"]))}): in the census, no ledger row' for h, s in missing[:10]]
    if len(missing) > 10:
        ctx.problems.append(f'... {len(missing) - 10} more census shaders without a row ({len(missing)} in all)')
    settled, strata, pending, open_cells, ctx.cell_problems = collections.Counter(), collections.defaultdict(list), 0, \
        collections.Counter(), []
    for row in rows:
        key = (row['hash'], row['stage'])
        if not in_part(key):
            continue
        if key not in found:
            ctx.problems.append(f'{row["hash"]} {row["stage"]}: not in the census')
        for axis in AXES:
            problems, result = check_cell(ctx, row, axis, cited, key)
            ctx.cell_problems += problems
            if problems:
                open_cells[(axis, ctx.facts.get(key, ({},))[0].get('family', 'unparsed'))] += 1
            if result and not problems:
                kind, state, override = result
                pending += state == 'pending'
                level = 'A' if 'A' in found.get(key, {'tiers': ()})['tiers'] else 'B'
                verdict = split_cell(row[axis])[0]
                stratum = (axis, verdict, kind, level) if kind == 'hand' else (axis, verdict, kind,
                                                                               ctx.facts[key][0].get('family', 'unparsed'))
                settled[(axis, verdict, kind.partition(':')[0], level)] += 1
                strata[stratum].append((row['hash'], row['stage'], axis, hashlib.sha256(
                    (row[axis] + '|' + row[axis + '_ev']).encode()).hexdigest()[:10], override))
    control_problems = controls(ctx, rows, found)
    picked = sample(ctx, strata, args) if (args.sample or args.review or args.final) else []
    review_problems = review(ctx, picked, args) if (args.review or args.final) else []
    if args.final:
        changed = refacts(ctx, found, picked, rows)
        ctx.problems += [f'facts of {sha[:12]} differ from the cache (S-r3)' for sha in changed]
    problems = control_problems + ctx.problems + ctx.cell_problems + review_problems
    code = 3 if control_problems else 1 if problems else 4 if pending else 0
    if args.final and code == 4:
        code = 1
    part = ''
    if only is not None or args.family:
        code, part = (1 if code in (0, 4) else code), 'PART (--only / --family) '  # N2, n1
    print(f'shader_audit {args.game}: {part}{census_line}, rows {len(rows)}, settled {sum(settled.values())}, open cells '
          f'{sum(open_cells.values())}, pending boxes {pending}, PROBLEMS {len(problems)}, exit {code}; checker '
          f'{digest(__file__)} manifest {digest(ctx.manifest_path)} pack {ctx.game["pack"]}={pack_now} traces {ctx.trace_digest}')
    scales = collections.Counter(ctx.scale.values())
    print(f'  traces: {len(traces)} in the manifest folders (scale read from each: '
          + ', '.join(f'{s} x{n}' for s, n in sorted(scales.items(), key=str)) + '; None never counts as a scale); cited ids: '
          + (', '.join(cited) or 'none'))
    print('\n'.join('  ' + line for line in census_holes(ctx)))
    for stratum, count in sorted(settled.items()):
        print('  settled\t' + '\t'.join(stratum) + f'\t{count}')
    for (axis, family), count in sorted(open_cells.items(), key=lambda item: -item[1])[:40]:
        print(f'  open\t{axis}\t{family}\t{count}')
    if open_cells:  # the sizing gate: boxes / rules needed per axis
        print('  open families: ' + ', '.join(f'{axis} {sum(1 for a, _ in open_cells if a == axis)} families / '
                                              f'{sum(n for (a, _), n in open_cells.items() if a == axis)} cells'
                                              for axis in AXES))
    for text in problems[:400]:
        print(f'  PROBLEM\t{text}')
    if len(problems) > 400:
        print(f'  ... {len(problems) - 400} more problems')
    if args.sample:
        print(f'  sample (nonce {args.nonce!r}): answer each line in review/<game>_<prefix>_<nonce>.tsv '
              '(hash, stage, axis, row digest, AGREE / DISAGREE)')
        print('\n'.join('  review\t' + '\t'.join(item[:4]) for item in picked))
    return code


def sample(ctx, strata, args):
    """The review sample (S1, S2): one seed per stratum from the reviewer's nonce; per rule one row per family in its
    scope (up to 8 families) plus every control and override row; per hand stratum 5 rows, 8 for level A."""
    nonce = args.nonce or (open(args.review, encoding='utf-8').readline().partition('nonce:')[2].strip() if args.review else '')
    if not nonce:
        ctx.problems.append('the sample needs the reviewer\'s nonce (--nonce, or "nonce:" in the review file\'s first line)')
        return []
    picked, by_rule = [], collections.defaultdict(list)
    for stratum, items in sorted(strata.items()):
        if stratum[2] == 'hand':
            seed = hashlib.sha256((nonce + repr(stratum) + repr(sorted(items))).encode()).hexdigest()
            picked += random.Random(seed).sample(sorted(items), min(8 if stratum[3] == 'A' else 5, len(items)))
        else:
            by_rule[stratum[2]].append((stratum[3], items))
    for rule, families in sorted(by_rule.items()):
        seed = hashlib.sha256((nonce + rule + repr(sorted(f for f, _ in families))).encode()).hexdigest()
        chosen = random.Random(seed).sample(sorted(families), min(8, len(families)))
        for family, items in chosen:
            picked.append(random.Random(seed + family).choice(sorted(items)))
    controls_rows = {v for r in ctx.rules.values() for side in r.get('controls', {}).get(ctx.args.game, {}).values() for v in side}
    for items in strata.values():
        picked += [i for i in items if (i[4] or i[0] in controls_rows) and i not in picked]
    return picked


def review(ctx, picked, args):
    if not args.review:
        return ['--final needs --review <the reviewer\'s file>']
    answers = {}
    for line in open(args.review, encoding='utf-8'):
        cells = line.rstrip('\n').split('\t')
        if len(cells) >= 5 and not line.startswith('#'):
            answers[tuple(cells[:4])] = cells[4].upper()
    return [f'review {" ".join(item[:4])}: {answers.get(tuple(item[:4])) or "not answered (or the row changed since)"}'
            for item in picked if answers.get(tuple(item[:4])) != 'AGREE']


def refacts(ctx, found, picked, rows):
    """--final: extract the facts of every sampled and control row again and compare them with the cache (S-r3)."""
    wanted = {(i[0], i[1]) for i in picked}
    wanted |= {(v, r['stage']) for rule in ctx.rules.values() for side in rule.get('controls', {}).get(ctx.args.game, {}).values()
               for v in side for r in rows if r['hash'] == v}
    shas = {found[k]['sha'] for k in wanted if k in found and found[k]['sha']}
    _, changed = load_facts(ctx, found, wanted, 1, force=shas)
    return changed


def census_report(ctx, found, census_line, args):
    stages = collections.Counter(k[1] for k in found)
    kinds = collections.Counter()
    errors = collections.Counter()
    unknown = collections.Counter()
    for key in found:
        fact, have, error = ctx.facts.get(key, ({}, frozenset(), None))
        for kind in KINDS:
            kinds[kind] += kind in have
        if error:
            errors[error.split(':')[0]] += 1
        for op in fact.get('unknown', []):
            unknown[op] += 1
    views = getattr(ctx.pack, 'VIEW_CONTROLS', {}).get(args.game, {})  # S3: the add-on's own View slots
    for value, slot in views.items():
        got = ctx.facts.get((value, 'vs'), ({},))[0].get('view_slot', 'not identified')
        if got != slot:
            ctx.problems.append(f'View control {value}: the add-on reads View slot {slot}, the facts say {got}')
    code = 1 if ctx.problems else 0
    print(f'shader_audit {args.game}: {census_line}, stages {", ".join(f"{s} {n}" for s, n in sorted(stages.items()))}, '
          f'PROBLEMS {len(ctx.problems)}, exit {code}; checker {digest(__file__)} manifest {digest(ctx.manifest_path)} '
          f'traces {ctx.trace_digest}')
    print('  facts: ' + ', '.join(f'{k} {kinds[k]}' for k in KINDS) + f' of {len(found)}'
          + (f'; not extracted: {dict(errors)}' if errors else ''))
    print('\n'.join('  ' + line for line in census_holes(ctx)))
    if views:
        print(f'  View controls: {len(views)} of the add-on\'s View slots checked against the facts')
    if ctx.notes:
        print(f'  notes ({len(ctx.notes)}): ' + '; '.join(ctx.notes[:8]))
    if unknown:
        print('  unknown ops / operands (dataflow kind dropped): ' + ', '.join(f'{k} {v}' for k, v in unknown.most_common(15)))
    if args.verify_hlsl:
        checked, mismatches = verify_hlsl(ctx, found, ctx.facts)
        path = os.path.join(ctx.args.tools, 'audit', f'hlsl_check_{args.game}.tsv')
        with open(path, 'w', encoding='utf-8', newline='\n') as out:
            out.write('hash\tstage\twhy\n' + ''.join(m + '\n' for m in mismatches))
        print(f'  hlsl: {checked} decompiled files compared with their bytecode, {len(mismatches)} disagree ({path})')
    for text in ctx.problems[:100]:
        print(f'  PROBLEM\t{text}')
    return code


def selftest(args):
    """The must-fail fixture (S-r4): each case of <fixture>/cases.tsv runs the checker and must give its exit code and
    contain (or not contain) its strings. Columns: name, game, ledger (in the fixture folder), extra args, exit,
    must contain (| separated), must not contain."""
    failures, count = [], 0
    for line in open(os.path.join(args.selftest, 'cases.tsv'), encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        name, game, ledger, extra, exit_code, contains, absent = (line.rstrip('\n').split('\t') + [''] * 7)[:7]
        command = [sys.executable, '-I', '-B', os.path.abspath(__file__), '--game', game, '--tools', args.tools,
                   '--tree', args.tree, '--ledger', os.path.join(args.selftest, ledger), '--jobs', '1',
                   '--handoff', os.path.join(args.selftest, 'handoff.md')] + extra.replace('{fixture}', args.selftest).split()
        run = subprocess.run(command, capture_output=True, text=True)
        count += 1
        why = []
        if str(run.returncode) != exit_code:
            why.append(f'exit {run.returncode}, want {exit_code}')
        why += [f'missing "{s}"' for s in contains.split('|') if s and s not in run.stdout]
        why += [f'has "{s}"' for s in absent.split('|') if s and s in run.stdout]
        print(f'{"PASS" if not why else "FAIL"}\t{name}' + ('\t' + '; '.join(why) if why else ''))
        if why:
            failures.append(name)
            print('\n'.join('    ' + l for l in (run.stdout + run.stderr).splitlines()[:30]))
    print(f'selftest: {count - len(failures)} of {count} cases pass')
    return 1 if failures or not count else 0


if __name__ == '__main__':
    sys.exit(main())
