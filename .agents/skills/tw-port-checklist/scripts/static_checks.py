"""Static checks for mistakes that shipped once; prints each finding, exit 1 on any.

python -I static_checks.py [--rev <commit>]   (--rev: that commit's files, e.g. a fix's parent as the control that must fail)

1. tonemap: every game folder that calls rt_scene::Install draws the Debug View from its tone mapper: rt_scene::OnTonemapDrawn
   after each anti_aliasing::OnTonemapDrawn, in the same on_drawn. Pharaoh's never did and its Debug View rows stayed hidden;
   the page dump sets debug_view_drawn itself, so only this check sees it.
2. comptr: `&x` passed where a function or named lambda takes a WRL ComPtr<T>* and reads what it holds (`*p`, `p->Get()`,
   or hands `p` on). ComPtr's operator& releases the object first (ComPtrRef's conversion), so such a callee always sees an
   empty ComPtr: GrowBuffer(&member) made 16 MiB of buffers every frame, KeepCamera(&member) a D3D11 buffer every call.
   Pass std::addressof(x). A callee that only writes (`p->GetAddressOf()`) is an out-parameter: `&x` is fine there.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
GAMES = 'src/games/'
SUFFIXES = ('.hpp', '.cpp', '.h')
CLOSE = {'(': ')', '[': ']', '{': '}', '<': '>'}
KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'decltype', 'static_cast', 'reinterpret_cast', 'const_cast'}
# Left on purpose (HANDOFF "CPU ROUND"): made new on purpose until the maintainer decides.
KNOWN = {('src/games/twattila/dlaa_helper/rt_d3d12.hpp', '&sharc_buffers_[index]'): "SHaRC's cache never outlives a frame",
         ('src/games/twattila/dlaa_helper/rt_d3d12.hpp', '&gpu.uvs'): 'GPU micromap bake, per bake',
         ('src/games/twattila/dlaa_helper/rt_d3d12.hpp', '&gpu.transient[pool]'): 'GPU micromap bake, per bake'}
COMMENT = re.compile(r'("(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|//[^\n]*|/\*.*?\*/', re.S)


def without_comments(text):
    return COMMENT.sub(lambda match: match.group(1) or re.sub(r'[^\n]', ' ', match.group(0)), text)


def read_files(rev):
    if rev is None:
        files = {path.relative_to(ROOT).as_posix(): path.read_text(encoding='utf-8', errors='replace')
                 for path in (ROOT / GAMES).rglob('*') if path.suffix in SUFFIXES}
    else:
        listing = subprocess.run(['git', '-C', str(ROOT), 'ls-tree', '-r', '--name-only', rev, '--', GAMES], capture_output=True, text=True,
                                 check=True).stdout
        names = [name for name in listing.splitlines() if name.endswith(SUFFIXES)]
        batch = subprocess.run(['git', '-C', str(ROOT), 'cat-file', '--batch'], input=''.join(f'{rev}:{name}\n' for name in names).encode(),
                               capture_output=True, check=True).stdout
        files, at = {}, 0
        for name in names:
            header_end = batch.index(b'\n', at)
            size = int(batch[at:header_end].split()[2])
            files[name] = batch[header_end + 1:header_end + 1 + size].decode('utf-8', errors='replace')
            at = header_end + 1 + size + 1
    return {name: without_comments(text) for name, text in files.items()}


def pieces(text, start, brackets='([{'):
    """The top-level comma-separated pieces inside the bracket at `start`, and the index after its match."""
    found, stack, begin = [], [], start + 1
    for index in range(start, len(text)):
        char = text[index]
        if char in brackets:
            stack.append(CLOSE[char])
        elif stack and char == stack[-1]:
            stack.pop()
            if not stack:
                found.append(text[begin:index])
                return found, index + 1
        elif char == ',' and len(stack) == 1:
            found.append(text[begin:index])
            begin = index + 1
    return found, len(text)


def line_of(text, index):
    return text.count('\n', 0, index) + 1


def check_tonemap(files):
    findings = []
    hosts = sorted({name.split('/')[2] for name, text in files.items() if 'rt_scene::Install(' in text})
    for host in hosts:
        texts = {name: text for name, text in files.items() if name.startswith(f'{GAMES}{host}/')}
        if not any('rt_scene::OnTonemapDrawn(' in text for text in texts.values()):
            findings.append(f'tonemap: {host} installs rt_scene but never calls rt_scene::OnTonemapDrawn (its Debug View rows stay hidden)')
            continue
        for name, text in texts.items():
            for match in re.finditer(r'anti_aliasing::OnTonemapDrawn\(', text):
                if 'rt_scene::OnTonemapDrawn(' not in text[match.end():text.find('}', match.end())]:
                    findings.append(f'tonemap: {name}:{line_of(text, match.start())} anti_aliasing::OnTonemapDrawn with no rt_scene::OnTonemapDrawn after it')
    return findings, hosts


def reads_held(body, parameter):
    """Whether a body reads what the ComPtr<T>* `parameter` held before its first write (handing the pointer on counts as a
    read: unknown). Written first (GetAddressOf, Reset, `*p = `): an out-parameter."""
    p = re.escape(parameter)
    write = re.search(rf'\b{p}\s*->\s*(?:GetAddressOf|ReleaseAndGetAddressOf|Reset)\b|\*\s*{p}\s*=(?!=)', body)
    read = re.search(rf'\*\s*{p}\b(?!\s*=(?!=))|\b{p}\s*->\s*(?!GetAddressOf\b|ReleaseAndGetAddressOf\b|Reset\b)\w|[(,]\s*{p}\s*[,)]', body)
    return read is not None and (write is None or read.start() < write.start())


def check_comptr(files):
    # (file, name) of every function or named lambda whose ComPtr<T>* parameter at a place is read: the places.
    takers = {}
    for name, text in files.items():
        for match in re.finditer(r'\bComPtr\s*<', text):
            _, type_end = pieces(text, match.end() - 1, '<')
            declared = re.match(r'\s*\*(?!\*)\s*(\w*)\s*[,)=]', text[type_end:])
            if not declared or not declared.group(1):
                continue
            depth, index = 0, match.start() - 1
            while index >= 0 and not (depth == 0 and text[index] in '([{'):
                depth += (text[index] in ')]}') - (text[index] in '([{')
                index -= 1
            if index < 0 or text[index] != '(':
                continue
            head = text[max(0, index - 200):index]
            named_lambda = re.search(r'\b(\w+)\s*=\s*\[[^\]]*\]\s*$', head)
            called = re.search(r'(\w+)\s*$', head)
            function = named_lambda.group(1) if named_lambda else (called.group(1) if called else None)
            if function is None or function in KEYWORDS:
                continue
            _, parameters_end = pieces(text, index)
            body_start = re.match(r'[^;{]*', text[parameters_end:]).end() + parameters_end
            if body_start >= len(text) or text[body_start] != '{':
                continue  # a declaration: its definition is read where it is
            _, body_end = pieces(text, body_start)
            if not reads_held(text[body_start:body_end], declared.group(1)):
                continue
            before, _ = pieces(text[:match.start()] + ')', index, '([{<')
            takers.setdefault(function, {}).setdefault(name, set()).add(len(before) - 1)
    findings, known = [], []
    for name, text in files.items():
        for function, definitions in takers.items():
            places = definitions.get(name) or set().union(*definitions.values())
            for match in re.finditer(rf'(?<![\w:])(?:\w+::)*{function}\s*\(', text):
                given, _ = pieces(text, match.end() - 1)
                for place in places:
                    if place >= len(given) or not re.fullmatch(r'\s*&(?!&)\s*[\w.\->\[\]]+\s*', given[place]):
                        continue
                    argument = given[place].strip()
                    line = f'comptr: {name}:{line_of(text, match.start())} {function}(.. {argument} ..)'
                    if (name, argument) in KNOWN:
                        known.append(f'{line}: known, {KNOWN[(name, argument)]}')
                    else:
                        findings.append(f'{line}: ComPtr\'s & releases what it holds first: std::addressof({argument[1:].strip()})')
    return findings, known, takers


if __name__ == '__main__':
    rev = sys.argv[sys.argv.index('--rev') + 1] if '--rev' in sys.argv else None
    files = read_files(rev)
    tonemap, hosts = check_tonemap(files)
    comptr, known, takers = check_comptr(files)
    for finding in tonemap + comptr + known:
        print(finding)
    print(f'tonemap: {len(hosts)} rt_scene hosts, {len(tonemap)} findings; comptr: {len(takers)} callees read a ComPtr<T>* '
          f'({", ".join(sorted(takers))}), {len(comptr)} findings, {len(known)} known{f" (at {rev})" if rev else ""}')
    sys.exit(1 if tonemap or comptr else 0)
