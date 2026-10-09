#include "core.hpp"
#include "CWorld.h"
#include "CPools.h"
#include <cmath>
#include <unordered_map>
#include <cstring>

namespace vcsm {
void UpdateWater(){
    auto p=ToGta(state.position);float level=0;
    // Proven in this EXE: cBuoyancy::ProcessBuoyancy CALL 5C2C80, five cdecl args;
    // callee reads floats x/y/z, float* at esp+24, ignoreZ at esp+28 after prologue.
    using Query=bool(__cdecl*)(float,float,float,float*,bool);
    auto query=reinterpret_cast<Query>(0x5c2c80);
    bool found=false;
    if(p.x>WORLD_MIN_X && p.x<WORLD_MAX_X && p.y>WORLD_MIN_Y && p.y<WORLD_MAX_Y)
        found=query(p.x,p.y,p.z,&level,true);
    sm.set_mario_water_level(mario,found?static_cast<int>((level-origin.z)/Scale):-110000);
    static bool last=false;if(found!=last){last=found;Log("GTA water region=%d surfaceZ=%.3f",found,level);}
}
void InteractionTick(CPlayerPed* player,unsigned tick){
    if(!CPools::ms_pPedPool)return;
    static std::unordered_map<int,unsigned> cooldown;
    auto p=ToGta(state.position);
    for(int i=0;i<CPools::ms_pPedPool->m_nSize;++i){auto ped=CPools::ms_pPedPool->GetAt(i);
        if(!ped || ped==player || ped->m_bInVehicle || ped->m_fHealth<=0)continue;
        float dx=ped->pos.x-p.x,dy=ped->pos.y-p.y,dz=ped->pos.z-0.85f-p.z;
        // Upstream attack API classifies actions/angles; the host must first establish
        // physical overlap, because that API does not do a broad-phase distance check.
        if(dx*dx+dy*dy>0.85f*0.85f || dz>1.5f || dz<-1.6f)continue;
        int ref=CPools::ms_pPedPool->GetRef(ped);
        auto it=cooldown.find(ref);if(it!=cooldown.end() && tick-it->second<18)continue;
        CVector target=ToSm(CVector(ped->pos.x,ped->pos.y,ped->pos.z-0.85f));
        if(!sm.mario_attack(mario,target.x,target.y,target.z,1.7f/Scale))continue;
        cooldown[ref]=tick;
        float before=ped->m_fHealth;
        bool heavy=state.action==0x0188088a || state.action==0x00880456 || state.action==0x018008aa
            || state.action==0x0080045a || state.action==0x008008a9 || state.action==0x0080023c;
        // Native SetFall ABI/prologue was checked against this executable, including RET 0x0c.
        const unsigned char fall[]={0x53,0x56,0x57,0x55,0x89,0xcd,0x83,0xec,0x10,0x31,0xf6,0x83};
        using Fall=void(__thiscall*)(CPed*,int,int,unsigned char);
        if(!memcmp(reinterpret_cast<void*>(0x4fd9f0),fall,sizeof(fall)))
            reinterpret_cast<Fall>(0x4fd9f0)(ped,heavy?1800:800,ANIM_MAN_FLOOR_HIT_F,1);
        ped->ReactToAttack(player);
        ped->InflictDamage(player,heavy?WEAPONTYPE_RAMMEDBYCAR:WEAPONTYPE_UNARMED,heavy?120.f:30.f,static_cast<ePedPieceTypes>(0),0);
        float len=sqrtf(dx*dx+dy*dy);if(len>0.001f){float force=heavy?0.45f:0.10f;
            ped->m_vecMoveSpeed.x+=dx/len*force;ped->m_vecMoveSpeed.y+=dy/len*force;}
        if(heavy){ped->m_vecMoveSpeed.z=0.2f;ped->bKnockedUpIntoAir=true;}
        Log("SM64 attack NPC ref=%d action=%08x heavy=%d health %.1f -> %.1f",ref,state.action,heavy,before,ped->m_fHealth);
    }
    if(cooldown.size()>256)cooldown.clear();
}
}
