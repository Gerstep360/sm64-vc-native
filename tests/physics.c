/* Real libsm64/ROM oracle. This synthetic floor is only a test, never the GTA world. */
#include <libsm64.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static float positions[SM64_GEO_MAX_TRIANGLES*9],normals[SM64_GEO_MAX_TRIANGLES*9],colors[SM64_GEO_MAX_TRIANGLES*9],uv[SM64_GEO_MAX_TRIANGLES*6];
static struct SM64MarioGeometryBuffers geo={positions,normals,colors,uv,0};
static struct SM64MarioState state;
static struct SM64Surface floor_mesh[2]={
    {0,0,0,{{-10000,0,-10000},{-10000,0,10000},{10000,0,-10000}}},
    {0,0,0,{{10000,0,-10000},{-10000,0,10000},{10000,0,10000}}}};
static void tick(int id,struct SM64MarioInputs* in){sm64_mario_tick(id,in,&state,&geo);}
static void diagnostic(const char* s){if(strstr(s,"ROM geometry") || strstr(s,"ERROR"))puts(s);}
int main(int argc,char** argv) {
    if(argc!=2){fprintf(stderr,"physics.exe ROM.z64\n");return 2;}
    FILE* f=fopen(argv[1],"rb");if(!f) return 2;
    unsigned char* rom=malloc(8388608);size_t n=fread(rom,1,8388608,f);fclose(f);if(n!=8388608)return 2;
    unsigned char* atlas=malloc(4*SM64_TEXTURE_WIDTH*SM64_TEXTURE_HEIGHT);
    sm64_register_debug_print_function(diagnostic);
    sm64_global_init(rom,atlas);free(rom);
    sm64_static_surfaces_load(floor_mesh,2);
    int id=sm64_mario_create(0,5,0);if(id<0)return 3;
    struct SM64MarioInputs in={0,1,0,0,0,0,0};
    for(int i=0;i<60;++i)tick(id,&in);
    printf("floor y=%.3f triangles=%u animation=%d\n",state.position[1],geo.numTrianglesUsed,state.animID);
    unsigned hash=2166136261u;unsigned char* b=(unsigned char*)positions;
    for(unsigned i=0;i<geo.numTrianglesUsed*9*sizeof(float);++i)hash=(hash^b[i])*16777619u;
    printf("original geometry position FNV32=%08x\n",hash);
    if(fabsf(state.position[1])>1 || geo.numTrianglesUsed==0)return 4;
    in.stickY=-1;
    for(int i=0;i<60;++i)tick(id,&in);
    float moved=hypotf(state.position[0],state.position[2]);
    printf("run displacement=%.3f forwardVelocity=%.3f\n",moved,state.forwardVelocity);
    if(moved<100)return 5;
    in.stickY=0;
    for(int i=0;i<30;++i)tick(id,&in);
    in.buttonA=1;tick(id,&in);in.buttonA=0;float peak=0;
    for(int i=0;i<50;++i){tick(id,&in);if(state.position[1]>peak)peak=state.position[1];}
    printf("jump peak=%.3f landedY=%.3f\n",peak,state.position[1]);
    if(peak<50 || fabsf(state.position[1])>1)return 6;
    // Stress floor refresh while Mario retains floor pointers, including changed triangle count.
    for(int i=0;i<1000;++i){sm64_static_surfaces_load(floor_mesh,(i%2)+1);tick(id,&in);}
    printf("streaming refresh 1000 ticks completed; finite=%d\n",isfinite(state.position[1]));
    if(!isfinite(state.position[1]))return 7;
    struct SM64Surface large[2]={
        {0,0,0,{{-100000,0,-100000},{-100000,0,100000},{100000,0,-100000}}},
        {0,0,0,{{100000,0,-100000},{-100000,0,100000},{100000,0,100000}}}};
    sm64_static_surfaces_load(large,2);
    float bigfloor=sm64_surface_find_floor_height(0,100,0);
    printf("large world triangle floor=%.3f\n",bigfloor);if(fabsf(bigfloor)>1)return 8;
    sm64_set_mario_position(id,0,0,0);sm64_set_mario_action(id,0x0c400201);
    sm64_set_mario_health(id,0x880);sm64_mario_take_damage(id,2,0,0,0,100);
    for(int i=0;i<90;++i)tick(id,&in);
    printf("damage health=%04x action=%08x\n",(unsigned short)state.health,state.action);
    if(state.health>=0x880)return 9;
    sm64_mario_kill(id);int died=0;
    for(int i=0;i<180;++i){tick(id,&in);if(state.animID==3 || state.animID==0x2e || state.animID==0x32)died=1;}
    printf("original death animation=%d finalAnim=%d health=%04x\n",died,state.animID,(unsigned short)state.health);
    if(!died)return 10;
    sm64_mario_delete(id);sm64_global_terminate();free(atlas);
    puts("PASS authentic libsm64 x86 floor/run/jump/geometry/lifecycle");return 0;
}
