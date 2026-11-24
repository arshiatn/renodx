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
"""

# Tales rt_shadows.hpp DEPTH_SHADERS: the View slot the add-on reads per prepass vertex shader (the S3 controls).
VIEW_CONTROLS = {'tales': {'C81C5E1F': 0, '287D5CA8': 1, '344F8ACF': 1, '5F4BCA38': 1, 'F1D731D5': 1, '2C264050': 0}}

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
                 '(writes-velocity).',
        'controls': {'tales': {'positive': ['4CD6A3D9'], 'negative': ['BD2D0865']},
                     'expeditions': {'positive': ['6A657BEA', '6E8E2927'], 'negative': ['E976B71D']}},
    },
}
