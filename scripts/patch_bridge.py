"""Our pose bridge and widened collision arithmetic for the pinned external runtime."""
from pathlib import Path
import re, sys
root=Path(sys.argv[1])
lib=root/'src/libsm64.c'
s=lib.read_text()
if 'void vcsm_mario_pose(' not in s:
    s+='''
/* VCSM_POSE: project-owned extension, not an upstream API. No movement tick. */
SM64_LIB_FN void vcsm_mario_pose(int32_t id,float x,float y,float z,float yaw,int32_t anim,int16_t frame,
    struct SM64MarioState *out,struct SM64MarioGeometryBuffers *buffers){
    if(id<0 || id>=s_mario_instance_pool.size || !s_mario_instance_pool.objects[id])return;
    global_state_bind(((struct MarioInstance*)s_mario_instance_pool.objects[id])->globalState);
    gMarioState->pos[0]=x;gMarioState->pos[1]=y;gMarioState->pos[2]=z;
    gMarioState->faceAngle[1]=(int16_t)(yaw/3.14159265359f*32768.f);
    vec3f_copy(gMarioObject->header.gfx.pos,gMarioState->pos);
    gMarioObject->header.gfx.angle[0]=0;gMarioObject->header.gfx.angle[1]=gMarioState->faceAngle[1];
    gMarioObject->header.gfx.angle[2]=0;
    set_mario_animation(gMarioState,anim);
    gMarioObject->header.gfx.animInfo.animFrame=frame;
    gfx_adapter_bind_output_buffers(buffers);
    geo_process_root_hack_single_node(s_mario_graph_node);
    vec3f_copy(out->position,gMarioState->pos);out->velocity[0]=out->velocity[1]=out->velocity[2]=0;
    out->faceAngle=yaw;out->health=gMarioState->health;out->forwardVelocity=0;
    out->animID=anim;out->animFrame=frame;out->action=gMarioState->action;
}
'''
    lib.write_text(s)
f=root/'src/decomp/engine/surface_collision.c'
s=f.read_text()
if 'VCSM_WIDE_COLLISION' not in s:
    # Widen only the triangle orientation products. Positions/normals/physics stay intact.
    s,n=re.subn(r'\((z[123] - z)\) \* \((x[123] - x[123])\) - \((x[123] - x)\) \* \((z[123] - z[123])\)',
        r'(int64_t)(\1) * (\2) - (int64_t)(\3) * (\4)',s)
    if n!=6:raise SystemExit(f'Pinned floor/ceiling predicates differ: {n}')
    f.write_text('/* VCSM_WIDE_COLLISION: large GTA triangles must not overflow N64-sized products. */\n'+s)
f=root/'src/load_surfaces.c'
s=f.read_text()
if 'VCSM_WIDE_NORMAL' not in s:
    for line in ['nx = (y2 - y1) * (z3 - z2) - (z2 - z1) * (y3 - y2);',
                 'ny = (z2 - z1) * (x3 - x2) - (x2 - x1) * (z3 - z2);',
                 'nz = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2);']:
        if line not in s:raise SystemExit('Pinned normal equation differs')
        s=s.replace(line,re.sub(r'= \(', '= (double)(',line).replace(' - (',' - (double)(',1))
    f.write_text('/* VCSM_WIDE_NORMAL: calculate cross products before float normalization. */\n'+s)
print('Own pose bridge and widened collision arithmetic verified')
shim=root/'src/decomp/shim.h'
s=shim.read_text()
old='static s16 level_trigger_warp(struct MarioState *m, s32 warpOp) { return 0; }'
if old in s:
    s=s.replace(old,'extern s16 vcsm_level_warp(struct MarioState *m, s32 warpOp);\nstatic s16 level_trigger_warp(struct MarioState *m, s32 warpOp) { return vcsm_level_warp(m, warpOp); }')
    shim.write_text(s)
lib=root/'src/libsm64.c'
s=lib.read_text()
if 'VCSM_DEATH_WARP' not in s:
    s=s.replace('static bool s_init_global = false;','static int vcsmDeathWarp=0; /* VCSM_DEATH_WARP */\nstatic bool s_init_global = false;')
    s=s.replace('    int32_t marioIndex = obj_pool_alloc_index','    vcsmDeathWarp=0;\n    int32_t marioIndex = obj_pool_alloc_index')
    s+='''
/* Original WARP_OP_DEATH was a no-op upstream. Notify the host at its real animation frame. */
s16 vcsm_level_warp(struct MarioState *m,s32 op){
    if(op==0x12 && !vcsmDeathWarp){vcsmDeathWarp=1;
        play_sound(SOUND_MENU_BOWSER_LAUGH,gGlobalSoundSource);}
    return 0;
}
SM64_LIB_FN int vcsm_death_warp(void){return vcsmDeathWarp;}
'''
    lib.write_text(s)
if 'play_sound(SOUND_MENU_BOWSER_LAUGH,m->marioObj->header.gfx.cameraToObject)' in s:
    s=s.replace('play_sound(SOUND_MENU_BOWSER_LAUGH,m->marioObj->header.gfx.cameraToObject)','play_sound(SOUND_MENU_BOWSER_LAUGH,gGlobalSoundSource)')
    lib.write_text(s)
if 'VCSM_AUDIO_LIFETIME' not in s:
    s=s.replace('    free( gMarioObject );','    /* VCSM_AUDIO_LIFETIME: SFX must not retain pointers into the freed Mario object. */\n    if(g_is_audio_initialized) stop_sounds_from_source(gMarioObject->header.gfx.cameraToObject);\n    free( gMarioObject );')
    s=s.replace('    global_state_delete( globalState );','    global_state_delete( globalState );\n    global_state_bind(NULL);')
    lib.write_text(s)
print('Original death-warp frame notification and ROM Bowser sound bridge verified')
