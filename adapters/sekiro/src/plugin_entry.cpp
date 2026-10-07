#include "sekiro_adapter.hpp"
#include "mc/session.hpp"

#include <memory>

namespace {

// Declaration order = construction order; Session must die before the adapter it references.
std::unique_ptr<mc::adapter::SekiroAdapter> g_adapter;
std::unique_ptr<mc::Session> g_session;

} // namespace

extern "C" {

void SekiroMod_Initialize(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera) {
    g_session.reset();
    g_adapter = std::make_unique<mc::adapter::SekiroAdapter>(player, camera);
    g_session = std::make_unique<mc::Session>(mc::Ports{*g_adapter, *g_adapter, *g_adapter, *g_adapter});

    g_session->loadDefaultHotbar();
}

void SekiroMod_Shutdown() {
    g_session.reset(); // restores the native model and destroys Steve parts
    g_adapter.reset();
}

void SekiroMod_SetSteveMode(bool active) {
    if (g_session) {
        g_session->setActive(active);
    }
}

bool SekiroMod_IsSteveModeActive() {
    return g_session && g_session->isActive();
}

void SekiroMod_UpdatePointers(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera) {
    if (g_adapter) {
        g_adapter->setPlayerCharacter(player);
        g_adapter->setPlayerCamera(camera);
    }
}

// EntityId 0 (none) and 1 (local player) are reserved; returns false for them.
bool SekiroMod_RegisterEntity(uint64_t entity_id, sekiro::native::ChrIns* entity) {
    if (!g_adapter || entity_id <= static_cast<uint64_t>(mc::EntityId::LocalPlayer)) {
        return false;
    }
    return g_adapter->registerEntity(static_cast<mc::EntityId>(entity_id), entity);
}

void SekiroMod_UnregisterEntity(uint64_t entity_id) {
    if (g_adapter && entity_id > static_cast<uint64_t>(mc::EntityId::LocalPlayer)) {
        g_adapter->unregisterEntity(static_cast<mc::EntityId>(entity_id));
    }
}

// `input` may be null (no events this frame). on_ground is filled from the adapter when the
// caller leaves it at the default.
void SekiroMod_Tick(float delta_time, const mc::InputSnapshot* input) {
    if (!g_session || !g_adapter) {
        return;
    }
    mc::InputSnapshot snapshot = input ? *input : mc::InputSnapshot{};
    snapshot.on_ground = g_adapter->isPlayerOnGround();
    snapshot.on_ground_is_estimate = true; // vertical-speed heuristic, see InputSnapshot
    g_session->tick(delta_time, snapshot);
}

mc::Session* SekiroMod_GetSession() {
    return g_session.get();
}

const mc::adapter::SekiroAdapter* SekiroMod_GetAdapter() {
    return g_adapter.get();
}

} // extern "C"
