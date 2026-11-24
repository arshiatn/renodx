"""Rule pack of the must-fail fixture's fixture_host / fixture_host_tales (13.8): an rt CAPTURED rule scoped to the
game whose manifest names the host claim (MUST-1: a host-specific verdict), cross-checked against its traces (MUST-4)."""

RULES = {
    'host-capture-b': {
        'axis': 'rt', 'verdict': 'CAPTURED', 'reason': '', 'stages': ('vs',),
        'games': ('fixture_host',),  # the host's verdict: SceneDraw exists in fixture_host only
        'scope': lambda f: f['stage'] == 'vs',
        'needs': {'signature'},
        'predicate': lambda f: f['position_only'],
        'proof': 'fixture: a position-only VS draws depth only, so the host captures it where it draws the scene depth',
        # C81C5E1F: depth only into the scene depth in trace q (host claim); 00E012F5: the skinned table row
        'controls': {'fixture_host': {'positive': ['C81C5E1F'], 'negative': ['00E012F5']}},
    },
}
