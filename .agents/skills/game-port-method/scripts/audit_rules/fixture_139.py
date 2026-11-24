"""Rule pack of the must-fail fixture's fixture_139 (13.9 MUST-C): a mesh-uv rule, cross-checked against the traces by
the mesh-uv claim, so a rule cell on a PS a screen VS draws fails. fixture_139_pair uses this pack too; its extra trace
breaks the positive control's pairing (C6), so the rule exists in fixture_139 only."""

RULES = {
    'meshuv-b': {
        'axis': 'scale', 'verdict': 'N/A', 'reason': 'mesh-uv-only', 'stages': ('ps',),
        'games': ('fixture_139',),  # fixture: its controls hold in fixture_139's traces only
        'scope': lambda f: f['stage'] == 'ps',
        'needs': {'signature', 'dataflow', 'view'},
        'predicate': lambda f: not f['sv_position_used'] and all(set(r[2]) <= {'input', 'world'} for r in f['read_roots']),
        'proof': 'fixture: a PS that reads only by interpolants (no SV_Position) is a mesh PS',
        # 63075590: mesh-uv by trace s (its VS 322EF1A7); 8A78F44C, the compiled pixel-load PS: SCALED by its facts
        'controls': {'fixture_139': {'positive': ['63075590'], 'negative': ['8A78F44C']}},
    },
}
