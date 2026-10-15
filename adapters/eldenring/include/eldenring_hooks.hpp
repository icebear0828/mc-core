#pragma once

#include <cstdint>
#include <string>
#include "eldenring_live.hpp"
#include "eldenring_los.hpp"

namespace eldenring::hooks {

// Initializes MinHook if not already initialized
bool EnsureMinHook();

// Helper file utilities
bool FileExists(const std::string& path);
bool ReadFileAll(const std::string& path, std::vector<uint8_t>& out);

// Player state helpers
uintptr_t PlayerChrPtr();
bool PlayerDead();
bool PlayerAirborne();
uintptr_t DataModuleOfChr(uintptr_t chr);
bool SetChrHp(uintptr_t chr, int32_t new_hp);
bool KillChr(uintptr_t chr);
bool GiveHp(uintptr_t data_module, int amount);
bool CallVfunc7(uintptr_t damage_module, uintptr_t attacker, void* ctx);
uint64_t NowTick();

// Static raycast & line-of-sight
live::RayResult CastStaticRay(const float from[3], const float disp[3]);
bool PlayerCanSee(const float player_feet[3], const float victim_feet[3]);

// Bullet / Bow projectile actions
bool QueueBulletFire(float mc_damage);
void RunBulletFire();

// Summon actions
bool QueueSummon(const char* source);
void RunSummonExperiment();

// World / Block interaction actions
bool StepMining(float dt, bool left_held);
bool TryIgniteTnt();
bool TryIgniteCreeper();
bool TryPlaceBlock();
bool TryUseSpawnEgg();
void BlocksTick();
void SendBlockMesh();

// Melee combat actions
void ClickAttack(float charged);
void EnqueueNearestHostile();
void DrainOnce(uintptr_t updating_data_module);

// Subsystem Hook setup functions
void SetupCreativeProbes(const std::string& game_dir, uintptr_t image_base, size_t image_size);
void SetupBulletLog(const std::string& game_dir, uintptr_t image_base, size_t image_size);
void SetupDamage(const std::string& game_dir, uintptr_t image_base, size_t image_size);

// Domain Services Setup
void SetupAllDomainServices(uintptr_t image_base, uint32_t rva_world);

// Master Hook Installer
bool InstallAllHooks(const std::string& game_dir, uintptr_t image_base, size_t image_size);

} // namespace eldenring::hooks
