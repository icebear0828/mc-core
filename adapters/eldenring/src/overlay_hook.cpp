#include "overlay_internal.hpp"

namespace erov {
namespace detail {

#if ENABLE_CS2_VIEWMODEL
std::string ResolveCsAssetPath(const std::string& path) {
    if (path.empty()) return path;
    const std::filesystem::path p(path);
    if (p.is_absolute() || g_cs_game_dir.empty()) return path;
    return (std::filesystem::path(g_cs_game_dir) / p).string();
}

// Loads the weapon requested by the hotkey dispatcher. The knife is the
// fallback: if a gun asset is missing we keep rendering the current weapon.
void UpdateCsWeaponSlot(uint8_t want_slot) {
    if (want_slot == g_cs_active_slot) return;
    Logf("overlay: CS weapon switch requested -> %u", static_cast<unsigned>(want_slot));

    bool loaded = false;
    if (want_slot == 0) {
        if (!g_cs_knife_model_path.empty() && std::filesystem::exists(g_cs_knife_model_path) &&
            g_cs.loadModel(g_s.device, g_cs_knife_model_path)) {
            loaded = true;
        }
    } else {
        const char* id = (want_slot == 1) ? "weapon_ak47"
                       : (want_slot == 2) ? "weapon_awp"
                       : "weapon_m4a1_silencer";
        if (const auto* found = eldenring::cs::CSWeaponRegistry::instance().findWeapon(id)) {
            auto def = *found;
            def.model_csvm_path = ResolveCsAssetPath(def.model_csvm_path);
            def.albedo_tex_path = ResolveCsAssetPath(def.albedo_tex_path);
            def.normal_tex_path = ResolveCsAssetPath(def.normal_tex_path);
            def.pbr_tex_path = ResolveCsAssetPath(def.pbr_tex_path);
            if (std::filesystem::exists(def.model_csvm_path) && g_cs.loadWeapon(g_s.device, def)) {
                // The gun table is shared by every gun: swap in this weapon's
                // maps unless they are already there. The decoded PNGs stay
                // cached, so a switch only pays the GPU upload.
                if (g_cs_gun_table_slot != want_slot) {
                    const bool any_real = g_cs.loadWeaponTextures(g_s.device, def, g_cs_ak_table_cpu);
                    g_cs_gun_table_slot = want_slot;
                    if (!any_real) {
                        Logf("overlay: CS weapon %u has no texture maps, using fallbacks",
                             static_cast<unsigned>(want_slot));
                    }
                }
                loaded = true;
            }
        }
    }

    if (loaded) {
        g_cs_active_slot = want_slot;
        Logf("overlay: CS weapon slot -> %u", static_cast<unsigned>(want_slot));
    } else {
        // Keep the current weapon and undo the request so we do not retry
        // a missing asset every frame.
        eldenring::RuntimeContext::Instance().mode().cs_weapon_slot.store(g_cs_active_slot);
        Logf("overlay: CS weapon slot %u unavailable, keeping %u",
             static_cast<unsigned>(want_slot), static_cast<unsigned>(g_cs_active_slot));
    }
}
#endif
void WaitForGpu() {
    if (!g_s.queue || !g_s.fence || !g_s.fence_event) return;
    const UINT64 v = ++g_s.fence_counter;
    if (SUCCEEDED(g_s.queue->Signal(g_s.fence, v)) && g_s.fence->GetCompletedValue() < v) {
        g_s.fence->SetEventOnCompletion(v, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 2000);
    }
}

void Teardown() {
    if (g_s.ready) WaitForGpu();
    g_steve.release();
#if ENABLE_CS2_VIEWMODEL
    g_cs.release();
#endif
#if ENABLE_OW_VIEWMODEL
    g_ow.release();
    SafeRelease(g_ow_srv_heap);
    g_ow_table_gpu = {};
    g_ow_initialized = false;
    g_ow_model_loaded = false;
    g_ow_load_failed = false;
#endif
    if (g_s.imgui_ready) {
        ImGui_ImplDX12_Shutdown();
        ImGui::DestroyContext();
        g_s.imgui_ready = false;
    }
    for (Frame& f : g_s.frames) {
        SafeRelease(f.alloc);
        SafeRelease(f.back);
        f.fence_value = 0;
    }
    SafeRelease(g_s.list);
    SafeRelease(g_s.rtv_heap);
    SafeRelease(g_s.srv_heap);
    SafeRelease(g_atlas_tex);
    SafeRelease(g_atlas_upload);
    g_atlas_ready = false;
#if ENABLE_CS2_VIEWMODEL
    SafeRelease(g_decal_tex);
    SafeRelease(g_decal_upload);
    g_decal_ready = false;
#endif
    SafeRelease(g_s.fence);
    if (g_s.fence_event) {
        CloseHandle(g_s.fence_event);
        g_s.fence_event = nullptr;
    }
    SafeRelease(g_s.device);
    SafeRelease(g_s.swap);
    g_s.queue = nullptr; // not owned
    g_s.ready = false;
}

// Looks for the queue pointer inside the swap chain object, to confirm the queue we learned from ExecuteCommandLists
// is the one DXGI presents with (the layout is dxgi.dll internal: only used for the log, never for the decision).
int FindQueueOffsetInSwapChain(IDXGISwapChain* sc, ID3D12CommandQueue* q) {
    const auto* words = reinterpret_cast<const uintptr_t*>(sc);
    for (int i = 0; i < 0x100; ++i) {
        uintptr_t v = 0;
        __try {
            v = words[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
        if (v == reinterpret_cast<uintptr_t>(q)) return i * static_cast<int>(sizeof(uintptr_t));
    }
    return -1;
}

bool Init(IDXGISwapChain* sc, ID3D12CommandQueue* queue) {
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3)))) {
        Logf("overlay: swap chain is not IDXGISwapChain3");
        return false;
    }
    g_s.swap = sc3;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc3->GetDesc(&desc))) return false;
    g_s.buffers = std::min<UINT>(desc.BufferCount, kMaxFrames);
    g_s.width = desc.BufferDesc.Width;
    g_s.height = desc.BufferDesc.Height;
    g_s.format = desc.BufferDesc.Format;
    if (FAILED(sc3->GetDevice(IID_PPV_ARGS(&g_s.device)))) return false;
    // The queue must belong to the same device as the swap chain.
    ID3D12Device* qdev = nullptr;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&qdev))) || qdev != g_s.device) {
        SafeRelease(qdev);
        Logf("overlay: captured queue belongs to another device, waiting for another one");
        SafeRelease(g_s.device);
        SafeRelease(g_s.swap);
        return false;
    }
    SafeRelease(qdev);
    g_s.queue = queue;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = g_s.buffers;
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 64; // 0: ImGui, 1-2: Steve, 3: HUD, 4-5: FP items, 6-7: Held, 8-9: Creeper, 10-11: Zombie, 12-13: Skeleton, 14-15: extra, 16-27: CS2 knife table, 32-43: CS2 gun table
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_s.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&g_s.rtv_heap))) ||
        FAILED(g_s.device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&g_s.srv_heap)))) {
        Logf("overlay: descriptor heaps failed");
        return false;
    }
    const UINT inc = g_s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_s.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < g_s.buffers; ++i) {
        Frame& f = g_s.frames[i];
        if (FAILED(g_s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc))) ||
            FAILED(sc3->GetBuffer(i, IID_PPV_ARGS(&f.back)))) {
            Logf("overlay: per-frame resources failed");
            return false;
        }
        f.rtv = h;
        D3D12_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = g_s.format;
        rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        g_s.device->CreateRenderTargetView(f.back, &rd, f.rtv);
        h.ptr += inc;
    }
    if (FAILED(g_s.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_s.frames[0].alloc, nullptr,
                                             IID_PPV_ARGS(&g_s.list)))) {
        Logf("overlay: command list failed");
        return false;
    }
    g_s.list->Close();
    if (FAILED(g_s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_s.fence)))) return false;
    g_s.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_s.fence_event) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplDX12_Init(g_s.device, static_cast<int>(g_s.buffers), g_s.format, g_s.srv_heap,
                             g_s.srv_heap->GetCPUDescriptorHandleForHeapStart(), g_s.srv_heap->GetGPUDescriptorHandleForHeapStart())) {
        ImGui::DestroyContext();
        Logf("overlay: ImGui_ImplDX12_Init failed");
        return false;
    }
    g_s.imgui_ready = true;
    {
        const UINT srv_inc = g_s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_depth_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
        g_depth_cpu.ptr += srv_inc;
        g_depth_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
        g_depth_gpu.ptr += srv_inc;
        g_steve.setDepthView(g_s.device, nullptr, g_depth_cpu);
        if (!g_steve.init(g_s.device, g_s.format, g_log)) {
            Logf("overlay: Steve renderer unavailable");
        } else {
            D3D12_CPU_DESCRIPTOR_HANDLE skin_cpu = g_depth_cpu;
            skin_cpu.ptr += srv_inc;
            // The skin the loader decoded from mods\mc_adapter\steve.png, or the flat grey-brown that was used before the skin existed.
            std::vector<uint8_t> flat;
            const std::vector<uint8_t>* pixels = &g_skin_rgba;
            unsigned sw = g_skin_w, sh = g_skin_h;
            if (pixels->empty() || sw == 0 || sh == 0) {
                sw = sh = 64;
                flat.resize(static_cast<size_t>(sw) * sh * 4);
                for (size_t k = 0; k < flat.size(); k += 4) {
                    flat[k] = 199;
                    flat[k + 1] = 153;
                    flat[k + 2] = 117;
                    flat[k + 3] = 255;
                }
                pixels = &flat;
                Logf("overlay: Steve skin: none given, flat colour");
            } else {
                char skin_msg[96];
                snprintf(skin_msg, sizeof(skin_msg), "overlay: Steve skin: %ux%u from file", sw, sh);
                Logf(skin_msg);
            }
            if (!g_steve.setSkin(g_s.device, pixels->data(), sw, sh, skin_cpu)) Logf("overlay: Steve skin upload failed");
            g_depth_copy2_cpu = {}; // a new heap: nothing of the old one is valid
            g_mob_table_gpu = {};
            g_mob_tables_gpu.clear();
            g_mob_depth_copy_cpus.clear();
            g_mob_model_gpu = false;

            // Upload all registered mob models to GPU
            for (const auto& [name, model] : g_mob_models) {
                if (g_steve.setMobModel(g_s.device, name, model)) {
                    Logf("overlay: mob model '%s' ready (%zu bones)", name.c_str(), model.bones.size());
                }
            }
            if (g_mob_model_given) {
                g_mob_model_gpu = g_steve.setMobModel(g_s.device, g_mob_model);
                Logf(g_mob_model_gpu ? "overlay: default mob model ready (Bedrock geometry, one mesh per bone)" : "overlay: default mob model upload failed, the mobs use the Steve rig");
            }
            g_mob_registry.setModel(g_mob_model_gpu ? &g_mob_model : nullptr);

            // Mobs list to allocate descriptor tables for
            // Each entry gets a [depth, skin] descriptor table; the props draw their own textures the same way.
            const std::vector<std::string> known_mobs = {"creeper", "zombie", "skeleton", "elytra_wings"};
            size_t slot_idx = 8;
            for (const auto& mob : known_mobs) {
                if (slot_idx + 1 >= 16) break;
                D3D12_CPU_DESCRIPTOR_HANDLE depth_slot = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
                depth_slot.ptr += static_cast<SIZE_T>(srv_inc) * slot_idx;
                D3D12_CPU_DESCRIPTOR_HANDLE skin_slot = depth_slot;
                skin_slot.ptr += srv_inc;

                g_steve.setDepthView(g_s.device, nullptr, depth_slot);
                g_mob_depth_copy_cpus.push_back(depth_slot);

                // Find skin for this mob
                const uint8_t* skin_data = nullptr;
                unsigned mob_sw = 0, mob_sh = 0;
                auto it = g_mob_skins.find(mob);
                if (it != g_mob_skins.end() && !it->second.rgba.empty()) {
                    skin_data = it->second.rgba.data();
                    mob_sw = it->second.w;
                    mob_sh = it->second.h;
                } else if (!g_mob_rgba.empty() && g_mob_w != 0 && g_mob_h != 0) {
                    skin_data = g_mob_rgba.data();
                    mob_sw = g_mob_w;
                    mob_sh = g_mob_h;
                }

                if (skin_data && mob_sw != 0 && mob_sh != 0) {
                    if (g_steve.setMobSkin(g_s.device, mob, skin_data, mob_sw, mob_sh, skin_slot)) {
                        D3D12_GPU_DESCRIPTOR_HANDLE gpu_table = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
                        gpu_table.ptr += static_cast<UINT64>(srv_inc) * slot_idx;
                        g_mob_tables_gpu[mob] = gpu_table;
                        if (g_mob_table_gpu.ptr == 0) {
                            g_mob_table_gpu = gpu_table;
                            g_depth_copy2_cpu = depth_slot;
                        }
                        Logf("overlay: mob skin '%s' ready (%ux%u at slot %zu)", mob.c_str(), mob_sw, mob_sh, slot_idx);
                    }
                }
                slot_idx += 2;
            }
        }
        g_depth_dirty.store(true); // bind the depth captured so far
        CreateAtlasTexture(srv_inc);
#if ENABLE_CS2_VIEWMODEL
        CreateCsDecalTexture(srv_inc);
#endif
#if ENABLE_CS2_VIEWMODEL
        // A build without MC gameplay must never start in MC mode, even if a
        // config file or a missing CS asset would otherwise fall back to it.
        // A combined build keeps the configured startup mode (mc_er_mode.txt).
#if !ENABLE_MC_MODE
        eldenring::RuntimeContext::Instance().mode().mc_mode.store(false);
#endif
        if (!g_cs.init(g_s.device, g_s.format, g_log)) {
            Logf("overlay: CS viewmodel renderer unavailable");
        } else {
            D3D12_CPU_DESCRIPTOR_HANDLE cs_table_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
            cs_table_cpu.ptr += static_cast<SIZE_T>(srv_inc) * 16;
            D3D12_CPU_DESCRIPTOR_HANDLE glove_cpu = cs_table_cpu;
            glove_cpu.ptr += srv_inc;
            D3D12_CPU_DESCRIPTOR_HANDLE ak_table_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
            ak_table_cpu.ptr += static_cast<SIZE_T>(srv_inc) * 32;
            g_cs_weapon_tex_cpu = cs_table_cpu;
            g_cs_glove_tex_cpu = glove_cpu;
            g_cs_knife_table_cpu = cs_table_cpu;
            g_cs_ak_table_cpu = ak_table_cpu;
            g_cs_gun_table_slot = 0;

            D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_base = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
            g_cs_knife_table_gpu = srv_gpu_base;
            g_cs_knife_table_gpu.ptr += static_cast<UINT64>(srv_inc) * 16;
            g_cs_ak_table_gpu = srv_gpu_base;
            g_cs_ak_table_gpu.ptr += static_cast<UINT64>(srv_inc) * 32;

            const std::string& game_dir = eldenring::diagnostics::GetGameDir();
            g_cs_game_dir = game_dir;

            std::vector<std::string> model_candidates;
            if (!game_dir.empty()) {
                model_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "cs_butterfly_viewmodel.bin").string());
                model_candidates.push_back((std::filesystem::path(game_dir) / "cs_butterfly_viewmodel.bin").string());
            }
            model_candidates.push_back("mods\\mc_adapter\\cs_butterfly_viewmodel.bin");
            model_candidates.push_back("cs_butterfly_viewmodel.bin");

            for (const auto& p : model_candidates) {
                if (std::filesystem::exists(p) && g_cs.loadModel(g_s.device, p)) {
                    char cs_msg[256];
                    snprintf(cs_msg, sizeof(cs_msg), "overlay: CS viewmodel model loaded successfully from %s", p.c_str());
                    Logf(cs_msg);
                    g_cs_knife_model_path = p;
                    // Auto-start CS only when no other mode is active. A
                    // combined build keeps whatever mc_er_mode.txt selected
                    // (mc by default), so deploying OW or MC never silently
                    // switches the player into CS.
                    auto& cs_mode_ref = eldenring::RuntimeContext::Instance().mode();
                    if (!cs_mode_ref.ow_mode.load() && !cs_mode_ref.mc_mode.load()) {
                        cs_mode_ref.cs_mode.store(true);
                        cs_mode_ref.first_person.store(true);
                    }
                    break;
                }
            }

            std::vector<std::string> knife_tex_candidates;
            if (!game_dir.empty()) {
                knife_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "knife_butterfly.png").string());
                knife_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "knife_butterfly" / "knife_butterfly_color_psd_5a5a48d7.png").string());
                knife_tex_candidates.push_back((std::filesystem::path(game_dir) / "knife_butterfly.png").string());
            }
            knife_tex_candidates.push_back("mods\\mc_adapter\\knife_butterfly.png");
            knife_tex_candidates.push_back("mods\\mc_adapter\\textures\\knife_butterfly\\knife_butterfly_color_psd_5a5a48d7.png");
            knife_tex_candidates.push_back("knife_butterfly.png");

            // The packed ORM map is produced by tools/cs2/texture_packer.py:
            // R=AO, G=roughness, B=metallic. The raw CS2 `*_rough_psd` map is
            // not an ORM map, so it must never be used as a fallback here.
            std::vector<std::string> knife_norm_candidates;
            std::vector<std::string> knife_pbr_candidates;
            if (!game_dir.empty()) {
                knife_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "knife_butterfly_normal.png").string());
                knife_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "knife_butterfly" / "default_normal_tga_4fc2d3dd.png").string());
                knife_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "knife_butterfly_pbr.png").string());
            }
            knife_norm_candidates.push_back("mods\\mc_adapter\\knife_butterfly_normal.png");
            knife_norm_candidates.push_back("mods\\mc_adapter\\textures\\knife_butterfly\\default_normal_tga_4fc2d3dd.png");
            knife_norm_candidates.push_back("knife_butterfly_normal.png");
            knife_pbr_candidates.push_back("mods\\mc_adapter\\knife_butterfly_pbr.png");
            knife_pbr_candidates.push_back("knife_butterfly_pbr.png");

            std::vector<std::string> glove_tex_candidates;
            if (!game_dir.empty()) {
                glove_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "glove_hardknuckle.png").string());
                glove_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "glove_hardknuckle_black_color_psd_8f00aa00.png").string());
                glove_tex_candidates.push_back((std::filesystem::path(game_dir) / "glove_hardknuckle.png").string());
            }
            glove_tex_candidates.push_back("mods\\mc_adapter\\glove_hardknuckle.png");
            glove_tex_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\glove_hardknuckle_black_color_psd_8f00aa00.png");
            glove_tex_candidates.push_back("glove_hardknuckle.png");

            std::vector<std::string> glove_norm_candidates;
            std::vector<std::string> glove_pbr_candidates;
            if (!game_dir.empty()) {
                glove_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "glove_hardknuckle_normal.png").string());
                glove_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "glove_hardknuckle_normal_tga_a8df881b.png").string());
                glove_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "glove_hardknuckle_pbr.png").string());
            }
            glove_norm_candidates.push_back("mods\\mc_adapter\\glove_hardknuckle_normal.png");
            glove_norm_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\glove_hardknuckle_normal_tga_a8df881b.png");
            glove_norm_candidates.push_back("glove_hardknuckle_normal.png");
            glove_pbr_candidates.push_back("mods\\mc_adapter\\glove_hardknuckle_pbr.png");
            glove_pbr_candidates.push_back("glove_hardknuckle_pbr.png");

            std::string knife_png;
            for (const auto& p : knife_tex_candidates) {
                if (std::filesystem::exists(p)) { knife_png = p; break; }
            }
            std::string glove_png;
            for (const auto& p : glove_tex_candidates) {
                if (std::filesystem::exists(p)) { glove_png = p; break; }
            }

            std::vector<std::string> bare_tex_candidates;
            if (!game_dir.empty()) {
                bare_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm.png").string());
                bare_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "bare_arm_133_color_psd_5b76e0cf.png").string());
                bare_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_133_color_psd_5b76e0cf.png").string());
            }
            bare_tex_candidates.push_back("mods\\mc_adapter\\bare_arm.png");
            bare_tex_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\bare_arm_133_color_psd_5b76e0cf.png");
            bare_tex_candidates.push_back("bare_arm_133_color_psd_5b76e0cf.png");

            std::vector<std::string> bare_norm_candidates;
            if (!game_dir.empty()) {
                bare_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_normal.png").string());
                bare_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "bare_arm_normal_tga_c9fefc2d.png").string());
                bare_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_normal_tga_c9fefc2d.png").string());
            }
            bare_norm_candidates.push_back("mods\\mc_adapter\\bare_arm_normal.png");
            bare_norm_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\bare_arm_normal_tga_c9fefc2d.png");
            bare_norm_candidates.push_back("bare_arm_normal_tga_c9fefc2d.png");

            std::vector<std::string> bare_pbr_candidates;
            if (!game_dir.empty()) {
                bare_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_pbr.png").string());
            }
            bare_pbr_candidates.push_back("mods\\mc_adapter\\bare_arm_pbr.png");
            bare_pbr_candidates.push_back("bare_arm_pbr.png");

            std::string bare_png;
            for (const auto& p : bare_tex_candidates) {
                if (std::filesystem::exists(p)) { bare_png = p; break; }
            }
            std::string bare_norm_png;
            for (const auto& p : bare_norm_candidates) {
                if (std::filesystem::exists(p)) { bare_norm_png = p; break; }
            }
            std::string bare_pbr_png;
            for (const auto& p : bare_pbr_candidates) {
                if (std::filesystem::exists(p)) { bare_pbr_png = p; break; }
            }

            std::string knife_norm_png;
            for (const auto& p : knife_norm_candidates) {
                if (std::filesystem::exists(p)) { knife_norm_png = p; break; }
            }
            std::string knife_pbr_png;
            for (const auto& p : knife_pbr_candidates) {
                if (std::filesystem::exists(p)) { knife_pbr_png = p; break; }
            }
            std::string glove_norm_png;
            for (const auto& p : glove_norm_candidates) {
                if (std::filesystem::exists(p)) { glove_norm_png = p; break; }
            }
            std::string glove_pbr_png;
            for (const auto& p : glove_pbr_candidates) {
                if (std::filesystem::exists(p)) { glove_pbr_png = p; break; }
            }

            std::vector<std::string> sleeve_tex_candidates;
            if (!game_dir.empty()) {
                sleeve_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "sleeves" / "sleeve_fbi_color.png").string());
                sleeve_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "sleeve_fbi_color.png").string());
            }
            sleeve_tex_candidates.push_back("mods\\mc_adapter\\textures\\sleeves\\sleeve_fbi_color.png");
            sleeve_tex_candidates.push_back("sleeve_fbi_color.png");

            std::vector<std::string> sleeve_norm_candidates;
            if (!game_dir.empty()) {
                sleeve_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "sleeves" / "sleeve_fbi_normal.png").string());
                sleeve_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "sleeve_fbi_normal.png").string());
            }
            sleeve_norm_candidates.push_back("mods\\mc_adapter\\textures\\sleeves\\sleeve_fbi_normal.png");
            sleeve_norm_candidates.push_back("sleeve_fbi_normal.png");

            std::vector<std::string> sleeve_pbr_candidates;
            if (!game_dir.empty()) {
                sleeve_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "sleeves" / "sleeve_fbi_pbr.png").string());
                sleeve_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "sleeve_fbi_pbr.png").string());
            }
            sleeve_pbr_candidates.push_back("mods\\mc_adapter\\textures\\sleeves\\sleeve_fbi_pbr.png");
            sleeve_pbr_candidates.push_back("sleeve_fbi_pbr.png");

            auto firstExisting = [](const std::vector<std::string>& candidates) {
                for (const auto& p : candidates) {
                    if (std::filesystem::exists(p)) return p;
                }
                return std::string();
            };
            const std::string sleeve_png = firstExisting(sleeve_tex_candidates);
            const std::string sleeve_norm_png = firstExisting(sleeve_norm_candidates);
            const std::string sleeve_pbr_png = firstExisting(sleeve_pbr_candidates);

            g_cs.loadTexturesFromFiles(g_s.device, knife_png, glove_png, cs_table_cpu, glove_cpu,
                                       knife_norm_png, glove_norm_png, knife_pbr_png, glove_pbr_png,
                                       bare_png, bare_norm_png, bare_pbr_png,
                                       sleeve_png, sleeve_norm_png, sleeve_pbr_png);
            // The knife/glove/bare/sleeve maps are uploaded and never re-read;
            // only gun maps are worth keeping decoded for later switches.
            erov::ReleaseCsDecodeCache();

            // Load the AK-47 weapon maps into the second table once. Switching
            // then only swaps the table handle: no PNG decode, no 48 MB upload.
            if (const auto* ak = eldenring::cs::CSWeaponRegistry::instance().findWeapon("weapon_ak47")) {
                auto ak_def = *ak;
                ak_def.albedo_tex_path = ResolveCsAssetPath(ak_def.albedo_tex_path);
                ak_def.normal_tex_path = ResolveCsAssetPath(ak_def.normal_tex_path);
                ak_def.pbr_tex_path = ResolveCsAssetPath(ak_def.pbr_tex_path);
                g_cs.loadWeaponTextures(g_s.device, ak_def, ak_table_cpu);
                g_cs_gun_table_slot = 1;
            }
            // Glove + bare-arm + sleeve descriptors are shared by both weapon tables.
            const UINT shared_slots[] = {1, 3, 5, 6, 7, 8, 9, 10, 11};
            for (UINT shared : shared_slots) {
                D3D12_CPU_DESCRIPTOR_HANDLE src{cs_table_cpu.ptr + static_cast<SIZE_T>(srv_inc) * shared};
                D3D12_CPU_DESCRIPTOR_HANDLE dst{ak_table_cpu.ptr + static_cast<SIZE_T>(srv_inc) * shared};
                g_s.device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }
#endif
#if ENABLE_OW_VIEWMODEL
        {
            // The gauntlet owns a three-slot SRV heap. It never shares a slot
            // with the CS2 knife/gun tables, so either subsystem can be built
            // without the other.
            D3D12_DESCRIPTOR_HEAP_DESC ow_heap{};
            ow_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            ow_heap.NumDescriptors = 3;
            ow_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (SUCCEEDED(g_s.device->CreateDescriptorHeap(&ow_heap, IID_PPV_ARGS(&g_ow_srv_heap))) &&
                g_ow.init(g_s.device, g_s.format, g_log)) {
                g_ow_table_gpu = g_ow_srv_heap->GetGPUDescriptorHandleForHeapStart();
                g_ow_initialized = true;
                Logf("overlay: OW gauntlet renderer ready (dedicated 3-slot SRV heap)");
            } else {
                Logf("overlay: OW gauntlet renderer unavailable");
            }
        }
#endif
        {
            // The first-person arm and the sprites of the default hotbar items.
            std::vector<SteveRenderer::FpItemCell> cells;
            const float aw = static_cast<float>(g_atlas_w), ah = static_cast<float>(g_atlas_h);
            std::vector<mc::ItemId> sprite_items = mc::paletteItems();
            for (mc::ItemId id : {mc::ItemId::BowPulling0, mc::ItemId::BowPulling1, mc::ItemId::BowPulling2}) sprite_items.push_back(id); // the drawn bow, never in a slot
            for (mc::ItemId id : sprite_items) {
                if (const mc::hud::HudUV* uv = eldenring::render::uvForItem(id)) {
                    cells.push_back({static_cast<uint16_t>(id), static_cast<int>(std::lround(uv->u0 * aw)), static_cast<int>(std::lround(uv->v0 * ah)),
                                     static_cast<int>(std::lround((uv->u1 - uv->u0) * aw)), static_cast<int>(std::lround((uv->v1 - uv->v0) * ah))});
                }
            }
            g_fp_built = g_steve.initFirstPerson(g_s.device, g_atlas_rgba.empty() ? nullptr : g_atlas_rgba.data(), g_atlas_w, g_atlas_h, cells);
            Logf(g_fp_built ? "overlay: first-person view model ready" : "overlay: first-person view model failed");
            Logf(g_steve.initHeldItems(g_s.device, g_atlas_rgba.empty() ? nullptr : g_atlas_rgba.data(), g_atlas_w, g_atlas_h, cells)
                     ? "overlay: third-person held items ready"
                     : "overlay: third-person held items failed");
        }
    }
    g_s.last_frame = std::chrono::steady_clock::now();
    g_s.ready = true;
    char msg[256];
    snprintf(msg, sizeof(msg), "overlay: ready (buffers=%u, %ux%u, format=%d, queue=%p, queue found in swap chain at +0x%X)", g_s.buffers,
             g_s.width, g_s.height, static_cast<int>(g_s.format), static_cast<void*>(queue),
             static_cast<unsigned>(std::max(0, FindQueueOffsetInSwapChain(sc, queue))));
    Logf(msg);
    if (g_steve_cfg.depth_probe) probe::Install(g_s.list, g_log);
    return true;
}
void STDMETHODCALLTYPE CreateDsvDetour(ID3D12Device* d, ID3D12Resource* res, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
                                       D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    g_create_dsv_orig(d, res, desc, handle);
    if (res == nullptr) return;
    if (g_steve_cfg.depth_probe) probe::NoteDsv(handle.ptr, res); // every depth texture, whatever its size
    const D3D12_RESOURCE_DESC rd = res->GetDesc();
    if (rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd.Format != DXGI_FORMAT_R32G8X24_TYPELESS || rd.Width < 1024 ||
        rd.DepthOrArraySize != 1) {
        return;
    }
    if (!eldenring::depth::isSceneDepth(rd.Width, rd.Height, g_s.width, g_s.height)) {
        static int ignored_logged = 0;
        if (ignored_logged++ < 10 && g_log) g_log("overlay: ignored depth %llux%u (the back buffer is %ux%u): not the scene's", static_cast<unsigned long long>(rd.Width), rd.Height, g_s.width, g_s.height);
        return;
    }
    {
        std::lock_guard<std::mutex> g(g_depth_mutex);
        if (g_depth_res == res) return;
        bool known = false;
        for (ID3D12Resource* c : g_depth_candidates) known = known || c == res;
        if (!known) {
            res->AddRef();
            g_depth_candidates.push_back(res);
            if (g_depth_candidates.size() > 8) {
                g_depth_candidates.front()->Release();
                g_depth_candidates.erase(g_depth_candidates.begin());
            }
        }
        res->AddRef();
        if (g_depth_res) g_depth_res->Release();
        g_depth_res = res;
        g_depth_w = static_cast<UINT>(rd.Width);
        g_depth_h = rd.Height;
    }
    g_depth_dirty.store(true);
    static int logged = 0;
    if (logged++ < 20 && g_log) {
        g_log("overlay: scene depth captured: %p %llux%u format=%d samples=%u flags=0x%X dsv_format=%d", static_cast<void*>(res),
              static_cast<unsigned long long>(rd.Width), rd.Height, static_cast<int>(rd.Format), rd.SampleDesc.Count,
              static_cast<unsigned>(rd.Flags), desc ? static_cast<int>(desc->Format) : -1);
    }
}

void STDMETHODCALLTYPE ExecuteDetour(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (g_queue.load(std::memory_order_relaxed) == nullptr) {
        const D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) g_queue.store(q);
    }
    if (g_steve_cfg.depth_probe) probe::NoteExecute(n, lists);
    g_execute_orig(q, n, lists);
}

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (g_steve_cfg.depth_probe) probe::OnPresent();
    if (!g_hook_state.failed()) {
        ID3D12CommandQueue* q = g_queue.load();
        if (!g_s.ready && q != nullptr) {
            if (!Init(sc, q)) {
                // Init may fail only because the first captured queue is the wrong device; give up after it released everything.
                Teardown();
                g_hook_state.on_init_failed();
                if (g_hook_state.failed()) {
                    Logf("overlay: giving up after repeated init failures");
                }
            } else {
                g_hook_state.on_init_ok();
            }
        }
        if (g_s.ready) {
            __try {
                RenderFrame(sc);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_hook_state.on_exception();
                Logf("overlay: exception while drawing, overlay disabled");
            }
        }
    }
    return g_present_orig(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE ResizeDetour(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    if (g_s.ready) {
        Teardown(); // rebuilt on the next Present
        g_hook_state.on_resize();
        Logf("overlay: resize, resources released");
    }
    return g_resize_orig(sc, count, w, h, fmt, flags);
}

// Throw-away device, queue and swap chain, only to read the vtable function addresses.
bool ResolveTargets(void*& present, void*& resize, void*& execute, void*& create_dsv) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"mc_er_dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"mc", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    bool ok = false;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* q = nullptr;
    IDXGIFactory4* fac = nullptr;
    IDXGISwapChain1* sc1 = nullptr;
    if (hwnd && SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))) {
            DXGI_SWAP_CHAIN_DESC1 sd{};
            sd.Width = 8;
            sd.Height = 8;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (SUCCEEDED(fac->CreateSwapChainForHwnd(q, hwnd, &sd, nullptr, nullptr, &sc1))) {
                void** sv = *reinterpret_cast<void***>(sc1);
                void** qv = *reinterpret_cast<void***>(q);
                present = sv[kVtblPresent];
                resize = sv[kVtblResizeBuffers];
                execute = qv[kVtblExecuteCommandLists];
                create_dsv = (*reinterpret_cast<void***>(dev))[kVtblDeviceCreateDsv];
                ok = true;
            }
        }
    }
    SafeRelease(sc1);
    SafeRelease(fac);
    SafeRelease(q);
    SafeRelease(dev);
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}
bool InstallOverlayHooks(HudProvider provider, LogFn log) {
    g_provider = provider;
    g_log = log;
    void *present = nullptr, *resize = nullptr, *execute = nullptr, *create_dsv = nullptr;
    if (!ResolveTargets(present, resize, execute, create_dsv)) {
        Logf("overlay: could not resolve the D3D12/DXGI vtable functions");
        return false;
    }
    if (MH_CreateHook(execute, reinterpret_cast<void*>(&ExecuteDetour), reinterpret_cast<void**>(&g_execute_orig)) != MH_OK ||
        MH_CreateHook(present, reinterpret_cast<void*>(&PresentDetour), reinterpret_cast<void**>(&g_present_orig)) != MH_OK ||
        MH_CreateHook(resize, reinterpret_cast<void*>(&ResizeDetour), reinterpret_cast<void**>(&g_resize_orig)) != MH_OK ||
        MH_CreateHook(create_dsv, reinterpret_cast<void*>(&CreateDsvDetour), reinterpret_cast<void**>(&g_create_dsv_orig)) != MH_OK) {
        Logf("overlay: MH_CreateHook failed");
        return false;
    }
    if (MH_EnableHook(execute) != MH_OK || MH_EnableHook(present) != MH_OK || MH_EnableHook(resize) != MH_OK ||
        MH_EnableHook(create_dsv) != MH_OK) {
        Logf("overlay: MH_EnableHook failed");
        return false;
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "overlay: hooks installed (Present=%p ResizeBuffers=%p ExecuteCommandLists=%p CreateDepthStencilView=%p)", present,
             resize, execute, create_dsv);
    Logf(msg);
    return true;
}

} // namespace detail
} // namespace erov
