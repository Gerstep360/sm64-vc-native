#include "core.hpp"
#include "MinHook.h"
#include "CWorld.h"
#include "CTimer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vcsm {
// The SDK declares void, but the inspected x86 callee returns a success flag in AL.
using DamageFn=bool(__thiscall*)(CPed*,void*,eWeaponType,float,ePedPieceTypes,unsigned char);
static DamageFn original{};
static bool installed=false;
static bool __fastcall Damage(CPed* ped,void*,void* source,eWeaponType weapon,float amount,ePedPieceTypes piece,unsigned char direction){
    if(!MarioOwnsPed(ped))return original(ped,source,weapon,amount,piece,direction);
    if(!std::isfinite(amount) || amount<=0 || state.health<0x100 || state.invincTimer>0)return false;
    if((int(weapon)<=11 && ped->bMeleeProof) || (int(weapon)>=17 && int(weapon)<=35 && ped->bBulletProof)
       || ((weapon==WEAPONTYPE_RAMMEDBYCAR || weapon==WEAPONTYPE_RUNOVERBYCAR) && ped->bCollisionProof)
       || (weapon==WEAPONTYPE_EXPLOSION && ped->bExplosionProof)
       || ((weapon==WEAPONTYPE_FLAMETHROWER || weapon==WEAPONTYPE_MOLOTOV) && ped->bFireProof))return false;
    // Native callers already establish the bullet/car/explosion contact. Mario owns
    // health and knockback, so GTA must not start its own fall/death animations here.
    float absorbed=std::min(ped->m_fArmour,amount);ped->m_fArmour-=absorbed;amount-=absorbed;
    auto& info=CWorld::Players[CWorld::PlayerInFocus];
    if(absorbed>0)info.m_nLastTimeArmourLost=CTimer::m_snTimeInMilliseconds;
    if(amount<=0)return true;
    unsigned wedges=std::clamp(static_cast<unsigned>(ceilf(amount/12.5f)),1u,8u);
    CVector at=ToGta(state.position);
    // 'source' can be a projectile or null, not always a CPed; use the native entity
    // position only when memory is readable. Otherwise knock back from the front.
    MEMORY_BASIC_INFORMATION m{};
    auto address=reinterpret_cast<uintptr_t>(source);
    if(source && VirtualQuery(source,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))
       && address+sizeof(CEntity)<=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize)
        at=static_cast<CEntity*>(source)->pos;
    else {at.x+=sinf(state.faceAngle);at.y-=cosf(state.faceAngle);}
    auto sp=ToSm(at);
    if(MarioDriving())sm.set_mario_health(mario,uint16_t(std::max(0xff,int(state.health)-int(wedges)*256)));
    else {sm.mario_take_damage(mario,wedges,0,sp.x,sp.y,sp.z);sm.set_mario_invincibility(mario,30);}
    info.m_nLastTimeEnergyLost=CTimer::m_snTimeInMilliseconds;
    Log("GTA -> Mario damage weapon=%d native=%.2f wedges=%u armour=%.1f",int(weapon),amount,wedges,ped->m_fArmour);
    return true;
}
bool DamageInit(){
    const unsigned char bytes[]={0xd9,0x05,0x70,0x41,0x69,0x00,0x53,0x56,0x57,0x55,0x83,0xec};
    if(memcmp(reinterpret_cast<void*>(0x525b20),bytes,sizeof(bytes))){Log("REFUSED: verified InflictDamage prologue changed");return false;}
    auto s=MH_Initialize();if(s!=MH_OK){Log("MinHook init failed %d",s);return false;}
    s=MH_CreateHook(reinterpret_cast<void*>(0x525b20),&Damage,reinterpret_cast<void**>(&original));
    if(s==MH_OK)s=MH_EnableHook(reinterpret_cast<void*>(0x525b20));
    if(s!=MH_OK){Log("damage hook failed %d",s);MH_Uninitialize();return false;}
    installed=true;Log("verified GTA damage bridge installed");return true;
}
void DamageClose(){if(installed){MH_DisableHook(reinterpret_cast<void*>(0x525b20));MH_RemoveHook(reinterpret_cast<void*>(0x525b20));MH_Uninitialize();installed=false;}}
}
