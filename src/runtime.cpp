#include "core.hpp"
#include "CWorld.h"
#include "CPools.h"
#include "CPad.h"
#include "CCamera.h"
#include "CTimer.h"
#include "CStoredCollPoly.h"
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace vcsm {
Sm64 sm;
Geometry geometry;
Geometry previousGeometry;
SM64MarioState state{};
SM64MarioState previousState{};
float renderAlpha=1.f;
CVector origin(0,0,0);
std::vector<uint8_t> texture;
int32_t mario=-1;
static bool initialized=false,failed=false;
static CPlayerPed* player=nullptr;
static void** originalVtable=nullptr;
static std::array<void*,32> proxyVtable{};
static bool savedVisible{},savedCollision{};
static double accumulator=0;
static DWORD lastTime=0,lastLog=0;
static unsigned ticks=0;
static uint32_t lastAction=~0u;
static uint8_t pendingA=0,pendingB=0,pendingZ=0;
static CVector lastProxy(0,0,0);
static bool nativeVehicle=false,resumeAfterHospital=false;
static DWORD nativeSince=0;
static unsigned deathTicks=0,lives=3,coins=0;
static unsigned warpTicks=0;
float DeathTransition(){return resumeAfterHospital?1.f:warpTicks?std::min(1.f,(warpTicks+renderAlpha)/48.f):0.f;}
static float publishedHealth=100.f;
static int lastMoney=0;
static std::vector<std::pair<RpAtomic*,unsigned>> proxyAtomics;
static RpAtomic* HideAtomic(RpAtomic* a,void*){
    auto it=std::find_if(proxyAtomics.begin(),proxyAtomics.end(),[&](auto& v){return v.first==a;});
    if(it==proxyAtomics.end())proxyAtomics.emplace_back(a,RpAtomicGetFlags(a));
    RpAtomicSetFlags(a,RpAtomicGetFlags(a)&~rpATOMICRENDER);return a;
}
static void HideProxy(){if(player && player->m_pRwClump)RpClumpForAllAtomics(player->m_pRwClump,HideAtomic,nullptr);}
static RpAtomic* RestoreAtomic(RpAtomic* a,void*){
    auto it=std::find_if(proxyAtomics.begin(),proxyAtomics.end(),[&](auto& v){return v.first==a;});
    if(it!=proxyAtomics.end())RpAtomicSetFlags(a,it->second);return a;
}
bool MarioOwnsPed(CPed* p){return mario>=0 && p==player;}
bool MarioDriving(){return nativeVehicle;}
unsigned MarioLives(){return lives;}
unsigned MarioCoins(){return coins;}
unsigned MarioStars(){return unsigned(std::max(0,CWorld::Players[CWorld::PlayerInFocus].m_nCollectablesCollected));}
void LibDiagnostic(const char* s){if(strstr(s,"ERROR") || strstr(s,"Tried to") || strstr(s,"failed"))Log("libsm64: %s",s);}
CVector ToSm(const CVector& p){return CVector((p.x-origin.x)/Scale,(p.z-origin.z)/Scale,-(p.y-origin.y)/Scale);}
CVector ToGta(const float* p){return CVector(origin.x+p[0]*Scale,origin.y-p[2]*Scale,origin.z+p[1]*Scale);}
static bool PlayerValid() {
    if(!player || !CPools::ms_pPedPool) return false;
    // Compare addresses without dereferencing a potentially deleted player.
    for(int i=0;i<CPools::ms_pPedPool->m_nSize;++i) if(CPools::ms_pPedPool->GetAt(i)==player) return true;
    return false;
}
static void __fastcall ProxyControl(CPlayerPed* ped,void*) {
    if(mario<0 || ped!=player) return;
    // Keep GTA's player identity and camera target while SM64 owns movement.
    ped->m_vecMoveSpeed=CVector(0,0,0); ped->m_vecTurnSpeed=CVector(0,0,0);
    ped->bUsesCollision=true;ped->bIsVisible=false;
    if(!deathTicks){auto pad=CPad::GetPad(0);ped->ProcessWeaponSwitch(pad);
        auto weapon=ped->m_aWeapons[ped->m_nCurrentWeapon].m_eWeaponType;
        if(weapon!=WEAPONTYPE_UNARMED && weapon!=WEAPONTYPE_BRASSKNUCKLE)ped->ProcessPlayerWeapon(pad);}
}
static void __fastcall ProxyCollision(CPlayerPed*,void*) {}
void BeforeProcess(){
    if(!PlayerValid() || mario<0 || deathTicks)return;
    auto pad=CPad::GetPad(0);static bool previousEnter=false;
    bool enter=pad->GetExitVehicle();bool pressed=enter && !previousEnter;previousEnter=enter;
    if(nativeVehicle || !pressed || !CPools::ms_pVehiclePool)return;
    float closest=16.f;
    for(int i=0;i<CPools::ms_pVehiclePool->m_nSize;++i){auto v=CPools::ms_pVehiclePool->GetAt(i);if(!v)continue;
        float dx=v->pos.x-player->pos.x,dy=v->pos.y-player->pos.y,dz=v->pos.z-player->pos.z;
        closest=std::min(closest,dx*dx+dy*dy+dz*dz);}
    if(closest>=16.f)return;
    nativeVehicle=true;nativeSince=GetTickCount();
    // GTA owns entry, exit, driving and traffic physics. Preserve only Mario's renderer.
    for(int i:{8,9,10,12})proxyVtable[i]=originalVtable[i];
    player->bUsesCollision=savedCollision;player->bIsVisible=false;
    Log("vehicle control handed to GTA; native enter key");
}
static bool Init() {
    if(initialized) return true;
    if(failed) return false;
    auto rom=Read(GameDir()+"sm64.us.z64");
    if(rom.size()!=8388608 || Hash(rom,BCRYPT_SHA1_ALGORITHM)!="9bef1128717f958171a4afac3ed78ee2bb4e86ce") {
        Log("ROM rejected: require original US big-endian 8MiB ROM");failed=true;return false;
    }
    // MinHook enumerates/suspends threads; install after startup, never under the DLL loader lock.
    if(!DamageInit() || !sm.Load()){failed=true;return false;}
    texture.resize(4*SM64_TEXTURE_WIDTH*SM64_TEXTURE_HEIGHT);
    sm.register_debug_print_function(LibDiagnostic);
    sm.global_init(rom.data(),texture.data());
    AudioInit(rom);HudInit(rom);
    initialized=true;Log("libsm64 initialized; authentic atlas %dx%d and ROM animations",SM64_TEXTURE_WIDTH,SM64_TEXTURE_HEIGHT);
    return true;
}
static void Spawn(CPlayerPed* ped) {
    if(!Init() || ped->m_bInVehicle) return;
    origin=CVector(ped->pos.x,ped->pos.y,0);
    ResetCollision();
    if(!RefreshCollision(ped->pos,true)){Log("spawn deferred: no valid resident collision");return;}
    bool found=false;float ground=CWorld::FindGroundZFor3DCoord(ped->pos.x,ped->pos.y,ped->pos.z+3.f,&found);
    if(!found){Log("spawn refused: GTA ground query failed");return;}
    CColPoint hit{};CEntity* groundEntity=nullptr;CStoredCollPoly poly{};
    if(CWorld::ProcessVerticalLine(CVector(ped->pos.x,ped->pos.y,ped->pos.z+3.f),-20.f,hit,groundEntity,true,false,false,true,true,false,&poly)) {
        float nz=0;if(poly.m_bIsActual){auto& a=poly.m_aVertices[0];auto& b=poly.m_aVertices[1];auto& c=poly.m_aVertices[2];nz=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
        Log("ground query model=%d type=%d poly=%d windingNZ=%f normalZ=%f",groundEntity?groundEntity->m_nModelIndex:-1,groundEntity?groundEntity->m_nType:-1,poly.m_bIsActual,nz,hit.m_vNormal.z);
    }
    CVector p=ToSm(CVector(ped->pos.x,ped->pos.y,ground+0.05f));
    float smFloor=sm.surface_find_floor_height(p.x,p.y+200.f,p.z);
    if(!std::isfinite(smFloor) || smFloor<-10000.f || std::abs(smFloor-p.y)>250.f) {
        Log("spawn refused: floor disagreement GTA=%f SM64=%f",ground,smFloor);return;
    }
    mario=sm.mario_create(p.x,smFloor+5.f,p.z);
    if(mario<0){Log("sm64_mario_create failed");return;}
    state={};state.position[0]=p.x;state.position[1]=smFloor+5.f;state.position[2]=p.z;
    previousState=state;previousGeometry.buffers.numTrianglesUsed=0;renderAlpha=1.f;
    player=ped;savedVisible=ped->bIsVisible;savedCollision=ped->bUsesCollision;
    lastProxy=ped->pos;
    originalVtable=*reinterpret_cast<void***>(ped);
    std::copy_n(originalVtable,proxyVtable.size(),proxyVtable.begin());
    proxyVtable[8]=reinterpret_cast<void*>(&ProxyControl);
    proxyVtable[9]=proxyVtable[10]=reinterpret_cast<void*>(&ProxyCollision);
    // VC's visibility setup can set bIsVisible again. Suppress the proxy's actual
    // virtual render and pre-render instead of relying on that mutable flag.
    proxyVtable[13]=reinterpret_cast<void*>(&ProxyCollision);
    *reinterpret_cast<void***>(ped)=proxyVtable.data();
    ped->bUsesCollision=true;ped->bIsVisible=false;
    deathTicks=warpTicks=0;nativeVehicle=false;publishedHealth=ped->m_fHealth;
    sm.set_mario_health(mario,uint16_t(std::min(0x880u,unsigned(std::clamp(publishedHealth,1.f,100.f)/100.f*2048.f)+0x80u)));
    lastMoney=CWorld::Players[CWorld::PlayerInFocus].m_nMoney;
    accumulator=0;lastTime=GetTickCount();ticks=0;lastAction=~0u;
    pendingA=pendingB=pendingZ=0;
    Log("Mario spawned id=%d GTA street=(%.3f,%.3f,%.3f) scale=%.6f proxy active",mario,ped->pos.x,ped->pos.y,ground,Scale);
}
void Stop() {
    if(PlayerValid()) {
        if(player->m_pRwClump)RpClumpForAllAtomics(player->m_pRwClump,RestoreAtomic,nullptr);
        *reinterpret_cast<void***>(player)=originalVtable;
        player->bUsesCollision=savedCollision;player->bIsVisible=savedVisible;
        player->m_vecMoveSpeed=CVector(0,0,0);
    }
    player=nullptr;originalVtable=nullptr;
    proxyAtomics.clear();
    if(mario>=0 && initialized) sm.mario_delete(mario);
    if(mario>=0) Log("Mario stopped; player physics restored");
    mario=-1;geometry.buffers.numTrianglesUsed=0;accumulator=0;ResetCollision();
    nativeVehicle=false;deathTicks=warpTicks=0;
}
void PlayerDeleted(CPed* p){if(p==player) Stop();}
void Shutdown() {
    Stop();DamageClose();AudioClose();DestroyRenderer();
    if(initialized){sm.global_terminate();initialized=false;}
    if(sm.dll){FreeLibrary(sm.dll);sm.dll=nullptr;}
    Log("shutdown complete");
}
void Process() {
    auto ped=CWorld::Players[CWorld::PlayerInFocus].m_pPed;
    if(ped)InstallRuntimeHooks();
    if(mario>=0 && (ped!=player || !PlayerValid())) Stop();
    bool paused=CTimer::m_UserPause || CTimer::m_CodePause;
    if(initialized)AudioPump(paused || mario<0);
    if(paused){lastTime=GetTickCount();accumulator=0;renderAlpha=1.f;return;}
    static bool wasM=false;static DWORD lastToggle=0;
    bool heldM=(GetAsyncKeyState('M')&0x8000)!=0;
    bool toggle=heldM && !wasM && GetTickCount()-lastToggle>350;wasM=heldM;
    if(toggle)lastToggle=GetTickCount();
    if(toggle){if(mario>=0) Stop();else if(ped) Spawn(ped);}
    if(resumeAfterHospital && mario<0 && ped && ped->m_fHealth>50.f
       && CWorld::Players[CWorld::PlayerInFocus].m_nPlayerState==PLAYERSTATE_PLAYING){
        Spawn(ped);if(mario>=0){resumeAfterHospital=false;Log("Mario restored after native hospital restart");}}
    if(mario<0) return;
    HideProxy();
    // Opt-in one-shot local test bridge. No network listener or automatic commands.
    auto command=Read(GameDir()+"SM64VCNative.test");
    if(!command.empty()){
        DeleteFileA((GameDir()+"SM64VCNative.test").c_str());
        std::string text(command.begin(),command.end());float damage=0;
        if(text.rfind("KILL",0)==0)sm.mario_kill(mario);
        else if(sscanf(text.c_str(),"GTA_DAMAGE %f",&damage)==1 && damage>0 && damage<=1000)
            player->InflictDamage(nullptr,WEAPONTYPE_PISTOL,damage,static_cast<ePedPieceTypes>(0),0);
        Log("local test command consumed: %s",text.c_str());
    }
    auto& info=CWorld::Players[CWorld::PlayerInFocus];
    if(info.m_nMoney>lastMoney){++coins;Log("native money collected delta=%d total=%d",info.m_nMoney-lastMoney,info.m_nMoney);}
    lastMoney=info.m_nMoney;
    if(!deathTicks && player->m_fHealth>publishedHealth+0.5f){
        unsigned restored=unsigned((player->m_fHealth-publishedHealth)*256.f/12.5f);
        sm.set_mario_health(mario,uint16_t(std::min(0x880u,unsigned(std::max(0,int(state.health)))+restored)));
        Log("native health pickup restored Mario health units=%u",restored);}
    if(nativeVehicle){
        bool transitioning=player->m_bInVehicle || player->m_ePedState==PEDSTATE_ENTER_CAR
            || player->m_ePedState==PEDSTATE_CAR_JACK || player->m_ePedState==PEDSTATE_EXIT_CAR
            || player->m_ePedState==PEDSTATE_SEEK_CAR || player->m_ePedState==PEDSTATE_SEEK_BOAT;
        if(!transitioning && GetTickCount()-nativeSince>1500){
            nativeVehicle=false;proxyVtable[8]=reinterpret_cast<void*>(&ProxyControl);
            proxyVtable[9]=proxyVtable[10]=reinterpret_cast<void*>(&ProxyCollision);
            CVector sp=ToSm(CVector(player->pos.x,player->pos.y,player->pos.z-0.85f));
            RefreshCollision(player->pos,true);
            sm.set_mario_position(mario,sp.x,sp.y,sp.z);sm.set_mario_velocity(mario,0,0,0);
            sm.set_mario_forward_velocity(mario,0);sm.set_mario_action(mario,0x0c400201);
            state.position[0]=sp.x;state.position[1]=sp.y;state.position[2]=sp.z;
            previousState=state;previousGeometry.buffers.numTrianglesUsed=0;
            accumulator=0;lastTime=GetTickCount();lastProxy=player->pos;
            Log("native vehicle exit complete; Mario physics resumed");
        }else{
            auto v=player->m_pVehicle;CVector p=player->pos;float yaw=state.faceAngle;
            if(player->m_bInVehicle && v){p.z-=0.42f;yaw=atan2f(v->up.x,-v->up.y);}else p.z-=0.85f;
            auto sp=ToSm(p);
            sm.pose(mario,sp.x,sp.y,sp.z,yaw,player->m_bInVehicle?0x84:0xc3,player->m_bInVehicle?20:0,&state,&geometry.buffers);
            static DWORD poseLog=0;
            if(GetTickCount()-poseLog>1000){poseLog=GetTickCount();float ymin=1e9f,ymax=-1e9f;
                for(unsigned i=0;i<geometry.buffers.numTrianglesUsed*3;++i){ymin=std::min(ymin,geometry.position[3*i+1]);ymax=std::max(ymax,geometry.position[3*i+1]);}
                Log("native vehicle pedState=%d inCar=%d model=%d GTA=(%.2f,%.2f,%.2f) geomY=(%.2f,%.2f) tris=%u",int(player->m_ePedState),player->m_bInVehicle,
                    v?v->m_nModelIndex:-1,p.x,p.y,p.z,ymin*Scale,ymax*Scale,geometry.buffers.numTrianglesUsed);}
            previousState=state;previousGeometry.buffers.numTrianglesUsed=0;renderAlpha=1.f;
            publishedHealth=std::max(1.f,std::clamp((state.health-0x80)/256.f,0.f,8.f)*12.5f);
            player->m_fHealth=publishedHealth;
            lastTime=GetTickCount();accumulator=0;lastProxy=player->pos;
            player->bIsVisible=false;
            if(state.health>=0x100)return;
            // Native vehicles must stop controlling the ped before Mario's death sequence.
            if(player->m_bInVehicle){Stop();info.KillPlayer();resumeAfterHospital=true;return;}
            nativeVehicle=false;proxyVtable[8]=reinterpret_cast<void*>(&ProxyControl);
            proxyVtable[9]=proxyVtable[10]=reinterpret_cast<void*>(&ProxyCollision);
        }
    }
    // Scripted doors and missions can teleport GTA's player independently of ProcessControl.
    // Accept their new position once; forcing the old SM64 position causes a portal loop.
    CVector changed(player->pos.x-lastProxy.x,player->pos.y-lastProxy.y,player->pos.z-lastProxy.z);
    if(changed.x*changed.x+changed.y*changed.y+changed.z*changed.z>0.25f) {
        CVector target=player->pos;target.z-=0.85f;
        ResetCollision();
        if(!RefreshCollision(target,true)){Log("script relocation deferred; collision unavailable");accumulator=0;return;}
        CVector sp=ToSm(target);
        float floor=sm.surface_find_floor_height(sp.x,sp.y+250.f,sp.z);
        if(floor<-10000.f || !std::isfinite(floor)){Log("script relocation deferred; no floor");accumulator=0;return;}
        if(sp.y<floor+5.f)sp.y=floor+5.f;
        sm.set_mario_position(mario,sp.x,sp.y,sp.z);sm.set_mario_velocity(mario,0,0,0);
        sm.set_mario_forward_velocity(mario,0);sm.set_mario_action(mario,0x0c400201); // ACT_IDLE, upstream enum.
        state.position[0]=sp.x;state.position[1]=sp.y;state.position[2]=sp.z;
        previousState=state;previousGeometry.buffers.numTrianglesUsed=0;renderAlpha=1.f;
        geometry.buffers.numTrianglesUsed=0;accumulator=0;lastTime=GetTickCount();
        lastProxy=player->pos;
        Log("accepted GTA script relocation to (%.3f,%.3f,%.3f)",target.x,target.y,target.z);
    }
    auto pad=CPad::GetPad(0);
    SM64MarioInputs in{};
    const auto& keys=CPad::NewKeyState;
    in.stickX=std::clamp(pad->GetPedWalkLeftRight()/128.f,-1.f,1.f);
    in.stickY=std::clamp(pad->GetPedWalkUpDown()/128.f,-1.f,1.f);
    // GTA forwards is negative stickY, which is what upstream libsm64 expects.
    in.buttonA=pad->JumpJustDown() || pad->NewState.ButtonSquare || keys.standardKeys[32];
    in.buttonB=pad->GetWeapon()!=0;
    in.buttonZ=pad->NewState.RightShoulder2 || keys.lctrl || keys.rctrl || keys.standardKeys['Z'];
    // Keyboard has full authentic running speed by default. Shift + direction enables
    // a bounded faster host traversal; physics still advance at the original 30 Hz.
    bool fast=pad->GetSprint();
    if(player->m_aWeapons[player->m_nCurrentWeapon].m_eWeaponType!=WEAPONTYPE_UNARMED)in.buttonB=0;
    auto& front=TheCamera.m_asCams[TheCamera.m_nActiveCam].m_vecFront;
    float len=sqrtf(front.x*front.x+front.y*front.y);
    in.camLookX=len>0.001f?front.x/len:0.f;
    in.camLookZ=len>0.001f?-front.y/len:-1.f;
    pendingA|=in.buttonA;pendingB|=in.buttonB;pendingZ|=in.buttonZ;
    if(deathTicks || state.health<0x100){in.stickX=in.stickY=0;in.buttonA=in.buttonB=in.buttonZ=0;pendingA=pendingB=pendingZ=0;}
    DWORD now=GetTickCount();double delta=std::min((now-lastTime)/1000.0,0.2);lastTime=now;
    accumulator+=delta;
    bool collisionOk=RefreshCollision(ToGta(state.position),false);
    if(!collisionOk){accumulator=0;return;}
    while(accumulator>=1.0/30.0) {
        previousState=state;
        previousGeometry.position=geometry.position;previousGeometry.normal=geometry.normal;
        previousGeometry.buffers.numTrianglesUsed=geometry.buffers.numTrianglesUsed;
        in.buttonA|=pendingA;in.buttonB|=pendingB;in.buttonZ|=pendingZ;
        if(!RefreshVehicleCollision(ToGta(state.position))){accumulator=0;return;}
        UpdateWater();
        sm.mario_tick(mario,&in,&state,&geometry.buffers);
        if(fast && state.action==0x04000440 && state.forwardVelocity>0.f && state.forwardVelocity<48.f)
            sm.set_mario_forward_velocity(mario,48.f);
        pendingA=pendingB=pendingZ=0;
        accumulator-=1.0/30.0;++ticks;
        InteractionTick(player,ticks);
        if(state.health<0x100){
            if(!deathTicks){Log("original Mario death animation started action=%08x anim=%d",state.action,state.animID);if(lives) --lives;}
            ++deathTicks;
            if(sm.deathWarp() && !warpTicks){warpTicks=1;Log("original death warp frame reached; ROM Bowser transition 48 ticks");}
            if(warpTicks && ++warpTicks>=48){
                Stop();info.KillPlayer();resumeAfterHospital=true;
                Log("Mario death sequence finished; native hospital restart requested");return;}
        }
        if(geometry.buffers.numTrianglesUsed>SM64_GEO_MAX_TRIANGLES || !std::isfinite(state.position[1])) {
            Log("invalid Mario output; stopping");Stop();return;
        }
        if(state.action!=lastAction){Log("action=%08x anim=%d frame=%d tick=%u",state.action,state.animID,state.animFrame,ticks);lastAction=state.action;}
    }
    renderAlpha=static_cast<float>(accumulator*30.0);
    float smooth[3];for(int j=0;j<3;++j)smooth[j]=previousState.position[j]+renderAlpha*(state.position[j]-previousState.position[j]);
    CVector p=ToGta(smooth);
    // GTA's ped origin sits above feet. Camera/streaming/mission systems follow this proxy.
    player->pos=CVector(p.x,p.y,p.z+0.85f);
    player->m_fHeadingCurrent=player->m_fHeadingGoal=atan2f(-sinf(state.faceAngle),-cosf(state.faceAngle));
    publishedHealth=std::max(1.f,std::clamp((state.health-0x80)/256.f,0.f,8.f)*12.5f);
    player->m_fHealth=publishedHealth;
    lastProxy=player->pos;
    player->m_vecMoveSpeed=CVector(0,0,0);player->m_vecTurnSpeed=CVector(0,0,0);
    player->bIsStanding=std::abs(state.velocity[1])<0.01f;
    player->UpdateRwFrame();player->RemoveAndAdd();
    if(now-lastLog>1000){lastLog=now;Log("tick=%u pos=(%.3f,%.3f,%.3f) velY=%.3f tris=%u camera=(%.2f,%.2f,%.2f) input=(%.2f,%.2f,%u,%u,%u)",
        ticks,p.x,p.y,p.z,state.velocity[1],geometry.buffers.numTrianglesUsed,TheCamera.pos.x,TheCamera.pos.y,TheCamera.pos.z,
        in.stickX,in.stickY,in.buttonA,in.buttonB,in.buttonZ);}
}
}
