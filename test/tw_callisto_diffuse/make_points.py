# Writes slide_points.h, the references tests.cpp's double slide model is checked against (independent of
# callisto_diffuse.h):
# - the talk's knobs: its 10 published Callisto fits (slides 107-111, scalar: tints white, p 0.5 where not listed),
#   Proxima at GGX alpha 0.16 / 0.49 / 0.77 / 1 (slide 128), and c2 at o 0.25 / 1 / -1, p 0.5 in each of the three
#   alpha_s readings (slide 90: radians, angle / 90 degrees, cosines);
# - R = c1 x c2 x Proxima at sampled (l, v), an own numpy reading of slides 85 / 90 / 128 with explicit h vectors
#   (n = +z, l toward the light, v toward the viewer, phi 0 = backscatter), both in the upper hemisphere;
# - the integrals at full precision from that reading, on tests.cpp's grid (E: Gauss-Legendre 128 in cos x 128
#   midpoints in phi over [0, pi]; bonds: 64 Gauss-Legendre views x 96 x 96): E at view 0 / 45 / 70 / 85, the bond
#   albedo, and c2's R cos rows;
# - the same integrals as cal_ (callisto_check.txt, midpoint grid) and cdr_ (cdr_callisto.txt, Gauss-Legendre)
#   printed them, with their printed decimals (cdr_ lists 8 talk fits: the two metals are 1 by construction).
# usage: python -I make_points.py <cal_/callisto_check.txt> <cdr_/cdr_callisto.txt> <slide_points.h>
import math
import re
import sys

import numpy as np

# name, rho_r, n_r, m_r, rho_f, n_f, m_f, o, p, proxima alpha, alpha_s reading (0 radians, 1 angle / 90, 2 cosines)
MODELS = [
    ('black soft plastic', 3, 0.7, 0.75, 175, 0.7, 0.75, 0, 0.5, 0, 0),
    ('blue fabric', 3, 0.5, 0.75, 40, 0.6, 0.95, 0, 0.5, 0, 0),
    ('yellow phenolic', 0.3, 0.85, 1, 0.6, 0.85, 1, 0.25, 0.5, 0, 0),
    ('yellow plastic', 3.5, 0.25, 1, 5, 0.6, 0.85, 0, 0.5, 0, 0),
    ('gold metallic paint 2', 1, 0.5, 0.75, 1, 0.5, 1, 0, 0.5, 0, 0),
    ('light red paint', 6.5, 0.5, 0.65, 1, 0.5, 1, 0.2, 0.5, 0, 0),
    ('red fabric 2', 1.25, 0.5, 0.75, 25, 0.6, 0.97, -0.1, 0.5, 0, 0),
    ('specular red phenolic', 0.5, 0.8, 0.95, 0.4, 0.9, 1, 0.2, 0.5, 0, 0),
    ('two layer silver', 1, 0.5, 0.75, 1, 0.5, 1, 0, 0.5, 0, 0),
    ('yellow matte plastic', 0.6, 0.85, 1, 0.25, 0.75, 1, 0.25, 0.5, 0, 0),
    ('proxima alpha 0.16', 1, 0.75, 0.75, 1, 0.75, 0.75, 0, 0.5, 0.16, 0),
    ('proxima alpha 0.49', 1, 0.75, 0.75, 1, 0.75, 0.75, 0, 0.5, 0.49, 0),
    ('proxima alpha 0.77', 1, 0.75, 0.75, 1, 0.75, 0.75, 0, 0.5, 0.77, 0),
    ('proxima alpha 1', 1, 0.75, 0.75, 1, 0.75, 0.75, 0, 0.5, 1.0, 0),
]
for reading, reading_name in enumerate(('radians', 'normalized', 'cosines')):
    for o in (0.25, 1.0, -1.0):
        MODELS.append((f'c2 o {o:g} {reading_name}', 1, 0.75, 0.75, 1, 0.75, 0.75, o, 0.5, 0, reading))
INDEX = {model[0]: index for index, model in enumerate(MODELS)}


def falloff_exponent(x):
    return 5.0 * 2.0 * (1.0 - x)  # 5 r(x), r(x) = 2 (1 - x)


def lobe(l, v, model):
    """R for unit l, v of shape (..., 3), n = +z."""
    _, rho_r, n_r, m_r, rho_f, n_f, m_f, o, p, alpha, reading = model
    h = (l + v) / np.linalg.norm(l + v, axis=-1, keepdims=True)
    cos_i = l[..., 2]
    # Clipped to [0, 1] before any power (the slide's bars, and l = v gives 1 - cos_h = -2e-16 on closed grids).
    cos_d = np.clip(np.sum(l * h, axis=-1), 0.0, 1.0)
    cos_h = np.clip(h[..., 2], 0.0, 1.0)
    bar = lambda x: np.maximum(x, 0.0)
    alpha_f = (1.0 - cos_d) ** falloff_exponent(n_f) * cos_h ** falloff_exponent(m_f)
    alpha_r = (1.0 - cos_h) ** falloff_exponent(n_r) * cos_d ** falloff_exponent(m_r)
    c1 = (1.0 + (rho_f - 1.0) * alpha_f) * (1.0 + (rho_r - 1.0) * alpha_r)
    if reading == 2:
        theta_d, theta_h = cos_d, cos_h
    else:
        scale = 1.0 if reading == 0 else 0.5 * math.pi
        theta_d = np.arccos(cos_d) / scale
        theta_h = np.arccos(cos_h) / scale
    alpha_s = (1.0 - (1.0 - theta_d) ** 3) * (1.0 - (1.0 - theta_h) ** 3)
    edge = alpha_s * p
    with np.errstate(divide='ignore', invalid='ignore'):
        step = np.where(edge == 0.0, (cos_i > 0.0).astype(float), np.clip(cos_i / edge, 0.0, 1.0))
    c2 = 1.0 + (step * step * (3.0 - 2.0 * step) - 1.0) * alpha_s * o
    proxima = 1.0 + alpha * (-0.55 + 0.19 / bar(cos_i)) * (1.0 - np.sqrt(bar(-np.sum(v * l, axis=-1))))
    return c1 * c2 * proxima


def direction(theta, phi):
    t, f = np.radians(theta), np.radians(phi)
    return np.stack(np.broadcast_arrays(np.sin(t) * np.cos(f), np.sin(t) * np.sin(f), np.cos(t)), axis=-1)


GEOMETRY = [(tl, tv, ph) for tl in (5, 25, 45, 65, 85) for tv in (0, 20, 40, 60, 80) for ph in (0, 45, 90, 135, 180)]
points = []
for index, model in enumerate(MODELS):
    for theta_l, theta_v, phi in GEOMETRY:
        points.append((index, theta_l, theta_v, phi,
                       float(lobe(direction(theta_l, 0.0), direction(theta_v, phi), model))))

# Full-precision integrals on tests.cpp's grid. E(view) = (1 / pi) Int R cos dw over the light hemisphere (R even in
# the light's azimuth about the view's plane: phi over [0, pi]); bond = 2 Int E(mu_v) mu_v dmu_v.
def light_grid(count):
    nodes, weights = np.polynomial.legendre.leggauss(count)
    mu = 0.5 * (nodes + 1.0)
    phi = (np.arange(count) + 0.5) / count * 180.0
    return mu, 0.5 * weights, direction(np.degrees(np.arccos(mu))[:, None], phi[None, :])


E_GRID = light_grid(128)    # E: Gauss-Legendre 128 in cos x 128 midpoints in phi
BOND_GRID = light_grid(96)  # bonds: 64 Gauss-Legendre views x (96 x 96)
view_nodes, view_weights = np.polynomial.legendre.leggauss(64)
VIEW_MU, VIEW_W = 0.5 * (view_nodes + 1.0), 0.5 * view_weights


def albedo(model, view, grid):
    mu, weights, lights = grid
    return float(2.0 * np.sum(weights * mu * np.mean(lobe(lights, np.broadcast_to(view, lights.shape), model), axis=1)))


def value_of(name, kind, at):
    model = MODELS[INDEX[name]]
    if kind == 0:
        return albedo(model, direction(at, 0.0), E_GRID)
    if kind == 1:
        return float(sum(2.0 * w * mu * albedo(model, direction(math.degrees(math.acos(mu)), 0.0), BOND_GRID)
                         for mu, w in zip(VIEW_MU, VIEW_W)))
    l = np.array([math.sqrt(1.0 - at * at), 0.0, at])
    return float(at * lobe(l, np.array([0.0, 0.0, 1.0]), model))


# The printed integrals: (source, model, kind, at, value, decimals); kind 0 E at view `at` degrees, 1 bond, 2 R cos at
# cos_i `at`, view 0.
printed = []
NUMBER = r'(-?\d+\.\d+)'


def add(source, name, kind, at, text):
    printed.append((source, name, kind, at, float(text), len(text.split('.')[1])))


def section(text, title):
    return text.split('## ' + title, 1)[1].split('\n## ', 1)[0]


cal = open(sys.argv[1], encoding='utf-8').read()
for line in section(cal, '2.').splitlines():
    match = re.match(r'^(.*?)\s+' + r'\s+'.join([NUMBER] * 5) + r'\s*$', line)
    if not match:
        continue
    label = match.group(1).strip()
    name = next((model[0] for model in MODELS if label.startswith(model[0])), None)
    if label.startswith('proxima ag '):
        name = 'proxima alpha ' + f'{float(label.split()[2]):g}'
    for view, text in zip((0, 45, 70, 85), match.groups()[1:5]):
        add('cal_', name, 0, view, text)
    add('cal_', name, 1, 0, match.group(6))
for line in section(cal, '4.').splitlines():
    match = re.match(r'^(radians|normalized|cosines)\s+E0\s+' + NUMBER + r'\s+E45\s+' + NUMBER + r'\s+E70\s+' + NUMBER
                     + r'\s+bond\s+' + NUMBER, line)
    if match:
        name = f'c2 o 0.25 {match.group(1)}'
        for view, text in zip((0, 45, 70), match.groups()[1:4]):
            add('cal_', name, 0, view, text)
        add('cal_', name, 1, 0, match.group(5))
    match = re.match(r'^o\s+' + NUMBER + r': R cos at .*?:\s+' + r'\s+'.join([NUMBER] * 4), line)
    if match and float(match.group(1)) != 0.0:
        for cos_i, text in zip((0.02, 0.1, 0.25, 0.5), match.groups()[1:]):
            add('cal_', f'c2 o {float(match.group(1)):g} radians', 2, cos_i, text)

cdr = open(sys.argv[2], encoding='utf-8').read()
for line in section(cdr, '2.').splitlines():  # 8 fits: cdr_ leaves out the two metals (1 by construction)
    match = re.match(r'^(.*?)\s+' + r'\s+'.join([NUMBER] * 4) + r'\s+bond\s+' + NUMBER, line)
    if match:
        name = match.group(1).strip()
        for view, text in zip((0, 45, 70, 85), match.groups()[1:5]):
            add('cdr_', name, 0, view, text)
        add('cdr_', name, 1, 0, match.group(6))
for line in section(cdr, '6.').splitlines():
    match = re.match(r'^\s*o\s+' + NUMBER + r':\s+' + r'\s+'.join([NUMBER] * 4), line)
    if match:
        for cos_i, text in zip((0.02, 0.1, 0.25, 0.5), match.groups()[1:]):
            add('cdr_', f'c2 o {float(match.group(1)):g} radians', 2, cos_i, text)
    match = re.match(r'^\s*bond o \.25 (radians|normalized|cosines)\s+' + NUMBER, line)
    if match:
        add('cdr_', f'c2 o 0.25 {match.group(1)}', 1, 0, match.group(2))
for alpha, text in zip((0.16, 0.49, 0.77, 1), re.findall(NUMBER, section(cdr, '8.'))[-4:]):
    add('cdr_', f'proxima alpha {alpha:g}', 1, 0, text)

# Every integral either file printed, once, at full precision.
integrals = sorted({(INDEX[name], kind, at) for _, name, kind, at, _, _ in printed})
values = {key: value_of(MODELS[key[0]][0], key[1], key[2]) for key in integrals}

with open(sys.argv[3], 'w', newline='\n') as f:
    f.write('// Generated by make_points.py (an own numpy reading of the Callisto slides 85 / 90 / 128) and from\n'
            '// cal_/callisto_check.txt and cdr_/cdr_callisto.txt (the integrals they printed).\n')
    f.write('#ifndef TEST_TW_CALLISTO_DIFFUSE_SLIDE_POINTS_H_\n#define TEST_TW_CALLISTO_DIFFUSE_SLIDE_POINTS_H_\n\n')
    f.write('// The talk\'s knobs: intensity, falloff, tangent falloff (retro, then diffuse Fresnel), the smooth\n'
            '// terminator\'s o and p, Proxima\'s GGX alpha and the alpha_s reading (0 radians, 1 angle / 90, 2 cosines).\n')
    f.write('struct SlideModel {\n  const char* name;\n  double retro;\n  double retro_falloff;\n'
            '  double retro_tangent_falloff;\n  double fresnel;\n  double fresnel_falloff;\n'
            '  double fresnel_tangent_falloff;\n  double terminator;\n  double terminator_length;\n'
            '  double proxima_alpha;\n  int alpha_s_reading;\n};\n\n')
    f.write(f'static const SlideModel SLIDE_MODELS[{len(MODELS)}] = {{\n')
    for model in MODELS:
        f.write('    {"' + model[0] + '", ' + ', '.join(f'{x:g}' for x in model[1:10]) + f', {model[10]}' + '},\n')
    f.write('};\n\n')
    f.write('// Model, light and view zenith angles and the view\'s azimuth (degrees; phi 0 = backscatter), R.\n')
    f.write('struct SlidePoint {\n  int model;\n  double theta_l;\n  double theta_v;\n  double phi;\n  double lobe;\n};\n\n')
    f.write(f'static const SlidePoint SLIDE_POINTS[{len(points)}] = {{\n')
    for index, theta_l, theta_v, phi, value in points:
        f.write(f'    {{{index}, {theta_l}, {theta_v}, {phi}, {value:.17g}}},\n')
    f.write('};\n\n')
    f.write('// The model, the kind (0 E at view `at` degrees, 1 bond, 2 R cos at cos_i `at`, view 0) and the value at\n'
            '// full precision.\n')
    f.write('struct SlideIntegral {\n  int model;\n  int kind;\n  double at;\n  double value;\n};\n\n')
    f.write(f'static const SlideIntegral SLIDE_INTEGRALS[{len(integrals)}] = {{\n')
    for key in integrals:
        f.write(f'    {{{key[0]}, {key[1]}, {key[2]:g}, {values[key]:.17g}}},\n')
    f.write('};\n\n')
    f.write('// The same integrals as cal_ and cdr_ printed them: who, the model, the kind, at, the value and its\n'
            '// printed decimals.\n')
    f.write('struct SlidePrinted {\n  const char* source;\n  int model;\n  int kind;\n  double at;\n  double value;\n'
            '  int decimals;\n};\n\n')
    f.write(f'static const SlidePrinted SLIDE_PRINTED[{len(printed)}] = {{\n')
    for source, name, kind, at, value, decimals in printed:
        f.write(f'    {{"{source}", {INDEX[name]}, {kind}, {at:g}, {value:g}, {decimals}}},\n')
    f.write('};\n\n#endif  // TEST_TW_CALLISTO_DIFFUSE_SLIDE_POINTS_H_\n')
worst = max(abs(values[(INDEX[name], kind, at)] - value) / (1e-3 * max(abs(value), 0.1) + 0.5 * 10.0 ** -decimals)
            for _, name, kind, at, value, decimals in printed)
print(len(MODELS), 'models,', len(points), 'points,', len(integrals), 'integrals,', len(printed), 'printed;',
      f'worst printed gap {worst:.2f} x its tolerance')
