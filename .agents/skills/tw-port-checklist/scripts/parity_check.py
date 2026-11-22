"""Parity check: for every Total War game, what of Attila's package it still lacks (the Nothing dropped rule's gate).

Per game, three sources against one ledger:
  page rows   the game's page dump against Attila's (rows by key, as the maintainer's page_dump/games_compare.py reads them):
              keys only in Attila (each needs a ledger line) and keys only in the game (rows that should go back to Attila:
              the Two ways rule; listed, not open)
  casters     caster_census.py --whole-dump over the game's decompiler dump: every NOT HELD vertex shader needs a ledger line
  ledger      <tools>/parity/<game>.tsv: key or hash <TAB> status <TAB> owner, scene or proof [<TAB> source]; '#' starts a comment.
              The key column holds one or more page keys or 8-digit hashes (space-separated: one row of a parity table), a
              glob (Audio*), section:<Attila section> (every Attila-only key of that section), or feature:<words> (a feature
              without a row, checked on its own).
Statuses: HAVE, DONE, AS ATTILA (Attila lacks it too), WON'T GET (needs its proof), NEEDS A TRACE (needs its scene), SHARED STEP
(needs its owner), DO NOW (always open). Printed as
  OPEN     an Attila-only key or NOT HELD caster without a ledger line; DO NOW; NEEDS A TRACE without a scene, WON'T GET without
           a proof, SHARED STEP without an owner; an unknown status
  waiting  NEEDS A TRACE with its scene, SHARED STEP with its owner
  settled  HAVE, DONE, AS ATTILA, WON'T GET with proof; and any ledger line the tree has made moot (its key is on the game's
           page now, its hash held by the scene now)
Exit 1 if any game has an open line.

usage:
  python -I parity_check.py --tools <maintainer's tools/tw folder> --tree <repo checkout> [--games a th r2 t w w1 w2 p rr s2 | d1 d2 ru ry ta exr]
                            [--dump-tag <tag>] [--tag <tag>] [--bin <folder holding cmd_Decompiler-1.3.16>]
                            [--scale <letter>=<scale census .tsv> ...] [--fxc <fxc.exe>] [--out <report.txt>]

  --dump-tag  take the existing page dumps <tools>/page_dump/page_<letter>_<tag>.txt instead of building them
  --tag       build them with <tools>/page_dump/dump_any.py <letter> <tag> <tree> (default tag: parity; Ryse with --helper, a dump
              taken by --dump-tag must be one too); one compile at a time
  --bin       default <tree>/bin (a worktree links it into the main checkout)
  --scale     print the BUG rows of a cached scale census table (scale_census.py --tsv); replays are not built here

Limits: static. A key in both pages counts as had, whatever the row does (games_compare's defaults and preset values are not
compared here); a row of another name doing Attila's job needs its HAVE line. The census's own limits hold (its family and
pass come from names and bytes; level B = in no trace). Shogun 2's page (one add-on since 2026-11-15, the DLSS overlay's rows
under other key names): most of Attila's keys need a ledger line there. Feature lines are checked only for their status and proof.
The page dumps compile the addon on the CPU (no GPU); the census disassembles with fxc into <tools>/parity/asm/<letter>. Both
wait while <tools>/quiet_run.lock exists (a game run measuring CPU frame times).
"""
import argparse
import collections
import fnmatch
import os
import re
import subprocess
import sys
import time

GAMES = {  # letter: (ledger name, game folder, decompiler pack folder)
    'a': ('attila', 'twattila', 'dump_shader_twattila_pack'),
    'th': ('thrones', 'twthronesofbritannia', 'dump_shader_twthronesofbritannia_pack'),
    'r2': ('rome2', 'twrome2', 'dump_shader_twrome2_pack'),
    't': ('troy', 'twtroy', 'dump_shader_twtroy_pack'),
    'w': ('wh3', 'twwarhammer3', 'dump_shader_twwarhammer3_pack'),
    'w1': ('wh1', 'twwarhammer', 'dump_shader_twwarhammer_pack'),
    'w2': ('wh2', 'twwarhammer2', 'dump_shader_twwarhammer2_pack'),
    'p': ('pharaoh', 'twpharaohdynasties', 'dump_shader_twpharaohdynasties_pack'),
    'rr': ('romeremastered', 'twromeremastered', 'dump_shader_twromeremastered_pack'),
    's2': ('shogun2', 'twshogun2completeedition', 'dump_shader_shogun2_pack'),
    # The Divinity games' censuses as caster dumps (<tools>/../nontw/parity_dumps.py: cso\ + names.tsv), not decompiler dumps.
    'd1': ('divinity1', 'divinityoriginalsinenhanced', 'nontw/divinity1/parity_dump'),
    'd2': ('divinity2', 'divinity2originalsindefinitive', 'nontw/divinity2/parity_dump'),
    # R.U.S.E. (D3D9; no cmd_Decompiler dump: its shaders are in its own census, tools/nontw/ruse/census).
    'ru': ('ruse', 'ruse', None),
    # Ryse, Tales of Arise, Expeditions: Rome (2026-11-22): their rays capture draws of their own, not rt_scene's SCENE_SHADERS.
    'ry': ('ryse', 'rysesonofrome', None),
    'ta': ('tales', 'talesofarise', None),
    'exr': ('expeditions', 'expeditionsrome', None),
}
# Why a game has no caster census here (pack None): its casters are ledger lines.
NO_CENSUS = {
    'ru': 'none (D3D9: no D3D11 casters)',
    'ry': 'none (every opaque G-buffer draw by its shape: ray_tracing.hpp SceneDraw; caster lines in the ledger)',
    'ta': 'none (the prepass by rt_shadows lists: ray_tracing.hpp SceneDraw; census tools/nontw/tales/census/census_t10_casters.txt)',
    'exr': 'none (the depth prepass by its shape: ray_tracing.hpp SceneDraw; caster lines in the ledger)',
}
# Checked only when named (--games d1 d2 ru ry ta exr): not Total War games, their ledgers held to Attila's page all the same.
NOT_TOTAL_WAR = ('d1', 'd2', 'ru', 'ry', 'ta', 'exr')
# Pages dumped in the helper-present state (dump_any.py --helper): Ryse's hand-drawn Graphics 1 of 2 / 2 of 2 list their
# anti-aliasing and frame-generation widgets only with the helper beside the add-on.
HELPER_PAGE = ('ry',)
SETTLED = ('HAVE', 'DONE', 'AS ATTILA', "WON'T GET")
WAITING = ('NEEDS A TRACE', 'SHARED STEP')
HERE = os.path.dirname(os.path.abspath(__file__))


def read_rows(path):
    """{key: row fields} of a page dump's '== rows' sections (page_dump_body.h's format; Shogun 2's two add-ons both)."""
    rows = {}
    section = None
    for line in open(path, encoding='utf-8', errors='replace'):
        if line.startswith('== '):
            section = line.split()[1]
        elif section == 'rows' and '\t' in line:
            fields = dict(re.findall(r'(\w+)=([^\t]*)', line.rstrip('\n').split('\t', 1)[1]))
            if fields.get('key'):
                rows.setdefault(fields['key'], fields)
    return rows


def read_ledger(path):
    lines = []
    for number, line in enumerate(open(path, encoding='utf-8') if os.path.exists(path) else [], 1):
        if not line.strip() or line.startswith('#'):
            continue
        cells = line.rstrip('\n').split('\t') + ['', '', '']
        key = cells[0].strip()
        keys = [key] if key.startswith(('section:', 'feature:')) else key.replace(',', ' ').split()
        lines.append({'key': key, 'keys': [k.upper() if re.fullmatch(r'[0-9A-Fa-f]{8}', k) else k for k in keys],
                      'status': cells[1].strip().upper(), 'note': cells[2].strip(), 'source': cells[3].strip(), 'line': number})
    return lines


def census(args, letter, tools):
    """{vs hash: verdict} of caster_census.py --whole-dump for the game, and the census's summary line."""
    _, folder, pack = GAMES[letter]
    if pack is None:
        return {}, NO_CENSUS[letter]
    root = os.path.join(args.bin, 'cmd_Decompiler-1.3.16')
    # A pack with a folder in its name lives in the maintainer's tools (beside tools/tw), not under bin.
    dump = os.path.join(os.path.dirname(tools), pack) if '/' in pack else os.path.join(root, pack)
    if not os.path.isdir(dump):
        found = [name for name in os.listdir(root) if name.startswith('dump_shader') and name.endswith('_pack') and
                 folder.replace('tw', '', 1) in name] if os.path.isdir(root) else []
        if not found:
            return None, f'no decompiler dump {pack} under {root}'
        dump = os.path.join(root, found[0])
    out = os.path.join(tools, 'parity', f'census_{letter}.txt')
    cmd = [sys.executable, '-I', os.path.join(HERE, 'caster_census.py'), '--whole-dump', '--dump', dump, '--cache',
           os.path.join(tools, 'parity', 'asm', letter), '--addon', os.path.join(args.tree, 'src', 'games', folder, 'addon.cpp'),
           '--out', out]
    if args.fxc:
        cmd += ['--fxc', args.fxc]
    while os.path.exists(os.path.join(tools, 'quiet_run.lock')):  # a game run measuring CPU frame times: no long job meanwhile
        print('quiet_run.lock: waiting', file=sys.stderr, flush=True)
        time.sleep(120)
    result =subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    if result.returncode != 0 or not os.path.exists(out):
        return None, 'caster_census.py failed: ' + (result.stderr.strip().splitlines() or ['?'])[-1]
    verdicts = {}
    text = open(out, encoding='utf-8').read()
    for found in re.finditer(r'^([0-9A-F]{8})\t([^\t]+)\t([^\t]*)\t([^\t]*)\t([^\t]*)\t[^\t]*\t([^\t]*)', text, re.M):
        verdicts[found.group(1)] = {'verdict': found.group(2), 'family': found.group(3), 'pass': found.group(4),
                                    'why': found.group(5), 'names': found.group(6)}
    return verdicts, text.splitlines()[1] if text.count('\n') > 1 else ''


def page_dump(args, letter, tools):
    path = os.path.join(tools, 'page_dump', f'page_{letter}_{args.dump_tag or args.tag}.txt')
    if not args.dump_tag:
        result = subprocess.run([sys.executable, '-I', os.path.join(tools, 'page_dump', 'dump_any.py'), letter, args.tag, args.tree]
                                + (['--helper'] if letter in HELPER_PAGE else []), capture_output=True, text=True, errors='replace')
        if result.returncode != 0:
            return None, 'dump_any.py failed: ' + ' / '.join((result.stdout + result.stderr).strip().splitlines()[-3:])
    if not os.path.exists(path):
        return None, f'no page dump {path}'
    return read_rows(path), path


def match(line_key, key, attila_row):
    if line_key == key:
        return True
    if line_key.startswith('section:'):
        return attila_row is not None and attila_row.get('section', '') == line_key[8:].strip()
    return '*' in line_key and fnmatch.fnmatchcase(key, line_key)


def check_line(line):
    """Why a ledger line is open ('' if not), and its state: settled / waiting / open."""
    status, note = line['status'], line['note']
    if status in SETTLED:
        if status == "WON'T GET" and len(note.split()) < 2:
            return 'WON\'T GET without its proof', 'open'
        return '', 'settled'
    if status == 'NEEDS A TRACE':
        return ('', 'waiting') if len(note.split()) >= 3 else ('NEEDS A TRACE without a named scene', 'open')
    if status == 'SHARED STEP':
        return ('', 'waiting') if note else ('SHARED STEP without an owner', 'open')
    if status == 'DO NOW':
        return 'DO NOW, not done', 'open'
    return f'unknown status {status!r}', 'open'


def check_game(args, letter, tools, attila_rows, out):
    name = GAMES[letter][0]
    ledger_path = os.path.join(tools, 'parity', f'{name}.tsv')
    ledger = read_ledger(ledger_path)
    used = set()
    opened, waiting, settled = [], [], collections.Counter()
    head = [f'== {name} ({letter}): ledger {os.path.basename(ledger_path)} ({len(ledger)} lines{"" if ledger else ", none yet"})']

    def find(key, attila_row=None):
        for index, line in enumerate(ledger):
            if any(match(line_key, key, attila_row) for line_key in line['keys']):
                used.add(index)
                return line
        return None

    def take(subject, line):
        why, state = check_line(line)
        text = f'{subject}\t{line["status"]}\t{line["note"][:150]}'
        if state == 'open':
            opened.append(f'{subject}\t{why}\t{line["note"][:150]}')
        elif state == 'waiting':
            waiting.append(text)
        else:
            settled[line['status']] += 1

    # Page rows.
    only_here = []
    if letter != 'a':
        rows, where = page_dump(args, letter, tools)
        if rows is None:
            opened.append(f'(page)\tnot checked: {where}')
            rows = {}
        else:
            only_attila = sorted(attila_rows.keys() - rows.keys())
            only_here = sorted(rows.keys() - attila_rows.keys())
            head.append(f'page: {os.path.basename(where)} {len(rows)} keys; only in Attila {len(only_attila)}, only here {len(only_here)}')
            for key in only_attila:
                row = attila_rows[key]
                subject = f'{key} [{row.get("section", "")}] {row.get("label", "")[:40]!r}'
                line = find(key, row)
                if line is None:
                    opened.append(f'{subject}\tAttila row without a ledger line')
                else:
                    take(subject, line)
    else:
        rows = attila_rows

    # Casters.
    verdicts, summary = census(args, letter, tools)
    if verdicts is None:
        opened.append(f'(casters)\tnot checked: {summary}')
        verdicts = {}
    else:
        head.append('casters: ' + summary)
    for vs, row in sorted(verdicts.items()):
        if row['verdict'] != 'NOT HELD':
            continue
        subject = f'{vs} {row["family"]} {row["pass"]} {row["names"].split(";")[0][:40]}'
        line = find(vs)
        if line is None:
            opened.append(f'{subject}\tNOT HELD caster without a ledger line ({row["why"][:60]})')
        else:
            take(subject, line)

    # Ledger lines nothing above used: features, and keys / hashes the tree may have made moot.
    for index, line in enumerate(ledger):
        if index in used:
            continue
        keys = line['keys']
        if all(key in rows or (key in verdicts and verdicts[key]['verdict'] != 'NOT HELD') for key in keys):
            settled['moot (on the page / held now)'] += 1
        elif keys[0].startswith('feature:') or line['status'] in WAITING or line['status'] == 'DO NOW':
            take(line['key'][:80], line)
        else:
            settled['not in this tree\'s gaps'] += 1

    head.append(f'open {len(opened)}, waiting {len(waiting)}, settled {sum(settled.values())} '
                f'({", ".join(f"{status} {count}" for status, count in sorted(settled.items()))})')
    out += head
    out += [f'  OPEN\t{text}' for text in opened]
    out += [f'  waiting\t{text}' for text in waiting]
    if only_here:
        out.append(f'  only here (back to Attila? the Two ways rule): {" ".join(only_here)}')
    for entry in args.scale:
        game, _, table = entry.partition('=')
        if game == letter:
            out.append(f'  scale census BUG rows ({os.path.basename(table)}):')
            for line in open(table, encoding='utf-8', errors='replace'):
                cells = line.rstrip('\n').split('\t')
                if len(cells) > 4 and cells[3].startswith('BUG'):
                    out.append(f'    {cells[0]} {cells[1]} {cells[3]}: {cells[4][:140]}')
    out.append('')
    return len(opened), len(waiting), sum(settled.values())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tools', required=True)
    parser.add_argument('--tree', required=True)
    parser.add_argument('--games', nargs='+', default=[letter for letter in GAMES if letter not in NOT_TOTAL_WAR])
    parser.add_argument('--dump-tag')
    parser.add_argument('--tag', default='parity')
    parser.add_argument('--bin')
    parser.add_argument('--scale', action='append', default=[])
    parser.add_argument('--fxc')
    parser.add_argument('--out')
    args = parser.parse_args()
    args.tree = os.path.abspath(args.tree)
    args.bin = args.bin or os.path.join(args.tree, 'bin')
    tools = os.path.abspath(args.tools)
    os.makedirs(os.path.join(tools, 'parity'), exist_ok=True)
    attila_rows, where = page_dump(args, 'a', tools)
    if attila_rows is None:
        sys.exit(f"Attila's page dump: {where}")
    out = [f'Parity check: tree {args.tree}, Attila page {os.path.basename(where)} ({len(attila_rows)} keys)', '']
    totals = {}
    for letter in args.games:
        totals[letter] = check_game(args, letter, tools, attila_rows, out)
    out.append('game\topen\twaiting\tsettled')
    out += [f'{GAMES[letter][0]}\t{opened}\t{waiting}\t{settled}' for letter, (opened, waiting, settled) in totals.items()]
    text = '\n'.join(out) + '\n'
    if args.out:
        open(args.out, 'w', encoding='utf-8').write(text)
    sys.stdout.write(text)
    sys.exit(1 if any(opened for opened, _, _ in totals.values()) else 0)


main()
