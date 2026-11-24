"""Rule pack of the must-fail fixture's fixture_noview: SCALED rules whose predicates read no View facts. MUST-B: one fires
on the compiled PS with two View-sized buffers (its View is not identified: no 'view' facts, no roots), so its rule cell
must fail closed; MUST-A / MUST-D: two fire on the thread-id CS and the PS scaled by a non-View constant, which the
SCALED-by-rule tripwire refuses. No controls: the shapes are must-fail ones."""

SCALED = {'axis': 'scale', 'verdict': 'SCALED', 'reason': '', 'proof': 'fixture: none (a rule reading no View facts)'}
RULES = {
    'scaled-noview-b': SCALED | {
        'stages': ('ps',), 'scope': lambda f: f['stage'] == 'ps', 'needs': {'signature', 'dataflow', 'resources'},
        'predicate': lambda f: f['sv_position_used'] and sum(n >= 63 for n in f['constant_buffers'].values()) >= 2},
    'scaled-pixel-b': SCALED | {
        'stages': ('ps',), 'scope': lambda f: f['stage'] == 'ps', 'needs': {'signature', 'dataflow'},
        'predicate': lambda f: f['sv_position_used']},
    'scaled-cs-b': SCALED | {
        'stages': ('cs',), 'scope': lambda f: f['stage'] == 'cs', 'needs': {'signature', 'dataflow'},
        'predicate': lambda f: bool(f['reads'])},
}
