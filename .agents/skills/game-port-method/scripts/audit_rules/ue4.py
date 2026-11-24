"""Shader-audit rule pack for Unreal Engine 4.21 (Tales of Arise) and 4.23 (Expeditions: Rome).

A rule settles '?' cells it fires on (shader_audit.py --apply-rules) and counts only for its own (axis, verdict,
reason), inside its scope, with at least one positive (level A) and one negative control per game: hand rows of that
game's ledger proven without rules. Its predicate sees only the fact kinds it declares (needs). The checker's tripwires
run after it, and a rule cell whose verdict has a trace claim (depth-only, writes-velocity, no-velocity) fails where a
manifest trace disagrees.

Step one of this pack (design S3): the View uniform buffer is found per shader by the checker (games.py ENGINES view:
in a vertex shader the buffer whose rows 0-3 make the clip position, at least min_rows registers declared; in a pixel /
compute shader the one buffer that large). Its controls are Tales' 6 DEPTH_SHADERS view slots (rt_shadows.hpp), checked
by VIEW_CONTROLS below.

Per rule: stages, scope (over the signature facts), needs, predicate, controls {game: {positive, negative}}, proof.
Controls (step0 r3, traces = the manifest's g1g2 / g1 folders):
  ue-position-only-vs-b: C81C5E1F (Tales) and D82DA0C8 (Expeditions light volume) position-only, no velocity target in
  any trace; 4CD6A3D9 / 6A657BEA skinned velocity VS, OWN in every trace.
  ue-static-vs-b: BD2D0865 (Tales base pass, manual vertex fetch) and E976B71D (Expeditions base pass, GPU Scene
  primitive data: manifest primitive_id) level A with main-view colour lines, CAMERA by facts:static-chain;
  4CD6A3D9 / 6A657BEA skinned; edges: B1AAEBC8 (Tales: a second per-draw buffer in the position, world position
  offset) GAP by trace, 6E8E2927 (Expeditions: the GPU Scene velocity VS, View row 129 in its position) OWN by trace.
  ue-velocity-vs-b (fill part 1, 2026-11-24): 4CD6A3D9 (Tales) and 6A657BEA / 6E8E2927 (Expeditions) OWN by trace;
  BD2D0865 / E976B71D CAMERA by facts:static-chain. Before it went in (g24_ fill_rule_check.py): Expeditions 9 traced
  and 279 untraced VS, Tales 7 traced and 491 untraced; every traced one writes the velocity target in every trace.
  ue-mesh-uv-ps-b (fill part 2): 63075590 (Tales depth prepass PS) and 43311719 (Expeditions sprite PS) N/A/mesh-uv-only
  by trace; 1BC3762C (Tales) and 168BD0E1 (Expeditions; 13.9: was 18FCCF50, reopened by SHOULD-2) N/A/no-screen-read.
  ue-svpos-ps-b (fill part 2): 63A516AF (Tales base pass PS) SCALED by facts:roots-pixel and 18FCCF50 (Expeditions)
  by facts:roots-view; 1BC3762C / 168BD0E1 N/A/no-screen-read by facts:roots-world. Before they went in
  (fill_rule_check.py --list, fill4_c2_*.txt): mesh-uv Expeditions 1 traced + 195 untraced, Tales 4 + 59; svpos
  Expeditions 6 + 172 (84 already SCALED by facts), Tales 14 + 301; no override or control problem. 13.9: 18FCCF50 is
  open again (SHOULD-2: no Expeditions trace has a view line, so no traced SCALED cell holds there), so Expeditions has
  no level-A positive for ue-svpos-ps-b until the tracer writes one (F4).
  ue-shadow-depth-vs-b (13.9, rvg_ 13.8 re-check section 4): 0F234715 (Tales) and 1559C19F (Expeditions) shadow atlas
  VS, N/A/depth-only by trace; 4CD6A3D9 / 6A657BEA (skinned velocity VS, OWN by trace: they read the translation row
  and View rows 0-3) and E976B71D (Expeditions GPU Scene base pass, CAMERA by facts).
  A CS rule (dependent reads through buffers only) was dropped: 4 of the 22 traced Tales CS it fired on write targets
  whose size does not follow the render scale (fixed-target), which no CS fact tells apart.
"""
import re

# Tales rt_shadows.hpp DEPTH_SHADERS: the View slot the add-on reads per prepass vertex shader (the S3 controls).
VIEW_CONTROLS = {'tales': {'C81C5E1F': 0, '287D5CA8': 1, '344F8ACF': 1, '5F4BCA38': 1, 'F1D731D5': 1, '2C264050': 0}}
VIEW_MIN_ROWS = 63  # games.py View min_rows, 4.21 (4.23: 67): a smaller buffer is never the View
TRANSLATION = {'62', '66'}  # the View's PreViewTranslation row: 4.21 row 62, 4.23 row 66


def own_projection(f):
    """A mesh in the View's translated world projected by another matrix: a View-sized buffer's PreViewTranslation row
    feeds the clip position and none of the View-sized buffers' rows 0-7 (or a dynamic row) does, while a smaller
    buffer (the pass's own matrix, Unreal's shadow projection) feeds it too."""
    rows = {}
    for label in f['position_sources']:
        hit = re.fullmatch(r'(?:idx:)?cb(\d+)\[(\d+|dyn)\]', label)
        if hit:
            rows.setdefault(hit.group(1), set()).add(hit.group(2))
    view = {str(slot) for slot, count in f['constant_buffers'].items() if count >= VIEW_MIN_ROWS}
    from_view = set().union(*[rows.get(slot, set()) for slot in view])
    return bool(from_view & TRANSLATION) and 'dyn' not in from_view and not from_view & {str(r) for r in range(8)} \
        and any(slot not in view for slot in rows)


def roots_within(f, allowed):
    """Every read's coordinate roots are in 'allowed' (a shader that reads nothing counts)."""
    return all(set(r[2]) <= allowed for r in f['read_roots'])


RULES = {
    'ue-position-only-vs-b': {
        'axis': 'mv', 'verdict': 'N/A', 'reason': 'position-only', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs',
        'needs': {'signature'},
        'predicate': lambda f: f['position_only'],
        'proof': 'A vertex shader whose only output is SV_Position hands its pixel shader nothing, so its draws can\'t '
                 'carry an object\'s motion: no previous position reaches any pixel shader. They are depth, shadow, '
                 'stencil or clear passes, light volumes or screen passes. A mesh it draws in colour gets its object '
                 'motion from Unreal\'s velocity pass (a twin per draw), not from this vertex shader. A traced one fails '
                 'where any manifest trace binds a velocity target on its lines (no-velocity).',
        'controls': {'tales': {'positive': ['C81C5E1F'], 'negative': ['4CD6A3D9']},
                     'expeditions': {'positive': ['D82DA0C8'], 'negative': ['6A657BEA']}},
    },
    'ue-static-vs-b': {
        'axis': 'mv', 'verdict': 'CAMERA', 'reason': '', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs' and not f['position_only'],
        'needs': {'signature', 'dataflow', 'view'},
        'predicate': lambda f: f['static_chain'],
        'proof': 'The clip position comes only from vertex inputs, one per-draw source (a buffer or, in 4.22+, the GPU '
                 'Scene primitive\'s data read by its primitive id) and the View\'s projection and translation rows '
                 '(facts static-chain, the View projects it): static in world space for a given draw, '
                 'so camera motion is right; a moving primitive gets its object motion from Unreal\'s velocity pass (a '
                 'twin per draw).',
        'controls': {'tales': {'positive': ['BD2D0865'], 'negative': ['4CD6A3D9', 'B1AAEBC8']},
                     'expeditions': {'positive': ['E976B71D'], 'negative': ['6A657BEA', '6E8E2927']}},
    },
    'ue-velocity-vs-b': {
        'axis': 'mv', 'verdict': 'OWN', 'reason': '', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs' and not f['position_only'],
        'needs': {'signature', 'view'},
        'predicate': lambda f: bool(f['previous_rows_in_outputs']) and not f['static_chain'],
        'proof': 'The VS hands its pixel shader the previous frame\'s clip position, made with the View\'s previous '
                 'TranslatedWorldToClip rows (games.py previous: 83-86 in 4.21, 87-90 in 4.23): Unreal\'s velocity '
                 'pass VS (or a base pass that writes velocity), whose draws write the velocity target, so its draws '
                 'carry their own object motion. A static chain is left to ue-static-vs-b (CAMERA), so the two never '
                 'fire together. A traced one fails where any manifest trace line writes no velocity target '
                 '(writes-velocity). Residual: a base pass VS outputs previous rows too when r.BasePassOutputsVelocity '
                 'is on, and its draws write velocity only then; neither game\'s traced base pass writes velocity, so '
                 'the manifest should name that cvar (S5).',
        'controls': {'tales': {'positive': ['4CD6A3D9'], 'negative': ['BD2D0865']},
                     'expeditions': {'positive': ['6A657BEA', '6E8E2927'], 'negative': ['E976B71D']}},
    },
    'ue-shadow-depth-vs-b': {  # rvg_ 13.7 MUST-2 / 13.8 re-check MUST-E: semantic, not the output layout
        'axis': 'mv', 'verdict': 'N/A', 'reason': 'depth-only', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs' and not f['position_only'],
        'needs': {'signature', 'dataflow', 'resources'},
        'predicate': own_projection,
        'proof': 'The discriminator: the VS reads the View\'s PreViewTranslation row (62 in 4.21, 66 in 4.23) into its '
                 'clip position, and no View-sized buffer\'s projection rows 0-7 (or a dynamic row) feed it, so another '
                 'matrix projects it (Unreal\'s shadow depth pass: the shadow\'s own projection in its pass buffer). A '
                 'draw no View projects is not drawn into the main view and carries no main-view motion. The clause "a '
                 'smaller buffer feeds the position too" always holds in 4.21 (every mesh VS reads its Primitive '
                 'buffer), so it is not the discriminator. A large material buffer counts as View-sized (12185B5A: '
                 'cb4[68] beside the View cb1[133]); that only makes the rows 0-7 test stricter (it may drop a shadow '
                 'VS, never add one). The constants are the 4.21 / 4.23 union (63 rows; row 62 or 66); each engine\'s '
                 'own values (63 / 62, 67 / 66) give identical fires (Expeditions 830, Tales 921; rvg_ '
                 's138_rules_probe2). Fired VS that read more View data (camera origin and axes, time) are world '
                 'position offset or camera-facing casters; none reads a full other View projection. Traced fires '
                 'draw only into the shadow atlas (Expeditions 38 VS / 25,689 lines, Tales 18 / 1,881), and traced rows '
                 'are cross-checked by the depth-only trace claim. Residual: a colour shadow pass (reflective shadow '
                 'maps, translucent shadow) keeps mv N/A, but its reason is not depth-only; the depth-only claim '
                 'refuses a traced one, an untraced one carries the wrong reason.',
        'controls': {'tales': {'positive': ['0F234715'], 'negative': ['4CD6A3D9']},
                     'expeditions': {'positive': ['1559C19F'], 'negative': ['E976B71D', '6A657BEA']}},
    },
    'ue-mesh-uv-ps-b': {
        'axis': 'scale', 'verdict': 'N/A', 'reason': 'mesh-uv-only', 'stages': ('ps',),
        'scope': lambda f: f['stage'] == 'ps',
        'needs': {'signature', 'dataflow', 'view'},
        'predicate': lambda f: {'TEXCOORD10', 'SV_ISFRONTFACE0'} <= set(f['input_semantics'])
        and not f['sv_position_used'] and any('input' in r[2] for r in f['read_roots'])
        and roots_within(f, {'input', 'world'}),
        'proof': 'A pixel shader of a vertex factory drawn in the view (the factory\'s tangent interpolant TEXCOORD10 '
                 'and SV_IsFrontFace among its inputs: base pass, depth prepass, translucency of meshes and sprites) '
                 'that uses no SV_Position and reads only by interpolants and constants. The interpolants of such a '
                 'pass are the factory\'s texture coordinates, vertex colour and sprite sub-UVs, made per vertex, so '
                 'its reads do not depend on the render scale. Out of it: shadow depth PS (no SV_IsFrontFace; '
                 'fill4_ps_passes.py: all 58 traced in both games draw only into the shadow atlas: fixed-target) and '
                 'screen passes (DrawRectangle\'s TEXCOORD0, no TEXCOORD10). Residual: a material whose custom UVs '
                 'its VS makes from View rows or a projection (screen-aligned UVs) reads by screen position; the '
                 'mesh-uv trace claim checks that on traced rows (all 5 it fires on pass: Tales 4, Expeditions 1), '
                 'untraced rows carry it.',
        'controls': {'tales': {'positive': ['63075590'], 'negative': ['1BC3762C']},
                     'expeditions': {'positive': ['43311719'], 'negative': ['168BD0E1']}},
    },
    'ue-svpos-ps-b': {
        'axis': 'scale', 'verdict': 'SCALED', 'reason': '', 'stages': ('ps',),
        'scope': lambda f: f['stage'] == 'ps',
        'needs': {'signature', 'dataflow', 'view'},
        # shadow depth PS (the tangent pair first, the shadow VS's TEXCOORD7, no SV_IsFrontFace) draw the atlas
        'predicate': lambda f: f['sv_position_used'] and roots_within(f, {'pixel', 'view', 'world'})
        and not (f['input_semantics'][:2] == ['TEXCOORD10', 'TEXCOORD11'] and 'TEXCOORD7' in f['input_semantics']
                 and 'SV_ISFRONTFACE0' not in f['input_semantics']),
        'proof': 'A pixel shader that uses SV_Position and reads only by pixel, View and constant roots, or reads '
                 'nothing: facts roots-pixel / roots-view without their "at least one screen read". With no screen '
                 'read, SV_Position feeds per-pixel math only (dithered LOD and opacity, pixel depth offset, the '
                 'View\'s SV_Position-to-world rows), right at any view size. Out of it: Unreal\'s shadow depth PS, '
                 'which draw the shadow atlas (fixed-target; fill4_ps_passes.py: all 58 traced in both games). Traced '
                 'rows it fires on (Tales 14, Expeditions 6) draw the scene depth or scene-sized colour, and none '
                 'passes fixed-target. Residual: an untraced PS of another fixed-size pass (a capture, a cube face) '
                 'is FIXED/fixed-target, which the rule calls SCALED. The shadow exclusion is a layout test: 7 PS with '
                 'the tangent pair first and no SV_IsFrontFace but no TEXCOORD7 look like shadow depth PS and are not '
                 'excluded (Expeditions 28000D96, 57BE1875, 84EB9902, 93EB6A72, 95E2C2D3, FB3AD561; Tales 2AA215FE). '
                 'A label risk only (SCALED where FIXED/fixed-target may hold, no scale bug hidden); they stay '
                 'UNCLEAR:fixed-target-class for the closure tool until classified (rvg_ 13.8 re-check NIT-2).',
        'controls': {'tales': {'positive': ['63A516AF'], 'negative': ['1BC3762C']},
                     'expeditions': {'positive': ['18FCCF50'], 'negative': ['168BD0E1']}},
    },
}
