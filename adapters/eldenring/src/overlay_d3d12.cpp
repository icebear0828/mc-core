#include "overlay_internal.hpp"

namespace erov {
using namespace detail;

void SetSteveConfig(const SteveConfig& cfg) { g_steve_cfg = cfg; }

void SetOwConfig(const OwConfig& cfg) {
#if ENABLE_OW_VIEWMODEL
    g_ow_cfg = cfg;
#else
    (void)cfg;
#endif
}

float OwClipDuration(const char* name) {
#if ENABLE_OW_VIEWMODEL
    return g_ow.clipDuration(name);
#else
    (void)name;
    return 0.f;
#endif
}

void RequestFrameTrace(int frames) { g_trace_left.store(frames); }

void SetMobModel(const mc::model::EntityModel& model) {
    SetMobModel("", model);
}

void SetMobModel(std::string_view name, const mc::model::EntityModel& model) {
    g_mob_model = model;
    g_mob_model_given = !model.bones.empty();
    if (!name.empty() && !model.bones.empty()) {
        g_mob_models[std::string(name)] = model;
        g_mob_registry.registerModel(name, &g_mob_models[std::string(name)]);
    }
}

void RegisterModel(std::string_view name, const mc::model::EntityModel& model) {
    if (name.empty() || model.bones.empty()) return;
    g_mob_models[std::string(name)] = model;
    g_mob_registry.registerModel(name, &g_mob_models[std::string(name)]);
}

void SetMobSkin(const uint8_t* rgba, unsigned width, unsigned height) {
    SetMobSkin("", rgba, width, height);
}

void SetMobSkin(std::string_view name, const uint8_t* rgba, unsigned width, unsigned height) {
    g_mob_rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    g_mob_w = width;
    g_mob_h = height;
    if (!name.empty()) {
        MobSkinEntry entry;
        entry.rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
        entry.w = width;
        entry.h = height;
        g_mob_skins[std::string(name)] = std::move(entry);
    }
}

void SetMobs(const std::vector<eldenring::mobs::MobSnapshot>& mobs) {
    std::lock_guard<std::mutex> g(g_mob_mutex);
    g_mob_snapshot = mobs;
}

void SetArrows(const std::vector<eldenring::summon::ArrowDraw>& arrows) {
    std::lock_guard<std::mutex> g(g_arrow_mutex);
    g_arrow_snapshot = arrows;
}

void SpawnFx(FxKind kind, const float world_pos[3], int count) {
    const float no_dir[3] = {0.f, 0.f, 0.f};
    SpawnFx(kind, world_pos, no_dir, count);
}

void SpawnFx(FxKind kind, const float world_pos[3], const float dir[3], int count) {
    FxEvent e{kind, {world_pos[0], world_pos[1], world_pos[2]}, count, {dir[0], dir[1], dir[2]}};
    std::lock_guard<std::mutex> g(g_fx_mutex);
    if (g_fx_events.size() < 64) g_fx_events.push_back(e);
}

void SpawnTracer(const float from[3], const float to[3], float life_s) {
    TracerEvent t{{from[0], from[1], from[2]}, {to[0], to[1], to[2]}, GetTickCount64(), life_s > 0.01f ? life_s : 0.08f};
    std::lock_guard<std::mutex> g(g_tracer_mutex);
    if (g_tracer_events.size() < 64) g_tracer_events.push_back(t);
}

#if ENABLE_CS2_VIEWMODEL
void SpawnDecal(const float pos[3], const float normal[3]) {
    const uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> g(g_decal_mutex);
    g_decal_ring.addOrRefresh(pos, normal, now, eldenring::cs::kImpactDecalLifeS);
}
#endif

void SetHudAtlas(const uint8_t* rgba, unsigned width, unsigned height) {
    g_atlas_rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    g_atlas_w = width;
    g_atlas_h = height;
}

void SetSteveSkin(const uint8_t* rgba, unsigned width, unsigned height) {
    g_skin_rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    g_skin_w = width;
    g_skin_h = height;
}

void RequestScreenshot(const char* utf8_path) {
    const std::string requested = utf8_path != nullptr ? utf8_path : "";
    // F4 passes a bare filename; the game's working directory is not the game
    // folder, so WIC cannot create the file. Resolve it next to the game exe.
    std::string exe_dir;
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0) {
        exe_dir = std::filesystem::path(exe).parent_path().string();
    }
    const std::string path = resolve_screenshot_path(requested, exe_dir);
    {
        std::lock_guard<std::mutex> g(g_shot_mutex);
        g_shot_path = path;
    }
    g_shot_requested.store(true);
}

float CsClipDuration(const char* name) {
#if ENABLE_CS2_VIEWMODEL
    return g_cs.clipDuration(name);
#else
    (void)name;
    return 0.f;
#endif
}

void SetBlockMesh(const mc::rig::RigMesh& mesh) {
    std::lock_guard<std::mutex> g(g_block_mutex);
    g_block_mesh = mesh;
    g_blocks_dirty.store(true);
}

void SetTerrainCrackMesh(const mc::rig::RigMesh& mesh) {
    std::lock_guard<std::mutex> g(g_crack_mutex);
    g_crack_mesh = mesh;
    g_crack_dirty.store(true);
}

void ScreenSize(float& width, float& height) {
    width = static_cast<float>(g_s.width);
    height = static_cast<float>(g_s.height);
}

void CycleDepthCandidate() {
    std::lock_guard<std::mutex> g(g_depth_mutex);
    if (g_depth_candidates.empty()) return;
    size_t cur = 0;
    for (size_t i = 0; i < g_depth_candidates.size(); ++i) {
        if (g_depth_candidates[i] == g_depth_res) cur = i;
    }
    ID3D12Resource* next = g_depth_candidates[(cur + 1) % g_depth_candidates.size()];
    next->AddRef();
    if (g_depth_res) g_depth_res->Release();
    g_depth_res = next;
    const D3D12_RESOURCE_DESC rd = next->GetDesc();
    g_depth_w = static_cast<UINT>(rd.Width);
    g_depth_h = rd.Height;
    g_depth_dirty.store(true);
    if (g_log) g_log("overlay: depth candidate %zu of %zu bound: %p %llux%u", (cur + 1) % g_depth_candidates.size() + 1,
                     g_depth_candidates.size(), static_cast<void*>(next), static_cast<unsigned long long>(rd.Width), rd.Height);
}

bool Install(HudProvider provider, LogFn log) { return detail::InstallOverlayHooks(provider, log); }

} // namespace erov
