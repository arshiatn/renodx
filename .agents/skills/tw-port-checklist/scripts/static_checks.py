"""Static checks for mistakes that shipped once; prints each finding, exit 1 on any.

python -I static_checks.py [--rev <commit>]   (--rev: that commit's files, e.g. a fix's parent as the control that must fail)

1. tonemap: every game folder that calls rt_scene::Install draws the Debug View from its tone mapper: rt_scene::OnTonemapDrawn
   after each anti_aliasing::OnTonemapDrawn, in the same on_drawn. Pharaoh's never did and its Debug View rows stayed hidden;
   the page dump sets debug_view_drawn itself, so only this check sees it.
2. comptr: `&x` passed where a function or named lambda takes a WRL ComPtr<T>* and reads what it holds (`*p`, `p->Get()`,
   or hands `p` on). ComPtr's operator& releases the object first (ComPtrRef's conversion), so such a callee always sees an
   empty ComPtr: GrowBuffer(&member) made 16 MiB of buffers every frame, KeepCamera(&member) a D3D11 buffer every call.
   Pass std::addressof(x). A callee that only writes (`p->GetAddressOf()`) is an out-parameter: `&x` is fine there.
3. views (all of src/, every round, above all after an upstream merge): the view filter (src/utils/resource.hpp
   ViewFilterData) is exact only while every ResourceViewInfo write sets its bit. Inside resource.hpp each access to
   `resource_view_infos` must be one the filter's protocol covers (VIEW_MAP_CALLS: by function), the two wrappers must call
   MarkViewFilter after the write (Update once, Upsert in both lambdas), the pointer forms must call TurnOffViewFilter; outside
   resource.hpp only swapchain_v1.hpp's one raw write (v1 is never selected; its pointer-form calls turn the filter off
   first) and the known pointer-form callers (KNOWN_POINTER_CALLS) may exist. A new raw writer would make the filter answer
   "no clone" for an enabled view (the original texture instead of its upgraded clone).
4. pipelines (all of src/, every round, above all after an upstream merge): the bind cache (src/utils/pipeline.hpp
   BindCacheData) is exact only while every write to `pipeline_infos` bumps its generation inside the write's lock. Inside
   pipeline.hpp each access must be one the protocol covers (PIPELINE_MAP_CALLS: by function), and the writers and the
   bind-info callback list changes must call InvalidateBindCache (PIPELINE_PROTOCOL); utils/shader.hpp's deprecated pointer
   forms must call TurnOffBindCache. Outside pipeline.hpp: no `data->pipeline_infos`, no const_cast to PipelineInfo /
   PipelineShaderDetails (shader.hpp's three known: the pointer forms and PopulateStageState, which hands read-only
   pointers to mods::shader), no write through a `pipeline_details->` / `pipeline_shader_details->` pointer. A new raw
   writer would bind a stale (possibly destroyed) replacement pipeline.
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
UTILS_RESOURCE = 'src/utils/resource.hpp'
# resource.hpp's functions and the view-map calls each may make (the filter's protocol covers these: section 3).
VIEW_MAP_CALLS = {'GetResourceViewInfo': {'try_emplace_p', 'if_contains'}, 'GetResourceViewInfoUnsafe': {'try_emplace_p', 'if_contains_unsafe'},
                  'GetLiveResourceViewInfo': {'if_contains'}, 'UpdateResourceViewInfo': {'modify_if'}, 'UpsertResourceViewInfo': {'lazy_emplace_l'},
                  'ForEachResourceViewInfo': {'for_each'}, 'EmplaceResourceViewInfoOrReuse': {'try_emplace_p'}, 'Use': {'for_each'}}
# The protocol calls each function must make: (callee, how many times).
VIEW_PROTOCOL = {'UpdateResourceViewInfo': ('MarkViewFilter', 1), 'UpsertResourceViewInfo': ('MarkViewFilter', 2),
                 'GetResourceViewInfoUnsafe': ('TurnOffViewFilter', 1), 'EmplaceResourceViewInfoOrReuse': ('TurnOffViewFilter', 1)}
# Raw view-map accesses and pointer-form calls outside resource.hpp, by file: how many (v1 is never selected; ue-extended
# reads only; both turn the filter off before the pointer leaves).
KNOWN_RAW_VIEW_MAP = {'src/mods/swapchain_v1.hpp': 1}
KNOWN_POINTER_CALLS = {'src/mods/swapchain_v1.hpp': 8, 'src/games/ue-extended/addon.cpp': 2}
UTILS_PIPELINE = 'src/utils/pipeline.hpp'
UTILS_SHADER = 'src/utils/shader.hpp'
# pipeline.hpp's functions and the pipeline-map calls each may make (the bind cache's protocol covers these: section 4).
PIPELINE_MAP_CALLS = {'UpdatePipelineInfo': {'modify_if'}, 'GetPipelineInfo': {'if_contains'}, 'GetPipelineShaderDetails': {'if_contains'},
                      'UpdatePipelineShaderDetails': {'modify_if'}, 'OnInitPipeline': {'lazy_emplace_l'},
                      'OnDestroyPipeline': {'modify_if', 'erase_if'}, 'OnDestroyDevice': {'for_each_m', 'erase_if'}}
# The InvalidateBindCache calls each function must make: one a write lambda, one a callback-list change, two in Use.
PIPELINE_PROTOCOL = {'UpdatePipelineInfo': 1, 'UpdatePipelineShaderDetails': 1, 'OnInitPipeline': 2, 'OnDestroyPipeline': 3,
                     'OnDestroyDevice': 2, 'RegisterOnBindInfoCallback': 1, 'UnregisterOnBindInfoCallback': 1, 'Use': 2}
# shader.hpp's const_casts out of the map: the two deprecated pointer forms (they turn the cache off) and PopulateStageState.
KNOWN_PIPELINE_CASTS = {UTILS_SHADER: 3}
COMMENT = re.compile(r'("(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|//[^\n]*|/\*.*?\*/', re.S)


def without_comments(text):
    return COMMENT.sub(lambda match: match.group(1) or re.sub(r'[^\n]', ' ', match.group(0)), text)


def read_files(rev, prefix=GAMES):
    if rev is None:
        files = {path.relative_to(ROOT).as_posix(): path.read_text(encoding='utf-8', errors='replace')
                 for path in (ROOT / prefix).rglob('*') if path.suffix in SUFFIXES}
    else:
        listing = subprocess.run(['git', '-C', str(ROOT), 'ls-tree', '-r', '--name-only', rev, '--', prefix], capture_output=True, text=True,
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


def functions_of(text):
    """(start, name) of resource.hpp's namespace-scope function definitions, in order."""
    return [(match.start(), match.group(1)) for match in re.finditer(r'^(?:inline|static)\b[^;{]*?\b(\w+)\s*\(', text, re.M)]


def enclosing(functions, index):
    names = [name for start, name in functions if start < index]
    return names[-1] if names else None


def pointer_calls(text):
    """GetResourceViewInfo calls without a callback (the pointer forms), and the other pointer-form functions."""
    found = []
    for match in re.finditer(r'(?<![\w:])(?:\w+::)*(GetResourceViewInfo|GetResourceViewInfoUnsafe|EmplaceResourceViewInfoOrReuse)\s*\(', text):
        given, _ = pieces(text, match.end() - 1)
        if match.group(1) != 'GetResourceViewInfo' or len(given) < 2 or not given[1].strip().startswith('['):
            found.append(match.start())
    return found


def check_views(files):
    findings = []
    resource = files.get(UTILS_RESOURCE)
    if resource is None:
        return [f'views: {UTILS_RESOURCE} missing'], 0
    functions = functions_of(resource)
    accesses = 0
    for match in re.finditer(r'\bresource_view_infos\b\s*(?:\.\s*(\w+)|(\[))', resource):
        accesses += 1
        call = match.group(1) or 'operator[]'
        function = enclosing(functions, match.start())
        if call not in VIEW_MAP_CALLS.get(function, set()):
            findings.append(f'views: {UTILS_RESOURCE}:{line_of(resource, match.start())} {function}() calls resource_view_infos.{call}: '
                            'not covered by the view filter\'s protocol (VIEW_MAP_CALLS)')
    for function, (callee, count) in VIEW_PROTOCOL.items():
        bodies = [start for start, name in functions if name == function]
        found = 0
        for start in bodies:
            body_start = resource.index('{', start)
            _, body_end = pieces(resource, body_start)
            found += len(re.findall(rf'\b{callee}\s*\(', resource[body_start:body_end]))
        if found < count:
            findings.append(f'views: {UTILS_RESOURCE} {function}() calls {callee} {found} times, the protocol needs {count}')
    # The pointer form of GetResourceViewInfo (the one with `create`).
    pointer_form = re.search(r'ResourceViewInfo\*\s*GetResourceViewInfo\s*\([^)]*create[^)]*\)\s*\{', resource)
    if pointer_form is None or 'TurnOffViewFilter(' not in resource[pointer_form.end():pieces(resource, pointer_form.end() - 1)[1]]:
        findings.append(f'views: {UTILS_RESOURCE} the pointer-form GetResourceViewInfo does not call TurnOffViewFilter')
    for name, text in files.items():
        if name == UTILS_RESOURCE:
            continue
        raw = [match.start() for match in re.finditer(r'\bresource_view_infos\b', text)]
        if len(raw) > KNOWN_RAW_VIEW_MAP.get(name, 0):
            findings += [f'views: {name}:{line_of(text, index)} accesses resource_view_infos outside resource.hpp (the view filter '
                         "misses its writes: use UpdateResourceViewInfo / UpsertResourceViewInfo)" for index in raw]
        calls = pointer_calls(text)
        if len(calls) > KNOWN_POINTER_CALLS.get(name, 0):
            findings += [f'views: {name}:{line_of(text, index)} a pointer-form ResourceViewInfo call (turns the view filter off for the '
                         'process: use GetResourceViewInfo(view, f) / UpdateResourceViewInfo)' for index in calls]
    return findings, accesses


def calls_in(text, functions, function, callee):
    """How many times `function`'s bodies (every definition of that name) call `callee`."""
    found = 0
    for start, name in functions:
        if name != function:
            continue
        body_start = text.index('{', start)
        _, body_end = pieces(text, body_start)
        found += len(re.findall(rf'\b{callee}\s*\(', text[body_start:body_end]))
    return found


def check_pipelines(files):
    findings = []
    pipeline, shader = files.get(UTILS_PIPELINE), files.get(UTILS_SHADER)
    if pipeline is None or shader is None:
        return [f'pipelines: {UTILS_PIPELINE} or {UTILS_SHADER} missing'], 0
    functions = functions_of(pipeline)
    accesses = 0
    for match in re.finditer(r'\bpipeline_infos\b\s*(?:\.\s*(\w+)|(\[))', pipeline):
        accesses += 1
        call = match.group(1) or 'operator[]'
        function = enclosing(functions, match.start())
        if call not in PIPELINE_MAP_CALLS.get(function, set()):
            findings.append(f'pipelines: {UTILS_PIPELINE}:{line_of(pipeline, match.start())} {function}() calls pipeline_infos.{call}: '
                            'not covered by the bind cache\'s protocol (PIPELINE_MAP_CALLS)')
    for function, count in PIPELINE_PROTOCOL.items():
        if (found := calls_in(pipeline, functions, function, 'InvalidateBindCache')) < count:
            findings.append(f'pipelines: {UTILS_PIPELINE} {function}() calls InvalidateBindCache {found} times, the protocol needs {count}')
    shader_functions = functions_of(shader)
    if (found := calls_in(shader, shader_functions, 'Use', 'InvalidateBindCache')) < 2:
        findings.append(f'pipelines: {UTILS_SHADER} Use() calls InvalidateBindCache {found} times (after its RegisterModule and '
                        'UnregisterModule: the bind-info callback reads the event handler and use_replace_on_bind)')
    pointer_form = re.search(r'PipelineShaderDetails\*\s*GetPipelineShaderDetails\s*\([^)]*\)\s*\{', shader)
    if pointer_form is None or 'TurnOffBindCache(' not in shader[pointer_form.end():pieces(shader, pointer_form.end() - 1)[1]]:
        findings.append(f'pipelines: {UTILS_SHADER} the pointer-form GetPipelineShaderDetails does not call TurnOffBindCache')
    if calls_in(shader, shader_functions, 'GetCurrentState', 'TurnOffBindCache') < 1:
        findings.append(f'pipelines: {UTILS_SHADER} the deprecated GetCurrentState does not call TurnOffBindCache')
    cast = re.compile(r'const_cast\s*<\s*(?:const\s+)?(?:[\w:]*::)?(?:PipelineInfo|PipelineShaderDetails)\b')
    write = re.compile(r'\b(?:pipeline_details|pipeline_shader_details)\s*->\s*[\w.\[\]]+\s*(?:=(?!=)|\+\+|--|[-+|&^]=)'
                       r'|\b(?:pipeline_details|pipeline_shader_details)\s*->\s*[\w.]+\s*\.\s*'
                       r'(?:emplace|emplace_back|push_back|clear|insert|erase|assign|resize|reset)\s*\(')
    for name, text in files.items():
        if name == UTILS_PIPELINE:
            continue
        findings += [f'pipelines: {name}:{line_of(text, match.start())} accesses the pipeline map outside pipeline.hpp (the bind cache '
                     'misses its writes: use UpdatePipelineInfo / UpdatePipelineShaderDetails)'
                     for match in re.finditer(r'\bdata\s*->\s*pipeline_infos\b', text)]
        casts = [match.start() for match in cast.finditer(text)]
        if len(casts) > KNOWN_PIPELINE_CASTS.get(name, 0):
            findings += [f'pipelines: {name}:{line_of(text, index)} a const_cast of the pipeline map\'s info (a write through it '
                         'skips the bind cache\'s generation)' for index in casts]
        findings += [f'pipelines: {name}:{line_of(text, match.start())} writes through a PipelineShaderDetails pointer (use '
                     'UpdatePipelineShaderDetails)' for match in write.finditer(text)]
    return findings, accesses


if __name__ == '__main__':
    rev = sys.argv[sys.argv.index('--rev') + 1] if '--rev' in sys.argv else None
    files = read_files(rev)
    tonemap, hosts = check_tonemap(files)
    comptr, known, takers = check_comptr(files)
    all_files = read_files(rev, 'src/')
    views, view_accesses = check_views(all_files)
    pipelines, pipeline_accesses = check_pipelines(all_files)
    for finding in tonemap + comptr + known + views + pipelines:
        print(finding)
    print(f'tonemap: {len(hosts)} rt_scene hosts, {len(tonemap)} findings; comptr: {len(takers)} callees read a ComPtr<T>* '
          f'({", ".join(sorted(takers))}), {len(comptr)} findings, {len(known)} known; views: {view_accesses} view-map calls in '
          f'resource.hpp, {len(views)} findings; pipelines: {pipeline_accesses} pipeline-map calls in pipeline.hpp, '
          f'{len(pipelines)} findings{f" (at {rev})" if rev else ""}')
    sys.exit(1 if tonemap or comptr or views or pipelines else 0)
