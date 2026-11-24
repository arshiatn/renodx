"""Rule pack of the must-fail fixture's fixture_badscope (13.8 MUST-1): 'games' naming no game, naming a game whose
manifest uses another pack, and a rule scoped to this game without controls; 13.9 SHOULD-1: an rt CAPTURED rule scoped
to a game whose manifest names no host_state (only the host claim cross-checks a capture rule)."""

POSITION_ONLY = {
    'axis': 'mv', 'verdict': 'N/A', 'reason': 'position-only', 'stages': ('vs',),
    'scope': lambda f: f['stage'] == 'vs',
    'needs': {'signature'},
    'predicate': lambda f: f['position_only'],
    'proof': 'fixture: as ue4.py',
}
RULES = {
    'empty-games-b': POSITION_ONLY | {'games': ()},
    'foreign-game-b': POSITION_ONLY | {'games': ('fixture_badscope', 'fixture')},
    'no-controls-b': POSITION_ONLY | {'games': ('fixture_badscope',)},
    'capture-nohost-b': POSITION_ONLY | {'axis': 'rt', 'verdict': 'CAPTURED', 'reason': '', 'games': ('fixture_badscope',)},
}
