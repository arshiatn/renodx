"""Shader-audit manifest (G24): what every game's census is, which tables and files may prove a verdict, and the engine
knowledge the facts use. Checker-owned and reviewed: audit implementers never edit it (a needed change goes through the
coordinator and the same reviewer); the checker prints this file's SHA-256 in every summary line.

Roots of every path: tools (--tools), bin (--bin, default <repo>/bin), repo (--tree).

ENGINES[engine]:
  view            how the facts find Unreal's View uniform buffer (None: the engine has no identified view rows, so CAMERA,
                  scale N/A and the roots claims fail closed until it gets one):
                    min_rows      a View buffer's declared size is at least this many registers (row 62 / 66, the
                                  PreViewTranslation every mesh VS reads, or the size rows 122 / 123 a pass reads)
                    projection    rows the final clip position may read (TranslatedWorldToClip, WorldToClip)
                    translation   PreViewTranslation (world -> translated world, the camera-relative origin)
                    previous      the previous frame's TranslatedWorldToClip (a velocity VS reads it)
  velocity_formats DXGI formats ('f<number>' as traces print them) of the engine's velocity target; empty: the velocity
                  trace claims fail closed
  screen_space    True: a VS whose clip position is only its vertex inputs, vertex / instance id and literals is a
                  screen pass (facts no-geometry); False until the engine's pack is reviewed for it (r2-S2)
  primitive_id    the uint vertex input that indexes the GPU Scene primitive's data (Unreal 4.22+ LocalVertexFactory):
                  a structured buffer read only by it is per-draw data, not bones (r2-M1); every one of Expeditions' 603
                  VS with a uint ATTRIBUTE13 reads exactly one structured buffer by it alone and by nothing else

GAMES[game]:
  engine, pack    ENGINES key; rule pack module in this folder (None: no rules yet)
  sources         (tier S shipped / R runtime dump, root, path, glob, floor): floor = unique (hash, stage) the source must
                  yield; when a (hash, stage) is in several files the first source's file is the census file
  not_game        {(hash, stage): where}: shaders in a dump that are not the game's (an injected DLL's); never counted
  trace_folders, trace_floor, trace_files   traces (only these give level A), the unique (hash, stage) they must hold and
                  the trace files they must hold (the summary prints a digest of their paths and sizes)
  holes           what the census can't see (printed in every summary)
  axes            stages per axis; scale also applies to a VS with a projected output, mv to a PS that a trace shows
                  writing the velocity target, rt to a PS that can change a caster's shape when rt_ps is set
  capture_tables / motion_tables / key_tables   {(repo file, SYMBOL): None = membership, or (verdict, regex) = the
                  entry's deciding field: an entry matching the regex gives that verdict}; one line per deciding field
  host_state      the trace claim that shows the RT host takes a draw (rt CAPTURED by trace:), or None
  src_files       repo files or globs src: may cite, per axis
  ours            repo globs whose 0x<HASH> file names are shaders we build or replace
  tools           registered commands for out: / test: {id: {cmd, output, hash_column, verdict_column, map}}
  dead            hashes known never to load; no_rt: the game has no RT host
  box_pending     the per-game file gap boxes wait in until the coordinator copies them into HANDOFF
"""

UE_VIEW_421 = {'min_rows': 63, 'projection': range(0, 8), 'translation': (62,), 'previous': range(83, 87)}
# 4.23 adds ClipToWorld (rows 8-11): every row from TranslatedWorldToView on lies 4 rows later.
UE_VIEW_423 = {'min_rows': 67, 'projection': range(0, 8), 'translation': (66,), 'previous': range(87, 91)}

ENGINES = {
    'ue4.21': {'view': UE_VIEW_421, 'velocity_formats': ('f35',), 'screen_space': True},
    'ue4.23': {'view': UE_VIEW_423, 'velocity_formats': ('f35',), 'screen_space': True, 'primitive_id': 'ATTRIBUTE13'},
    'warscape': {'view': None, 'velocity_formats': (), 'screen_space': False},
    'cryengine': {'view': None, 'velocity_formats': (), 'screen_space': False},
    'divinity': {'view': None, 'velocity_formats': (), 'screen_space': False},  # Larian's Divinity Engine (D3D11)
    'iriszoom': {'view': None, 'velocity_formats': (), 'screen_space': False},  # Eugen's IrisZoom (D3D9: no facts yet)
}

DUMPS = 'cmd_Decompiler-1.3.16/'
TALES_RT = 'src/games/talesofarise_dlss/rt_shadows.hpp'
# g24_'s completeness check (2026-11-24): every shader in the game's own files against the dumps. The shaders the dumps
# missed were extracted read-only to tools/audit/missing/<game>/cso (compare.tsv: hash, model, the file it came from).
MISSING = 'audit/missing/'
DXGI = {(value, stage): 'dxgi.dll in the game folder' for value, stage in (
    ('1261561B', 'ps'), ('18399C0C', 'vs'), ('93196DEA', 'cs'), ('B1F08524', 'vs'), ('C1955997', 'ps'))}
TW_HOLE = 'packs installed on ATN\'s PC only (DLC he does not own is not in the dump)'


def census_only(engine, sources, holes, game):
    """A game with a census and no rule pack yet ('game': its src/games folder)."""
    return {
        'engine': engine, 'pack': None, 'sources': sources, 'not_game': {},
        'trace_folders': [], 'trace_floor': 0, 'trace_files': 0, 'holes': holes,
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs', 'ds'}}, 'rt_ps': False,
        'capture_tables': {}, 'motion_tables': {}, 'key_tables': {}, 'host_state': None,
        'src_files': {}, 'ours': [f'src/games/{game}/**/*0x*.hlsl'], 'tools': {}, 'dead': set(), 'no_rt': False,
        'box_pending': f'tools:audit/boxes_pending_{game}.tsv',
    }


def tw(folder, floor, game):
    """A Total War game: census only until the Warscape pack (parity_check.py's dump folder names)."""
    return census_only('warscape', [('S', 'bin', DUMPS + folder, '**/*.cso', floor)], [TW_HOLE], game)


def pack_dump(game, shipped, runtime=0):
    """shd_'s pack dumps (2026-11-24): every shader of the game's own files in cso\\, run-time built ones in runtime\\;
    not_game.tsv lists the shaders of injected and library DLLs, which are not dumped."""
    folder = f'{DUMPS}dump_shader_{game}_pack/'
    return [('S', 'bin', folder + 'cso', '*.cso', shipped)] + \
        ([('R', 'bin', folder + 'runtime', '*.cso', runtime)] if runtime else [])


def blood(game, count, pack):
    """A Total War game whose dump skipped its blood pack (tw_shaders.py skips packs named blood*)."""
    entry = GAMES[game]
    return {'sources': entry['sources'] + [('S', 'tools', MISSING + game + '/cso', '*.cso', count)],
            'holes': entry['holes'] + [f'the dump skipped {pack}: its {count} shaders come from {MISSING}{game}']}


GAMES = {
    'tales': {
        'engine': 'ue4.21', 'pack': 'ue4',
        # ATN 2026-11-24: the census is ATN's startup dump (root + New_1), not the paks (never decrypted) or traces.
        'sources': [('R', 'bin', DUMPS + 'dump_shaders_tales_of_arise', '**/*.cso', 13525)], 'not_game': {},
        'trace_folders': ['tools:nontw/tales/g1g2'], 'trace_floor': 393, 'trace_files': 27,
        'holes': ['shaders the game compiles after startup (materials streamed in later): more runtime dumps when ATN '
                  'plays; the encrypted paks are never read'],
        # rt on a PS only where it can change a caster's shape (MASKED_SHADERS: masked materials' depth PS)
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs'}}, 'rt_ps': True,
        'capture_tables': {(TALES_RT, 'DEPTH_SHADERS'): None,
                           (TALES_RT, 'MASKED_SHADERS'): ('SKIPPED/opacity-never', r'Opacity::NEVER')},
        'motion_tables': {},
        'key_tables': {(TALES_RT, 'SKINNED_MASKED_SHADERS'): None},
        'host_state': None,
        'src_files': {
            'scale': ['src/games/talesofarise/addon.cpp', 'src/games/talesofarise/dlss_graphics.hpp',
                      'src/games/talesofarise/**/*0x*.hlsl'],
            'rt': [TALES_RT, 'src/games/talesofarise/ray_tracing.hpp'],
            'mv': ['src/games/talesofarise/dlss_graphics.hpp'],
        },
        'ours': ['src/games/talesofarise/**/*0x*.hlsl', 'src/games/talesofarise_dlss/**/*0x*.hlsl'],
        'tools': {}, 'dead': set(), 'no_rt': False,
        'box_pending': 'tools:audit/boxes_pending_tales.tsv',
    },
    'expeditions': {
        'engine': 'ue4.23', 'pack': 'ue4',
        # ATN's startup dump: all_shaders_ holds every shader, the root a renamed copy of some (same bytes).
        'sources': [('R', 'bin', DUMPS + 'dump_shaders_Expeditions_rome/all_shaders_', '*.cso', 15735),
                    ('R', 'bin', DUMPS + 'dump_shaders_Expeditions_rome', '*.cso', 1248)], 'not_game': {},
        'trace_folders': ['tools:nontw/expeditions/g1'], 'trace_floor': 434, 'trace_files': 18,
        'holes': ['shaders compiled after startup; -d3d12 uses the same SM5 DXBC (its 5 DXIL libraries are ray-tracing '
                  'libraries, stage lib)'],
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs'}}, 'rt_ps': False,
        'capture_tables': {}, 'motion_tables': {}, 'key_tables': {},
        # ray_tracing.hpp SceneDraw: no colour target, the scene depth written.
        'host_state': 'depth-only',
        'src_files': {
            'scale': ['src/games/expeditionsrome/addon.cpp', 'src/games/expeditionsrome/dlss_graphics.hpp',
                      'src/games/expeditionsrome/**/*0x*.hlsl'],
            'rt': ['src/games/expeditionsrome/ray_tracing.hpp'],
            'mv': ['src/games/expeditionsrome/dlss_graphics.hpp'],
        },
        'ours': ['src/games/expeditionsrome/**/*0x*.hlsl'],
        'tools': {}, 'dead': set(), 'no_rt': False,
        'box_pending': 'tools:audit/boxes_pending_expeditions.tsv',
    },
    'ryse': {
        'engine': 'cryengine', 'pack': None,
        # The two runtime dumps, then the shipped shader cache (every dumped shader is in it). ATN 2026-11-24: every
        # shader counts, so already-extracted pack shaders are census sources; the audit itself never opens a pak.
        'sources': [('R', 'bin', DUMPS + 'dump_shader_ryse', '**/*.cso', 891),
                    ('R', 'bin', DUMPS + 'dump_shader_ryse2', '**/*.cso', 354),
                    ('S', 'tools', 'nontw/shader_packs/ryse/cso', '*.cso', 6421)], 'not_game': {},
        'trace_folders': [], 'trace_floor': 0, 'trace_files': 0,
        'holes': ['shaders the engine compiles outside its shipped cache (none in the two dumps)'],
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs'}}, 'rt_ps': False,
        'capture_tables': {}, 'motion_tables': {}, 'key_tables': {}, 'host_state': None,
        'src_files': {}, 'ours': ['src/games/rysesonofrome/**/*0x*.hlsl'], 'tools': {}, 'dead': set(), 'no_rt': False,
        'box_pending': 'tools:audit/boxes_pending_ryse.tsv',
    },
    'attila': tw('dump_shader_twattila_pack', 2535, 'twattila') | {'not_game': DXGI},
    'rome2': tw('dump_shader_twrome2_pack', 2602, 'twrome2'),
    'thrones': tw('dump_shader_twthronesofbritannia_pack', 2554, 'twthronesofbritannia'),
    'troy': tw('dump_shader_twtroy_pack', 3098, 'twtroy'),
    'pharaoh': tw('dump_shader_twpharaohdynasties_pack', 3151, 'twpharaohdynasties') | {'not_game': DXGI},
    'wh1': tw('dump_shader_twwarhammer_pack', 2879, 'twwarhammer'),
    'wh2': tw('dump_shader_twwarhammer2_pack', 3813, 'twwarhammer2') | {'not_game': DXGI},
    'wh3': tw('dump_shader_twwarhammer3_pack', 2977, 'twwarhammer3'),
    'romeremastered': tw('dump_shader_twromeremastered_pack', 784, 'twromeremastered'),
    'shogun2': tw('dump_shader_shogun2_pack', 673, 'twshogun2completeedition') | {
        'sources': [('S', 'bin', DUMPS + 'dump_shader_shogun2_pack', '**/*.cso', 673),
                    ('R', 'bin', DUMPS + 'dump_shaders_shogun_2', '*.cso', 478)]},
    'divinity1': census_only('divinity', pack_dump('divinity1', 3184, 4), [
        'not dumped: the 67 shaders of injected and library DLLs (the pack\'s not_game.tsv)',
        '4 run-time hashes of the 11-15 census (10EFCA41, 54167BD0, FF9741A1, 54EB9F2C) have no blob anywhere and are '
        'in no trace'], 'divinityoriginalsinenhanced'),
    'divinity2': census_only('divinity', pack_dump('divinity2', 7187, 1), [
        'not dumped: the 278 shaders of injected and library DLLs (the pack\'s not_game.tsv), the Switch / PS4 / Metal '
        'shaders (no DXBC) and Divinity 2 Classic (the add-on targets the Definitive Edition)',
        'counted: 2141 console-only shaders (_DX11X / Durango in every path of names.tsv); no PC trace draws them'],
        'divinity2originalsindefinitive'),
    'ruse': census_only('iriszoom', pack_dump('ruse', 1803), [
        'not dumped: the 5232 shaders of injected and library DLLs (the pack\'s not_game.tsv)',
        'D3D9 (vs_3_0 / ps_3_0): no facts (they read SM4 / SM5 DXBC), so every facts claim fails closed here'], 'ruse'),
}
GAMES['attila'] |= blood('attila', 812, 'blood.pack')
GAMES['attila']['holes'].append('the disabled tdd_pack mod (895 shaders) is not counted')
GAMES['rome2'] |= blood('rome2', 24, 'blood_rome2.pack')
GAMES['thrones'] |= blood('thrones', 812, 'blood.pack')
GAMES['romeremastered'] |= {
    'sources': GAMES['romeremastered']['sources'] + [('S', 'tools', MISSING + 'romeremastered/cso', '*.cso', 331)],
    'holes': [TW_HOLE, f'the dump missed 331 shaders of the game\'s .fxo files and exe: they come from {MISSING}'
              'romeremastered; the 182 libGLESv2 / D3D12Core library shaders are not the game\'s']}

# The must-fail fixture (tools/audit/fixture, outside the repo: game bytecode): 11 copies of Tales shaders and synthetic
# traces; shader_audit.py --selftest tools/audit/fixture runs its cases.tsv. fixture_strict carries the rules the strict
# facts view (n-r3-1), the controls (R3, level A) and the CAMERA tripwires must refuse; fixture_files wants one trace
# file more than there are (S0-M4); fixture_more and fixture_view add a trace whose line breaks a mesh-uv claim (S0-S3,
# open question 1); fixture_423 is Unreal 4.23 with an Expeditions GPU Scene VS and two compiled synthetic ones (r2-M1);
# fixture_ws gives the fixture an engine whose screen-space form is not reviewed (r2-S2); fixture_ng makes a traced key
# not the game's (n-r2-1).
FIXTURE = 'tools:audit/fixture/'
GAMES['fixture'] = GAMES['tales'] | {
    'pack': 'fixture',
    'sources': [('R', 'tools', 'audit/fixture/cso', '*.cso', 11)],
    'trace_folders': [FIXTURE + 'traces'], 'trace_floor': 10, 'trace_files': 7,
    'axes': {'mv': {'vs'}, 'scale': {'ps', 'cs'}, 'rt': {'vs'}},
    'motion_tables': {('src/games/twattila/addon.cpp', 'MOTION_SHADERS'): None},
    'box_pending': FIXTURE + 'boxes_pending_fixture.tsv',
}
GAMES['fixture_strict'] = GAMES['fixture'] | {'pack': 'fixture_strict'}
GAMES['fixture_files'] = GAMES['fixture'] | {'trace_files': 8}
GAMES['fixture_more'] = GAMES['fixture'] | {'trace_folders': [FIXTURE + 'traces', FIXTURE + 'traces_more'], 'trace_files': 8}
GAMES['fixture_view'] = GAMES['fixture'] | {
    'sources': GAMES['fixture']['sources'] + [('R', 'tools', 'audit/fixture/cso_view', '*.cso', 1)],
    'trace_folders': [FIXTURE + 'traces', FIXTURE + 'traces_view'], 'trace_files': 8}
GAMES['fixture_423'] = GAMES['fixture'] | {
    'engine': 'ue4.23', 'sources': [('R', 'tools', 'audit/fixture/cso423', '*.cso', 3)],
    'trace_folders': [], 'trace_floor': 0, 'trace_files': 0, 'axes': {'mv': {'vs'}, 'scale': set(), 'rt': set()}}
GAMES['fixture_ws'] = GAMES['fixture'] | {'engine': 'warscape'}
GAMES['fixture_ng'] = GAMES['fixture'] | {'not_game': {('5F4BCA38', 'vs'): 'a traced shader marked not the game\'s'},
                                          'trace_floor': 9}
