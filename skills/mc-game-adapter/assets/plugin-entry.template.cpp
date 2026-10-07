#include "{{GAME_NAME_LOWER}}_adapter.hpp"
#include "mc/hud.hpp"
#include "mc/session.hpp"

#include <memory>

namespace {

// The Session holds references to the adapter, so it must be destroyed first.
std::unique_ptr<mc::adapter::{{GAME_NAME}}Adapter> g_adapter;
std::unique_ptr<mc::Session> g_session;

} // namespace

void OnModInitialize() {
    g_adapter = std::make_unique<mc::adapter::{{GAME_NAME}}Adapter>();
    // The fifth port (IHostGameplay*) is optional: pass the adapter once it implements the reverse-engineered
    // host data (see docs/REVERSE_INTERFACES.md); nullptr keeps those actions off.
    g_session = std::make_unique<mc::Session>(mc::Ports{*g_adapter, *g_adapter, *g_adapter, *g_adapter, nullptr});
    g_session->loadDefaultHotbar();
}

void OnModShutdown() {
    g_session.reset(); // restores the native player model and destroys the Steve parts
    g_adapter.reset();
}

void OnSetSteveMode(bool active) {
    if (g_session) {
        g_session->setActive(active);
    }
}

// Called once per frame by the host loader with this frame's input events. Everything else (hotbar,
// placing/mining, melee, eating, bow, elytra, animation, projectiles) is driven by Session::tick.
void OnGameTick(float delta_time, const mc::InputSnapshot& input) {
    if (!g_session || !g_adapter) {
        return;
    }
    mc::InputSnapshot snapshot = input;
    snapshot.on_ground = g_adapter->isPlayerOnGround();   // implement on the adapter
    snapshot.on_ground_is_estimate = true;                 // set false once the host exposes a real grounded flag
    g_session->tick(delta_time, snapshot);
}
