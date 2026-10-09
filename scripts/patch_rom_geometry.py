"""Load original Mario vertices/lights from the user's ROM, inside the external libsm64.

Only metadata and our bridge code are generated. Decompiled model files remain in the
pinned external checkout. The compiled vertex arrays have zero initializers, not assets.
"""
from pathlib import Path
import re, sys

root = Path(sys.argv[1])
model = root/'src/decomp/mario/model.inc.c'
s = model.read_text()
if 'VCSM_ROM_VERTICES' not in s:
    tables = []
    def vertex(m):
        comments = re.findall(r'(?m)^// (0x04[0-9A-Fa-f]{6})', s[:m.start()])
        if not comments: raise SystemExit('Unlabelled original vertex array')
        count = m.group(2).count('{{{')
        if not count: raise SystemExit('Empty original vertex array')
        tables.append((m.group(1),int(comments[-1],16)&0xffffff,count))
        return f'static Vtx {m.group(1)}[{count}]; /* VCSM_ROM_VERTICES */'
    snew = re.sub(r'static const Vtx (\w+)\[\] = \{(.*?)\n\};',vertex,s,flags=re.S)
    if len(tables)<100: raise SystemExit('Incomplete original Mario vertex metadata')
    lights=[]
    def light(m):
        comments=re.findall(r'(?m)^// (0x04[0-9A-Fa-f]{6})',s[:m.start()])
        lights.append((m.group(1),int(comments[-1],16)&0xffffff))
        return f'static Lights1 {m.group(1)}; /* VCSM_ROM_LIGHTS */'
    # Offsets refer to s before editing, so collect light substitutions independently first.
    lighttext=re.sub(r'static const Lights1 (\w+) = gdSPDefLights1\(.*?\n\);',light,s,flags=re.S)
    for name,off in lights:
        snew=re.sub(r'static const Lights1 '+name+r' = gdSPDefLights1\(.*?\n\);',
                    f'static Lights1 {name}; /* VCSM_ROM_LIGHTS */',snew,flags=re.S)
    table='\n'.join(f'    {{ {name}, 0x{off:x}, {count} }},' for name,off,count in tables)
    copies='\n'.join(f'    memcpy(&{name}, data+0x{off:x}, sizeof({name}));' for name,off in lights)
    loader='''
/* VCSM_ROM_VERTICES: ROM supplies every original vertex, UV, normal and palette. */
#include "../tools/libmio0.h"
#include "../../debug_print.h"
#include <stdlib.h>
#include <string.h>
static int16_t vcsm_be16(const uint8_t *p){return (int16_t)(((uint16_t)p[0]<<8)|p[1]);}
int vcsm_load_rom_vertices(const uint8_t *rom){
    mio0_header_t head;
    if(memcmp(rom+1132368,"MIO0",4))return 0;
    mio0_decode_header(rom+1132368,&head);
    if(head.dest_size<0x20000 || head.dest_size>0x100000)return 0;
    uint8_t *data=malloc(head.dest_size);if(!data)return 0;
    mio0_decode(rom+1132368,data,NULL);
    struct Entry{Vtx *vertices;unsigned offset,count;} entries[]={
TABLE
    };
    unsigned count=0;
    for(unsigned k=0;k<sizeof(entries)/sizeof(entries[0]);++k){
        struct Entry e=entries[k];if(e.offset+16*e.count>head.dest_size){free(data);return 0;}
        for(unsigned i=0;i<e.count;++i){const uint8_t *p=data+e.offset+16*i;Vtx *v=&e.vertices[i];
            for(int j=0;j<3;++j)v->v.ob[j]=vcsm_be16(p+2*j);
            v->v.flag=(uint16_t)vcsm_be16(p+6);
            v->v.tc[0]=vcsm_be16(p+8);v->v.tc[1]=vcsm_be16(p+10);
            memcpy(v->v.cn,p+12,4);++count;
        }
    }
COPIES
    free(data);DEBUG_PRINT("ROM geometry loaded: %u vertices",count);return (int)count;
}
'''.replace('TABLE',table).replace('COPIES',copies)
    model.write_text(snew+loader)
    print(f'ROM geometry loader generated: {len(tables)} arrays, {sum(t[2] for t in tables)} vertices, {len(lights)} lights')
lib = root/'src/libsm64.c'
s=lib.read_text()
if 'vcsm_load_rom_vertices( rom )' not in s:
    old='    load_mario_textures_from_rom( rom, outTexture );'
    if old not in s: raise SystemExit('Pinned libsm64 init differs')
    s=s.replace(old,'''    extern int vcsm_load_rom_vertices(const uint8_t *rom);
    if(!vcsm_load_rom_vertices( rom )) { DEBUG_PRINT("ERROR loading original ROM geometry"); return; }
'''+old)
    lib.write_text(s)
print('Original ROM geometry loader patch verified')
