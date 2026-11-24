"""Rule pack of the must-fail fixture (shader_audit.py --selftest): the UE position-only rule with fixture controls."""

RULES = {
    'ue-position-only-vs-b': {
        'axis': 'mv', 'verdict': 'N/A', 'reason': 'depth-only', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs',
        'needs': {'signature'},
        'predicate': lambda f: f['position_only'],
        'proof': 'as ue4.py',
        # 2C264050: depth-only in trace f; B1AAEBC8: drawn with colour in trace a (GAP, not position-only)
        'controls': {'fixture': {'positive': ['2C264050'], 'negative': ['B1AAEBC8']}},
    },
}
