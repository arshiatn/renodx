"""Divinity: Original Sin EE's VFX Brightness lists (src/games/divinityoriginalsinenhanced/vfx_brightness.hpp) from the d1_
census (census.tsv and cso/*.asm, written from Data\\Materials.pak; the game's bytes stay out of the repo).

Every forward pixel shader whose material sources all sit under Materials/Effects, less those the add-on replaces, those
that read the scene's colour (_scene: refraction, heat haze, shields, water), those lit by the sun's shadows or SSAO
(effect surfaces) and the add-on's own fire and candles (shared.h's VFX_BRIGHTNESS). Of those, the ones whose every
material is a marker (INDICATOR) form the second row; the rest the first.

python -I -B vfx_hashes.py <census folder>  prints each class with its materials, then the two C++ array bodies."""
import re
import sys
from pathlib import Path

# Target / range / selection / perception markers. Spell circles (FX_MP_SpellCircle, EnergyCircle, CircleBang) are spells.
INDICATOR = re.compile(r'^FX_(D_AE_Targeter|MAT_RangePreview|MDA_Circle_Ground|MAT_GroundCircle|MAT_Circle_Player|MAT_TargetCircle'
                       r'|MAT_PerceptionOverlay|MAT_PreviewPath|MAT_SkillTarget|MAT_Circle|M_ST_SelectionSphere|MAT_Target)_')
OWN = ('0xED93F883', '0x93AEBB93')  # the add-on's fire and candles

census = Path(sys.argv[1])
classes = {'scene': [], 'lit': [], 'own': [], 'effects': [], 'indicators': []}
materials = {}
for line in (census / 'census.tsv').read_text(encoding='utf-8').splitlines()[1:]:
    cols = line.split('\t')
    if len(cols) < 14 or not cols[1].startswith('ps') or cols[2] != 'material:forward' or cols[12]:
        continue
    sources = cols[13].split()
    if not all('/Materials/Effects/' in s for s in sources):
        continue
    hash_ = cols[0]
    materials[hash_] = [s.split('/')[-1] for s in sources]
    names = re.findall(r'^// (\S+)\s+texture', (census / 'cso' / f'{hash_}.{cols[1]}.asm').read_text(encoding='utf-8', errors='replace'), re.M)
    if '_scene' in names:
        classes['scene'].append(hash_)
    elif '_ShadowSampler' in names or '_SSAOSampler' in names:
        classes['lit'].append(hash_)
    elif hash_ in OWN:
        classes['own'].append(hash_)
    elif all(INDICATOR.match(m) for m in materials[hash_]):
        classes['indicators'].append(hash_)
    else:
        classes['effects'].append(hash_)
for name, members in classes.items():
    print(f'== {name}: {len(members)}')
    for h in sorted(members):
        print(f'  {h} {" ".join(materials[h])}')
for name in ('effects', 'indicators'):
    keep = sorted(classes[name])
    print(f'// {name}: {len(keep)}')
    for at in range(0, len(keep), 8):
        print('    ' + ', '.join(keep[at:at + 8]) + ',')
