#include "overlay_internal.hpp"

namespace erov {
namespace detail {

bool GameIsForeground() {
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (fg != nullptr) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}


namespace {

const mc::model::EntityModel* PropModel(const char* name) {
    auto it = g_mob_models.find(name);
    return it == g_mob_models.end() ? nullptr : &it->second;
}

// Draws a Bedrock model (elytra wings, trident) attached to a Steve part. `delta` is the world-space motion of that
// part since the rest pose; the model's own bone pivots line up with the Steve rig's (same Bedrock coordinates).
void DrawProp(const char* name, const mc::model::EntityModel* model, const mc::rig::Mat4& delta, const SteveParams& sp,
              const mc::rig::Mat4& vp, const SteveState& steve, D3D12_CPU_DESCRIPTOR_HANDLE rtv, float r, float g, float b,
              float flap) {
    if (model == nullptr || !g_steve.mobModelReady(name)) return;
    const mc::Vec3 feet{steve.feet[0], steve.feet[1], steve.feet[2]};
    std::vector<mc::Quat> extra(model->bones.size());
    if (flap != 0.f) {
        const mc::Quat right{std::sin(flap * 0.5f), 0.f, 0.f, std::cos(flap * 0.5f)};
        const mc::Quat left{-std::sin(flap * 0.5f), 0.f, 0.f, std::cos(flap * 0.5f)};
        for (size_t i = 0; i < model->bones.size(); ++i) {
            if (model->bones[i].name == "rightWing") extra[i] = eldenring::render::hostQuat(right);
            else if (model->bones[i].name == "leftWing") extra[i] = eldenring::render::hostQuat(left);
        }
    }
    auto bones = mc::model::boneMatrices(*model, extra, feet, steve.yaw, eldenring::render::kBasis);
    for (auto& bone : bones) bone = bone * delta;
    SteveParams pp = sp;
    pp.keep_depth = true;
    pp.held_item = 0;
    pp.held_table = {};
    pp.offhand_item = 0;
    pp.tint[0] = r;
    pp.tint[1] = g;
    pp.tint[2] = b;
    pp.tint[3] = 1.f; // solid: the shader skips the skin alpha cut-out for a fully opaque tint
    g_steve.drawMobModel(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp, bones, pp, rtv, name);
}

} // namespace

void RenderFrame(IDXGISwapChain* sc) {
    HudState hud;
    SteveState steve;
#if ENABLE_CS2_VIEWMODEL
    if (!g_provider || !g_provider(hud, steve) || !hud.show || (!hud.mc_mode && !hud.cs_mode && !hud.ow_mode)) return;
#else
    if (!g_provider || !g_provider(hud, steve) || !hud.show || (!hud.mc_mode && !hud.ow_mode)) return;
#endif

    if (g_depth_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_depth_mutex);
        g_steve.setDepthView(g_s.device, g_depth_res, g_depth_cpu);
        if (g_depth_copy_cpu.ptr != 0) g_steve.setDepthView(g_s.device, g_depth_res, g_depth_copy_cpu);
        if (g_depth_copy2_cpu.ptr != 0) g_steve.setDepthView(g_s.device, g_depth_res, g_depth_copy2_cpu);
        for (auto h : g_mob_depth_copy_cpus) {
            if (h.ptr != 0) g_steve.setDepthView(g_s.device, g_depth_res, h);
        }
    }

    const UINT idx = g_s.swap->GetCurrentBackBufferIndex();
    if (idx >= g_s.buffers) return;
    Frame& f = g_s.frames[idx];
    if (f.fence_value != 0 && g_s.fence->GetCompletedValue() < f.fence_value) {
        g_s.fence->SetEventOnCompletion(f.fence_value, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 1000);
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::max(1.f / 1000.f, std::chrono::duration<float>(now - g_s.last_frame).count());
    g_s.last_frame = now;

    // Frame-time probe: opt in with mc_er_perflog.txt (a 5 s window makes sustained drops visible).
    static const bool perf_log = [] {
        const std::string& dir = eldenring::diagnostics::GetGameDir();
        return !dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / "mc_er_perflog.txt");
    }();
    static double perf_accum = 0.0;
    static unsigned perf_frames = 0;
    if (perf_log) {
        perf_accum += dt;
        ++perf_frames;
        if (perf_accum >= 5.0) {
            Logf("perf: %.1f fps (%.2f ms/frame)", perf_frames / perf_accum, 1000.0 * perf_accum / perf_frames);
            perf_accum = 0.0;
            perf_frames = 0;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(g_s.width), static_cast<float>(g_s.height)};
    io.DeltaTime = dt;
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    UpdateWorldFx(steve, dt, io.DisplaySize.x, io.DisplaySize.y);
    DrawHud(hud, io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Render();

    f.alloc->Reset();
    g_s.list->Reset(f.alloc, nullptr);
    RecordAtlasUpload(g_s.list);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = f.back;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_s.list->ResourceBarrier(1, &b);
    g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {g_s.srv_heap};
    g_s.list->SetDescriptorHeaps(1, heaps);
    if (g_steve_cfg.depthview_gain > 0.f && g_depth_res != nullptr) {
        g_steve.drawDepthView(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, g_steve_cfg.depthview_gain,
                              static_cast<float>(g_depth_w), static_cast<float>(g_depth_h));
    }
    bool blocks_drawn = false;
    if (g_blocks_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_block_mutex);
        g_steve.setBlocks(g_block_mesh);
    }
    if (g_crack_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_crack_mutex);
        g_steve.setTerrainCrack(g_crack_mesh);
    }
    if (steve.cam_valid && g_steve.ready() && g_atlas_ready && g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
        const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
        const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
        SteveParams bp;
        bp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.25f : 0.f); // 1.25: occluded by the scene, but not within 2.2 m of the camera
        bp.depth_const = g_steve_cfg.depth_const;
        bp.rel_bias = g_steve_cfg.rel_bias;
        bp.abs_bias = g_steve_cfg.abs_bias;
        bp.depth_w = static_cast<float>(g_depth_w);
        bp.depth_h = static_cast<float>(g_depth_h);
        blocks_drawn = g_steve.drawBlocks(g_s.device, g_s.list, g_s.srv_heap, g_held_table_gpu, g_s.width, g_s.height, vp, bp, f.rtv, idx);
        // The crack gets a near-zero bias: the block bias (8% + 5 cm) would force a floor decal to
        // float in front of the floor to survive the scene-depth test. It is a separate mesh, so it
        // is drawn even when there are no placed blocks.
        SteveParams cp = bp;
        cp.mode = g_depth_res == nullptr ? 0.f : 1.0f;
        cp.rel_bias = 0.0f;
        cp.abs_bias = 0.001f;
        cp.keep_depth = blocks_drawn;
        const bool crack_drawn = g_steve.drawTerrainCrack(g_s.device, g_s.list, g_s.srv_heap, g_held_table_gpu, g_s.width, g_s.height, vp, cp, f.rtv, idx);
        static uint64_t last_crack_log = 0;
        const uint64_t now_crack = GetTickCount64();
        if (g_log && now_crack - last_crack_log > 1000) {
            last_crack_log = now_crack;
            size_t cv = 0, ci = 0;
            {
                std::lock_guard<std::mutex> g(g_crack_mutex);
                cv = g_crack_mesh.vertices.size();
                ci = g_crack_mesh.indices.size();
            }
            g_log("crackdraw: mesh=%zu/%zu drawn=%d mode=%.2f rel=%.3f abs=%.3f", cv, ci, crack_drawn ? 1 : 0,
                  cp.mode, cp.rel_bias, cp.abs_bias);
        }
        if (blocks_drawn || crack_drawn) g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    }
    g_death_tracker.update(dt, steve.dead);
    if (g_death_tracker.justRespawned()) {
        g_anim.reset();
        g_motion.reset();
    }
    const bool should_draw_steve = steve.draw && g_death_tracker.shouldDrawModel();
    if (should_draw_steve && g_steve.ready()) {
        const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
        const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
        if (!steve.dead) {
            g_sleep_tracker.update(dt, steve.sleeping);
            const float sleep_frac = g_sleep_tracker.sleepFraction();

            mc::SteveAnimInput anim_in = g_motion.update(dt, steve.feet, steve.yaw);
            anim_in.swing_progress = steve.swing;
            anim_in.bow_charge = steve.bow_charge;
            anim_in.bow_offhand = steve.bow_offhand;
            anim_in.is_blocking = steve.blocking;
            anim_in.block_offhand = steve.block_offhand;
            anim_in.is_crouching = steve.sneaking;
            anim_in.is_sprinting = steve.sprinting;
            anim_in.on_ground = steve.on_ground;
            anim_in.is_sleeping = (sleep_frac > 0.05f);
            if (!anim_in.is_sleeping) {
                g_head.update(steve.cam.forward.x, steve.cam.forward.y, steve.cam.forward.z, steve.yaw, anim_in);
            }
            // Phase A debug triggers (mc_er_dbg_anim.txt): glide, riptide and the trident charge come from the
            // debug keys, not from gameplay yet.
            anim_in.is_gliding = steve.gliding;
            anim_in.trident_charge = steve.trident_charge;
            {
                static float dbg_riptide_t = 0.f;
                if (steve.riptide) dbg_riptide_t = std::fmod(dbg_riptide_t + dt, 0.6f);
                else dbg_riptide_t = 0.f;
                anim_in.riptide = steve.riptide ? (dbg_riptide_t / 0.6f) : 0.f;
            }
            g_anim.update(dt, anim_in);
            if (g_trace_left.load() > 0) {
                g_trace_left.fetch_sub(1);
                char tr[220];
                snprintf(tr, sizeof(tr), "trace: dt=%.4f feet=%.4f %.4f %.4f cam=%.4f %.4f %.4f yaw=%.3f fwd=%.3f strafe=%.3f", dt, steve.feet[0],
                         steve.feet[1], steve.feet[2], steve.cam.position.x, steve.cam.position.y, steve.cam.position.z, steve.yaw,
                         anim_in.forward_speed, anim_in.strafe_speed);
                Logf(tr);
            }
        } else {
            g_sleep_tracker.reset();
        }
        auto parts = eldenring::render::posedMatrices(g_anim.getTransforms(), g_anim.rootTransform(), g_anim.upperTransform(),
                                                     {steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw);
        // Phase A debug particles: a firework boost on End, the riptide water ring while it is active.
        {
            static uint32_t last_firework = 0;
            if (steve.firework_seq != last_firework) {
                last_firework = steve.firework_seq;
                const float pos[3] = {steve.feet[0], steve.feet[1] + 1.0f, steve.feet[2]};
                const float dir[3] = {steve.cam.forward.x, steve.cam.forward.y, steve.cam.forward.z};
                SpawnFx(FxKind::Firework, pos, dir, 0);
            }
            if (steve.riptide) {
                static float acc = 0.f;
                static float phase = 0.f;
                acc += dt;
                if (acc >= 0.05f) {
                    acc = 0.f;
                    phase = std::fmod(phase + 0.1f, 1.f);
                    const float pos[3] = {steve.feet[0], steve.feet[1] + 0.5f, steve.feet[2]};
                    const float no_dir[3] = {0.f, 0.f, 0.f};
                    SpawnFx(FxKind::Riptide, pos, no_dir, static_cast<int>(phase * 10.f));
                }
            }
        }
        const float fall_frac = g_death_tracker.fallFraction();
        if (fall_frac > 0.f) {
            const mc::rig::Mat4 fall = eldenring::render::deathFallMatrix({steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw, fall_frac);
            for (auto& m : parts) m = m * fall;
        } else {
            const float sleep_frac = g_sleep_tracker.sleepFraction();
            if (sleep_frac > 0.f) {
                const mc::rig::Mat4 sleep = eldenring::render::sleepPoseMatrix({steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw, sleep_frac);
                for (auto& m : parts) m = m * sleep;
            }
        }
        if (steve.shadow && g_steve.shadowReady() && g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
            const float ground = steve.ground_valid ? steve.ground_y : steve.feet[1];
            SteveParams shp;
            shp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.25f : 0.f); // like the blocks: not within 2.2 m of the camera
            shp.depth_const = g_steve_cfg.depth_const;
            shp.rel_bias = g_steve_cfg.rel_bias;
            shp.abs_bias = g_steve_cfg.abs_bias;
            shp.depth_w = static_cast<float>(g_depth_w);
            shp.depth_h = static_cast<float>(g_depth_h);
            shp.keep_depth = blocks_drawn;
            g_steve.drawShadows(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp,
                                {eldenring::shadow::shadowAt(steve.feet[0], steve.feet[1], steve.feet[2], ground)}, shp, f.rtv);
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
        }
        SteveParams sp;
        sp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.debug == 2 ? 3.f : (g_steve_cfg.debug != 0 ? 2.f : (g_steve_cfg.occlusion ? 1.f : 0.f)));
        sp.depth_const = g_steve_cfg.depth_const;
        sp.rel_bias = g_steve_cfg.rel_bias;
        sp.abs_bias = g_steve_cfg.abs_bias;
        sp.depth_w = static_cast<float>(g_depth_w);
        sp.depth_h = static_cast<float>(g_depth_h);
        sp.tint[0] = 1.f; // Minecraft's hurt flash: red over the lit skin for the 10 ticks after a hit
        sp.tint[1] = sp.tint[2] = 0.f;
        sp.tint[3] = 0.4f * (g_death_tracker.isDying() ? 1.0f : std::min(1.f, steve.hurt * 4.f));
        sp.keep_depth = blocks_drawn; // the blocks left their depth in the figure's buffer: the figure sorts against them
        sp.held_item = static_cast<uint16_t>(eldenring::bow::shownBow(static_cast<mc::ItemId>(steve.held_item), steve.bow_charge));
        // a bow in the offhand shows the drawn-bow sprite while it is drawn (the held item in the right hand stays what it is)
        sp.shield_blocking = steve.blocking;
        sp.block_offhand = steve.block_offhand;
        sp.offhand_item = steve.bow_offhand ? static_cast<uint16_t>(eldenring::bow::shownBow(static_cast<mc::ItemId>(steve.offhand_item), steve.bow_charge))
                                            : steve.offhand_item;
        sp.held_table = g_atlas_ready ? g_held_table_gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
        if (g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
            g_steve.draw(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp, parts, sp, f.rtv);
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr); // ImGui draws without a depth view
        }
        // Phase A debug props: the elytra wings follow the torso, the trident follows the right arm.
        if (!steve.dead && (steve.gliding || steve.trident_charge > 0.f)) {
            const mc::Vec3 feet_v{steve.feet[0], steve.feet[1], steve.feet[2]};
            const mc::rig::Mat4 rest_body = mc::rig::partMatrix(mc::StevePart::Body, mc::Quat{}, feet_v, steve.yaw, eldenring::render::kBasis);
            const mc::rig::Mat4 body_delta =
                eldenring::render::inverseRigid(rest_body) * parts[static_cast<size_t>(mc::StevePart::Body)];
            // The trident follows the hand's position but not the arm's rotation: the 12-part rig has no wrist, so
            // inheriting the charge pose's 186-degree arm rotation would swing the shaft down the player's back.
            // The prop mesh is baked at its rest pose and the trident bone's pivot sits exactly at the hand, so the
            // delta has to carry that pivot from the rest hand to the posed one.
            const mc::model::EntityModel* trident_model = PropModel("trident");
            const mc::model::Bone* trident_bone = trident_model != nullptr ? trident_model->findBone("trident") : nullptr;
            const mc::Vec3 trident_pivot =
                trident_bone != nullptr ? mc::model::bonePivotHost(*trident_bone, eldenring::render::kBasis) : mc::Vec3{};
            const mc::rig::Mat4 rest_arm = mc::rig::partMatrix(mc::StevePart::RightArm, mc::Quat{}, feet_v, steve.yaw, eldenring::render::kBasis);
            const mc::Vec3 hand_rest = mc::rig::transformPoint(rest_arm, trident_pivot);
            const mc::Vec3 hand_posed = mc::rig::transformPoint(parts[static_cast<size_t>(mc::StevePart::RightArm)], trident_pivot);
            const mc::rig::Mat4 trident_delta = mc::rig::translation(hand_posed - hand_rest);
            if (steve.gliding) {
                static float flap_t = 0.f;
                flap_t += dt;
                DrawProp("elytra_wings", PropModel("elytra_wings"), body_delta, sp, vp, steve, f.rtv, 0.82f, 0.84f, 0.9f,
                         0.35f * std::sin(flap_t * 8.f));
            }
            if (steve.trident_charge > 0.f) {
                DrawProp("trident", PropModel("trident"), trident_delta, sp, vp, steve, f.rtv, 0.10f, 0.55f, 0.55f, 0.f);
            }
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
        }
        if (sp.mode > 2.5f) {
            static auto last_log = std::chrono::steady_clock::now();
            const auto t = std::chrono::steady_clock::now();
            uint32_t st[64];
            if (t - last_log > std::chrono::seconds(2) && g_steve.readStats(st)) {
                last_log = t;
                uint64_t total = 0;
                for (uint32_t c : st) total += c;
                if (g_log && total > 0) {
                    int order[64];
                    for (int i = 0; i < 64; ++i) order[i] = i;
                    std::sort(order, order + 64, [&](int x, int y) { return st[x] > st[y]; });
                    char line[400];
                    int n = snprintf(line, sizeof(line), "steve: K=depth*z histogram, %llu px; top bins:", static_cast<unsigned long long>(total));
                    for (int i = 0; i < 5; ++i) {
                        const double centre = 0.0005 * std::pow(2.0, (order[i] + 0.5) / 8.0);
                        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " [K~%.5f: %.0f%%]", centre,
                                      100.0 * st[order[i]] / static_cast<double>(total));
                    }
                    g_log("%s (assumed %.5f)", line, g_steve_cfg.depth_const);
                }
            }
        }
    }
    // The mobs over the summons: the same rig, one pose per summoned character. They sort against the scene (and the blocks) like Steve.
    if (steve.cam_valid && g_steve.ready() && g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
        std::vector<eldenring::mobs::MobSnapshot> snapshot;
        {
            std::lock_guard<std::mutex> g(g_mob_mutex);
            snapshot = g_mob_snapshot;
        }
        std::vector<eldenring::summon::ArrowDraw> arrow_snapshot;
        {
            std::lock_guard<std::mutex> g(g_arrow_mutex);
            arrow_snapshot = g_arrow_snapshot;
        }
        const auto draws = g_mob_registry.update(dt, snapshot);
        if (!draws.empty() || !arrow_snapshot.empty()) {
            const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
            const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
            bool keep = blocks_drawn || should_draw_steve; // the blocks and Steve left their depth in the figure's buffer
            if (!draws.empty()) {
                if (steve.shadow && g_steve.shadowReady()) {
                    std::vector<eldenring::shadow::Shadow> shadows;
                    for (const auto& d : draws) shadows.push_back(eldenring::shadow::shadowAt(d.feet[0], d.feet[1], d.feet[2], d.feet[1])); // a mob stands on the ground
                    SteveParams shp;
                    shp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.25f : 0.f);
                    shp.depth_const = g_steve_cfg.depth_const;
                    shp.rel_bias = g_steve_cfg.rel_bias;
                    shp.abs_bias = g_steve_cfg.abs_bias;
                    shp.depth_w = static_cast<float>(g_depth_w);
                    shp.depth_h = static_cast<float>(g_depth_h);
                    shp.keep_depth = keep;
                    g_steve.drawShadows(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp, shadows, shp, f.rtv);
                }
                for (const auto& d : draws) {
                    SteveParams mp;
                    mp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.f : 0.f);
                    mp.depth_const = g_steve_cfg.depth_const;
                    mp.rel_bias = g_steve_cfg.rel_bias;
                    mp.abs_bias = g_steve_cfg.abs_bias;
                    mp.depth_w = static_cast<float>(g_depth_w);
                    mp.depth_h = static_cast<float>(g_depth_h);
                    mp.keep_depth = keep;
                    mp.held_item = static_cast<uint16_t>(d.held_item);
                    mp.held_table = g_held_table_gpu;
                    mp.tint[0] = 1.f; // the same red flash as Steve's hurt: 10 ticks, strongest at the start; a dying mob stays red
                    mp.tint[1] = mp.tint[2] = 0.f;
                    mp.tint[3] = 0.4f * std::min(1.f, d.hurt * 4.f);
                    if (d.white_flash > 0.f) {
                        mp.tint[0] = mp.tint[1] = mp.tint[2] = 1.f;
                        mp.tint[3] = 0.7f * d.white_flash;
                    }
                    D3D12_GPU_DESCRIPTOR_HANDLE table = g_mob_table_gpu.ptr != 0 ? g_mob_table_gpu : g_depth_gpu;
                    if (!d.model_type.empty()) {
                        auto it = g_mob_tables_gpu.find(d.model_type);
                        if (it != g_mob_tables_gpu.end() && it->second.ptr != 0) {
                            table = it->second;
                        }
                    }
                    if (d.generic) g_steve.drawMobModel(g_s.list, g_s.srv_heap, table, g_s.width, g_s.height, vp, d.bones, mp, f.rtv, d.model_type);
                    else g_steve.draw(g_s.list, g_s.srv_heap, table, g_s.width, g_s.height, vp, d.parts, mp, f.rtv);
                    keep = true; // the next one sorts against this one
                }
            }
            if (!arrow_snapshot.empty() && g_held_table_gpu.ptr != 0) {
                std::vector<mc::rig::Mat4> arrow_matrices;
                arrow_matrices.reserve(arrow_snapshot.size());
                for (const auto& a : arrow_snapshot) {
                    arrow_matrices.push_back(eldenring::summon::arrowWorldMatrix(a));
                }
                SteveParams arrow_p;
                arrow_p.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.f : 0.f);
                arrow_p.depth_const = g_steve_cfg.depth_const;
                arrow_p.rel_bias = g_steve_cfg.rel_bias;
                arrow_p.abs_bias = g_steve_cfg.abs_bias;
                arrow_p.depth_w = static_cast<float>(g_depth_w);
                arrow_p.depth_h = static_cast<float>(g_depth_h);
                arrow_p.keep_depth = keep;
                g_steve.drawArrows(g_s.list, g_s.srv_heap, g_held_table_gpu, g_s.width, g_s.height, vp, arrow_matrices, arrow_p, f.rtv);
            }
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
        }
    } else {
        g_mob_registry.clear();
    }
    [[maybe_unused]] auto& r_mode = eldenring::RuntimeContext::Instance().mode();
#if ENABLE_CS2_VIEWMODEL
    if (r_mode.cs_mode.load() && g_cs.ready()) {
        // A weapon switch releases the old VB/IB and textures; wait for the
        // GPU to finish the previous frame before replacing them.
        if (r_mode.cs_weapon_slot.load() != g_cs_active_slot) {
            WaitForGpu();
        }
        UpdateCsWeaponSlot(r_mode.cs_weapon_slot.load());
    }
    const bool cs_mode_on = r_mode.cs_mode.load();
    const bool cs_active = cs_mode_on && g_cs.ready() && GameIsForeground();
#else
    const bool cs_mode_on = false;
    const bool cs_active = false;
#endif
    const bool mc_mode_on = eldenring::RuntimeContext::Instance().mode().mc_mode.load(std::memory_order_relaxed);
    const bool ow_mode_on = eldenring::RuntimeContext::Instance().mode().ow_mode.load(std::memory_order_relaxed);
    // Do not fall back to the MC first-person model while CS mode or OW mode owns the view,
    // or while MC mode is off.
    const bool mc_fp_active = !cs_active && !cs_mode_on && !ow_mode_on && mc_mode_on && steve.first_person && !steve.dead && steve.cam_valid && g_steve.ready() && g_fp_built && g_steve.firstPersonReady() &&
        g_steve.ensureDepth(g_s.device, g_s.width, g_s.height);

#if ENABLE_OW_VIEWMODEL
    const bool ow_active = ow_mode_on && steve.first_person && !steve.dead && GameIsForeground() && g_ow_initialized;
    if (ow_active) {
        if (!g_ow_model_loaded && !g_ow_load_failed) {
            const std::string& ow_game_dir = eldenring::diagnostics::GetGameDir();
            std::vector<std::string> ow_model_candidates;
            if (!ow_game_dir.empty()) {
                ow_model_candidates.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet.bin").string());
            }
            ow_model_candidates.push_back("mods\\mc_adapter\\ow_gauntlet.bin");
            ow_model_candidates.push_back("ow_gauntlet.bin");
            for (const auto& p : ow_model_candidates) {
                if (std::filesystem::exists(p) && g_ow.loadModel(g_s.device, p)) {
                    g_ow_model_loaded = true;
                    Logf("overlay: OW gauntlet loaded from %s", p.c_str());
                    break;
                }
            }
            if (!g_ow_model_loaded) {
                g_ow_load_failed = true;
                Logf("overlay: OW gauntlet model not found");
            } else {
                auto firstExisting = [](const std::vector<std::string>& candidates) {
                    for (const auto& p : candidates) {
                        if (std::filesystem::exists(p)) return p;
                    }
                    return std::string();
                };
                std::vector<std::string> albedo, normal, pbr;
                if (!ow_game_dir.empty()) {
                    albedo.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet.png").string());
                    normal.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet_normal.png").string());
                    pbr.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet_pbr.png").string());
                }
                albedo.push_back("mods\\mc_adapter\\ow_gauntlet.png");
                normal.push_back("mods\\mc_adapter\\ow_gauntlet_normal.png");
                pbr.push_back("mods\\mc_adapter\\ow_gauntlet_pbr.png");
                if (g_ow.loadTextures(g_s.device, firstExisting(albedo), firstExisting(normal), firstExisting(pbr),
                                      g_ow_srv_heap->GetCPUDescriptorHandleForHeapStart())) {
                    Logf("overlay: OW gauntlet textures loaded");
                } else {
                    Logf("overlay: OW gauntlet textures missing");
                }
            }
        }
        if (g_ow_model_loaded) {
            g_ow.setConfig(g_ow_cfg);
            const mc::rig::Mat4 proj = g_ow.computeProjection(g_s.width, g_s.height);
            g_ow.tick(dt, r_mode.ow_state.load(), r_mode.ow_anim_time.load(), r_mode.ow_action_variant.load());
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_steve.ready() ? g_steve.dsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
            g_ow.draw(g_s.list, g_ow_srv_heap, g_ow_table_gpu, g_s.width, g_s.height, proj, f.rtv, dsv);
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
        }
    } else
#endif
#if ENABLE_CS2_VIEWMODEL
    if (cs_active && !steve.dead) {
        const mc::rig::Mat4 proj = g_cs.computeViewmodelProjection(g_s.width, g_s.height);
        // The knife and the shared gun table have their own descriptors;
        // switching is just a descriptor-handle swap.
        D3D12_GPU_DESCRIPTOR_HANDLE cs_tex_table =
            (r_mode.cs_weapon_slot.load() == 0) ? g_cs_knife_table_gpu : g_cs_ak_table_gpu;
        if (g_steve.ready()) {
            g_steve.ensureDepth(g_s.device, g_s.width, g_s.height);
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_steve.ready() ? g_steve.dsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
        g_cs.tick(dt, r_mode.cs_knife_state.load(), r_mode.cs_knife_anim_time.load(),
                  r_mode.cs_action_variant.load(), r_mode.cs_action_hit.load());
        g_cs.draw(g_s.list, g_s.srv_heap, cs_tex_table, g_s.width, g_s.height, proj, f.rtv, dsv);
        g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    } else
#endif
    if (mc_fp_active) {
        namespace fp = eldenring::fp;
        // The hand: lowered/raised by the hotbar and the attack cooldown, swaying behind the view, bobbing with the steps.
        const mc::ItemId wanted = static_cast<mc::ItemId>(steve.held_item);
        g_hand.tick(dt, wanted, std::clamp(steve.cooldown, 0.f, 1.f));
        const float pitch_deg = -std::asin(std::clamp(steve.cam.forward.y, -1.f, 1.f)) * 180.f / fp::kPi; // positive = looking down
        const float yaw_deg = std::atan2(steve.cam.forward.x, steve.cam.forward.z) * 180.f / fp::kPi;
        float d_pitch = 0.f, d_yaw = 0.f;
        g_sway.update(dt, pitch_deg, yaw_deg, d_pitch, d_yaw);
        g_walk_dist += steve.speed_mps * dt * 0.6f;
        const float bob_target = steve.on_ground ? std::min(0.1f, steve.speed_mps / 20.f) : 0.f;
        g_walk_bob += (bob_target - g_walk_bob) * (1.f - std::pow(0.6f, dt * 20.f));
        const fp::M4 base = fp::mul(fp::walkBob(g_walk_dist, g_walk_bob), fp::handSway(d_pitch, d_yaw));
        const float equipped = g_hand.equipped();
        const bool drawing_bow = steve.bow_ticks > 0.f && g_hand.shownItem() == mc::ItemId::Bow;
        const bool main_is_shield = g_hand.shownItem() == mc::ItemId::Shield;
        const bool main_raised = steve.blocking && !steve.block_offhand; // the shield in the main hand is the one being raised
        const fp::M4 item_pose = drawing_bow ? fp::bowPose(steve.bow_ticks, equipped)
                                 : main_is_shield ? fp::shieldMainHandPose(main_raised, steve.swing, equipped)
                                 : steve.eating > 0.f ? fp::eatPose(steve.eating, equipped) : fp::itemPose(steve.swing, equipped);
        const bool held_block = eldenring::blocks::blockForItem(static_cast<mc::ItemId>(g_hand.shownItem())) != mc::BlockId::Air;
        const fp::M4 item_disp = main_is_shield ? fp::shieldFpDisplay(false, main_raised) : (held_block ? fp::blockDisplay() : fp::itemDisplay());
        const fp::M4 item_world = fp::mul(fp::mul(base, item_pose), item_disp);
        const fp::M4 arm_world = fp::mul(base, fp::bareArmPose(steve.swing, equipped));
        const mc::ItemId shown = eldenring::bow::shownBow(g_hand.shownItem(), drawing_bow ? steve.bow_charge : 0.f); // the bow with the arrow nocked while it is drawn
        const mc::rig::Mat4 proj = mc::rig::perspectiveLH(70.f * fp::kDeg, static_cast<float>(g_s.width) / static_cast<float>(g_s.height), 0.05f, 20.f);
        D3D12_GPU_DESCRIPTOR_HANDLE skin_table = g_depth_gpu; // slots 1 and 2: depth, skin
        g_steve.drawFirstPerson(g_s.list, g_s.srv_heap, skin_table, g_item_table_gpu, g_s.width, g_s.height, proj, fp::toHost(arm_world),
                                fp::toHost(item_world), static_cast<uint16_t>(shown), f.rtv);
        // Offhand item in first person (e.g. shield in offhand, torch, etc.)
        if (steve.offhand_item != 0) {
            const bool off_is_shield = static_cast<mc::ItemId>(steve.offhand_item) == mc::ItemId::Shield;
            const bool off_block = eldenring::blocks::blockForItem(static_cast<mc::ItemId>(steve.offhand_item)) != mc::BlockId::Air;
            const fp::M4 off_pose = fp::offhandItemPose(0.f); // the offhand only follows the arm transform: raised or not is the display transform
            const fp::M4 off_disp = off_is_shield ? fp::shieldFpDisplay(true, steve.blocking && steve.block_offhand)
                                                  : (off_block ? fp::blockDisplay() : fp::itemDisplay());
            const fp::M4 off_world = fp::mul(fp::mul(base, off_pose), off_disp);
            const uint16_t off_shown = steve.bow_offhand && steve.bow_ticks > 0.f
                                           ? static_cast<uint16_t>(eldenring::bow::shownBow(static_cast<mc::ItemId>(steve.offhand_item), steve.bow_charge))
                                           : steve.offhand_item;
            g_steve.drawFirstPersonItem(g_s.list, g_s.srv_heap, g_item_table_gpu, g_s.width, g_s.height, proj, fp::toHost(off_world),
                                        off_shown, f.rtv);
        }
        g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr); // ImGui draws without a depth view
    }
    if (hud.inv_open && g_steve.ready()) {
        g_steve.ensureDepth(g_s.device, g_s.width, g_s.height);
    }
    g_current_rtv = f.rtv;
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_s.list);
    g_inv_preview.active = false;
    g_current_rtv = {};
    // A screenshot (F4): the finished back buffer is copied into a read-back buffer in the same command list.
    ID3D12Resource* shot_buf = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT shot_fp{};
    UINT shot_rows = 0;
    std::string shot_path;
    const FrameBarrierPlan barrier_plan = plan_frame_barriers(g_shot_requested.exchange(false));
    if (barrier_plan.copy_screenshot) {
        {
            std::lock_guard<std::mutex> g(g_shot_mutex);
            shot_path = g_shot_path;
        }
        const D3D12_RESOURCE_DESC rd = f.back->GetDesc();
        UINT64 row_bytes = 0, total = 0;
        g_s.device->GetCopyableFootprints(&rd, 0, 1, 0, &shot_fp, &shot_rows, &row_bytes, &total);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(g_s.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&shot_buf)))) {
            shot_buf = nullptr;
            Logf("screenshot: read-back buffer failed");
        } else {
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            g_s.list->ResourceBarrier(1, &b);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = shot_buf;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = shot_fp;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = f.back;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            g_s.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
    }
    if (!barrier_plan.copy_screenshot || shot_buf == nullptr) b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_s.list->ResourceBarrier(1, &b);
    g_s.list->Close();
    ID3D12CommandList* lists[] = {g_s.list};
    g_execute_orig(g_s.queue, 1, lists);
    f.fence_value = ++g_s.fence_counter;
    g_s.queue->Signal(g_s.fence, f.fence_value);
    if (shot_buf != nullptr) {
        g_s.fence->SetEventOnCompletion(f.fence_value, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 2000);
        const D3D12_RESOURCE_DESC rd = f.back->GetDesc();
        void* mapped = nullptr;
        D3D12_RANGE range{0, shot_fp.Footprint.RowPitch * shot_rows};
        const bool rgba = rd.Format == DXGI_FORMAT_R8G8B8A8_UNORM || rd.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool bgra = rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM || rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const bool r10 = rd.Format == DXGI_FORMAT_R10G10B10A2_UNORM; // the game's own back buffer (format 24)
        if ((rgba || bgra || r10) && SUCCEEDED(shot_buf->Map(0, &range, &mapped))) {
            const unsigned w = static_cast<unsigned>(rd.Width), h = rd.Height;
            std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
            for (unsigned y = 0; y < h; ++y) {
                const uint8_t* srow = static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * shot_fp.Footprint.RowPitch;
                uint8_t* drow = out.data() + static_cast<size_t>(y) * w * 4;
                for (unsigned x = 0; x < w; ++x) {
                    if (r10) {
                        uint32_t packed;
                        std::memcpy(&packed, srow + x * 4, sizeof(packed));
                        eldenring::pixel::unpackR10G10B10A2(packed, drow + x * 4);
                        drow[x * 4 + 3] = 255;
                        continue;
                    }
                    drow[x * 4 + 0] = srow[x * 4 + (bgra ? 2 : 0)];
                    drow[x * 4 + 1] = srow[x * 4 + 1];
                    drow[x * 4 + 2] = srow[x * 4 + (bgra ? 0 : 2)];
                    drow[x * 4 + 3] = 255;
                }
            }
            D3D12_RANGE none{0, 0};
            shot_buf->Unmap(0, &none);
            const bool ok = EncodePngFile(shot_path, out.data(), w, h, w * 4);
            Logf(ok ? "screenshot: saved" : "screenshot: writing the PNG failed");
            if (g_log) g_log("screenshot: %ux%u -> %s", w, h, shot_path.c_str());
        } else {
            Logf("screenshot: the back buffer format is not 8-bit RGBA/BGRA or the read-back map failed");
        }
        shot_buf->Release();
    }
    (void)sc;
}

} // namespace detail
} // namespace erov
