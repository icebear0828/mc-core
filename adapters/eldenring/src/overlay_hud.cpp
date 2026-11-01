#include "overlay_internal.hpp"

namespace erov {
namespace detail {

// Clip-space position of a world point (row vector times the matrix); false when it is behind the camera.
bool ProjectToScreen(const mc::rig::Mat4& m, const float p[3], float w, float h, float fov_y, ImVec2& out, float& px_per_m) {
    const auto& a = m.m;
    const float cx = p[0] * a[0] + p[1] * a[4] + p[2] * a[8] + a[12];
    const float cy = p[0] * a[1] + p[1] * a[5] + p[2] * a[9] + a[13];
    const float cw = p[0] * a[3] + p[1] * a[7] + p[2] * a[11] + a[15];
    if (!(cw > 0.05f)) return false;
    out = {(cx / cw * 0.5f + 0.5f) * w, (0.5f - cy / cw * 0.5f) * h};
    px_per_m = (h * 0.5f) / (std::tan(fov_y * 0.5f) * cw);
    return true;
}

#if ENABLE_CS2_VIEWMODEL
// The muzzle flash is drawn in screen space at the gun's muzzle track: the viewmodel has its own projection,
// so projecting the track with the viewmodel matrix is the only position that stays on the barrel.
void DrawCsMuzzleFlash(ImDrawList* dl, float w, float h) {
    auto& mode = eldenring::RuntimeContext::Instance().mode();
    if (!mode.cs_mode.load() || mode.cs_weapon_slot.load() == 0) return;
    if (mode.cs_knife_state.load() != 2) return;
    const float t = mode.cs_knife_anim_time.load();
    if (t > 0.07f) return;

    float x = 0.f, y = 0.f;
    if (!g_cs.muzzleScreen(static_cast<unsigned>(w), static_cast<unsigned>(h), x, y)) return;
    const float fade = std::max(0.f, 1.f - t / 0.07f);
    const float r = 16.f * (0.55f + 0.45f * fade);
    dl->AddCircleFilled({x, y}, r, IM_COL32(255, 200, 110, static_cast<int>(190.f * fade)));
    dl->AddCircleFilled({x, y}, r * 0.45f, IM_COL32(255, 255, 235, static_cast<int>(235.f * fade)));
    for (int i = 0; i < 4; ++i) {
        const float a = 0.785398f * static_cast<float>(i) + 0.392699f;
        dl->AddLine({x, y}, {x + std::cos(a) * r * 1.7f, y + std::sin(a) * r * 1.7f},
                    IM_COL32(255, 210, 140, static_cast<int>(110.f * fade)), 2.f);
    }
}
#endif

// Tracers are 2D lines projected from world space: cheap, readable and independent of the scene depth buffer.
void DrawTracers(ImDrawList* dl, float w, float h) {
    if (!g_fx_valid) return;
    const uint64_t now = GetTickCount64();
    std::vector<TracerEvent> live;
    {
        std::lock_guard<std::mutex> g(g_tracer_mutex);
        for (auto it = g_tracer_events.begin(); it != g_tracer_events.end();) {
            const float age = static_cast<float>(now - it->start_ms) / 1000.f;
            if (age >= it->life_s) {
                it = g_tracer_events.erase(it);
            } else {
                live.push_back(*it);
                ++it;
            }
        }
    }
    for (const TracerEvent& t : live) {
        const float age = static_cast<float>(now - t.start_ms) / 1000.f;
        const float travel = std::max(1e-4f, t.life_s * 0.7f);
        const float u = std::clamp(age / travel, 0.f, 1.f);
        const float fade = age < travel
            ? 1.f
            : std::max(0.f, 1.f - (age - travel) / std::max(1e-4f, t.life_s - travel));
        if (fade <= 0.f) continue;

        float d[3] = {t.to[0] - t.from[0], t.to[1] - t.from[1], t.to[2] - t.from[2]};
        const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(dist > 1e-3f)) continue;
        for (int i = 0; i < 3; ++i) d[i] /= dist;

        // A short streak flies from the muzzle to the impact, not a line across the whole path. The tail is
        // long enough to read at a distance, and the core is drawn over a soft glow.
        const float head = u * dist;
        const float tail = std::max(0.f, head - std::min(10.f, dist));
        float hp[3], tp[3];
        for (int i = 0; i < 3; ++i) {
            hp[i] = t.from[i] + d[i] * head;
            tp[i] = t.from[i] + d[i] * tail;
        }
        ImVec2 a, b;
        float ppm = 0.f;
        if (!ProjectToScreen(g_fx_vp, tp, w, h, g_fx_fov, a, ppm)) continue;
        if (!ProjectToScreen(g_fx_vp, hp, w, h, g_fx_fov, b, ppm)) continue;
        dl->AddLine(a, b, IM_COL32(255, 196, 96, static_cast<int>(70.f * fade)), 6.f);
        dl->AddLine(a, b, IM_COL32(255, 238, 180, static_cast<int>(235.f * fade)), 2.5f);
        dl->AddCircleFilled(b, 2.5f, IM_COL32(255, 250, 225, static_cast<int>(220.f * fade)), 8);
    }
}

#if ENABLE_CS2_VIEWMODEL
// Bullet holes on floors and walls. The quad lies in the surface plane (built from the Havok normal), so a
// floor hit reads as a flat mark rather than a screen-facing sprite. The last fifth of the lifetime fades.
void DrawDecals(ImDrawList* dl, float w, float h) {
    if (!g_fx_valid) return;
    const uint64_t now = GetTickCount64();
    std::vector<eldenring::cs::ImpactDecal> live;
    {
        std::lock_guard<std::mutex> g(g_decal_mutex);
        g_decal_ring.expire(now);
        live.reserve(g_decal_ring.size());
        for (size_t i = 0; i < g_decal_ring.size(); ++i) live.push_back(g_decal_ring.at(i));
    }
    for (const eldenring::cs::ImpactDecal& d : live) {
        const float age = static_cast<float>(now - d.start_ms) / 1000.f;
        const float fade = eldenring::cs::decalFade(age, d.life_s);
        if (fade <= 0.f) continue;

        float tangent[3], bitangent[3];
        eldenring::cs::decalTangentBasis(d.normal, tangent, bitangent);
        const float half = 0.08f; // 16 cm across; the authentic texture's hole fills most of it
        const float lift = 0.015f;
        const float center[3] = {d.pos[0] + d.normal[0] * lift, d.pos[1] + d.normal[1] * lift,
                                 d.pos[2] + d.normal[2] * lift};
        ImVec2 q[4];
        float ppm = 0.f;
        bool visible = true;
        for (int i = 0; i < 4 && visible; ++i) {
            const float sx = (i == 0 || i == 3) ? -1.f : 1.f;
            const float sy = (i < 2) ? -1.f : 1.f;
            const float p[3] = {
                center[0] + tangent[0] * half * sx + bitangent[0] * half * sy,
                center[1] + tangent[1] * half * sx + bitangent[1] * half * sy,
                center[2] + tangent[2] * half * sx + bitangent[2] * half * sy,
            };
            visible = ProjectToScreen(g_fx_vp, p, w, h, g_fx_fov, q[i], ppm);
        }
        if (!visible) continue;

        const ImVec2 c{(q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f,
                       (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f};
        const float span = std::max(std::fabs(q[0].x - q[2].x), std::fabs(q[0].y - q[2].y));
        if (g_decal_ready) {
            // Authentic CS2 concrete bullet-hole decal, extracted from the user's own VPK.
            const ImTextureID tex = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(g_decal_gpu.ptr));
            const ImU32 col = IM_COL32(225, 225, 225, static_cast<int>(255.f * fade));
            if (span < 4.f) {
                // Too far / too grazing to resolve the quad: draw the texture at a minimum readable size.
                const float s = std::max(4.f, span * 1.5f);
                dl->AddImage(tex, ImVec2{c.x - s, c.y - s}, ImVec2{c.x + s, c.y + s},
                             ImVec2{0.f, 0.f}, ImVec2{1.f, 1.f}, col);
            } else {
                dl->AddImageQuad(tex, q[0], q[1], q[2], q[3], ImVec2{0.f, 0.f}, ImVec2{1.f, 0.f},
                                 ImVec2{1.f, 1.f}, ImVec2{0.f, 1.f}, col);
            }
        } else if (span < 4.f) {
            // Procedural fallback when the extracted CS2 texture is unavailable.
            dl->AddCircleFilled(c, 4.f, IM_COL32(190, 178, 155, static_cast<int>(85.f * fade)), 8);
            dl->AddCircleFilled(c, 2.5f, IM_COL32(12, 10, 9, static_cast<int>(235.f * fade)), 8);
        } else {
            const auto shrink = [&](float s, ImVec2 out[4]) {
                for (int i = 0; i < 4; ++i) out[i] = ImVec2{c.x + (q[i].x - c.x) * s, c.y + (q[i].y - c.y) * s};
            };
            // Dust ring, scorch, then the dark hole.
            dl->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(190, 178, 155, static_cast<int>(80.f * fade)));
            ImVec2 scorch[4];
            shrink(0.62f, scorch);
            dl->AddQuadFilled(scorch[0], scorch[1], scorch[2], scorch[3],
                              IM_COL32(48, 40, 32, static_cast<int>(150.f * fade)));
            ImVec2 hole[4];
            shrink(0.30f, hole);
            dl->AddQuadFilled(hole[0], hole[1], hole[2], hole[3],
                              IM_COL32(12, 10, 9, static_cast<int>(230.f * fade)));
            for (int i = 0; i < 4; ++i) {
                const ImVec2 e{c.x + (q[i].x - c.x) * 0.9f, c.y + (q[i].y - c.y) * 0.9f};
                dl->AddLine(c, e, IM_COL32(32, 27, 22, static_cast<int>(95.f * fade)), 1.f);
            }
        }
    }
}
#endif

void DrawFx(ImDrawList* dl, ImTextureID atlas, float w, float h) {
    if (!g_fx_valid || !g_atlas_ready) return;
    for (const eldenring::fx::Particle& p : g_particles.alive()) {
        ImVec2 s;
        float ppm = 0.f;
        if (!ProjectToScreen(g_fx_vp, p.pos, w, h, g_fx_fov, s, ppm)) continue;
        const float half = std::max(2.f, p.size * ppm * 0.5f);
        const float fade = eldenring::fx::ParticleSystem::fade(p);
        if (p.kind == eldenring::fx::Kind::Spark) {
            dl->AddCircleFilled(s, std::max(1.f, half), IM_COL32(255, 220, 140, static_cast<int>(255 * fade)), 6);
            continue;
        }
        if (p.kind == eldenring::fx::Kind::Dust) {
            dl->AddCircleFilled(s, std::max(1.5f, half), IM_COL32(175, 165, 148, static_cast<int>(65 * fade)), 8);
            dl->AddCircleFilled(s, std::max(1.f, half * 0.55f), IM_COL32(205, 195, 178, static_cast<int>(55 * fade)), 8);
            continue;
        }
        if (p.kind == eldenring::fx::Kind::Firework) {
            dl->AddCircleFilled(s, std::max(1.f, half), IM_COL32(255, 220, 140, static_cast<int>(255 * fade)), 6);
            continue;
        }
        if (p.kind == eldenring::fx::Kind::Riptide) {
            dl->AddCircleFilled(s, std::max(1.f, half), IM_COL32(110, 200, 255, static_cast<int>(220 * fade)), 8);
            continue;
        }
        const mc::hud::HudUV* uv = nullptr;
        ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(255 * std::min(1.f, fade * 1.5f)));
        switch (p.kind) {
            case eldenring::fx::Kind::Crit: uv = &mc::hud::kUV_PARTICLE_CRIT; break;
            case eldenring::fx::Kind::Damage: uv = &mc::hud::kUV_PARTICLE_DAMAGE; break;
            case eldenring::fx::Kind::Sweep: {
                static const mc::hud::HudUV* const frames[8] = {&mc::hud::kUV_PARTICLE_SWEEP_0, &mc::hud::kUV_PARTICLE_SWEEP_1, &mc::hud::kUV_PARTICLE_SWEEP_2,
                                                                  &mc::hud::kUV_PARTICLE_SWEEP_3, &mc::hud::kUV_PARTICLE_SWEEP_4, &mc::hud::kUV_PARTICLE_SWEEP_5,
                                                                  &mc::hud::kUV_PARTICLE_SWEEP_6, &mc::hud::kUV_PARTICLE_SWEEP_7};
                uv = frames[std::clamp(p.frame, 0, 7)];
                col = IM_COL32(255, 255, 255, 255);
                break;
            }
            // Firework and Riptide are drawn as ImGui circles above, so they never reach this atlas switch.
            case eldenring::fx::Kind::Firework:
            case eldenring::fx::Kind::Riptide: break;
        }
        dl->AddImage(atlas, {s.x - half, s.y - half}, {s.x + half, s.y + half}, {uv->u0, uv->v0}, {uv->u1, uv->v1}, col);
    }
}

// An item's icon in a slot: blocks are drawn as the cube Minecraft shows (three textured faces, see eldenring_blockicon.hpp), everything
// else as its flat sprite.
void DrawItemIcon(ImDrawList* dl, ImTextureID atlas, const mc::HudRect& r, mc::ItemId item) {
    const mc::BlockId block = eldenring::blocks::blockForItem(item);
    if (block != mc::BlockId::Air) {
        const eldenring::blocks::IconCube icon = eldenring::blocks::blockIcon(block);
        for (int i = 0; i < icon.count; ++i) {
            const eldenring::blocks::IconFace& f = icon.faces[i];
            const ImVec2 q[4] = {{r.x + f.p[0][0] * r.w, r.y + f.p[0][1] * r.h}, {r.x + f.p[1][0] * r.w, r.y + f.p[1][1] * r.h},
                                 {r.x + f.p[2][0] * r.w, r.y + f.p[2][1] * r.h}, {r.x + f.p[3][0] * r.w, r.y + f.p[3][1] * r.h}};
            const int c = static_cast<int>(255.f * f.shade);
            dl->AddImageQuad(atlas, q[0], q[1], q[2], q[3], {f.uv.u0, f.uv.v0}, {f.uv.u1, f.uv.v0}, {f.uv.u1, f.uv.v1}, {f.uv.u0, f.uv.v1}, IM_COL32(c, c, c, 255));
        }
        return;
    }
    if (const mc::hud::HudUV* uv = eldenring::render::uvForItem(item)) dl->AddImage(atlas, {r.x, r.y}, {r.x + r.w, r.y + r.h}, {uv->u0, uv->v0}, {uv->u1, uv->v1});
}

// The inventory screen: Minecraft's 3-row container (item palette on top, the player's slots below), the stack on the cursor,
// a tooltip and our own mouse pointer.
void DrawInventory(const HudState& hud, float w, float h) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImTextureID atlas = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(g_atlas_gpu.ptr));
    dl->AddRectFilled({0.f, 0.f}, {w, h}, IM_COL32(16, 16, 16, 150)); // Minecraft dims the world behind a screen
    const mc::InventoryLayout lay(w, h, hud.creative);
    const mc::HudRect p = lay.panel();
    const float gs = static_cast<float>(lay.scale());
    const float fs = 8.f * gs;
    const ImU32 label = IM_COL32(64, 64, 64, 255);

    auto drawSlotBox = [&](const mc::HudRect& r) {
        dl->AddLine({r.x, r.y}, {r.x + r.w, r.y}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({r.x, r.y}, {r.x, r.y + r.h}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({r.x, r.y + r.h}, {r.x + r.w, r.y + r.h}, IM_COL32(255, 255, 255, 200), gs);
        dl->AddLine({r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, IM_COL32(255, 255, 255, 200), gs);
        dl->AddRectFilled({r.x + gs, r.y + gs}, {r.x + r.w - gs, r.y + r.h - gs}, IM_COL32(139, 139, 139, 255));
    };

    auto stackCount = [&](const mc::HudRect& r, unsigned n) {
        if (n <= 1) return;
        char cnt[8];
        snprintf(cnt, sizeof(cnt), "%u", n);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
        const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
        dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
        dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
    };
    auto icon = [&](const mc::HudRect& r, mc::ItemId item) { DrawItemIcon(dl, atlas, r, item); };

    // 1. Draw Container Base Panel (176 x 166)
    dl->AddRectFilled({p.x, p.y}, {p.x + p.w, p.y + p.h}, IM_COL32(198, 198, 198, 255));
    dl->AddLine({p.x, p.y}, {p.x + p.w, p.y}, IM_COL32(255, 255, 255, 255), 2.f * gs);
    dl->AddLine({p.x, p.y}, {p.x, p.y + p.h}, IM_COL32(255, 255, 255, 255), 2.f * gs);
    dl->AddLine({p.x, p.y + p.h}, {p.x + p.w, p.y + p.h}, IM_COL32(85, 85, 85, 255), 2.f * gs);
    dl->AddLine({p.x + p.w, p.y}, {p.x + p.w, p.y + p.h}, IM_COL32(85, 85, 85, 255), 2.f * gs);

    dl->AddText(ImGui::GetFont(), fs, {p.x, p.y - 10.f * gs}, IM_COL32(255, 255, 255, 230), hud.creative ? "Creative Mode" : "Survival Mode");
    if (!hud.creative && hud.armor_points > 0) {
        char arm_label[48];
        const float red = std::min(80.f, static_cast<float>(hud.armor_points) * 4.0f);
        snprintf(arm_label, sizeof(arm_label), "Armor: %d (-%.0f%%)", hud.armor_points, red);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, arm_label);
        const ImVec2 pos{p.x + p.w - sz.x - 2.f * gs, p.y - 10.f * gs};
        dl->AddText(ImGui::GetFont(), fs, {pos.x + 1.f, pos.y + 1.f}, IM_COL32(0, 0, 0, 200), arm_label);
        dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(85, 255, 255, 255), arm_label);
    }

    const auto& palette = mc::paletteItems();

    if (hud.creative) {
        // Creative mode palette on top
        dl->AddText(ImGui::GetFont(), fs, {lay.titleItems().x, lay.titleItems().y}, label, "Items");
        for (int i = 0; i < mc::InventoryLayout::kPaletteSlots; ++i) {
            drawSlotBox(lay.paletteSlot(i));
        }
        for (size_t i = 0; i < palette.size() && i < mc::InventoryLayout::kPaletteSlots; ++i) {
            icon(lay.paletteSlot(static_cast<int>(i)), palette[i]);
        }
    } else {
        // Vanilla Minecraft Survival layout:
        // Central 3D Player Preview Window (26, 8, 51, 72)
        const mc::HudRect prev = lay.playerPreview();
        dl->AddRectFilled({prev.x, prev.y}, {prev.x + prev.w, prev.y + prev.h}, IM_COL32(16, 16, 16, 255));
        dl->AddLine({prev.x, prev.y}, {prev.x + prev.w, prev.y}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({prev.x, prev.y}, {prev.x, prev.y + prev.h}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({prev.x, prev.y + prev.h}, {prev.x + prev.w, prev.y + prev.h}, IM_COL32(255, 255, 255, 180), gs);
        dl->AddLine({prev.x + prev.w, prev.y}, {prev.x + prev.w, prev.y + prev.h}, IM_COL32(255, 255, 255, 180), gs);

        // Queue real 3D Steve character rendering inside the preview window
        g_inv_preview.active = true;
        g_inv_preview.rect = prev;
        g_inv_preview.mouse_x = hud.mouse_x;
        g_inv_preview.mouse_y = hud.mouse_y;
        g_inv_preview.scale = gs;
        const int sel = std::clamp(hud.selected_slot, 0, 8);
        g_inv_preview.held_item = static_cast<uint16_t>(hud.hotbar[sel]);
        g_inv_preview.offhand_item = static_cast<uint16_t>(hud.offhand_item);

        dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            if (!g_inv_preview.active || !g_steve.ready() || g_current_rtv.ptr == 0) return;
            const float cx = g_inv_preview.rect.x + g_inv_preview.rect.w * 0.5f;
            const float cy = g_inv_preview.rect.y + g_inv_preview.rect.h * 0.5f;
            float look_yaw = 0.f, look_pitch = 0.f, body_yaw = 0.f;
            eldenring::preview::computePreviewTracking(g_inv_preview.mouse_x, g_inv_preview.mouse_y, cx, cy,
                                                       g_inv_preview.scale, look_yaw, look_pitch, body_yaw);
            const auto parts = eldenring::preview::computePreviewPartMatrices(look_yaw, look_pitch, body_yaw);
            const mc::rig::Mat4 vp = eldenring::preview::computePreviewViewProjection(g_inv_preview.rect.w / g_inv_preview.rect.h);

            const D3D12_VIEWPORT pvp{g_inv_preview.rect.x, g_inv_preview.rect.y, g_inv_preview.rect.w, g_inv_preview.rect.h, 0.f, 1.f};
            const D3D12_RECT psc{static_cast<LONG>(g_inv_preview.rect.x), static_cast<LONG>(g_inv_preview.rect.y),
                                static_cast<LONG>(g_inv_preview.rect.x + g_inv_preview.rect.w),
                                static_cast<LONG>(g_inv_preview.rect.y + g_inv_preview.rect.h)};

            SteveParams sp;
            sp.mode = 0.f; // 0: no scene occlusion
            sp.depth_const = 0.0501f;
            sp.rel_bias = 0.f;
            sp.abs_bias = 0.f;
            sp.tint[0] = sp.tint[1] = sp.tint[2] = sp.tint[3] = 0.f;
            sp.keep_depth = false;
            sp.held_item = g_inv_preview.held_item;
            sp.offhand_item = g_inv_preview.offhand_item;
            sp.shield_blocking = false;
            sp.block_offhand = false;
            sp.held_table = g_atlas_ready ? g_held_table_gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};

            g_steve.drawInventoryPreview(g_s.list, g_s.srv_heap, g_depth_gpu, pvp, psc, vp, parts, sp, g_current_rtv);
        }, nullptr);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

        // 2x2 Crafting grid and result
        dl->AddText(ImGui::GetFont(), fs, {lay.titleCrafting().x, lay.titleCrafting().y}, label, "Crafting");
        for (int i = 0; i < 4; ++i) drawSlotBox(lay.craftSlot(i));
        drawSlotBox(lay.craftResultSlot());
        // Crafting arrow
        dl->AddLine({p.x + 135.f * gs, p.y + 37.f * gs}, {p.x + 148.f * gs, p.y + 37.f * gs}, IM_COL32(139, 139, 139, 255), 2.f * gs);
        dl->AddTriangleFilled({p.x + 148.f * gs, p.y + 33.f * gs}, {p.x + 148.f * gs, p.y + 41.f * gs}, {p.x + 152.f * gs, p.y + 37.f * gs}, IM_COL32(139, 139, 139, 255));

        // 2. 4 Armor slots on the left (x = 8, y = 8, 26, 44, 62)
        for (int i = 0; i < 4; ++i) {
            const mc::HudRect arm = lay.armorSlot(i);
            drawSlotBox(arm);
            const mc::HudRect arm_item{arm.x + 1.f * gs, arm.y + 1.f * gs, 16.f * gs, 16.f * gs};
            if (hud.armor_item[i] == 0) {
                if (g_atlas_ready) {
                    if (const mc::hud::HudUV* wuv = eldenring::render::uvForEmptyArmorSlot(static_cast<mc::ArmorSlot>(i))) {
                        dl->AddImage(atlas, {arm_item.x, arm_item.y}, {arm_item.x + arm_item.w, arm_item.y + arm_item.h},
                                     {wuv->u0, wuv->v0}, {wuv->u1, wuv->v1}, IM_COL32(255, 255, 255, 90));
                    }
                }
            } else {
                icon(arm_item, static_cast<mc::ItemId>(hud.armor_item[i]));
                stackCount(arm_item, hud.armor_count[i]);
            }
        }

        // 3. Offhand slot in inventory (x = 77, y = 62)
        const mc::HudRect off = lay.offhandSlot();
        drawSlotBox(off);
        const mc::HudRect off_item{off.x + 1.f * gs, off.y + 1.f * gs, 16.f * gs, 16.f * gs};
        if (hud.offhand_item == 0) {
            if (g_atlas_ready) {
                const mc::hud::HudUV& suv = mc::hud::kUV_ITEM_SHIELD;
                dl->AddImage(atlas, {off_item.x, off_item.y}, {off_item.x + off_item.w, off_item.y + off_item.h},
                             {suv.u0, suv.v0}, {suv.u1, suv.v1}, IM_COL32(255, 255, 255, 90));
            }
        } else {
            icon(off_item, static_cast<mc::ItemId>(hud.offhand_item));
            stackCount(off_item, hud.offhand_count);
        }
    }

    dl->AddText(ImGui::GetFont(), fs, {lay.titleInventory().x, lay.titleInventory().y}, label, "Inventory");

    // 4. Main Inventory (3x9) and Hotbar (1x9)
    for (int i = 0; i < 36; ++i) {
        const mc::HudRect r = lay.invSlot(i);
        drawSlotBox(r);
        const mc::ItemId item = static_cast<mc::ItemId>(hud.inv_item[i]);
        if (item != mc::ItemId::None) {
            const mc::HudRect item_r{r.x + 1.f * gs, r.y + 1.f * gs, 16.f * gs, 16.f * gs};
            icon(item_r, item);
            stackCount(item_r, hud.inv_count[i]);
        }
    }

    // 5. Hover: slot highlight and tooltip
    const mc::SlotRef hover = lay.hitTest(hud.mouse_x, hud.mouse_y);
    mc::ItemId hovered = mc::ItemId::None;
    mc::HudRect hover_r{};
    bool show_hover = false;
    if (hover.kind == mc::SlotRef::Kind::Palette && static_cast<size_t>(hover.index) < palette.size()) {
        hovered = palette[static_cast<size_t>(hover.index)];
        hover_r = lay.paletteSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Inventory && hover.index >= 0 && hover.index < 36) {
        hovered = static_cast<mc::ItemId>(hud.inv_item[hover.index]);
        hover_r = lay.invSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Offhand) {
        hovered = static_cast<mc::ItemId>(hud.offhand_item);
        hover_r = lay.offhandSlot();
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Armor && hover.index >= 0 && hover.index < 4) {
        hovered = static_cast<mc::ItemId>(hud.armor_item[hover.index]);
        hover_r = lay.armorSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Crafting && hover.index >= 0 && hover.index < 4) {
        hover_r = lay.craftSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::CraftResult) {
        hover_r = lay.craftResultSlot();
        show_hover = true;
    }
    if (show_hover) {
        dl->AddRectFilled({hover_r.x, hover_r.y}, {hover_r.x + hover_r.w, hover_r.y + hover_r.h}, IM_COL32(255, 255, 255, 128));
    }

    // the stack on the cursor is drawn centred on the pointer, above everything
    const mc::ItemId held = static_cast<mc::ItemId>(hud.cursor_item);
    if (held != mc::ItemId::None) {
        const mc::HudRect r{hud.mouse_x - 8.f * gs, hud.mouse_y - 8.f * gs, 16.f * gs, 16.f * gs};
        icon(r, held);
        stackCount(r, hud.cursor_count);
    } else if (hovered != mc::ItemId::None) {
        const char* name = mc::HudEngine::getItemDisplayName(hovered);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, name);
        const float pad = 3.f * gs;
        const float tx = std::min(hud.mouse_x + 12.f * gs, w - sz.x - 2.f * pad), ty = hud.mouse_y - 12.f * gs;
        dl->AddRectFilled({tx - pad, ty - pad}, {tx + sz.x + pad, ty + sz.y + pad}, IM_COL32(16, 0, 16, 230));
        dl->AddRect({tx - pad, ty - pad}, {tx + sz.x + pad, ty + sz.y + pad}, IM_COL32(80, 0, 160, 255), 0.f, 0, gs);
        dl->AddText(ImGui::GetFont(), fs, {tx, ty}, IM_COL32(255, 255, 255, 255), name);
    }
    // the pointer (the game's own cursor is not shown while the mouse is captured)
    const float u = std::max(1.f, gs);
    const ImVec2 m{hud.mouse_x, hud.mouse_y};
    dl->AddTriangleFilled(m, {m.x, m.y + 11.f * u}, {m.x + 7.f * u, m.y + 8.f * u}, IM_COL32(255, 255, 255, 255));
    dl->AddTriangle(m, {m.x, m.y + 11.f * u}, {m.x + 7.f * u, m.y + 8.f * u}, IM_COL32(0, 0, 0, 255), 1.f);
}


void DrawHud(const HudState& hud, float w, float h) {
    if (hud.ow_mode) {
        // Overwatch HUD: crosshair + ability legend. The MC hearts/hotbar must
        // not leak into OW mode (the CS branch returns for the same reason).
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float s = std::max(0.5f, h / 1080.f);
        const ImU32 cyan = IM_COL32(120, 255, 220, 235);
        const ImU32 shadow = IM_COL32(0, 0, 0, 190);
        dl->AddCircleFilled({cx, cy}, 1.8f * s, shadow);
        dl->AddCircleFilled({cx, cy}, 1.2f * s, cyan);
        const float gap = 7.f * s, len = 9.f * s, th = 2.f * s;
        auto tick = [&](float x0, float y0, float x1, float y1) {
            dl->AddLine({x0, y0}, {x1, y1}, shadow, th + 2.f * s);
            dl->AddLine({x0, y0}, {x1, y1}, cyan, th);
        };
        tick(cx - gap - len, cy, cx - gap, cy);
        tick(cx + gap, cy, cx + gap + len, cy);
        tick(cx, cy - gap - len, cx, cy - gap);
        tick(cx, cy + gap, cx, cy + gap + len);

        // Hit/kill feedback from the DamageQueue: the same diagonal ticks the
        // CS branch uses. Without this an OW punch deals damage but the screen
        // gives no confirmation.
        if (hud.hit > 0.f) {
            const float a = std::clamp(hud.hit, 0.f, 1.f);
            const float hit_gap = (hud.hit_crit ? 8.f : 6.f) * s, hit_len = (hud.hit_crit ? 14.f : 10.f) * s;
            const ImU32 col = hud.hit_crit ? IM_COL32(255, 170, 0, static_cast<int>(255 * a))
                                           : IM_COL32(255, 255, 255, static_cast<int>(255 * a));
            const float d = 0.70710678f;
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    dl->AddLine({cx + sx * hit_gap * d, cy + sy * hit_gap * d},
                                {cx + sx * (hit_gap + hit_len) * d, cy + sy * (hit_gap + hit_len) * d},
                                col, 3.f * s);
                }
            }
        }
        if (hud.kill > 0.f) {
            const float a = std::clamp(hud.kill, 0.f, 1.f);
            const float kill_gap = 11.f * s, kill_len = 18.f * s;
            const ImU32 col = IM_COL32(225, 40, 40, static_cast<int>(255 * a));
            const float d = 0.70710678f;
            dl->AddLine({cx - kill_gap * d, cy - kill_gap * d}, {cx + kill_len * d, cy + kill_len * d}, col, 3.f * s);
            dl->AddLine({cx + kill_gap * d, cy - kill_gap * d}, {cx - kill_len * d, cy + kill_len * d}, col, 3.f * s);
        }

        const char* legend[4] = {"[LMB] Hand Cannon", "[RMB] Rocket Punch", "[Q] Seismic Slam", "[E] Rising Uppercut"};
        const float fs = 15.f * s;
        for (int i = 0; i < 4; ++i) {
            const float px = 24.f * s;
            const float py = h - (110.f - i * 24.f) * s;
            dl->AddText(ImGui::GetFont(), fs, {px + s, py + s}, IM_COL32(0, 0, 0, 200), legend[i]);
            dl->AddText(ImGui::GetFont(), fs, {px, py}, cyan, legend[i]);
        }
        return;
    }
#if ENABLE_CS2_VIEWMODEL
    if (hud.cs_mode) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float cx = w * 0.5f, cy = h * 0.5f;
        const ImU32 green = IM_COL32(0, 255, 120, 240);
        const ImU32 shadow = IM_COL32(0, 0, 0, 200);
        const float gap = 5.f, len = 9.f, th = 2.f;

        // Center dot
        dl->AddRectFilled({cx - 1.f, cy - 1.f}, {cx + 2.f, cy + 2.f}, shadow);
        dl->AddRectFilled({cx, cy}, {cx + 1.f, cy + 1.f}, green);

        auto line = [&](float x0, float y0, float x1, float y1) {
            dl->AddLine({x0, y0}, {x1, y1}, shadow, th + 2.f);
            dl->AddLine({x0, y0}, {x1, y1}, green, th);
        };
        line(cx - gap - len, cy, cx - gap, cy);
        line(cx + gap, cy, cx + gap + len, cy);
        line(cx, cy - gap - len, cx, cy - gap);
        line(cx, cy + gap, cx, cy + gap + len);

        // Hit/kill feedback from the DamageQueue: four diagonal ticks, a red X on a kill.
        const float hs = std::max(0.5f, h / 1080.f);
        if (hud.hit > 0.f) {
            const float a = std::clamp(hud.hit, 0.f, 1.f);
            const float hit_gap = (hud.hit_crit ? 7.f : 5.f) * hs, hit_len = (hud.hit_crit ? 12.f : 8.f) * hs;
            const ImU32 col = hud.hit_crit ? IM_COL32(255, 170, 0, static_cast<int>(255 * a)) : IM_COL32(255, 255, 255, static_cast<int>(255 * a));
            const float s = 0.70710678f;
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    dl->AddLine({cx + sx * hit_gap * s, cy + sy * hit_gap * s}, {cx + sx * (hit_gap + hit_len) * s, cy + sy * (hit_gap + hit_len) * s}, col,
                                2.5f * hs);
                }
            }
        }
        if (hud.kill > 0.f) {
            const float a = std::clamp(hud.kill, 0.f, 1.f);
            const float kill_gap = 9.f * hs, kill_len = 16.f * hs;
            const ImU32 col = IM_COL32(225, 40, 40, static_cast<int>(255 * a));
            const float s = 0.70710678f;
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    dl->AddLine({cx + sx * kill_gap * s, cy + sy * kill_gap * s}, {cx + sx * (kill_gap + kill_len) * s, cy + sy * (kill_gap + kill_len) * s}, col,
                                3.5f * hs);
                }
            }
        }

        // Weapon Card in bottom right
        const float card_w = 270.f, card_h = 105.f;
        const float x = w - card_w - 25.f, y = h - card_h - 25.f;
        dl->AddRectFilled({x, y}, {x + card_w, y + card_h}, IM_COL32(12, 18, 26, 220), 6.f);
        dl->AddRect({x, y}, {x + card_w, y + card_h}, IM_COL32(0, 255, 140, 230), 6.f, 0, 2.0f);

        auto& r_mode = eldenring::RuntimeContext::Instance().mode();
        const uint8_t state = r_mode.cs_knife_state.load();
        const uint8_t slot = r_mode.cs_weapon_slot.load();
        const bool is_gun = slot != 0;
        const char* weapon_name = (slot == 1) ? "AK-47 (CS2)"
                                : (slot == 2) ? "AWP (CS2)"
                                : (slot == 3) ? "M4A1-S (CS2)"
                                : "BUTTERFLY KNIFE (CS2)";
        const char* status_str = (state == 1) ? "[INSPECTING...]" :
                                 (state == 2) ? (is_gun ? "[FIRING...]" : "[LIGHT ATTACK]") :
                                 (state == 3) ? (is_gun ? "[SECONDARY]" : "[HEAVY ATTACK]") :
                                 (state == 4) ? "[DEPLOYING...]" :
                                 (state == 5) ? "[RELOADING...]" :
                                 (state == 6) ? "[BACKSTAB]" : "[READY]";
        dl->AddText(ImGui::GetFont(), 19.f, {x + 14.f, y + 10.f}, IM_COL32(0, 255, 150, 255), weapon_name);
        dl->AddText(ImGui::GetFont(), 15.f, {x + 14.f, y + 36.f}, IM_COL32(255, 215, 0, 255), status_str);
        if (is_gun) {
            char ammo_str[64];
            snprintf(ammo_str, sizeof(ammo_str), "%u / %u",
                     static_cast<unsigned>(r_mode.cs_ammo.load()),
                     static_cast<unsigned>(r_mode.cs_reserve.load()));
            dl->AddText(ImGui::GetFont(), 22.f, {x + card_w - 96.f, y + 10.f}, IM_COL32(255, 255, 255, 245), ammo_str);
            dl->AddText(ImGui::GetFont(), 13.f, {x + 14.f, y + 60.f}, IM_COL32(200, 210, 225, 230), "[F] Inspect | [LMB] Fire | [R] Reload");
            dl->AddText(ImGui::GetFont(), 12.f, {x + 14.f, y + 80.f}, IM_COL32(160, 175, 195, 210), "[1]AK [2]AWP [3]Knife [4]M4 | [F1] Toggle");
        } else {
            dl->AddText(ImGui::GetFont(), 13.f, {x + 14.f, y + 60.f}, IM_COL32(200, 210, 225, 230), "[F] Spin Inspect  |  [LMB] Slash");
            dl->AddText(ImGui::GetFont(), 13.f, {x + 14.f, y + 80.f}, IM_COL32(160, 175, 195, 210), "[RMB] Heavy Attack  |  [F1] Mode Toggle");
        }
        return;
    }
#endif
    if (hud.inv_open) {
        DrawInventory(hud, w, h);
        return;
    }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float scale = std::max(0.5f, h / 1080.f);
    const float cell = 20.f * scale;
    const float gap = 2.f * scale;
    const float slot = 44.f * scale;
    const float hotbar_w = 9.f * slot;
    const float x0 = (w - hotbar_w) * 0.5f;
    const float y_bar = h - slot - 8.f * scale;
    const mc::HudLayout layout(w, h);
    const ImTextureID atlas = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(g_atlas_gpu.ptr));
    auto sprite = [&](const mc::HudRect& r, const mc::hud::HudUV& uv) {
        dl->AddImage(atlas, {r.x, r.y}, {r.x + r.w, r.y + r.h}, {uv.u0, uv.v0}, {uv.u1, uv.v1});
    };
    const float ratio_hp = hud.max_hp > 0 ? std::clamp(static_cast<float>(hud.hp) / static_cast<float>(hud.max_hp), 0.f, 1.f) : 0.f;
    const int hp_halves = static_cast<int>(std::ceil(ratio_hp * 20.f));
    if (g_atlas_ready) {
        // Real Minecraft sprites at vanilla positions (mc::HudLayout), the Minecraft GUI scale of this resolution.
        sprite(layout.hotbar(), mc::hud::kUV_HOTBAR);
        for (int i = 0; i < 9; ++i) DrawItemIcon(dl, atlas, layout.item(i), static_cast<mc::ItemId>(hud.hotbar[i]));
        sprite(layout.selection(std::clamp(hud.selected_slot, 0, 8)), mc::hud::kUV_HOTBAR_SELECTION);
        // Offhand slot (to the left of hotbar, vanilla hotbar_offhand_left sprite)
        const float gs = static_cast<float>(layout.scale());
        if (hud.offhand_item != 0) {
            sprite(layout.offhand(), mc::hud::kUV_HOTBAR_OFFHAND_LEFT);
            DrawItemIcon(dl, atlas, layout.offhandItem(), static_cast<mc::ItemId>(hud.offhand_item));
            if (hud.offhand_count > 1) {
                char cnt[8];
                snprintf(cnt, sizeof(cnt), "%u", static_cast<unsigned>(hud.offhand_count));
                const float fs = layout.textSize();
                const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
                const mc::HudRect r = layout.offhandItem();
                const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
                dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
                dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
            }
        }
        // The hearts as Gui.renderHearts draws them: the container blinks after a hit and the hearts just lost show white, one heart jumps
        // while Regeneration is active, and the row shakes at two hearts or less.
        const int abs_halves = static_cast<int>(std::lround(hud.absorption_mc));
        g_hearts.update(ImGui::GetIO().DeltaTime, hp_halves, abs_halves, hud.regen);
        const bool blink = g_hearts.blink();
        const int shown_before = g_hearts.displayHealth();
        for (int i = 0; !hud.creative && i < 10; ++i) { // creative mode shows no health, hunger or experience
            mc::HudRect r = layout.heart(i);
            if (g_hearts.regenIndex() == i) r.y -= 2.f * gs;
            r.y += static_cast<float>(g_hearts.shake(i)) * gs;
            sprite(r, blink ? mc::hud::kUV_HEART_CONTAINER_BLINKING : mc::hud::kUV_HEART_CONTAINER);
            if (blink) {
                if (2 * i + 1 < shown_before) sprite(r, mc::hud::kUV_HEART_FULL_BLINKING);
                else if (2 * i + 1 == shown_before) sprite(r, mc::hud::kUV_HEART_HALF_BLINKING);
            }
            if (2 * i + 1 < hp_halves) sprite(r, mc::hud::kUV_HEART_FULL);
            else if (2 * i + 1 == hp_halves) sprite(r, mc::hud::kUV_HEART_HALF);
        }
        // hunger: ten icons from the right, shaking once the saturation is used up
        if (!hud.creative) {
            static uint32_t food_rng = 7u;
            static float food_clock = 0.f;
            static int food_shake[10] = {};
            food_clock += ImGui::GetIO().DeltaTime;
            if (food_clock >= 0.05f) {
                food_clock = 0.f;
                for (int& v : food_shake) {
                    food_rng = food_rng * 1664525u + 1013904223u;
                    v = static_cast<int>((food_rng >> 16) % 3u) - 1;
                }
            }
            for (int i = 0; i < 10; ++i) {
                mc::HudRect r = layout.food(i);
                if (hud.food_shaking) r.y += static_cast<float>(food_shake[i]) * gs;
                sprite(r, mc::hud::kUV_HUNGER_CONTAINER);
                if (2 * i + 1 < hud.food) sprite(r, mc::hud::kUV_HUNGER_FULL);
                else if (2 * i + 1 == hud.food) sprite(r, mc::hud::kUV_HUNGER_HALF);
            }
        }
        // experience bar and level number
        if (!hud.creative) {
            const mc::HudRect bar = layout.xpBar();
            sprite(bar, mc::hud::kUV_XP_BAR_BACKGROUND);
            const int px = std::clamp(static_cast<int>(hud.xp_progress * 183.f), 0, 182);
            if (px > 0) {
                const mc::hud::HudUV& uv = mc::hud::kUV_XP_BAR_PROGRESS;
                const float frac = static_cast<float>(px) / 182.f;
                dl->AddImage(atlas, {bar.x, bar.y}, {bar.x + bar.w * frac, bar.y + bar.h}, {uv.u0, uv.v0}, {uv.u0 + (uv.u1 - uv.u0) * frac, uv.v1});
            }
            if (hud.xp_level > 0) {
                char lvl[8];
                snprintf(lvl, sizeof(lvl), "%d", hud.xp_level);
                const float fs = layout.textSize();
                const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, lvl);
                const float tx = layout.xpLevelCentreX() - sz.x * 0.5f, ty = layout.xpLevelTextTop();
                for (const ImVec2 o : {ImVec2{gs, 0.f}, ImVec2{-gs, 0.f}, ImVec2{0.f, gs}, ImVec2{0.f, -gs}}) {
                    dl->AddText(ImGui::GetFont(), fs, {tx + o.x, ty + o.y}, IM_COL32(0, 0, 0, 255), lvl); // the black outline
                }
                dl->AddText(ImGui::GetFont(), fs, {tx, ty}, IM_COL32(0x80, 0xFF, 0x20, 255), lvl);
            }
        }
        // stack sizes (Minecraft: bottom right of the icon, white with a dark shadow, only above 1)
        for (int i = 0; i < 9; ++i) {
            if (hud.hotbar_count[i] <= 1 || hud.hotbar[i] == 0) continue;
            char cnt[8];
            snprintf(cnt, sizeof(cnt), "%u", static_cast<unsigned>(hud.hotbar_count[i]));
            const float fs = layout.textSize();
            const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
            const mc::HudRect r = layout.item(i);
            const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
            dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
            dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
        }
        // absorption (golden apple, totem): golden hearts in a row above the health, 2 Minecraft HP per heart
        if (!hud.creative) {
            const int full = static_cast<int>(std::floor(hud.absorption_mc / 2.f));
            const bool half = hud.absorption_mc - 2.f * static_cast<float>(full) >= 1.f;
            for (int i = 0; i < std::min(10, full + (half ? 1 : 0)); ++i) {
                mc::HudRect r = layout.heart(i);
                r.y -= 10.f * static_cast<float>(layout.scale());
                sprite(r, mc::hud::kUV_HEART_CONTAINER);
                sprite(r, i < full ? mc::hud::kUV_HEART_ABSORB_FULL : mc::hud::kUV_HEART_ABSORB_HALF);
            }
        }
        // armor bar: 10 chestplate icons in a row above the hearts (or above golden absorption hearts)
        if (!hud.creative && hud.armor_points > 0) {
            const bool has_absorb = hud.absorption_mc > 0.f;
            for (int i = 0; i < 10; ++i) {
                const mc::HudRect r = layout.armor(i, has_absorb);
                sprite(r, mc::hud::kUV_ARMOR_EMPTY);
                if (2 * i + 1 < hud.armor_points) {
                    sprite(r, mc::hud::kUV_ARMOR_FULL);
                } else if (2 * i + 1 == hud.armor_points) {
                    sprite(r, mc::hud::kUV_ARMOR_HALF);
                }
            }
        }
    }
    // hotbar
    for (int i = 0; i < 9 && !g_atlas_ready; ++i) {
        const float x = x0 + static_cast<float>(i) * slot;
        dl->AddRectFilled({x, y_bar}, {x + slot, y_bar + slot}, IM_COL32(0, 0, 0, 120));
        const bool sel = i == hud.selected_slot;
        dl->AddRect({x, y_bar}, {x + slot, y_bar + slot}, sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(110, 110, 110, 255),
                    0.f, 0, (sel ? 3.f : 1.5f) * scale);
    }
    // hearts: 10 hearts = 20 half hearts, from the real HP
    const float ratio = hud.max_hp > 0 ? std::clamp(static_cast<float>(hud.hp) / static_cast<float>(hud.max_hp), 0.f, 1.f) : 0.f;
    const int halves = static_cast<int>(std::ceil(ratio * 20.f));
    const float y_hearts = y_bar - cell - 6.f * scale;
    for (int i = 0; i < 10 && !g_atlas_ready && !hud.creative; ++i) {
        const float x = x0 + static_cast<float>(i) * (cell + gap);
        dl->AddRectFilled({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(40, 0, 0, 200));
        const int fill = std::clamp(halves - i * 2, 0, 2);
        if (fill >= 1) {
            const float fw = fill == 2 ? cell : cell * 0.5f;
            dl->AddRectFilled({x, y_hearts}, {x + fw, y_hearts + cell}, IM_COL32(205, 30, 30, 255));
        }
        dl->AddRect({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.5f * scale);
    }
    // armor bar fallback (drawn above hearts when atlas is not present)
    if (!g_atlas_ready && !hud.creative && hud.armor_points > 0) {
        const bool has_absorb = hud.absorption_mc > 0.f;
        const float y_armor = y_hearts - (has_absorb ? 2.f : 1.f) * (cell + 4.f * scale);
        for (int i = 0; i < 10; ++i) {
            const float x = x0 + static_cast<float>(i) * (cell + gap);
            dl->AddRectFilled({x, y_armor}, {x + cell, y_armor + cell}, IM_COL32(35, 35, 35, 200));
            const int fill = std::clamp(hud.armor_points - i * 2, 0, 2);
            if (fill >= 1) {
                const float fw = fill == 2 ? cell : cell * 0.5f;
                dl->AddRectFilled({x, y_armor}, {x + fw, y_armor + cell}, IM_COL32(180, 200, 220, 255));
            }
            dl->AddRect({x, y_armor}, {x + cell, y_armor + cell}, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.5f * scale);
        }
    }
    // crosshair
    const float cx = w * 0.5f, cy = h * 0.5f, arm = 10.f * scale;
    dl->AddLine({cx - arm, cy}, {cx + arm, cy}, IM_COL32(255, 255, 255, 200), 2.f * scale);
    dl->AddLine({cx, cy - arm}, {cx, cy + arm}, IM_COL32(255, 255, 255, 200), 2.f * scale);
    // hit marker: four diagonal ticks around the crosshair (Minecraft style); orange and longer for a critical hit
    if (hud.hit > 0.f) {
        const float a = std::clamp(hud.hit, 0.f, 1.f);
        const float tick_gap = (hud.hit_crit ? 7.f : 5.f) * scale, len = (hud.hit_crit ? 12.f : 8.f) * scale;
        const ImU32 col = hud.hit_crit ? IM_COL32(255, 170, 0, static_cast<int>(255 * a)) : IM_COL32(255, 255, 255, static_cast<int>(255 * a));
        const float s = 0.70710678f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                dl->AddLine({cx + sx * tick_gap * s, cy + sy * tick_gap * s}, {cx + sx * (tick_gap + len) * s, cy + sy * (tick_gap + len) * s}, col,
                            2.5f * scale);
            }
        }
    }
    // kill marker: a red X, bigger and longer-lived
    if (hud.kill > 0.f) {
        const float a = std::clamp(hud.kill, 0.f, 1.f);
        const float tick_gap = 9.f * scale, len = 16.f * scale;
        const ImU32 col = IM_COL32(225, 40, 40, static_cast<int>(255 * a));
        const float s = 0.70710678f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                dl->AddLine({cx + sx * tick_gap * s, cy + sy * tick_gap * s}, {cx + sx * (tick_gap + len) * s, cy + sy * (tick_gap + len) * s}, col,
                            3.5f * scale);
            }
        }
    }
    DrawFx(dl, atlas, w, h);
    if (hud.mode_toast > 0.f) { // F5: the mode name, top left, fading out
        const float a = std::clamp(hud.mode_toast, 0.f, 1.f);
        const float fs = 18.f * scale;
        const ImVec2 pos{16.f * scale, 16.f * scale};
        const char* name = hud.creative ? "Creative Mode" : "Survival Mode";
        dl->AddText(ImGui::GetFont(), fs, {pos.x + scale, pos.y + scale}, IM_COL32(0, 0, 0, static_cast<int>(200 * a)), name);
        dl->AddText(ImGui::GetFont(), fs, pos, hud.creative ? IM_COL32(255, 220, 90, static_cast<int>(255 * a)) : IM_COL32(255, 255, 255, static_cast<int>(255 * a)), name);
    }
    // armor damage reduction live toast (shows damage breakdown when hit)
    if (hud.armor_toast_timer > 0.f) {
        const float a = std::clamp(hud.armor_toast_timer / 3.0f, 0.f, 1.f);
        const float fs = 15.f * scale;
        char msg[128];
        snprintf(msg, sizeof(msg), "[Armor] Raw: %d -> Final: %d (-%.1f%%, %d pts)",
                 hud.armor_toast_raw, hud.armor_toast_final, hud.armor_toast_ratio * 100.f, hud.armor_toast_pts);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, msg);
        const float px = 16.f * scale, py = 42.f * scale;
        dl->AddRectFilled({px - 4.f * scale, py - 2.f * scale}, {px + sz.x + 8.f * scale, py + sz.y + 4.f * scale},
                          IM_COL32(20, 20, 20, static_cast<int>(180 * a)), 4.f * scale);
        dl->AddText(ImGui::GetFont(), fs, {px + scale, py + scale}, IM_COL32(0, 0, 0, static_cast<int>(200 * a)), msg);
        dl->AddText(ImGui::GetFont(), fs, {px, py}, IM_COL32(85, 255, 255, static_cast<int>(255 * a)), msg);
    }
    // eating: a thin bar under the crosshair that fills in 1.6 s
    if (hud.eating > 0.f) {
        const float bw = 60.f * scale, bh = 5.f * scale, bx = cx - bw * 0.5f, by = cy + 22.f * scale;
        dl->AddRectFilled({bx, by}, {bx + bw, by + bh}, IM_COL32(0, 0, 0, 160));
        dl->AddRectFilled({bx, by}, {bx + bw * std::clamp(hud.eating, 0.f, 1.f), by + bh}, IM_COL32(240, 240, 240, 230));
    }
    // totem of undying: the icon swells in the middle of the screen and fades out
    if (hud.totem > 0.f && g_atlas_ready) {
        const float t = std::clamp(hud.totem, 0.f, 1.f);
        const float size = (150.f + 150.f * (1.f - t)) * scale;
        const float a = std::min(1.f, t * 1.6f);
        const mc::hud::HudUV& uv = mc::hud::kUV_ITEM_TOTEM_OF_UNDYING;
        dl->AddImage(atlas, {cx - size * 0.5f, cy - size * 0.5f}, {cx + size * 0.5f, cy + size * 0.5f}, {uv.u0, uv.v0}, {uv.u1, uv.v1},
                     IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
    }
    if (hud.slot_probe >= 0) {
        char pb[48];
        snprintf(pb, sizeof(pb), "PART SLOT %d HIDDEN", hud.slot_probe);
        dl->AddText(ImGui::GetFont(), 36.f * scale, {w * 0.5f - 200.f * scale, 60.f * scale}, IM_COL32(255, 230, 0, 255), pb);
    }
}

void UpdateWorldFx(const SteveState& steve, float dt, float w, float h) {
    // Combat particles: take the queued events, advance them, remember this frame's camera for the projection.
    g_fx_valid = steve.cam_valid;
    if (steve.cam_valid) {
        g_fx_vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / static_cast<float>(g_s.height));
        g_fx_fov = steve.fov_y;
    }
    std::vector<FxEvent> events;
    {
        std::lock_guard<std::mutex> g(g_fx_mutex);
        events.swap(g_fx_events);
    }
    for (const FxEvent& e : events) {
        switch (e.kind) {
            case FxKind::Crit: g_particles.spawnCrit(e.pos, g_fx_rng); break;
            case FxKind::Damage: g_particles.spawnDamage(e.pos, e.count, g_fx_rng); break;
            case FxKind::Sweep: g_particles.spawnSweep(e.pos); break;
            case FxKind::Spark: g_particles.spawnSpark(e.pos, g_fx_rng); break;
            case FxKind::Dust: g_particles.spawnDust(e.pos, g_fx_rng); break;
            case FxKind::Firework: g_particles.spawnFirework(e.pos, e.dir, g_fx_rng); break;
            // `count` doubles as a phase index here: each queued event advances the ring a little.
            case FxKind::Riptide: g_particles.spawnRiptide(e.pos, static_cast<float>(e.count) * 0.1f, g_fx_rng); break;
        }
    }
    g_particles.update(dt);
#if ENABLE_CS2_VIEWMODEL
    DrawDecals(ImGui::GetBackgroundDrawList(), w, h);
#endif
    DrawTracers(ImGui::GetBackgroundDrawList(), w, h);
#if ENABLE_CS2_VIEWMODEL
    DrawCsMuzzleFlash(ImGui::GetBackgroundDrawList(), w, h);
#endif
}

} // namespace detail
} // namespace erov
