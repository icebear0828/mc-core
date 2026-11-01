#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <vector>

#include "eldenring_shadow.hpp"
#include "render/d3d12_pipeline_builder.hpp"
#include "render/d3d12_util.hpp"
#include "render/steve_mesh_factory.hpp"
#include "render/steve_texture_cache.hpp"
#include "steve_renderer_d3d12.hpp"

namespace erov {
using d3d12::Rel;

bool SteveRenderer::init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log) {
    release();
    // Geometry: 12 parts, vertices already in the game's axes and metres, feet at the origin.
    const steve::RigCpuMesh rig = steve::BuildSteveRigMesh();
    unsigned total_indices = 0;
    if (!d3d12::UploadMesh(device, rig.vertices, rig.indices, vertices_, indices_, vbv_, ibv_, total_indices)) {
        if (log) log("steve: vertex/index buffers failed");
        release();
        return false;
    }
    (void)total_indices;
    for (size_t p = 0; p < static_cast<size_t>(mc::StevePart::Count); ++p) {
        base_vertex_[p] = rig.parts[p].base_vertex;
        first_index_[p] = rig.parts[p].first_index;
        index_count_[p] = rig.parts[p].index_count;
    }

    // Statistics buffer for the calibration mode: default heap (UAV), an upload buffer with the initial values, readback.
    {
        const uint32_t init_values[64] = {};
        stats_init_ = d3d12::UploadBuffer(device, init_values, sizeof(init_values));
        D3D12_HEAP_PROPERTIES hd{};
        hd.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_HEAP_PROPERTIES hr_{};
        hr_.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = 256;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RESOURCE_DESC ud = rd;
        ud.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        device->CreateCommittedResource(&hd, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&stats_));
        device->CreateCommittedResource(&hr_, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&stats_readback_));
    }

    // Root signature + figure/depth-view/shadow pipelines.
    if (!pipeline_.Create(device, rtv_format, log)) {
        release();
        return false;
    }

    // The blob shadow disc (one draw per visible shadow).
    const mc::rig::RigMesh disc = steve::BuildShadowDisc(static_cast<unsigned>(kShadowSegments));
    unsigned shadow_indices = 0;
    if (d3d12::UploadMesh(device, disc.vertices, disc.indices, shadow_vertices_, shadow_indices_, shadow_vbv_, shadow_ibv_, shadow_indices)) {
        shadow_index_count_ = shadow_indices;
    } else {
        Rel(shadow_vertices_);
        Rel(shadow_indices_);
    }
    return true;
}

void SteveRenderer::setBlocks(const mc::rig::RigMesh& mesh) {
    block_vertices_cpu_ = mesh.vertices;
    block_indices_cpu_ = mesh.indices;
    ++block_version_;
}

void SteveRenderer::beginPass(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                              unsigned width, unsigned height, ID3D12PipelineState* pso, bool clear_depth,
                              const D3D12_VIEWPORT* viewport, const D3D12_RECT* scissor, const D3D12_RECT* clear_rect) {
    d3d12::PassDesc pass;
    pass.list = list;
    pass.srv_heap = srv_heap;
    pass.rtv = rtv;
    pass.dsv = dsv_heap_ ? dsv_heap_->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    pass.width = width;
    pass.height = height;
    pass.pso = pso;
    pass.root = pipeline_.root;
    pass.clear_depth = clear_depth;
    pass.viewport = viewport;
    pass.scissor = scissor;
    pass.clear_rect = clear_rect;
    d3d12::BeginPass(pass);
}

void SteveRenderer::setConstants(ID3D12GraphicsCommandList* list, const mc::rig::Mat4& view_proj, const mc::rig::Mat4& world,
                                 const SteveParams& params, unsigned width, unsigned height, const float tint[4]) {
    list->SetGraphicsRoot32BitConstants(0, 16, view_proj.m.data(), 0);
    list->SetGraphicsRoot32BitConstants(0, 16, world.m.data(), 16);
    const float extra[8] = {params.depth_const, params.rel_bias, params.abs_bias, params.mode, params.depth_w, params.depth_h,
                            static_cast<float>(width), static_cast<float>(height)};
    list->SetGraphicsRoot32BitConstants(0, 8, extra, 32);
    list->SetGraphicsRoot32BitConstants(0, 4, tint, 40);
}

bool SteveRenderer::drawBlocks(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                               D3D12_GPU_DESCRIPTOR_HANDLE table, unsigned width, unsigned height, const mc::rig::Mat4& view_proj,
                               const SteveParams& params, D3D12_CPU_DESCRIPTOR_HANDLE rtv, unsigned frame) {
    if (!ready() || !own_depth_ || !dsv_heap_ || block_indices_cpu_.empty() || table.ptr == 0) return false;
    BlockSlot& slot = block_slots_[frame % 4];
    if (slot.version != block_version_ || !slot.vb) {
        Rel(slot.vb);
        Rel(slot.ib);
        if (!d3d12::UploadMesh(device, block_vertices_cpu_, block_indices_cpu_, slot.vb, slot.ib, slot.vbv, slot.ibv, slot.index_count)) return false;
        slot.version = block_version_;
    }
    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, true);
    list->SetGraphicsRootDescriptorTable(1, table);
    list->IASetVertexBuffers(0, 1, &slot.vbv);
    list->IASetIndexBuffer(&slot.ibv);
    const float no_tint[4] = {0.f, 0.f, 0.f, 0.f};
    setConstants(list, view_proj, mc::rig::Mat4{}, params, width, height, no_tint);
    list->DrawIndexedInstanced(slot.index_count, 1, 0, 0, 0);
    return true;
}

void SteveRenderer::setTerrainCrack(const mc::rig::RigMesh& mesh) {
    crack_vertices_cpu_ = mesh.vertices;
    crack_indices_cpu_ = mesh.indices;
    ++crack_version_;
}

bool SteveRenderer::drawTerrainCrack(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                                     D3D12_GPU_DESCRIPTOR_HANDLE table, unsigned width, unsigned height, const mc::rig::Mat4& view_proj,
                                     const SteveParams& params, D3D12_CPU_DESCRIPTOR_HANDLE rtv, unsigned frame) {
    if (!ready() || !own_depth_ || !dsv_heap_ || crack_indices_cpu_.empty() || table.ptr == 0) return false;
    BlockSlot& slot = crack_slots_[frame % 4];
    if (slot.version != crack_version_ || !slot.vb) {
        Rel(slot.vb);
        Rel(slot.ib);
        if (!d3d12::UploadMesh(device, crack_vertices_cpu_, crack_indices_cpu_, slot.vb, slot.ib, slot.vbv, slot.ibv, slot.index_count)) return false;
        slot.version = crack_version_;
    }
    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, !params.keep_depth);
    list->SetGraphicsRootDescriptorTable(1, table);
    list->IASetVertexBuffers(0, 1, &slot.vbv);
    list->IASetIndexBuffer(&slot.ibv);
    const float no_tint[4] = {0.f, 0.f, 0.f, 0.f};
    setConstants(list, view_proj, mc::rig::Mat4{}, params, width, height, no_tint);
    list->DrawIndexedInstanced(slot.index_count, 1, 0, 0, 0);
    return true;
}

void SteveRenderer::release() {
    for (BlockSlot& slot : block_slots_) {
        Rel(slot.vb);
        Rel(slot.ib);
    }
    for (BlockSlot& slot : crack_slots_) {
        Rel(slot.vb);
        Rel(slot.ib);
    }
    Rel(fp_vertices_);
    Rel(fp_indices_);
    fp_items_.clear();
    Rel(own_depth_);
    Rel(dsv_heap_);
    own_depth_w_ = own_depth_h_ = 0;
    Rel(mob_vertices_);
    Rel(mob_indices_);
    mob_ranges_.clear();
    for (auto& [name, m] : mob_meshes_) {
        if (m.vertices != mob_vertices_) Rel(m.vertices);
        if (m.indices != mob_indices_) Rel(m.indices);
    }
    mob_meshes_.clear();
    Rel(skin_.tex);
    Rel(skin_.upload);
    skin_.pending = false;
    Rel(mob_skin_.tex);
    Rel(mob_skin_.upload);
    mob_skin_.pending = false;
    for (auto& [name, s] : mob_skins_map_) {
        Rel(s.tex);
        Rel(s.upload);
    }
    mob_skins_map_.clear();
    Rel(stats_);
    Rel(stats_init_);
    Rel(stats_readback_);
    pipeline_.Release();
    Rel(shadow_vertices_);
    Rel(shadow_indices_);
    shadow_index_count_ = 0;
    Rel(vertices_);
    Rel(indices_);
    Rel(held_vertices_);
    Rel(held_indices_);
    held_items_.clear();
    Rel(offhand_vertices_);
    Rel(offhand_indices_);
    offhand_items_.clear();
    Rel(arrow_vertices_);
    Rel(arrow_indices_);
    arrow_index_count_ = 0;
}

bool SteveRenderer::readStats(uint32_t out[64]) {
    if (!stats_readback_) return false;
    void* mapped = nullptr;
    D3D12_RANGE range{0, 256};
    if (FAILED(stats_readback_->Map(0, &range, &mapped))) return false;
    std::memcpy(out, mapped, 256);
    D3D12_RANGE none{0, 0};
    stats_readback_->Unmap(0, &none);
    return true;
}

bool SteveRenderer::initFirstPerson(ID3D12Device* device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                                    const std::vector<FpItemCell>& cells) {
    Rel(fp_vertices_);
    Rel(fp_indices_);
    fp_items_.clear();
    const steve::FirstPersonMesh mesh = steve::BuildFirstPerson(atlas_rgba, atlas_w, atlas_h, cells);
    unsigned index_count = 0;
    if (!d3d12::UploadMesh(device, mesh.vertices, mesh.indices, fp_vertices_, fp_indices_, fp_vbv_, fp_ibv_, index_count)) {
        Rel(fp_vertices_);
        Rel(fp_indices_);
        return false;
    }
    fp_arm_[0] = mesh.arm_ranges[0];
    fp_arm_[1] = mesh.arm_ranges[1];
    fp_items_ = mesh.items;
    return true;
}

bool SteveRenderer::initHeldItems(ID3D12Device* device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                                  const std::vector<FpItemCell>& cells) {
    Rel(held_vertices_);
    Rel(held_indices_);
    held_items_.clear();
    if (!atlas_rgba) return false;
    const steve::HeldItemsMesh mesh = steve::BuildHeldItems(atlas_rgba, atlas_w, atlas_h, cells);
    if (mesh.vertices.empty()) return false;
    unsigned index_count = 0;
    if (!d3d12::UploadMesh(device, mesh.vertices, mesh.indices, held_vertices_, held_indices_, held_vbv_, held_ibv_, index_count)) {
        Rel(held_vertices_);
        Rel(held_indices_);
        return false;
    }
    held_items_ = mesh.draws;

    Rel(arrow_vertices_);
    Rel(arrow_indices_);
    arrow_index_count_ = 0;
    if (mesh.arrow_valid &&
        d3d12::UploadMesh(device, mesh.arrow_vertices, mesh.arrow_indices, arrow_vertices_, arrow_indices_, arrow_vbv_, arrow_ibv_, index_count)) {
        arrow_index_count_ = index_count;
    }

    // Offhand items (left arm, mirrored or forearm-mounted shield).
    Rel(offhand_vertices_);
    Rel(offhand_indices_);
    offhand_items_.clear();
    if (!mesh.off_vertices.empty() &&
        d3d12::UploadMesh(device, mesh.off_vertices, mesh.off_indices, offhand_vertices_, offhand_indices_, offhand_vbv_, offhand_ibv_, index_count)) {
        offhand_items_ = mesh.off_draws;
    }
    return true;
}

bool SteveRenderer::ensureDepth(ID3D12Device* device, unsigned width, unsigned height) {
    if (own_depth_ && own_depth_w_ == width && own_depth_h_ == height) return true;
    Rel(own_depth_);
    if (!dsv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = 1;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv_heap_)))) return false;
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = width;
    rd.Height = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_D32_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.0f;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&own_depth_)))) {
        return false;
    }
    device->CreateDepthStencilView(own_depth_, nullptr, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    own_depth_w_ = width;
    own_depth_h_ = height;
    return true;
}

bool SteveRenderer::setSkin(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return steve::CreateSkinTexture(device, rgba, width, height, slot, skin_);
}

bool SteveRenderer::setMobSkin(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return steve::CreateSkinTexture(device, rgba, width, height, slot, mob_skin_);
}

bool SteveRenderer::setMobSkin(ID3D12Device* device, std::string_view name, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    SkinGpu& upload = mob_skins_map_[std::string(name)];
    Rel(upload.tex);
    Rel(upload.upload);
    upload.pending = false;
    return steve::CreateSkinTexture(device, rgba, width, height, slot, upload);
}

void SteveRenderer::setDepthView(ID3D12Device* device, ID3D12Resource* depth, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depth, &sd, slot); // a null resource makes a valid null descriptor
}

void SteveRenderer::drawDepthView(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                                  D3D12_GPU_DESCRIPTOR_HANDLE depth_table, unsigned width, unsigned height, float gain, float depth_w,
                                  float depth_h) {
    if (!pipeline_.depth_view) return;
    D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->SetPipelineState(pipeline_.depth_view);
    list->SetGraphicsRootSignature(pipeline_.root);
    ID3D12DescriptorHeap* heaps[] = {srv_heap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(1, depth_table);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float extra[8] = {gain, 0.f, 0.f, 0.f, depth_w, depth_h, static_cast<float>(width), static_cast<float>(height)};
    list->SetGraphicsRoot32BitConstants(0, 8, extra, 32);
    list->DrawInstanced(3, 1, 0, 0);
}

bool SteveRenderer::setMobModel(ID3D12Device* device, const mc::model::EntityModel& model) {
    return setMobModel(device, "", model);
}

bool SteveRenderer::setMobModel(ID3D12Device* device, std::string_view name, const mc::model::EntityModel& model) {
    if (!device || model.bones.empty()) return false;
    const steve::MobCpuMesh cpu = steve::BuildMobMesh(model);
    if (cpu.vertices.empty() || cpu.indices.empty()) return false;
    MobGpuMesh gpu;
    unsigned index_count = 0;
    if (!d3d12::UploadMesh(device, cpu.vertices, cpu.indices, gpu.vertices, gpu.indices, gpu.vbv, gpu.ibv, index_count)) {
        Rel(gpu.vertices);
        Rel(gpu.indices);
        return false;
    }
    gpu.ranges = cpu.bone_ranges;
    gpu.right_arm_ix = cpu.right_arm_ix;
    gpu.left_arm_ix = cpu.left_arm_ix;
    if (name.empty()) {
        Rel(mob_vertices_);
        Rel(mob_indices_);
        mob_vertices_ = gpu.vertices;
        mob_indices_ = gpu.indices;
        mob_vbv_ = gpu.vbv;
        mob_ibv_ = gpu.ibv;
        mob_ranges_ = gpu.ranges;
    } else {
        auto it = mob_meshes_.find(std::string(name));
        if (it != mob_meshes_.end()) {
            if (it->second.vertices != mob_vertices_) Rel(it->second.vertices);
            if (it->second.indices != mob_indices_) Rel(it->second.indices);
        }
        mob_meshes_[std::string(name)] = gpu;
    }
    return true;
}

bool SteveRenderer::mobModelReady(std::string_view name) const {
    if (name.empty()) return mobModelReady();
    auto it = mob_meshes_.find(std::string(name));
    if (it != mob_meshes_.end()) return it->second.vertices != nullptr && !it->second.ranges.empty();
    return mobModelReady();
}

void SteveRenderer::drawMobModel(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table, unsigned width,
                                 unsigned height, const mc::rig::Mat4& view_proj, const std::vector<mc::rig::Mat4>& bones, const SteveParams& params,
                                 D3D12_CPU_DESCRIPTOR_HANDLE rtv, std::string_view model_type) {
    if (!ready() || !own_depth_ || !dsv_heap_) return;
    steve::RecordUpload(list, skin_);
    steve::RecordUpload(list, mob_skin_);
    for (auto& [name, s] : mob_skins_map_) steve::RecordUpload(list, s);
    const MobGpuMesh* mesh = nullptr;
    if (!model_type.empty()) {
        auto it = mob_meshes_.find(std::string(model_type));
        if (it != mob_meshes_.end()) mesh = &it->second;
    }
    const D3D12_VERTEX_BUFFER_VIEW* vbv = mesh ? &mesh->vbv : &mob_vbv_;
    const D3D12_INDEX_BUFFER_VIEW* ibv = mesh ? &mesh->ibv : &mob_ibv_;
    const std::vector<BoneRange>& ranges = mesh ? mesh->ranges : mob_ranges_;
    if (ranges.empty() || vbv->BufferLocation == 0 || ibv->BufferLocation == 0) return;

    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, !params.keep_depth);
    list->SetGraphicsRootDescriptorTable(1, depth_table);
    list->IASetVertexBuffers(0, 1, vbv);
    list->IASetIndexBuffer(ibv);
    for (size_t i = 0; i < ranges.size() && i < bones.size(); ++i) {
        if (ranges[i].index_count == 0) continue;
        setConstants(list, view_proj, bones[i], params, width, height, params.tint);
        list->DrawIndexedInstanced(ranges[i].index_count, 1, ranges[i].first_index, ranges[i].base_vertex, 0);
    }
    if (params.held_item != 0 && held_vertices_ && params.held_table.ptr != 0) {
        const int arm_ix = mesh ? mesh->right_arm_ix : -1;
        if (arm_ix >= 0 && static_cast<size_t>(arm_ix) < bones.size()) {
            const auto it = held_items_.find(params.held_item);
            if (it != held_items_.end()) {
                list->SetGraphicsRootDescriptorTable(1, params.held_table);
                list->IASetVertexBuffers(0, 1, &held_vbv_);
                list->IASetIndexBuffer(&held_ibv_);
                list->SetGraphicsRoot32BitConstants(0, 16, bones[static_cast<size_t>(arm_ix)].m.data(), 16);
                list->DrawIndexedInstanced(it->second.index_count, 1, it->second.first_index, it->second.base_vertex, 0);
            }
        }
    }
}

void SteveRenderer::drawShadows(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table, unsigned width,
                                unsigned height, const mc::rig::Mat4& view_proj, const std::vector<eldenring::shadow::Shadow>& shadows,
                                const SteveParams& params, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!shadowReady() || !own_depth_ || !dsv_heap_ || shadows.empty()) return;
    beginPass(list, srv_heap, rtv, width, height, pipeline_.shadow, !params.keep_depth);
    list->SetGraphicsRootDescriptorTable(1, depth_table);
    list->IASetVertexBuffers(0, 1, &shadow_vbv_);
    list->IASetIndexBuffer(&shadow_ibv_);
    for (const eldenring::shadow::Shadow& s : shadows) {
        if (!s.visible) continue;
        const float tint[4] = {0.f, 0.f, 0.f, s.strength};
        setConstants(list, view_proj, eldenring::shadow::shadowMatrix(s), params, width, height, tint);
        list->DrawIndexedInstanced(shadow_index_count_, 1, 0, 0, 0);
    }
}

void SteveRenderer::drawArrows(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                               D3D12_GPU_DESCRIPTOR_HANDLE atlas_table, unsigned width, unsigned height,
                               const mc::rig::Mat4& view_proj, const std::vector<mc::rig::Mat4>& arrow_matrices,
                               const SteveParams& params, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!ready() || !own_depth_ || !dsv_heap_ || arrow_matrices.empty() || atlas_table.ptr == 0) return;
    const bool have_custom_arrow = (arrow_vertices_ != nullptr && arrow_indices_ != nullptr && arrow_index_count_ > 0);
    const auto it = held_items_.find(static_cast<uint16_t>(mc::ItemId::Arrow));
    if (!have_custom_arrow && it == held_items_.end()) return;

    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, !params.keep_depth);
    list->SetGraphicsRootDescriptorTable(1, atlas_table);
    if (have_custom_arrow) {
        list->IASetVertexBuffers(0, 1, &arrow_vbv_);
        list->IASetIndexBuffer(&arrow_ibv_);
    } else {
        list->IASetVertexBuffers(0, 1, &held_vbv_);
        list->IASetIndexBuffer(&held_ibv_);
    }
    const float no_tint[4] = {0.f, 0.f, 0.f, 0.f};
    for (const auto& mat : arrow_matrices) {
        setConstants(list, view_proj, mat, params, width, height, no_tint);
        if (have_custom_arrow) {
            list->DrawIndexedInstanced(arrow_index_count_, 1, 0, 0, 0);
        } else {
            list->DrawIndexedInstanced(it->second.index_count, 1, it->second.first_index, it->second.base_vertex, 0);
        }
    }
}

void SteveRenderer::drawAttachments(ID3D12GraphicsCommandList* list, const eldenring::render::PartMatrices& parts, const SteveParams& params) {
    if (params.held_item != 0 && held_vertices_ && params.held_table.ptr != 0) {
        const bool raised = params.held_item == static_cast<uint16_t>(mc::ItemId::Shield) && params.shield_blocking && !params.block_offhand;
        const auto it = held_items_.find(static_cast<uint16_t>(params.held_item | (raised ? kBlockingKeyBit : 0)));
        if (it != held_items_.end()) {
            list->SetGraphicsRootDescriptorTable(1, params.held_table);
            list->IASetVertexBuffers(0, 1, &held_vbv_);
            list->IASetIndexBuffer(&held_ibv_);
            list->SetGraphicsRoot32BitConstants(0, 16, parts[static_cast<size_t>(mc::StevePart::RightArm)].m.data(), 16);
            list->DrawIndexedInstanced(it->second.index_count, 1, it->second.first_index, it->second.base_vertex, 0);
        }
    }
    if (params.offhand_item != 0 && offhand_vertices_ && params.held_table.ptr != 0) {
        const bool raised = params.offhand_item == static_cast<uint16_t>(mc::ItemId::Shield) && params.shield_blocking && params.block_offhand;
        const auto it = offhand_items_.find(static_cast<uint16_t>(params.offhand_item | (raised ? kBlockingKeyBit : 0)));
        if (it != offhand_items_.end()) {
            list->SetGraphicsRootDescriptorTable(1, params.held_table);
            list->IASetVertexBuffers(0, 1, &offhand_vbv_);
            list->IASetIndexBuffer(&offhand_ibv_);
            list->SetGraphicsRoot32BitConstants(0, 16, parts[static_cast<size_t>(mc::StevePart::LeftArm)].m.data(), 16);
            list->DrawIndexedInstanced(it->second.index_count, 1, it->second.first_index, it->second.base_vertex, 0);
        }
    }
}

void SteveRenderer::draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table,
                         unsigned width, unsigned height, const mc::rig::Mat4& view_proj,
                         const eldenring::render::PartMatrices& parts, const SteveParams& params, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!ready() || !own_depth_ || !dsv_heap_) return;
    steve::RecordUpload(list, skin_);
    steve::RecordUpload(list, mob_skin_);
    for (auto& [name, s] : mob_skins_map_) steve::RecordUpload(list, s);
    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, !params.keep_depth);
    list->SetGraphicsRootDescriptorTable(1, depth_table);
    list->IASetVertexBuffers(0, 1, &vbv_);
    list->IASetIndexBuffer(&ibv_);
    const bool measure = params.mode > 2.5f && stats_ && stats_init_ && stats_readback_;
    if (measure) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = stats_;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1, &b);
        list->CopyBufferRegion(stats_, 0, stats_init_, 0, 256);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        list->ResourceBarrier(1, &b);
        list->SetGraphicsRootUnorderedAccessView(2, stats_->GetGPUVirtualAddress());
    }
    for (size_t p = 0; p < static_cast<size_t>(mc::StevePart::Count); ++p) {
        setConstants(list, view_proj, parts[p], params, width, height, params.tint);
        list->DrawIndexedInstanced(index_count_[p], 1, first_index_[p], base_vertex_[p], 0);
    }
    drawAttachments(list, parts, params);
    if (measure) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = stats_;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &b);
        list->CopyBufferRegion(stats_readback_, 0, stats_, 0, 256);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1, &b);
    }
}

void SteveRenderer::drawInventoryPreview(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                                         D3D12_GPU_DESCRIPTOR_HANDLE skin_table, const D3D12_VIEWPORT& vp,
                                         const D3D12_RECT& sc, const mc::rig::Mat4& view_proj,
                                         const eldenring::render::PartMatrices& parts, const SteveParams& params,
                                         D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!ready() || !own_depth_ || !dsv_heap_) return;
    steve::RecordUpload(list, skin_);
    steve::RecordUpload(list, mob_skin_);
    beginPass(list, srv_heap, rtv, static_cast<unsigned>(vp.Width), static_cast<unsigned>(vp.Height), pipeline_.figure, true, &vp, &sc, &sc);
    list->SetGraphicsRootDescriptorTable(1, skin_table);
    list->IASetVertexBuffers(0, 1, &vbv_);
    list->IASetIndexBuffer(&ibv_);
    list->SetGraphicsRoot32BitConstants(0, 16, view_proj.m.data(), 0);
    const float extra[8] = {0.0501f, 0.f, 0.f, 0.f, 0.f, 0.f, vp.Width, vp.Height};
    list->SetGraphicsRoot32BitConstants(0, 8, extra, 32);
    list->SetGraphicsRoot32BitConstants(0, 4, params.tint, 40);
    for (size_t p = 0; p < static_cast<size_t>(mc::StevePart::Count); ++p) {
        list->SetGraphicsRoot32BitConstants(0, 16, parts[p].m.data(), 16);
        list->DrawIndexedInstanced(index_count_[p], 1, first_index_[p], base_vertex_[p], 0);
    }
    drawAttachments(list, parts, params);
}

} // namespace erov
