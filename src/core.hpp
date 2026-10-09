#pragma once
#include <windows.h>
#include <libsm64.h>
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include "CPlayerPed.h"

namespace vcsm {
constexpr float Scale = 0.0085f;
constexpr unsigned MaxSurfaces = 131072;
void Log(const char* fmt, ...);
std::string GameDir();
std::string Hash(const std::vector<uint8_t>& bytes, const wchar_t* algorithm);
std::vector<uint8_t> Read(const std::string& path);
bool Guard();

struct Sm64 {
    HMODULE dll{};
#define SM_API(name) decltype(&sm64_##name) name{};
    SM_API(register_debug_print_function)
    SM_API(register_play_sound_function)
    SM_API(global_init) SM_API(global_terminate)
    SM_API(static_surfaces_load)
    SM_API(mario_create) SM_API(mario_delete) SM_API(mario_tick)
    SM_API(set_mario_position) SM_API(set_mario_velocity)
    SM_API(set_mario_action) SM_API(set_mario_forward_velocity)
    SM_API(audio_init) SM_API(audio_tick)
    SM_API(set_mario_water_level) SM_API(mario_attack)
    SM_API(surface_find_floor_height)
    SM_API(mario_take_damage) SM_API(mario_heal) SM_API(mario_kill)
    SM_API(set_mario_health) SM_API(set_mario_faceangle)
    SM_API(set_mario_invincibility)
#undef SM_API
    // Our extension to the pinned library: render an original ROM pose without physics.
    using Pose=void(__cdecl*)(int32_t,float,float,float,float,int32_t,int16_t,SM64MarioState*,SM64MarioGeometryBuffers*);
    Pose pose{};
    int(__cdecl* deathWarp)(){};
    bool Load();
};
extern Sm64 sm;
struct Geometry {
    std::array<float, SM64_GEO_MAX_TRIANGLES*9> position{}, normal{}, color{};
    std::array<float, SM64_GEO_MAX_TRIANGLES*6> uv{};
    SM64MarioGeometryBuffers buffers{position.data(),normal.data(),color.data(),uv.data(),0};
};
extern Geometry geometry;
extern SM64MarioState state;
extern Geometry previousGeometry;
extern SM64MarioState previousState;
extern float renderAlpha;
extern CVector origin;
extern std::vector<uint8_t> texture;
extern int32_t mario;
CVector ToSm(const CVector& p);
CVector ToGta(const float* p);
bool RefreshCollision(const CVector& center, bool force);
void ResetCollision();
bool RefreshVehicleCollision(const CVector& center);
void Render();
void DestroyRenderer();
void Process();
void Stop();
void Shutdown();
void PlayerDeleted(CPed* p);
void AudioInit(const std::vector<uint8_t>& rom);
void AudioPump(bool paused);
void AudioClose();
void HudInit(const std::vector<uint8_t>& rom);
void HudRender();
void HudDestroy();
void InteractionTick(CPlayerPed* player,unsigned tick);
void UpdateWater();
void LibDiagnostic(const char* message);
bool DamageInit();
void DamageClose();
bool MarioOwnsPed(CPed* ped);
bool MarioDriving();
void BeforeProcess();
void InstallRuntimeHooks();
unsigned MarioLives();
unsigned MarioCoins();
unsigned MarioStars();
float DeathTransition();
}
