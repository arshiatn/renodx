"""Rule pack of the must-fail fixture's fixture_strict game: rvg_'s L4 shape, two CAMERA rules written as negations, and
the position-only rule with a positive control that is not level A (S0-S7)."""

# A wrong View slot (287D5CA8 reads slot 1, rt_shadows.hpp): --census-only must report it (S3).
VIEW_CONTROLS = {'fixture_strict': {'C81C5E1F': 0, '287D5CA8': 0}}

RULES = {
    # Under-declared needs: the predicate reads dataflow facts but declares only the signature, so the strict view
    # (n-r3-1) refuses every fire; it has no controls, so the count (R3) refuses the pack.
    'static-negation-b': {
        'axis': 'mv', 'verdict': 'CAMERA', 'reason': '', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs',
        'needs': {'signature'},
        'predicate': lambda f: not f['bone_like'] and not f['vtf'],
        'proof': 'none (must fail)',
        'controls': {'fixture_strict': {'positive': [], 'negative': []}},
    },
    # r2-S1: needs declared, so it fires on the screen quad 476D537C (no bones, no texture or indexed read): only the
    # CAMERA tripwire "the View does not project it" refuses the cell.
    'camera-negation-b': {
        'axis': 'mv', 'verdict': 'CAMERA', 'reason': '', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs' and not f['position_only'],
        'needs': {'signature', 'dataflow'},
        'predicate': lambda f: not f['bone_like'] and not f['vtf'] and not f['indexed_position'],
        'proof': 'none (must fail)',
        'controls': {'fixture_strict': {'positive': [], 'negative': []}},
    },
    # 287D5CA8 is in no fixture trace (level B) and settled by this rule: refused as a positive control.
    'ue-position-only-vs-b': {
        'axis': 'mv', 'verdict': 'N/A', 'reason': 'position-only', 'stages': ('vs',),
        'scope': lambda f: f['stage'] == 'vs',
        'needs': {'signature'},
        'predicate': lambda f: f['position_only'],
        'proof': 'none (must fail)',
        'controls': {'fixture_strict': {'positive': ['287D5CA8'], 'negative': ['B1AAEBC8']}},
    },
}
