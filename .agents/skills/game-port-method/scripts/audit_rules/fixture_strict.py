"""Rule pack of the must-fail fixture's fixture_strict game: rvg_'s L4 shape, a CAMERA rule written as a negation."""

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
}
