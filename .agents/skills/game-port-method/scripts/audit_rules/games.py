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
  velocity_formats DXGI formats ('f<number>' as traces print them) of the engine's velocity target

GAMES[game]:
  engine, pack    ENGINES key; rule pack module in this folder (None: no rules yet)
  sources         (tier S shipped / R runtime dump, root, path, glob, floor): floor = unique (hash, stage) the source must
                  yield; when a (hash, stage) is in several files the first source's file is the census file
  trace_folders, trace_floor   traces (only these give level A) and the unique (hash, stage) they must hold
  holes           what the census can't see (printed in the report)
  axes            stages per axis; scale also applies to a VS with a projected output, mv to a PS that a trace shows
                  writing the velocity target
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
    'ue4.21': {'view': UE_VIEW_421, 'velocity_formats': ('f35',)},
    'ue4.23': {'view': UE_VIEW_423, 'velocity_formats': ('f35',)},
    'warscape': {'view': None, 'velocity_formats': ()},
    'cryengine': {'view': None, 'velocity_formats': ()},
}

DUMPS = 'cmd_Decompiler-1.3.16/'
TALES_RT = 'src/games/talesofarise_dlss/rt_shadows.hpp'


def tw(folder, floor, game):
    """A Total War game: census only until the Warscape pack (parity_check.py's dump folder names)."""
    return {
        'engine': 'warscape', 'pack': None,
        'sources': [('S', 'bin', DUMPS + folder, '**/*.cso', floor)],
        'trace_folders': [], 'trace_floor': 0,
        'holes': ['packs installed on ATN\'s PC only (DLC he does not own is not in the dump)'],
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs', 'ds'}},
        'capture_tables': {}, 'motion_tables': {}, 'key_tables': {}, 'host_state': None,
        'src_files': {}, 'ours': [f'src/games/{game}/**/*0x*.hlsl'], 'tools': {}, 'dead': set(), 'no_rt': False,
        'box_pending': f'tools:audit/boxes_pending_{game}.tsv',
    }


GAMES = {
    'tales': {
        'engine': 'ue4.21', 'pack': 'ue4',
        # ATN 2026-11-24: the census is ATN's startup dump (root + New_1), not the paks (never decrypted) or traces.
        'sources': [('R', 'bin', DUMPS + 'dump_shaders_tales_of_arise', '**/*.cso', 13525)],
        'trace_folders': ['tools:nontw/tales/g1g2'], 'trace_floor': 393,
        'holes': ['shaders the game compiles after startup (materials streamed in later): more runtime dumps when ATN '
                  'plays; the encrypted paks are never read'],
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs', 'ps'}},
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
                    ('R', 'bin', DUMPS + 'dump_shaders_Expeditions_rome', '*.cso', 1248)],
        'trace_folders': ['tools:nontw/expeditions/g1'], 'trace_floor': 434,
        'holes': ['shaders compiled after startup; -d3d12 uses the same SM5 DXBC (its 5 DXIL libraries are ray-tracing '
                  'libraries, stage lib)'],
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs'}},
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
        'sources': [('R', 'bin', DUMPS + 'dump_shader_ryse', '**/*.cso', 891),
                    ('R', 'bin', DUMPS + 'dump_shader_ryse2', '**/*.cso', 354)],
        'trace_folders': [], 'trace_floor': 0,
        'holes': ['shaders compiled on demand and never dumped; ShaderCache.pak is not read (ATN 2026-11-24)'],
        'axes': {'mv': {'vs', 'ds'}, 'scale': {'ps', 'cs'}, 'rt': {'vs'}},
        'capture_tables': {}, 'motion_tables': {}, 'key_tables': {}, 'host_state': None,
        'src_files': {}, 'ours': ['src/games/rysesonofrome/**/*0x*.hlsl'], 'tools': {}, 'dead': set(), 'no_rt': False,
        'box_pending': 'tools:audit/boxes_pending_ryse.tsv',
    },
    'attila': tw('dump_shader_twattila_pack', 2540, 'twattila'),
    'rome2': tw('dump_shader_twrome2_pack', 2602, 'twrome2'),
    'thrones': tw('dump_shader_twthronesofbritannia_pack', 2554, 'twthronesofbritannia'),
    'troy': tw('dump_shader_twtroy_pack', 3098, 'twtroy'),
    'pharaoh': tw('dump_shader_twpharaohdynasties_pack', 3156, 'twpharaohdynasties'),
    'wh1': tw('dump_shader_twwarhammer_pack', 2879, 'twwarhammer'),
    'wh2': tw('dump_shader_twwarhammer2_pack', 3818, 'twwarhammer2'),
    'wh3': tw('dump_shader_twwarhammer3_pack', 2977, 'twwarhammer3'),
    'romeremastered': tw('dump_shader_twromeremastered_pack', 784, 'twromeremastered'),
    'shogun2': tw('dump_shader_shogun2_pack', 673, 'twshogun2completeedition') | {
        'sources': [('S', 'bin', DUMPS + 'dump_shader_shogun2_pack', '**/*.cso', 673),
                    ('R', 'bin', DUMPS + 'dump_shaders_shogun_2', '*.cso', 478)]},
}

# The must-fail fixture (tools/audit/fixture, outside the repo: game bytecode): 8 copies of Tales shaders and synthetic
# traces; shader_audit.py --selftest tools/audit/fixture runs its cases.tsv. fixture_strict carries the rule the strict
# facts view (n-r3-1) and the control count (R3) must refuse.
FIXTURE = 'tools:audit/fixture/'
GAMES['fixture'] = GAMES['tales'] | {
    'pack': 'fixture',
    'sources': [('R', 'tools', 'audit/fixture/cso', '*.cso', 8)],
    'trace_folders': [FIXTURE + 'traces'], 'trace_floor': 5,
    'axes': {'mv': {'vs'}, 'scale': {'ps', 'cs'}, 'rt': {'vs'}},
    'motion_tables': {('src/games/twattila/addon.cpp', 'MOTION_SHADERS'): None},
    'box_pending': FIXTURE + 'boxes_pending_fixture.tsv',
}
GAMES['fixture_strict'] = GAMES['fixture'] | {'pack': 'fixture_strict'}
