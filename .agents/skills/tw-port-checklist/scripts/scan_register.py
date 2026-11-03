"""Finds command_action::Register calls without callback data whose lambda takes a template T it can't deduce."""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[4] / 'src'
sources = {path: path.read_text(encoding='utf-8', errors='replace') for path in root.rglob('*') if path.suffix in ('.hpp', '.cpp', '.h')}
definitions = {}
for path, text in sources.items():
    for match in re.finditer(r'constexpr auto (\w+) = \[\]<typename T, typename Context>', text):
        definitions.setdefault(match.group(1), []).append(path)
for path, text in sources.items():
    for match in re.finditer(r'command_action::Register\(\s*([\w:]+)\s*,\s*(\{[^{}]*\})\s*(,\s*[^)]*)?\)', text):
        name = match.group(1).split('::')[-1]
        if match.group(3):
            continue
        if name in definitions:
            line = text.count('\n', 0, match.start()) + 1
            print(f'{path.relative_to(root)}:{line} {name} (T lambda in {", ".join(str(p.relative_to(root)) for p in definitions[name])})')
