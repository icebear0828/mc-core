# mc-core 性能审计：第三方模型提示词

> 用途：把 §1 整段复制给网页上的第三方模型（它对本项目一无所知）。
> §2 是我本地审计的结论，**不要**一起粘贴，避免锚定它的判断。
> 若模型上下文有限：保留 §1.1–§1.4、§1.6，代码摘录按需删减。

---

## §1 提示词（从这里开始复制）

你是一名资深 C++ / 游戏逆向 / 实时渲染性能工程师。下面是一个你从未见过的项目。请只基于我提供的描述和代码，做一次**只读性能审计**：找出可以在**不改变行为、不改变画面、不削弱安全校验**的前提下优化性能的地方。

你不能运行、调试或修改这个项目。请从代码结构推断热点，并明确标注哪些结论是「确定的」、哪些是「需要实测确认的」。

### 1.1 项目是什么

mc-core 是一个 C++20 静态库 + 每游戏适配器的中间件，把 Minecraft 的玩法（体素方块、挖掘、12 部件 Steve 模型、弹道、近战/横扫、HUD、末影珍珠、鞘翅、TNT、弓箭等）叠加到采用私有引擎的 3A 游戏里。目标游戏：Elden Ring（Dantelion2）、Sekiro、Black Myth Wukong（UE5）。单机/离线使用。

- **核心库** `include/mc/` + `src/`：纯数学与状态机，不依赖任何引擎。`Session::tick()` 每帧驱动动画、体素、弹道、战斗、HUD。
- **适配器** `adapters/<game>/`：
  - `eldenring`（主目标，代码量最大）：D3D12 overlay。用 MinHook 挂 `IDXGISwapChain::Present`、`ID3D12CommandQueue::ExecuteCommandLists`、`ID3D12Device::CreateDepthStencilView`；用 ImGui 画 HUD，并用自绘 D3D12 网格画 Steve 部件、方块、怪物、影子、箭矢。同时用 MinHook 挂游戏函数（相机步进、伤害结算、子弹生成、命中特效等）。
  - `sekiro`：D3D11 overlay。
  - `wukong`：UE5，通过 `UProceduralMeshComponent` 挂 12 个部件。
- **内存读取**：所有游戏内存访问都经过 `IMemoryReader::read()`（虚函数）→ `SafeCopy()`（`memcpy` + SEH `__try/__except`）。没有批量读接口，每次读都是一次虚调用 + SEH 帧。
- **线程模型**：游戏线程（相机步进钩子、伤害钩子）、Present/渲染线程（HUD + 网格）、按键线程（约 15 ms 轮询）、音频工作线程、存档工作线程。
- **构建**：MSVC `/W4 /WX`，C++20，无第三方依赖（除 ImGui、MinHook、Windows SDK）。三个独立开关：`ENABLE_MC_MODE` / `ENABLE_CS2_VIEWMODEL` / `ENABLE_OW_VIEWMODEL`。

### 1.2 性能约束

- overlay 跑在游戏自己的 `Present` 里：overlay 每帧多花 1 ms，游戏就少 1 ms。
- 目标 60+ FPS；不能改变游戏行为、画面结果。
- 现有 RTTI 校验是为了防止写坏 vtable 导致崩溃（曾发生 `0x200000001` 崩溃），**不能直接删除**；可以优化其实现。
- 所有建议必须能在现有代码结构里落地，不引入新依赖，不建议“重写架构 / 换语言 / 把整个渲染多线程化”这类不可执行方案。
- 代码风格：热路径避免每帧堆分配、避免不必要的字符串、避免重复全量扫描。

### 1.3 热路径地图（每帧 / 每 tick）

Elden Ring 每帧的调用链（`Present` 线程）：

```
PresentDetour (adapters/eldenring/src/overlay_hook.cpp:711)
 └─ RenderFrame (src/overlay_scene.cpp:58)
     ├─ g_provider(hud, steve)          // = HudProvider, src/ui/hud_overlay.cpp:17
     │   ├─ readSingleton(world)        // :29
     │   ├─ readSingleton(menu/loading/fade) // :40-42
     │   ├─ UpdateNativeModel(...)      // :68  → 27 次 RTTI 类检查
     │   └─ findNearestGrace(...)       // :107 → 全 chr-set 扫描
     ├─ ImGui NewFrame / DrawHud / Render
     ├─ g_steve.drawBlocks / drawTerrainCrack / draw / drawShadows / drawArrows / drawPearls
     └─ g_cs.tick / g_ow.tick / g_hand.tick
```

Elden Ring 每帧的调用链（游戏线程，相机步进钩子）：

```
InvokeCameraStepDetour (src/hooks/game_hooks.cpp:198)
 ├─ on_camera_step_pre (src/hooks/hook_setup.cpp:55)
 │   ├─ PhysWriteTestStep / PhysProbeStep
 │   ├─ BlocksCollisionStep
 │   ├─ EnemiesBlockCollisionStep   (src/world/tnt_runtime.cpp:9)  → enumerateEnemies(512)
 │   ├─ VirtualSummonsStep          (src/world/summon_runtime.cpp:136) → enumerateEnemies(512)
 │   └─ ... 其余 step
 └─ on_camera_step_post
```

另有一个每帧回调 `on_render_cam_copy`（`hook_setup.cpp:188`），它会：
- 调 `HideSummonModels()`（`src/world/summon_runtime.cpp:45`）→ `enumerateEnemies(4000, include_dead=true)`
- 再调一次 `UpdateNativeModel()`（与 HudProvider 里重复）

核心库热路径：

```
Session::tick (src/session.cpp:124)
 ├─ SteveAnimator::update (src/animator.cpp:68)   // 12 部件三角函数
 ├─ VoxelWorld::mineBlock / placeBlock (src/voxel_world.cpp)
 └─ BallisticsEngine::update (src/ballistics.cpp:38)
```

Sekiro 每帧：`DetourPresent`（`adapters/sekiro/src/dinput8_loader.cpp:1194`）→ `SekiroMod_Tick` → `Session::tick`，另有 `SyncEnemies` 每帧 `enumerateEnemies`。

### 1.4 关键代码摘录

#### A. RTTI 类检查：逐字节读类型名（`adapters/eldenring/include/eldenring_rtti.hpp:22-56`）

```cpp
// The mangled type name behind a vtable address, or nullopt for anything that does not look like an x64 COL.
inline std::optional<std::string> readTypeNameOfVtable(const IMemoryReader& reader, uintptr_t image_base, uint64_t vtable, size_t image_size = 0x10000000) {
    if (vtable < sizeof(uint64_t)) return std::nullopt;
    if (image_base != 0 && (vtable < image_base || vtable >= image_base + image_size)) return std::nullopt;
    uint64_t col = 0;
    if (!reader.read(static_cast<uintptr_t>(vtable) - sizeof(uint64_t), &col, sizeof(col)) || col == 0) return std::nullopt;
    if (image_base != 0 && (col < image_base || col >= image_base + image_size)) return std::nullopt;
    uint32_t signature = 0, td_rva = 0;
    if (!reader.read(static_cast<uintptr_t>(col), &signature, sizeof(signature)) || signature != rtti::kColSignatureX64) {
        return std::nullopt;
    }
    if (!reader.read(static_cast<uintptr_t>(col) + rtti::kTypeDescriptorRvaInCol, &td_rva, sizeof(td_rva)) || td_rva == 0 || (image_base != 0 && td_rva >= image_size)) {
        return std::nullopt;
    }
    std::string name;
    const uintptr_t name_at = image_base + td_rva + rtti::kNameInTypeDescriptor;
    for (size_t i = 0; i < rtti::kMaxNameLength; ++i) {   // kMaxNameLength = 160
        char c = 0;
        if (!reader.read(name_at + i, &c, 1)) return std::nullopt;  // 每个字节一次虚调用 + SEH
        if (c == 0) return name.empty() ? std::nullopt : std::optional<std::string>(name);
        name.push_back(c);
    }
    return std::nullopt; // unterminated
}

// True when `object`'s vtable is the measured one OR its RTTI name contains `name_token`.
inline bool objectIsClass(const IMemoryReader& reader, uintptr_t image_base, uintptr_t object, uint32_t measured_vtable_rva,
                          const char* name_token) {
    uint64_t vtable = 0;
    if (object == 0 || !reader.read(object, &vtable, sizeof(vtable))) return false;
    if (measured_vtable_rva != 0 && vtable == image_base + measured_vtable_rva) return true;
    const auto name = readTypeNameOfVtable(reader, image_base, vtable);
    return name && name->find(name_token) != std::string::npos;
}
```

#### B. 单例解析每次调用都走慢路径（`adapters/eldenring/include/eldenring_singletons.hpp:64-70`）

```cpp
inline uintptr_t readSingleton(const IMemoryReader& reader, uintptr_t image_base, uint32_t global_rva, const SingletonSig& sig) {
    if (global_rva == 0) return 0;
    uint64_t instance = 0;
    if (!reader.read(image_base + global_rva, &instance, sizeof(instance)) || instance == 0) return 0;
    const auto object = static_cast<uintptr_t>(instance);
    return objectIsClass(reader, image_base, object, 0, sig.rtti_token) ? object : 0;  // measured_rva = 0 → 永远慢路径
}
```

`PlayerChrPtr()` 内部就是 `readSingleton(...)`，而它被 29 处调用，其中多处在每帧路径上。

#### C. 全量角色枚举（`adapters/eldenring/include/eldenring_world.hpp:179-275`）

```cpp
inline bool isCharacterClass(const IMemoryReader& reader, uintptr_t image_base, uintptr_t chr) {
    return objectIsClass(reader, image_base, chr, layout::kEnemyInsVtableRva, ".?AVEnemyIns@CS@@") ||
           objectIsClass(reader, image_base, chr, layout::kPlayerInsVtableRva, ".?AVPlayerIns@CS@@");
}

inline bool enumerateEnemies(const IMemoryReader& reader, uintptr_t image_base, std::vector<EnemyInfo>& out, size_t max, bool include_dead = false) {
    out.clear();
    uint64_t world = 0, player = 0;
    if (!reader.read(image_base + layout::kWorldChrManGlobalRva, &world, sizeof(world)) || world == 0) return false;
    if (!reader.read(static_cast<uintptr_t>(world) + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0) {
        return false;
    }
    const auto player_chr = static_cast<uintptr_t>(player);
    float p0[3];
    if (!detail::readPhysicsPosition(reader, image_base, player_chr, p0)) return false;
    uint8_t player_team = 0;
    if (!reader.read(player_chr + layout::kTeamTypeInChrIns, &player_team, sizeof(player_team))) return false;

    std::vector<uintptr_t> sets_to_scan;
    sets_to_scan.reserve(layout::kMaxActiveChrSets + layout::kChrSetPointerCount);  // 128 + 196

    // 1. Scan dynamically active ChrSets
    uint32_t active_count = 0;
    if (reader.read(static_cast<uintptr_t>(world) + layout::kActiveChrSetCountInWorldChrMan, &active_count, sizeof(active_count)) &&
        active_count > 0 && active_count <= layout::kMaxActiveChrSets) {
        for (uint32_t a = 0; a < active_count; ++a) {
            uint64_t set = 0;
            if (reader.read(static_cast<uintptr_t>(world) + layout::kActiveChrSetArrayInWorldChrMan + a * layout::kActiveChrSetEntryStride,
                            &set, sizeof(set)) && set != 0) {
                if (std::find(sets_to_scan.begin(), sets_to_scan.end(), static_cast<uintptr_t>(set)) == sets_to_scan.end()) {
                    sets_to_scan.push_back(static_cast<uintptr_t>(set));
                }
            }
        }
    }

    // 2. Scan sparse ChrSet pointer array
    for (uint32_t s = 0; s < layout::kChrSetPointerCount; ++s) {
        uint64_t set = 0;
        if (reader.read(static_cast<uintptr_t>(world) + layout::kChrSetPointerArrayInWorldChrMan + s * 8, &set, sizeof(set)) &&
            set != 0) {
            if (std::find(sets_to_scan.begin(), sets_to_scan.end(), static_cast<uintptr_t>(set)) == sets_to_scan.end()) {
                sets_to_scan.push_back(static_cast<uintptr_t>(set));
            }
        }
    }

    std::vector<EnemyInfo> found;
    for (uintptr_t set : sets_to_scan) {
        if (found.size() >= max) break;
        uint32_t capacity = 0;
        uint64_t entries = 0;
        if (!reader.read(set + layout::kChrSetCapacity, &capacity, sizeof(capacity))) continue;
        if (!reader.read(set + layout::kChrSetEntries, &entries, sizeof(entries)) || entries == 0) continue;
        if (capacity == 0 || capacity > layout::kMaxChrSetCapacity) continue;  // 上限 4096
        for (uint32_t i = 0; i < capacity && found.size() < max; ++i) {
            uint64_t pair[2];
            if (!reader.read(static_cast<uintptr_t>(entries) + static_cast<uintptr_t>(i) * 16, pair, sizeof(pair))) break;
            const auto chr = static_cast<uintptr_t>(pair[0]);
            if (chr == 0 || chr == player_chr) continue;
            if (std::any_of(found.begin(), found.end(), [&](const EnemyInfo& e) { return e.chr == chr; })) continue;  // O(n²)
            if (!detail::isCharacterClass(reader, image_base, chr)) continue;   // 非 Enemy/Player 时走两次慢 RTTI
            EnemyInfo e;
            e.chr = chr;
            e.handle = pair[1];
            if (!reader.read(chr + layout::kNpcIdInChrIns, &e.npc_id, sizeof(e.npc_id))) continue;
            if (e.npc_id == layout::kGraceNpcId) continue;
            if (!reader.read(chr + layout::kTeamTypeInChrIns, &e.team, sizeof(e.team))) continue;
            Vitals v;
            if (!readVitals(reader, image_base, chr, v) || (v.hp <= 0 && !include_dead)) continue;
            e.hp = v.hp;
            e.max_hp = v.max_hp;
            float pos[3];
            if (!detail::readPhysicsPosition(reader, image_base, chr, pos)) continue;
            e.rel_x = pos[0] - p0[0];
            e.rel_y = pos[1] - p0[1];
            e.rel_z = pos[2] - p0[2];
            e.hostile = readTeamRelation(reader, image_base, player_team, e.team) == TeamRelation::Hostile;
            found.push_back(e);
        }
    }

    float p1[3];
    if (!detail::readPhysicsPosition(reader, image_base, player_chr, p1)) return false;
    const float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > layout::kMaxPlayerMoveWithinTick) return false;
    out = std::move(found);
    return true;
}
```

#### D. 找最近的赐福：同样全量扫描（`adapters/eldenring/include/eldenring_world.hpp:278-334`）

```cpp
inline bool findNearestGrace(const IMemoryReader& reader, uintptr_t image_base, uintptr_t world,
                             const float player_pos[3], float max_distance, float* out_dist = nullptr) {
    if (world == 0 || player_pos == nullptr) return false;
    uint32_t active_count = 0;
    std::vector<uintptr_t> sets_to_scan;
    sets_to_scan.reserve(layout::kMaxActiveChrSets + layout::kChrSetPointerCount);

    if (reader.read(world + layout::kActiveChrSetCountInWorldChrMan, &active_count, sizeof(active_count)) &&
        active_count > 0 && active_count <= layout::kMaxActiveChrSets) {
        for (uint32_t a = 0; a < active_count; ++a) {
            uint64_t set = 0;
            if (reader.read(world + layout::kActiveChrSetArrayInWorldChrMan + a * layout::kActiveChrSetEntryStride,
                            &set, sizeof(set)) && set != 0) {
                sets_to_scan.push_back(static_cast<uintptr_t>(set));
            }
        }
    }
    if (sets_to_scan.empty()) {
        for (uint32_t s = 0; s < layout::kChrSetPointerCount; ++s) {
            uint64_t set = 0;
            if (reader.read(world + layout::kChrSetPointerArrayInWorldChrMan + s * 8, &set, sizeof(set)) && set != 0) {
                sets_to_scan.push_back(static_cast<uintptr_t>(set));
            }
        }
    }

    const float max_dist_sq = max_distance * max_distance;
    float closest_sq = max_dist_sq + 1.0f;
    for (uintptr_t set : sets_to_scan) {
        uint32_t capacity = 0;
        uint64_t entries = 0;
        if (!reader.read(set + layout::kChrSetCapacity, &capacity, sizeof(capacity)) || capacity == 0 || capacity > layout::kMaxChrSetCapacity) continue;
        if (!reader.read(set + layout::kChrSetEntries, &entries, sizeof(entries)) || entries == 0) continue;
        for (uint32_t i = 0; i < capacity; ++i) {
            uint64_t pair[2];
            if (!reader.read(static_cast<uintptr_t>(entries) + static_cast<uintptr_t>(i) * 16, pair, sizeof(pair))) break;
            const auto chr = static_cast<uintptr_t>(pair[0]);
            if (chr == 0) continue;
            int32_t npc_id = 0;
            if (!reader.read(chr + layout::kNpcIdInChrIns, &npc_id, sizeof(npc_id)) || npc_id != layout::kGraceNpcId) continue;
            float pos[3];
            if (!detail::readPhysicsPosition(reader, image_base, chr, pos)) continue;
            const float dx = pos[0] - player_pos[0], dy = pos[1] - player_pos[1], dz = pos[2] - player_pos[2];
            const float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq <= max_dist_sq && dist_sq < closest_sq) closest_sq = dist_sq;
        }
    }
    if (closest_sq <= max_dist_sq) {
        if (out_dist) *out_dist = std::sqrt(closest_sq);
        return true;
    }
    return false;
}
```

#### E. 每帧的 HUD provider（`adapters/eldenring/src/ui/hud_overlay.cpp:17-150`，节选）

```cpp
bool HudProvider(erov::HudState& out, erov::SteveState& steve) {
    {
        std::vector<eldenring::mobs::MobSnapshot> mobs;
        std::vector<eldenring::summon::ArrowDraw> arrows;
        if (g_summon_hide.load(std::memory_order_relaxed) && g_mc_mode.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> g(g_summon_mutex);
            mobs = g_summon_list;      // 每帧复制
            arrows = g_summon_arrows;
        }
        erov::SetMobs(mobs);
        erov::SetArrows(arrows);
    }
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);  // 慢 RTTI
    if (world == 0) return false;
    uint64_t player = 0;
    Vitals v;
    if (!SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !readVitals(g_reader, g_img.base, static_cast<uintptr_t>(player), v)) {
        return false;
    }
    MenuState ms;
    LoadingState ls;
    float fade = 0.f;
    const bool have_state = readMenuState(g_reader, readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan), ms) &&
                            readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls) &&
                            readFadeAlpha(g_reader, readSingleton(g_reader, g_img.base, g_rva_fade, sigs::kCSFade), fade);
    // ...
    if (g_hide_native.load() || out.mc_mode || out.cs_mode || ow_fp) {
        const bool want_hidden = (out.mc_mode || out.cs_mode || ow_fp) && have_state && !ls.screen_loading;
        g_native_hide_wanted.store(want_hidden);
        UpdateNativeModel(static_cast<uintptr_t>(player), want_hidden);   // 27 次慢 RTTI
        // ...
    }
    // ...
    if (g_steve_enabled.load() && readCamera(...) && detail::readPhysicsPosition(...) && detail::readPhysicsOrientation(...)) {
        // ...
        float grace_dist = 999.f;
        const bool near_grace = findNearestGrace(g_reader, g_img.base, world, feet, 2.8f, &grace_dist);  // 全量扫描
        steve.sleeping = near_grace && (ms.menu_focused || fade >= 0.02f);
    }
    // ...
    return true;
}
```

#### F. 隐藏原版模型：每帧对 27 个部件做慢 RTTI（`adapters/eldenring/include/eldenring_model.hpp:84-109` + `src/ui/hud_bridge.cpp:163-242`）

```cpp
inline std::vector<uintptr_t> collectAsmModelDispFlagAddresses(const IMemoryReader& reader, uintptr_t image_base,
                                                               uintptr_t player_chr) {
    std::vector<uintptr_t> out;
    uint64_t model = 0;
    if (player_chr == 0 || !reader.read(player_chr + layout::kAsmModelInPlayerIns, &model, sizeof(model)) || model == 0) return out;
    const auto m = static_cast<uintptr_t>(model);
    if (!objectIsClass(reader, image_base, m, layout::kAsmModelVtableRva, layout::kAsmModelRtti)) return out;

    auto add_disp = [&](uintptr_t disp_addr) {
        uint64_t disp = 0;
        if (!reader.read(disp_addr, &disp, sizeof(disp)) || disp == 0) return;
        const auto d = static_cast<uintptr_t>(disp);
        if (!objectIsClass(reader, image_base, d, 0, "CSModelDispEntity")) return;  // measured_rva = 0 → 慢路径
        const uintptr_t at = d + layout::kDispFlags1;
        if (std::find(out.begin(), out.end(), at) == out.end()) out.push_back(at);
    };

    for (unsigned i = 0; i < layout::kAsmPartSlots; ++i) {   // kAsmPartSlots = 27
        uint64_t part = 0;
        if (!reader.read(m + layout::kAsmPartPointers + i * sizeof(uint64_t), &part, sizeof(part)) || part == 0) continue;
        add_disp(static_cast<uintptr_t>(part) + layout::kPartDispEntity);
    }
    add_disp(m + layout::kRootDispEntity);
    add_disp(m + layout::kAsmShadowDispEntity);
    return out;
}

void UpdateNativeModel(uintptr_t player, bool hide, const live::IMemoryReader& reader, uintptr_t base,
                       RuntimeContext& ctx, std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags,
                       std::mutex& native_mutex) {
    std::lock_guard<std::mutex> lock(native_mutex);
    if (ctx.ui().slots_changed.exchange(false)) hidden_flags.clear();
    if (!hide) { restoreAll(hidden_flags); return; }
    const std::vector<uintptr_t> addrs = live::collectAsmModelDispFlagAddresses(reader, base, player);
    // ... 逐个读/写 disp_flags，写入 hidden_flags 缓存
}
```

#### G. 相机步进：每帧 12+ 个 step（`adapters/eldenring/src/hooks/hook_setup.cpp:55-111`，节选）

```cpp
cb.on_camera_step_pre = [](float dt) {
    PhysWriteTestStep();
    PhysProbeStep(dt);
    if (g_bullet_fire_pending.load(std::memory_order_relaxed) &&
        GetCurrentThreadId() == g_game_tid.load(std::memory_order_relaxed) &&
        g_bullet_fire_pending.exchange(false)) {
        RunBulletFire();
    }
    FlightStep();
    SneakEyeHeightStep(dt);
    BlocksCollisionStep();
    McJumpStep();
    ElytraStep(dt);
    EnderPearlStep(dt);
    RiptideStep(dt);
    GroundSprintStep(dt);
    OwRocketPunchStep(dt);
    ShadowGroundStep();
    RestoreCamKeep();
    {
        const uintptr_t p = PlayerChrPtr();      // readSingleton → 慢 RTTI
        float p_feet[3] = {};
        if (p != 0 && detail::readPhysicsPosition(g_reader, g_img.base, p, p_feet)) {
            EnemiesBlockCollisionStep(p_feet);   // enumerateEnemies(512)
        }
    }
    VirtualSummonsStep(dt);                      // 有召唤时再 enumerateEnemies(512)
};

cb.on_render_cam_copy = [](void*, const std::function<void()>& orig) {
    HideSummonModels();                          // enumerateEnemies(4000, include_dead)
    if (g_native_hide_wanted.load(std::memory_order_relaxed)) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        uint64_t player = 0;
        if (world != 0 && SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0) {
            UpdateNativeModel(static_cast<uintptr_t>(player), true);   // 又一次 27 次慢 RTTI
        }
    }
    // ... 第一人称相机写入 ...
    orig();
    // ... 恢复相机 ...
};
```

#### H. 召唤物隐藏：每帧 4000 上限扫描 + std::map + O(n²)（`adapters/eldenring/src/world/summon_runtime.cpp:45-130`，节选）

```cpp
void HideSummonModels() {
    if (!g_summon_hide.load(std::memory_order_relaxed)) return;
    // ...
    std::vector<EnemyInfo> list;
    std::vector<eldenring::mobs::MobSnapshot> seen;
    if (!enumerateEnemies(g_reader, g_img.base, list, 4000, /*include_dead=*/true)) {   // 每帧全量
        std::lock_guard<std::mutex> g(g_summon_mutex);
        g_summon_list.clear();
        return;
    }
    // ... 对每个 team==kTeam 的角色读位置/朝向、隐藏模型 ...
    {
        static std::map<uintptr_t, int> last_hp;   // 每帧查/写
        static unsigned traced = 0;
        for (const auto& snap : seen) { /* last_hp.find / operator[] */ }
        for (auto it = last_hp.begin(); it != last_hp.end();) {
            bool still = false;
            for (const auto& snap : seen) still = still || snap.id == it->first;   // O(n²)
            // ...
        }
    }
    std::lock_guard<std::mutex> g(g_summon_mutex);
    g_summon_list = mc_on ? seen : std::vector<eldenring::mobs::MobSnapshot>{};
    g_summon_arrows = mc_on ? virt_arrows : std::vector<eldenring::summon::ArrowDraw>{};
}
```

#### I. 敌人方块碰撞：先枚举一次，再在内部重复枚举（`src/world/tnt_runtime.cpp:9-40` + `src/world/voxel_world_runtime.cpp:440-449`）

```cpp
void EnemiesBlockCollisionStep(const float player_feet[3]) {
    // ...
    std::vector<EnemyInfo> enemies;
    if (!enumerateEnemies(g_reader, g_img.base, enemies, 512)) {   // 第 1 次
        EntityManager::instance().clearCache();
        return;
    }
    EntityManager::instance().updateCacheFromGameThread(enemies);  // 按值传参 + 每帧 std::sort
    if (!mc_on) return;
    ProcessTntExplosionsOnGameThread(enemies);
    eldenring::world::VoxelWorldRuntime::Instance().stepEnemyBlockCollision(player_feet, g_ctx);
}

void VoxelWorldRuntime::stepEnemyBlockCollision(const float player_feet[3], RuntimeContext& ctx) {
    // ...
    std::vector<EnemyInfo> enemies;
    if (callbacks_.enumerate_enemies && !callbacks_.enumerate_enemies(enemies)) {   // 第 2 次，同一份数据
        return;
    }
    // ...
}
```

#### J. EntityManager 缓存：按值 + 每帧排序（`adapters/eldenring/include/eldenring_entity_manager.hpp:21-35`）

```cpp
void updateCacheFromGameThread(std::vector<EnemyInfo> enemies) {   // 按值 → 拷贝
    std::sort(enemies.begin(), enemies.end(), [](const EnemyInfo& a, const EnemyInfo& b) noexcept {
        const float da = a.rel_x * a.rel_x + a.rel_y * a.rel_y + a.rel_z * a.rel_z;
        const float db = b.rel_x * b.rel_x + b.rel_y * b.rel_y + b.rel_z * b.rel_z;
        return da < db;
    });
    std::lock_guard<std::mutex> g(mutex_);
    cached_enemies_ = std::move(enemies);
}

[[nodiscard]] std::vector<EnemyInfo> getCachedEnemies() const {   // 按值返回
    std::lock_guard<std::mutex> g(mutex_);
    return cached_enemies_;
}
```

#### K. RenderFrame 每帧构造多个 vector（`src/overlay_scene.cpp:371-468`，节选）

```cpp
std::vector<eldenring::mobs::MobSnapshot> snapshot;
{
    std::lock_guard<std::mutex> g(g_mob_mutex);
    snapshot = g_mob_snapshot;      // 每帧复制
}
std::vector<eldenring::summon::ArrowDraw> arrow_snapshot;
{
    std::lock_guard<std::mutex> g(g_arrow_mutex);
    arrow_snapshot = g_arrow_snapshot;   // 每帧复制
}
const auto draws = g_mob_registry.update(dt, snapshot);
// ...
std::vector<eldenring::shadow::Shadow> shadows;
for (const auto& d : draws) shadows.push_back(eldenring::shadow::shadowAt(...));
std::vector<mc::rig::Mat4> arrow_matrices;
arrow_matrices.reserve(arrow_snapshot.size());
for (const auto& a : arrow_snapshot) arrow_matrices.push_back(eldenring::summon::arrowWorldMatrix(a));
```

`DrawProp` 里还有一个每帧分配（`src/overlay_scene.cpp:31`）：

```cpp
std::vector<mc::Quat> extra(model->bones.size());
```

#### L. 渲染 draw（`src/steve_renderer_d3d12.cpp:593-653`，节选）

```cpp
void SteveRenderer::draw(...) {
    if (!ready() || !own_depth_ || !dsv_heap_) return;
    steve::RecordUpload(list, skin_);
    steve::RecordUpload(list, mob_skin_);
    for (auto& s : armor_skin_) steve::RecordUpload(list, s);
    for (auto& [name, s] : mob_skins_map_) steve::RecordUpload(list, s);
    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, !params.keep_depth);
    list->SetGraphicsRootDescriptorTable(1, depth_table);
    list->IASetVertexBuffers(0, 1, &vbv_);
    list->IASetIndexBuffer(&ibv_);
    for (size_t p = 0; p < static_cast<size_t>(mc::StevePart::Count); ++p) {   // 12 次 draw
        setConstants(list, view_proj, parts[p], params, width, height, params.tint);
        list->DrawIndexedInstanced(index_count_[p], 1, first_index_[p], base_vertex_[p], 0);
    }
    for (int layer = 0; layer < 2; ++layer) { /* armor: 每个可见部件再一次 draw */ }
    drawAttachments(list, parts, params);
    // ...
}
```

#### M. 日志：每行 flush / 每次开关文件（`src/core/diagnostics.cpp:16-28`；`adapters/sekiro/src/dinput8_loader.cpp:69-81`）

```cpp
void VLog(const char* fmt, va_list args) {
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, args);
    std::lock_guard<std::mutex> g(g_log_mutex);
    if (!g_log) return;
    const double elapsed = static_cast<double>(GetTickCount64() - g_start_ms) / 1000.0;
    fprintf(g_log, "[%8.3f] %s\n", elapsed, msg);
    fflush(g_log);      // 每行同步刷盘
}

// Sekiro
void Log(const char* fmt, ...) {
    char buf[1024];
    // ...
    FILE* f = nullptr;
    if (fopen_s(&f, "mc_adapter.log", "a") == 0 && f) {   // 每次 open/close
        fprintf(f, "[mc_adapter] %s\n", buf);
        fclose(f);
    }
}
```

#### N. 核心库：弹道删除（`src/ballistics.cpp:38-86`）

```cpp
void BallisticsEngine::update(float dt, const Vec3& player_hand_pos) {
    for (auto it = projectiles_.begin(); it != projectiles_.end();) {
        it->age += dt;
        if (it->age >= it->max_lifetime) { it = projectiles_.erase(it); continue; }   // vector::erase，O(n) 搬移
        // ...
        if (hit.has_hit) {
            // ...
            it = projectiles_.erase(it);   // 同样
            continue;
        }
        ++it;
    }
}
```

### 1.5 初步可疑点（未实测，请逐条验证 / 反驳 / 补充）

下面是我读代码时的怀疑点，**不保证正确**。请判断哪些成立、哪些不成立，并补充我漏掉的。

1. Elden Ring 每帧可能执行 **5 次全量 chr-set 扫描**：
   - `EnemiesBlockCollisionStep` → `enumerateEnemies(512)`
   - `stepEnemyBlockCollision` 内部再次 `enumerateEnemies(512)`
   - `VirtualSummonsStep` → `enumerateEnemies(512)`（有召唤时）
   - `HideSummonModels` → `enumerateEnemies(4000, include_dead)`
   - `HudProvider` → `findNearestGrace` 全量扫描
2. `readTypeNameOfVtable` 逐字节读 + `std::string` 分配，是每帧几十次慢 RTTI 的根源；`readSingleton` 和 `collectAsmModelDispFlagAddresses` 都传 `measured_vtable_rva = 0`，永远走慢路径。
3. `UpdateNativeModel` 每帧被调用两次（`HudProvider` 和 `on_render_cam_copy`），每次都重新做 27 个部件的 RTTI 扫描。
4. `enumerateEnemies` 内部 `std::any_of` 去重是 O(n²)，每次调用还重新分配 `sets_to_scan` / `found`。
5. `EntityManager::updateCacheFromGameThread` 按值传参 + 每帧 `std::sort`；`getCachedEnemies` 按值返回。
6. `HudProvider` / `RenderFrame` 每帧复制 `g_summon_list`、`g_summon_arrows`、`g_mob_snapshot`、`g_arrow_snapshot`。
7. `DrawProp` 每帧每 prop 分配 `std::vector<mc::Quat>`。
8. `findNearestGrace` 每帧执行，即使玩家不在赐福附近；可以低频化或只在菜单/淡出时执行。
9. `viewProjection` 每帧算 3 次；`QueryPerformanceFrequency` 每 camera step 调 2 次。
10. `g_melee_mutex` / `g_blocks_mutex` 在 Present 线程与游戏线程之间高频竞争；`g_blocks_mutex` 在 `stepShadowGround` 的 `block_top` 里加锁。
11. Sekiro 的 `Log()` 每次 `fopen/fwrite/fclose`；ER 的 `VLog` 每行 `fflush`。若热路径上有日志，会直接卡帧。
12. `BallisticsEngine::update` 用 `vector::erase` 逐个删除，弹射物多时 O(n²)。
13. `SteveRenderer::draw` 12 个部件 12 次 draw call + 每层 armor 再一次；`beginPass` 每次重复设置 viewport/scissor/RT/PSO。
14. `on_camera_step_pre` 每帧无条件执行 12+ 个 step，其中多个会读游戏内存；能否按模式/状态提前返回？
15. `HideSummonModels` 每帧构造 `std::map<uintptr_t,int>` 并做 O(n²) 清理。

### 1.6 审计要求与输出格式

请重点检查这些类别：

- **每帧重复工作**：同一份数据被多次扫描、多次排序、多次复制。
- **每帧堆分配 / 字符串**：`std::vector`、`std::string`、`std::map`、`std::function` 在热路径上的构造。
- **内存读取开销**：虚函数 + SEH 的 `read()` 调用次数；能否批量读、缓存、降低频率。
- **RTTI / 类检查**：慢路径的调用次数；能否缓存 vtable→类型名、用测得的 vtable RVA 快速比较。
- **锁竞争**：Present 线程与游戏线程共享的 mutex；持锁时间；能否缩小临界区。
- **GPU 状态冗余**：重复的 PSO/根签名/描述符表/视口设置；能否合批或实例化。
- **算法复杂度**：O(n²) 去重、`vector::erase` 循环、每帧全量排序。
- **调试/日志路径**：是否有日志或探针在热路径上同步写盘。

输出要求（中文，代码标识符保留英文）：

1. 先给一个**总表**：按影响从高到低排序，列为 `# | 严重度(P0/P1/P2) | 位置(file:line) | 问题一句话 | 预计收益 | 修复难度`。
2. 然后逐条展开：
   - **证据**：引用具体代码行，说明为什么慢（调用次数、复杂度、分配次数）。
   - **修复方案**：具体到函数/数据结构/调用点，给出伪代码或关键 diff 片段。
   - **风险**：会不会改变行为、会不会影响 RTTI 安全校验、要不要实测。
   - **验证方法**：在不能运行游戏的前提下，如何用日志/计数器/微基准验证。
3. 最后列出**你认为不值得改**的点（避免过度优化），以及**我漏掉的**问题。
4. 不要泛泛而谈“使用对象池”“开启 LTO”“用 SIMD”这类没有具体落点的建议；每条建议必须指向本项目的具体代码。

---

## §2 我的本地审计结论（不要粘贴）

> 以下是读代码得到的初步结论，未经实机 profiling。按影响排序。

### P0 — 每帧多次全量世界扫描

| # | 位置 | 问题 |
|---|---|---|
| 1 | `adapters/eldenring/src/world/tnt_runtime.cpp:9,23` | `EnemiesBlockCollisionStep` 每帧 `enumerateEnemies(512)` |
| 2 | `adapters/eldenring/src/world/voxel_world_runtime.cpp:446-449` | `stepEnemyBlockCollision` 内部再次 `enumerateEnemies(512)`，调用方已有一份 |
| 3 | `adapters/eldenring/src/world/summon_runtime.cpp:136,181` | `VirtualSummonsStep` 有召唤时每帧 `enumerateEnemies(512)` |
| 4 | `adapters/eldenring/src/world/summon_runtime.cpp:45,55` | `HideSummonModels`（`on_render_cam_copy` 每帧；开启 `mc_er_summonhide` 时）`enumerateEnemies(4000, include_dead)` |
| 5 | `adapters/eldenring/src/ui/hud_overlay.cpp:107` → `include/eldenring_world.hpp:278` | `HudProvider` 每帧 `findNearestGrace` 全量扫描 |

每次扫描遍历最多 128 个 active set + 196 个 sparse set，每个 set 最多 4096 项，每项至少一次 `IMemoryReader::read()`（虚调用 + SEH）。建议：
- 一次扫描结果在帧内复用：`EnemiesBlockCollisionStep` 把 `enemies` 传给 `stepEnemyBlockCollision` / `VirtualSummonsStep` / `HideSummonModels`；
- 或引入 10–20 Hz 的共享快照（游戏线程刷新，Present 线程只读）；
- `findNearestGrace` 降到 0.25–0.5 s 一次，或只在菜单/淡出/靠近赐福时执行。

### P0 — RTTI 慢路径每帧几十次

| # | 位置 | 问题 |
|---|---|---|
| 6 | `include/eldenring_rtti.hpp:23-45` | `readTypeNameOfVtable` 逐字节 `read()`（最多 160 次虚调用）+ `std::string` 分配 |
| 7 | `include/eldenring_singletons.hpp:64-70` | `readSingleton` 传 `measured_vtable_rva=0`，永远走慢路径；`PlayerChrPtr()` 29 处调用 |
| 8 | `include/eldenring_model.hpp:84-109` | 27 个部件逐个 `objectIsClass(..., 0, "CSModelDispEntity")`，全慢路径 |
| 9 | `src/ui/hud_overlay.cpp:68` + `src/hooks/hook_setup.cpp:194` | `UpdateNativeModel` 每帧执行两次，即最多 54 次慢 RTTI |

建议：
- 加一个 `unordered_map<uint64_t /*vtable*/, std::string>` 缓存类型名；一次 `memcpy` 读 160 字节而不是逐字节；
- `readSingleton` 缓存解析出的对象地址，每 1 s 或 `slots_changed` 时重新校验；
- `collectAsmModelDispFlagAddresses` 用测得的 `CSModelDispEntity` vtable RVA 做快速比较（如果逆向已确认），否则至少缓存 vtable→bool；
- `UpdateNativeModel` 只在 `slots_changed` / 游戏把部件重新显示时执行，而不是每帧。

### P1 — 每帧分配与拷贝

| # | 位置 | 问题 |
|---|---|---|
| 10 | `src/overlay_scene.cpp:31` | `DrawProp` 每帧每 prop 分配 `std::vector<mc::Quat>` |
| 11 | `src/ui/hud_overlay.cpp:19-27`；`src/overlay_scene.cpp:373-382` | 每帧复制 summon/mob/arrow 列表 |
| 12 | `src/world/summon_runtime.cpp:101-127` | 每帧 `std::map<uintptr_t,int>` + O(n²) 清理 |
| 13 | `include/eldenring_entity_manager.hpp:21-35` | 按值传参 + 每帧 `std::sort`；`getCachedEnemies` 按值返回 |
| 14 | `src/overlay_scene.cpp:146,187`；`src/overlay_hud.cpp:1171` | `viewProjection` 每帧 3 次 |
| 15 | `src/movement/movement_runtime.cpp:66,79` | `QueryPerformanceFrequency` 每 camera step 2 次 |

### P1 — 锁与日志

| # | 位置 | 问题 |
|---|---|---|
| 16 | `g_melee_mutex` / `g_blocks_mutex` | Present 线程与游戏线程高频竞争；`stepShadowGround` 的 `block_top` 在锁内做网格查找 |
| 17 | `src/core/diagnostics.cpp:19-27` | 每行日志 `fflush`，在 Present 线程上同步写盘 |
| 18 | `adapters/sekiro/src/dinput8_loader.cpp:69-81` | 每次日志 `fopen/fwrite/fclose` |

### P2 — 渲染与核心库

| # | 位置 | 问题 |
|---|---|---|
| 19 | `src/steve_renderer_d3d12.cpp:620-639` | 12 部件 12 次 draw call + armor/attachment 更多；`beginPass` 重复设置状态 |
| 20 | `src/ballistics.cpp:42-85` | `vector::erase` 逐个删除，O(n²) 最坏 |
| 21 | `include/mc/voxel_world.hpp:57` | `unordered_map<GridPos,...>` 每帧碰撞查询；块数上限 2048，可考虑扁平网格 |

### 做得好的地方（不要“优化”掉）

- 方块网格按 `version` 脏标记重建（`block_actions.cpp:25-37`）。
- 纹理上传用 `pending` 标记，只在需要时 `RecordUpload`。
- 方块 GPU buffer 用 4 个 slot 按帧轮转（`steve_renderer_d3d12.cpp:156`）。
- `RecordAtlasUpload` 只在 `g_atlas_pending` 时执行。
- `PhysProbeStep` / 调试探针都有开关和低频检查。

---

## §3 第三方审计复核（2026-11-04）

> 对网页模型返回的审计逐条拿代码核对后的结论。

### 采纳

- **`findNearestGrace` 惰性求值**：确认 `grace_dist` 在 `hud_overlay.cpp` 里没有后续读取，`near_grace` 只用于 `steve.sleeping = near_grace && (ms.menu_focused || fade >= 0.02f)`。把 `need` 放到 `&&` 左边、短路掉扫描，行为等价。
- **帧内共享敌人快照**：确认同一帧内 3–4 次 `enumerateEnemies`；`stepEnemyBlockCollision` 内部确实重复枚举同一份数据，应改为接收调用方列表。
- **批量读 entries**：确认逐槽 16B 读；`capacity * 16` 一次读 + 失败回退逐槽可行（`SafeCopy` 是 `memcpy` + SEH，跨页失败会整块失败，所以回退必须保留）。
- **RTTI 缓存 + 去掉 `std::string`**：可行；缓存键用 vtable 值、把 `image_base` 放进键即可。
- **失败读计数**：应最先做。
- **`readTeamRelation` memo**：确认每实体一次内存读，按 `(from,to)` memo 可行。
- **`UpdateNativeModel` 两次 + 跨线程锁**：确认。
- **`EntityManager` 按值/排序**：确认。
- **日志**：确认。

### 修正

1. **`kEnemyInsVtableRva` 不是 0**：`0x2A47090`；`kPlayerInsVtableRva = 0x2A7FBB0`（`eldenring_world.hpp:36-37`）。所以“所有角色都走慢路径”不成立。但 `isCharacterClass` 对既不是 `EnemyIns` 也不是 `PlayerIns` 的 chr 仍会走两次慢 RTTI，而且两次都重新读对象 vtable。
2. **线程归属**：3–4 次 `enumerateEnemies` 都在**游戏线程**（`on_camera_step_pre` / `on_render_cam_copy`），不在 Present 线程。Present 线程只有 `findNearestGrace` 一次扫描。所以“Present 线程扫描 ChrSet 导致失败读”只适用于 `findNearestGrace`。
3. **RTTI 总量被低估**：`UpdateNativeModel` 54 次（27×2）+ `readSingleton` 约 8–12 次 + `isCharacterClass` 对非 Enemy/Player 的 chr 各 2 次 × 3–4 次扫描。场景里 NPC/道具多时会明显超过 60–90 次/帧。
4. **`HideSummonModels` 默认开启**：`hook_setup.cpp:358` 在缺少 `mc_er_nosummonhide.txt` 时把它设为 `true`，所以 4000 上限扫描默认每帧执行，不是可选路径。

### 它漏掉的

1. **每帧 Havok 射线**：`stepShadowGround` → `CastStaticRay` → `CombatService::castStaticRay` → `invokeNativeRaycast`（`voxel_world_runtime.cpp:113`、`player_services.cpp:55`、`combat_service.cpp:433-449`）。这是引擎调用，可能是单次最贵的每帧操作，应先计时再决定。
2. **`HudProvider` 在模式全关时仍做完整工作**：`RenderFrame` 先调 provider，再判断 `hud.show` / 模式（`overlay_scene.cpp:62`）。可用 `RuntimeContext::mode()` 的原子量先早退。
3. **`findNearestGrace` 可以复用同一份敌人快照**：按 `npc_id == kGraceNpcId` 过滤即可，不必单独再扫一遍。
4. **`isCharacterClass` 两次 `objectIsClass` 各读一次对象 vtable**：合并成一次读取，再用同一份名字判断两个 token。
5. **Present 线程的 `UpdateNativeModel` 写 disp flags 与游戏线程存在数据竞争**：这是正确性问题，不只是性能问题。

### 建议落地顺序

1. 先加计数器（`read()` 总次数/失败次数、慢 RTTI 次数、`enumerateEnemies` 次数、锁持有时长、`CastStaticRay` 耗时），跑 30 秒拿基线。
2. 按实测排序：失败读 → 帧内共享快照 → 批量读 entries → RTTI 缓存 → `findNearestGrace` 惰性求值。
3. 最后再做锁/分配/渲染状态类小项。

---

## §4 计数器已实现（2026-11-04）

新增 header-only 模块 `adapters/eldenring/include/eldenring_perfstats.hpp`，默认关闭。在游戏目录放一个 `mc_er_perfstats.txt` 即开启；启动时 `mc_er.log` 会有一行 `perfstats: on (...)`。

每秒在 `mc_er.log` 输出一行：

```
perfstats/s: read=N fail=N slow_rtti=N enum=N (N slots) grace=N (N slots) unm=N ray=N X.Xus/call | blocks_lock wait=...ms hold=...ms max=...ms | melee_lock wait=...ms hold=...ms max=...ms
```

字段：

| 字段 | 含义 | 埋点 |
|---|---|---|
| `read` / `fail` | `SafeCopy` 调用次数 / 失败次数（失败 = 真实 SEH 异常） | `src/core/memory_scanner.cpp` |
| `slow_rtti` | `readTypeNameOfVtable` 调用次数（慢路径） | `include/eldenring_rtti.hpp` |
| `enum` | `enumerateEnemies` 调用次数 / 扫描槽数 | `include/eldenring_world.hpp` |
| `grace` | `findNearestGrace` 调用次数 / 扫描槽数 | `include/eldenring_world.hpp` |
| `unm` | `UpdateNativeModel` 调用次数 | `src/ui/hud_bridge.cpp` |
| `ray` | `CastStaticRay` 调用次数 / 平均耗时 | `src/core/player_services.cpp` |
| `blocks_lock` | `blocks_mutex` 等待 / 持有 / 最大持有时间 | `voxel_world_runtime.cpp`、`block_actions.cpp` |
| `melee_lock` | `melee_mutex` 等待 / 持有 / 最大持有时间 | `hud_overlay.cpp`、`movement_runtime.cpp` |

关闭时每次埋点只是一次 relaxed atomic load + 分支；开启后读取计数器本身有少量开销，只用于短时间 profiling。

测试：`tests/test_eldenring_perfstats.cpp`（8 个用例）。

---

## §5 实测结果与已落地优化（2026-11-04）

同一场景、MC 模式、60 FPS 左右，`mc_er_perfstats.txt` 开启后的每秒均值：

| 指标 | 基线 | 第四轮 | 变化 |
|---|---|---|---|
| `read` | 218–228 万/秒 | **75.8 万/秒** | **-66%** |
| `enum` | 181/秒（3 次/帧） | **61/秒（1 次/帧）** | -66% |
| `grace` | 60/秒（10.3 万槽） | **0** | -100% |
| `slow_rtti` | 6.8–8.0k/秒 | **240/秒** | -97% |
| `rtti_hit` | — | **~5.9k/秒** | 缓存命中率 ~96% |
| `fail` | 0 | 0 | 排除 SEH 异常假设 |
| `ray` | 240/秒 | 60/秒，~8 µs/次 | ~0.5 ms/秒 |
| `blocks_lock` | max <0.01 ms | max <0.01 ms | 可忽略 |
| `melee_lock` | max ~0.7 ms | max ~1.4 ms | 可忽略 |

已落地：

1. **`findNearestGrace` 惰性求值**（`hud_overlay.cpp`）：只有菜单/淡出时才扫描；CS/OW 模式下不再白扫 10 万槽/秒。
2. **去掉 `stepEnemyBlockCollision` 内部重复枚举**（`voxel_world_runtime.cpp` + `tnt_runtime.cpp`）：新增接收已有列表的重载。
3. **RTTI 名称缓存**（`eldenring_rtti.hpp`）：256 项 thread-local、direct-mapped；只在成功时缓存，失败下次重试；`objectIsClass` 用 `string_view` 命中，不再每次分配 `std::string`。
4. **敌人距离预过滤**（`voxel_world_runtime.cpp`）：用 `EnemyInfo.rel_*` 先按 55 m 过滤，再读物理模块。
5. **帧内共享敌人列表**（`eldenring_enemy_snapshot.hpp` + `tnt_runtime.cpp` + `summon_runtime.cpp`）：相机步枚举一次（含尸体，上限 4000），`HideSummonModels` 在 100 ms 内复用，不再第二次全量扫描。

未做（按实测排序，收益递减）：

- `enumerateEnemies` 内部的逐角色 `readVitals` / `readPhysicsPosition`（约 10–12 次读/角色）。下一步可缓存每个 chr 的 physics/data module 指针。
- `melee_lock` 的偶发 1–6 ms/秒持有；当前场景不构成瓶颈，需在更繁忙的场景复测。
- Havok 射线约 0.5 ms/秒，优先级低。
- 渲染 draw call 合批、`viewProjection` 重复计算等，收益在微秒级。

复测方法：游戏目录保留 `mc_er_perfstats.txt`，启动后看 `mc_er.log` 最后一段的 `perfstats/s:` 行。关闭时删掉该文件即可。

---

## §6 卡顿探针实测（2026-11-04）

§5 只统计了每秒平均读取次数，不能回答“卡不卡”。补了两个探针：

1. **帧时间分布**（`overlay_hook.cpp` 的 `PresentDetour`，Present 到 Present）：每秒统计帧数、`>16.7 / >33 / >50 ms` 的帧数和最差帧。
2. **`native_mutex` 等待/持有**（`hud_bridge.cpp` 的 `UpdateNativeModel` / `RestoreNativeModel`）：这是之前唯一没计时的跨线程锁。

12 秒稳定样本（MC 模式，世界中，60 FPS 上限）：

| 指标 | 数值 |
|---|---|
| 帧数 | 58–60/秒 |
| `>16.7 ms` | 27–34/秒 |
| `>33 ms` | **0** |
| `>50 ms` | **0** |
| 最差帧 | 19.1–31.5 ms（仅一次 31.5 ms） |
| `native_lock` wait | 0.011–0.015 ms/秒 |
| `native_lock` hold | 0.96–1.16 ms/秒 |
| `native_lock` max hold | **12–41 µs** |
| `melee_lock` wait | 0.009–0.017 ms/秒 |
| `melee_lock` max hold | 0.16–0.42 ms |
| `blocks_lock` | 可忽略 |
| `read` | 55–76 万/秒 |
| `enum` / `grace` / `slow_rtti` | 61/秒 / 0 / 240/秒 |

结论：

- **`native_mutex` 不是卡顿来源**：单次最长持有 41 µs，每秒总持有约 1 ms，等待约 14 µs。之前担心的“Present 线程持锁阻塞游戏线程”在这个场景没有发生。
- **`melee_lock` 也不是卡顿来源**：单次最长 0.42 ms，每秒总持有 1–2 ms。
- **没有 >33 ms 的帧**：12 秒内 0 次。唯一一次 31.5 ms 更像游戏侧的流式加载/着色器编译。
- `>16.7 ms` 计数偏高是 60 FPS vsync 的正常抖动；判断卡顿应看 `>33` / `>50`。

这组数据把“锁竞争导致卡顿”的假设基本排除。下一步若还有卡顿，优先看：

1. 更繁忙的场景（大量敌人、开背包、TNT、快速移动）复测 `>33` / `>50`；
2. `enumerateEnemies` 内部的逐角色读取（剩余平均开销的大头）；
3. 游戏自身的加载/着色器编译（用 `>50` 帧发生的时间点对照 `mc_er.log` 里的 `world=`/`load[...]` 行）。

探针 DLL 部署后哈希 `c4383ef0...`，旧 DLL 备份为游戏目录的 `dinput8.dll.bak_before_probes`。

### 全日志扫描（228 个 perfstats 秒）

上面的 12 秒样本是平静窗口。扫完整日志后，`>33 ms` 的帧集中在这些时间点：

| 时间 | 最差帧 | 同秒日志 | 判断 |
|---|---|---|---|
| 10–18s | 3.5s / 2.1s / 1.7s | `world=0`、加载画面 | 游戏启动/加载 |
| 47–52s | 289 ms | `screen=1`、`fade=1.00` | 游戏加载画面 |
| 64–70s | 327 / 235 / 118 ms | `fade=0.60→1.00`、`screen=1`、world 指针变化 | 快速旅行/加载 |
| **80.14s** | **6973 ms** | `CreateDevice keyboard` 反复重建、AK-47 CSVM 懒加载、`melee_lock max=18 ms` | **Alt-Tab/失焦** + CS 武器首次加载 |
| 210s | 495 ms | `flight`/`elytra` 状态切换、大位移 | 游戏流式加载/飞行 |
| 224–228s | 256 / 117 / 69 ms | `screen=1`、`fade=1.00`、`blocks: loading screen` | 加载画面 |

结论修正：

- **稳定游戏内没有 `>33 ms` 的帧**；卡顿集中在加载、快速旅行、失焦和首次切枪。
- **80 秒的 7 秒不是 mod**：同一秒键盘设备反复重建，是窗口失焦/Alt-Tab 的特征。
- **唯一明确的 mod 侧卡顿是 CS 武器模型首次切换**：`overlay_hook.cpp` 的 Init 只预加载了三把枪的贴图（`loadWeaponTextures`），模型（`loadWeapon` → `loadModel`）仍是懒加载；80 秒那次解析了 1.4MB CSVM，约 100ms。
- `melee_lock` 的 18ms 只在 80 秒出现一次，疑似背包/合成/存档竞争，量级远小于加载卡顿。
- 如果要消除首次切枪卡顿，需要缓存解析后的 CSVM（CPU 侧）或在 Init 预加载当前槽位的模型；`CsRenderer` 目前只持有一个 `model_`，不能同时缓存三把枪的 GPU 网格。
- **已修复（本分支）**：`loadModel` 现在按路径缓存解析后的 `CSViewModel`，Init 预加载刀 + AK/AWP/M4 的模型；切枪只拷贝解析结果并绑定已缓存的 GPU mesh。实测切枪窗口（32–46s）`>33=0`、`>50=0`，最差 29.6ms，`melee_lock` max 0.52ms，不再出现 ~100ms 的 CSVM 解析。
