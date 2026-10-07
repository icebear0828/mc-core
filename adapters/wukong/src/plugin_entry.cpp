#include "wukong_adapter.hpp"
#include "mc/session.hpp"

#include <memory>

namespace {

// Declaration order = construction order; Session must die before the adapter it references.
std::unique_ptr<mc::adapter::WukongAdapter> g_adapter;
std::unique_ptr<mc::Session> g_session;

} // namespace

extern "C" {

void WukongMod_Initialize(b1::native::APlayerController* controller) {
    g_session.reset();
    g_adapter = std::make_unique<mc::adapter::WukongAdapter>(controller);
    g_session = std::make_unique<mc::Session>(mc::Ports{*g_adapter, *g_adapter, *g_adapter, *g_adapter});
    g_session->loadDefaultHotbar();
}

void WukongMod_Shutdown() {
    g_session.reset(); // restores the native mesh and destroys Steve parts
    g_adapter.reset();
}

void WukongMod_SetSteveMode(bool active) {
    if (g_session) {
        g_session->setActive(active);
    }
}

bool WukongMod_IsSteveModeActive() {
    return g_session && g_session->isActive();
}

// EntityId 0 (none) and 1 (local player) are reserved; returns false for them.
bool WukongMod_RegisterEntity(uint64_t entity_id, b1::native::ABGUCharacter* entity) {
    if (!g_adapter || entity_id <= static_cast<uint64_t>(mc::EntityId::LocalPlayer)) {
        return false;
    }
    return g_adapter->registerEntity(static_cast<mc::EntityId>(entity_id), entity);
}

void WukongMod_UnregisterEntity(uint64_t entity_id) {
    if (g_adapter && entity_id > static_cast<uint64_t>(mc::EntityId::LocalPlayer)) {
        g_adapter->unregisterEntity(static_cast<mc::EntityId>(entity_id));
    }
}

// `input` may be null (no events this frame). on_ground is always filled from the adapter.
void WukongMod_Tick(float delta_time, const mc::InputSnapshot* input) {
    if (!g_session || !g_adapter) {
        return;
    }
    mc::InputSnapshot snapshot = input ? *input : mc::InputSnapshot{};
    snapshot.on_ground = g_adapter->isPlayerOnGround();
    snapshot.on_ground_is_estimate = true; // vertical-speed heuristic, see InputSnapshot
    g_session->tick(delta_time, snapshot);
}

mc::Session* WukongMod_GetSession() {
    return g_session.get();
}

} // extern "C"
