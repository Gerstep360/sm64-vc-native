#include "core.hpp"
#include "Events.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <cstring>

namespace vcsm {
std::string GameDir() {
    char path[MAX_PATH]{}; GetModuleFileNameA(nullptr,path,MAX_PATH);
    std::string p(path); return p.substr(0,p.find_last_of("\\/")+1);
}
void Log(const char* fmt, ...) {
    FILE* f = fopen((GameDir()+"SM64VCNative.log").c_str(),"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f,"[%02d:%02d:%02d.%03d] ",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    va_list a; va_start(a,fmt); vfprintf(f,fmt,a); va_end(a);
    fputc('\n',f); fclose(f);
}
std::vector<uint8_t> Read(const std::string& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if (!f) return {};
    auto n=f.tellg(); if(n<=0 || n>64*1024*1024) return {};
    std::vector<uint8_t> b(static_cast<size_t>(n)); f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(b.data()),b.size())) return {};
    return b;
}
std::string Hash(const std::vector<uint8_t>& b,const wchar_t* algorithm) {
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    DWORD size{},got{},length{};
    if(BCryptOpenAlgorithmProvider(&alg,algorithm,nullptr,0)<0) return {};
    BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),4,&got,0);
    BCryptGetProperty(alg,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&length),4,&got,0);
    std::vector<uint8_t> obj(size),out(length);
    bool ok=BCryptCreateHash(alg,&hash,obj.data(),size,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,const_cast<PUCHAR>(b.data()),static_cast<ULONG>(b.size()),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,out.data(),length,0)>=0;
    if(hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
    if(!ok) return {};
    std::string s; char hex[3]; for(auto v:out){sprintf(hex,"%02x",v);s+=hex;} return s;
}
bool Guard() {
    Log("startup pid=%lu x86 build 2026.10.09-native-gameplay-r1",GetCurrentProcessId());
    if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x400000) {
        Log("REFUSED: unsupported image base"); return false;
    }
    auto hash=Hash(Read(GameDir()+"gta-vc.exe"),BCRYPT_SHA256_ALGORITHM);
    if(hash!="04e4db72629eaa786fdd182ac224f4fd6d68806f5f4fe5c1fb5f756dc4da11d7") {
        Log("REFUSED: executable hash %s is not the inspected build",hash.c_str()); return false;
    }
    if(plugin::GetGameVersion()!=GAME_10EN) {Log("REFUSED: not GAME_10EN");return false;}
    // Exact call bytes from the inspected executable; fail before patching if another mod changed them.
    struct Check {uintptr_t addr; unsigned char bytes[5];};
    Check checks[]={{0x4a5da0,{0xe8,0x6b,0xe6,0xff,0xff}},{0x4a604a,{0xe8,0x21,0x05,0,0}},
        {0x4a5b8e,{0xe8,0x7d,0xf5,0xff,0xff}},{0x4a613d,{0xe8,0x0e,0xa1,0x0a,0x00}},
        {0x4a46f1,{0xe8,0x5a,0x01,0,0}},{0x50dbfb,{0xe8,0x30,0xda,0xfa,0xff}},
        {0x4a64d0,{0xe8,0x4b,0x0e,0x0b,0}},
        {0x65cdfb,{0xe8,0xa0,0x01,0xfe,0xff}},{0x65d1d8,{0xe8,0xc3,0xfd,0xfd,0xff}},
        {0x65d856,{0xe8,0x45,0xf7,0xfd,0xff}},{0x65e2a3,{0xe8,0xf8,0xec,0xfd,0xff}},
        {0x65e680,{0xe8,0x1b,0xe9,0xfd,0xff}},{0x65ed10,{0xe8,0x8b,0xe2,0xfd,0xff}},
        {0x65ef1d,{0xe8,0x7e,0xe0,0xfd,0xff}}};
    // Process call expected target confirmed separately in identity.json/build script.
    for(auto& c:checks) if(memcmp(reinterpret_cast<void*>(c.addr),c.bytes,5)) {
        // SilentPatch already chains ReInitGame at this CALL. Only chain calls into the
        // exact compatibility binaries inspected locally, never arbitrary detours.
        bool trusted=false;HMODULE owner=nullptr;
        if(*reinterpret_cast<uint8_t*>(c.addr)==0xe8){
            uintptr_t target=c.addr+5+*reinterpret_cast<int32_t*>(c.addr+1);
            if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(target),&owner)){
                char file[MAX_PATH]{};GetModuleFileNameA(owner,file,MAX_PATH);
                auto h=Hash(Read(file),BCRYPT_SHA256_ALGORITHM);
                trusted=h=="71eaf21c9bcba7010c389dd57badd56cdaae55cf73cc52e1941529d598a6c771"
                     || h=="1aa8018744eac3c64f3375a4f05df1ff58011d07618da00e680aac6a0ed62fdf";
                if(trusted)Log("verified CALL chain %08x -> %s +%x",unsigned(c.addr),file,unsigned(target-reinterpret_cast<uintptr_t>(owner)));
            }
        }
        if(!trusted){Log("REFUSED: hook site %08x changed",static_cast<unsigned>(c.addr));return false;}
    }
    const unsigned char water[]={0x53,0xd9,0xee,0xd9,0xee,0xd9,0x05,0x54};
    if(memcmp(reinterpret_cast<void*>(0x5c2c80),water,sizeof(water))){Log("REFUSED: verified water query changed");return false;}
    if(*reinterpret_cast<uint8_t*>(checks[0].addr)!=0xe8) {Log("REFUSED: process call changed");return false;}
    Log("accepted SHA256 and GAME_10EN; hook call sites verified"); return true;
}
bool Sm64::Load() {
    dll=LoadLibraryExA((GameDir()+"sm64.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!dll){Log("sm64.dll load failed: %lu",GetLastError());return false;}
#define BIND(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(dll,"sm64_" #name)); if(!name){Log("missing sm64_" #name);FreeLibrary(dll);dll=nullptr;return false;}
    BIND(register_debug_print_function) BIND(global_init) BIND(global_terminate)
    BIND(register_play_sound_function)
    BIND(static_surfaces_load) BIND(mario_create) BIND(mario_delete) BIND(mario_tick)
    BIND(set_mario_position) BIND(set_mario_velocity) BIND(surface_find_floor_height)
    BIND(set_mario_action) BIND(set_mario_forward_velocity)
    BIND(audio_init) BIND(audio_tick) BIND(set_mario_water_level) BIND(mario_attack)
    BIND(mario_take_damage) BIND(mario_heal) BIND(mario_kill) BIND(set_mario_health) BIND(set_mario_faceangle)
    BIND(set_mario_invincibility)
#undef BIND
    pose=reinterpret_cast<Pose>(GetProcAddress(dll,"vcsm_mario_pose"));
    if(!pose){Log("missing our vcsm_mario_pose extension");FreeLibrary(dll);dll=nullptr;return false;}
    deathWarp=reinterpret_cast<decltype(deathWarp)>(GetProcAddress(dll,"vcsm_death_warp"));
    if(!deathWarp){Log("missing our vcsm_death_warp extension");FreeLibrary(dll);dll=nullptr;return false;}
    return true;
}
}

struct Install {
    Install() {
        if(!vcsm::Guard()) return;
        plugin::Events::gameProcessEvent.after += vcsm::Process;
        vcsm::Log("process hook installed");
    }
} install;

void vcsm::InstallRuntimeHooks(){
        static bool installed=false;if(installed)return;installed=true;
        // WindowedMode creates/resizes its window during RenderWare initialization.
        // Install render/device callbacks only after the first initialized world update.
        plugin::Events::gameProcessEvent.before += vcsm::BeforeProcess;
        plugin::Events::renderSceneEvent.after += vcsm::Render;
        vcsm::Log("render hook installed");
        plugin::Events::shutdownRwEvent.before += vcsm::Shutdown;
        vcsm::Log("shutdown hook installed");
        plugin::Events::restartGameEvent.before += vcsm::Stop;
        vcsm::Log("restart hook installed");
        plugin::Events::pedDtorEvent.before += vcsm::PlayerDeleted;
        vcsm::Log("ped destructor hook installed");
        plugin::Events::d3dLostEvent.before += vcsm::DestroyRenderer;
        vcsm::Log("device hook installed");
        plugin::Events::drawFontsEvent.after += vcsm::HudRender;
        vcsm::Log("original HUD hook installed");
}
