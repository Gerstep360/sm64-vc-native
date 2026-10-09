"""Apply one bounded storage fix to the pinned external libsm64 source, idempotently."""
import sys
from pathlib import Path
p = Path(sys.argv[1]) / 'src/load_surfaces.c'
s = p.read_text()
old = '''    if( s_static_surface_list != NULL )
        free( s_static_surface_list );

    s_static_surface_count = numSurfaces;
    s_static_surface_list = malloc( sizeof( struct SM64SurfaceCollisionData ) * numSurfaces );'''
new = '''    /* VCSM_STABLE_SURFACES: preserve Mario's previous floor pointer between ticks. */
    if( numSurfaces > 131072 ) return;
    if( s_static_surface_list == NULL )
        s_static_surface_list = calloc( 131072, sizeof( struct SM64SurfaceCollisionData ) );
    if( s_static_surface_list == NULL ) return;
    s_static_surface_count = numSurfaces;'''
if 'VCSM_STABLE_SURFACES' not in s:
    if old not in s: raise SystemExit('Pinned surface loader differs: refusing fuzzy patch')
    p.write_text(s.replace(old,new))
print('libsm64 stable surface storage patch verified')
