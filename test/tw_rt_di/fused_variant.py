"""fused_variant.py <dlaa_helper folder>: the Attila D3D12 helper's fused DI variant (PT DI step 4c, ATN's fused pass; kept
as an option, off in the player's build): rt_di_spatiotemporal in place of rt_di_temporal + rt_di_spatial, by text
substitution on build.ps1 and rt_d3d12.hpp. Every DI pass build gets -D DI_FUSED (rt_di_surface.hlsli: the shading
reservoirs and their history in arrays 0 and 1 in turns; rt_di_spatiotemporal refuses to build without it).

  python -I test\\tw_rt_di\\fused_variant.py src\\games\\twattila\\dlaa_helper
  powershell -NoProfile -ExecutionPolicy Bypass -File src\\games\\twattila\\dlaa_helper\\build.ps1 <out folder>
  git checkout -- src/games/twattila/dlaa_helper/build.ps1 src/games/twattila/dlaa_helper/rt_d3d12.hpp

Each anchor must match once, or nothing is written for that file and the script stops."""
import sys

root = sys.argv[1]


def patch(name, pairs):
    path = f'{root}\\{name}'
    t = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in t else '\n'
    for old, new in pairs:
        old = old.replace('\n', nl)
        new = new.replace('\n', nl)
        if t.count(old) != 1:
            sys.exit(f'{name}: anchor found {t.count(old)} times: {old[:90]!r}')
        t = t.replace(old, new)
    open(path, 'w', encoding='utf-8', newline='').write(t)


patch('build.ps1', [
    ("@('rt_di_initial', 'PATH_DI', 'rt_di_initial', 'RT_DI_INITIAL'),", "@('rt_di_initial', 'PATH_DI DI_FUSED', 'rt_di_initial', 'RT_DI_INITIAL'),"),
    ("""               @('rt_di_temporal', 'PATH_DI', 'rt_di_temporal', 'RT_DI_TEMPORAL'), @('rt_di_spatial', 'PATH_DI', 'rt_di_spatial', 'RT_DI_SPATIAL'),
""", """               @('rt_di_spatiotemporal', 'PATH_DI DI_FUSED', 'rt_di_spatiotemporal', 'RT_DI_SPATIOTEMPORAL'),
"""),
    ("@('rt_di_shade', 'PATH_DI', 'rt_di_shade', 'RT_DI_SHADE'))", "@('rt_di_shade', 'PATH_DI DI_FUSED', 'rt_di_shade', 'RT_DI_SHADE'))"),
])
patch('rt_d3d12.hpp', [
    ("""#include "rt_di_spatial.h"
#include "rt_di_spatial_omm.h"
#include "rt_di_temporal.h"
#include "rt_di_temporal_omm.h"
""", """#include "rt_di_spatiotemporal.h"
#include "rt_di_spatiotemporal_omm.h"
"""),
    ("const std::array<DiBuild, 8> di_builds = {{", "const std::array<DiBuild, 7> di_builds = {{"),
    ("""        {RT_DI_TEMPORAL, RT_DI_TEMPORAL_OMM, &di_temporal_pipeline_},
        {RT_DI_SPATIAL, RT_DI_SPATIAL_OMM, &di_spatial_pipeline_},
""", """        {RT_DI_SPATIOTEMPORAL, RT_DI_SPATIOTEMPORAL_OMM, &di_temporal_pipeline_},
"""),
    ("{di_initial_pipeline_.Get(), di_temporal_pipeline_.Get(), di_spatial_pipeline_.Get(), di_shade_pipeline_.Get()}",
     "{di_initial_pipeline_.Get(), di_temporal_pipeline_.Get(), di_shade_pipeline_.Get()}"),
])
print('fused variant: ok')
