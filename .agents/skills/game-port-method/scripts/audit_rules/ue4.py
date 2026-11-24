"""Shader-audit rule pack for Unreal Engine 4.21 (Tales of Arise) and 4.23 (Expeditions: Rome).

A rule settles '?' cells it fires on (shader_audit.py --apply-rules) and counts only for its own (axis, verdict,
reason), inside its scope, with at least one positive and one negative control per game: hand rows of that game's ledger
proven without rules. Its predicate sees only the fact kinds it declares (needs). The checker's tripwires run after it,
and a rule cell whose verdict has a trace claim (depth-only, writes-velocity) fails where a manifest trace disagrees.

Step one of this pack (design S3): the View uniform buffer is found per shader by the checker (games.py ENGINES view:
in a vertex shader the buffer whose rows 0-3 make the clip position, at least min_rows registers declared; in a pixel /
compute shader the one buffer that large). Its controls are Tales' 6 DEPTH_SHADERS view slots (rt_shadows.hpp), checked
by VIEW_CONTROLS below.

Per rule: stages, scope (over the signature facts), needs, predicate, controls {game: {positive, negative}}, proof.
Controls (step 0, traces = the manifest's g1g2 / g1 folders):
  C81C5E1F (Tales) depth-only in every trace; 4CD6A3D9 (Tales) / 6A657BEA (Expeditions) skinned velocity VS, OWN in
  every trace; BD2D0865 (Tales base pass) / 8CDDBA8E (Expeditions) CAMERA by facts:static-chain.
  Expeditions has no positive control for ue-position-only-vs-b: every position-only VS its traces draw also draws
  colour (D82DA0C8 / 7B3F38C5 light volumes, D25C446D / 9D1A56C2 screen passes), so the rule stays refused there.
"""

# Tales rt_shadows.hpp DEPTH_SHADERS: the View slot the add-on reads per prepass vertex shader (the S3 controls).
VIEW_CONTROLS = {'tales': {'C81C5E1F': 0, '287D5CA8': 1, '344F8ACF': 1, '5F4BCA38': 1, 'F1D731D5': 1, '2C264050': 0}}

RULES = {
    'ue-position-only-vs-b': {
        'axis': 'mv', 'verdict': 'N/A', 'reason': 'depth-only', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs',
        'needs': {'signature'},
        'predicate': lambda f: f['position_only'],
        'proof': 'A vertex shader whose only output is SV_Position feeds no pixel shader input: in a mesh pass its draws '
                 'write depth (the depth prepass, shadow depths) and Unreal draws the mesh\'s colour and velocity with '
                 'other vertex shaders (level B where untraced). NOT a proof for screen passes and light volumes, which '
                 'are position-only too: traced ones fail by the trace cross-check; untraced ones are the open question '
                 'of the step-0 review.',
        'controls': {'tales': {'positive': ['C81C5E1F'], 'negative': ['4CD6A3D9']},
                     'expeditions': {'positive': [], 'negative': ['6A657BEA']}},
    },
    'ue-static-vs-b': {
        'axis': 'mv', 'verdict': 'CAMERA', 'reason': '', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs' and not f['position_only'],
        'needs': {'signature', 'dataflow', 'view'},
        'predicate': lambda f: f['static_chain'],
        'proof': 'The clip position comes only from vertex inputs, one per-draw buffer and the View\'s projection and '
                 'translation rows (facts static-chain): static in world space for a given draw, so camera motion is '
                 'right; a moving primitive gets its object motion from Unreal\'s velocity pass (a twin per draw).',
        'controls': {'tales': {'positive': ['BD2D0865'], 'negative': ['4CD6A3D9']},
                     'expeditions': {'positive': ['8CDDBA8E'], 'negative': ['6A657BEA']}},
    },
}
