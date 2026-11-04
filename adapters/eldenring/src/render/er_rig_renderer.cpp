#include "render/er_rig_renderer.hpp"

namespace erov {

namespace {

// The seam carries descriptor values as plain integers; D3D12 wants its own handle structs.
D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptor(mc::rig::GpuHandle slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};
    handle.ptr = static_cast<SIZE_T>(slot);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptor(mc::rig::GpuHandle slot) {
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    handle.ptr = static_cast<UINT64>(slot);
    return handle;
}

} // namespace

ErRigRenderer::ErRigRenderer(DXGI_FORMAT rtv_format, LogFn log) : rtv_format_(rtv_format), log_(log) {
    // The calibrated Elden Ring near plane: depth * view z is this constant (docs/ELDENRING_HANDOFF.md).
    depth_.depth_times_distance = 0.0501f;
}

bool ErRigRenderer::init(mc::rig::HostHandle device) {
    device_ = static_cast<ID3D12Device*>(device);
    if (device_ == nullptr) return false;
    return renderer_.init(device_, rtv_format_, log_);
}

bool ErRigRenderer::setSkin(const uint8_t* rgba, unsigned width, unsigned height, mc::rig::GpuHandle slot) {
    if (device_ == nullptr) return false;
    return renderer_.setSkin(device_, rgba, width, height, CpuDescriptor(slot));
}

void ErRigRenderer::setSceneDepth(mc::rig::HostHandle depth_texture, mc::rig::GpuHandle slot) {
    scene_depth_ = static_cast<ID3D12Resource*>(depth_texture);
    // The shader maps a back-buffer pixel onto the depth texture with the depth texture's own size
    // (dims.zw), which RigDraw does not carry: it only knows the back buffer.
    if (scene_depth_ != nullptr) {
        const D3D12_RESOURCE_DESC desc = scene_depth_->GetDesc();
        scene_depth_w_ = static_cast<unsigned>(desc.Width);
        scene_depth_h_ = desc.Height;
    } else {
        scene_depth_w_ = scene_depth_h_ = 0;
    }
    if (device_ == nullptr) return; // before init there is no descriptor heap to write into
    renderer_.setDepthView(device_, scene_depth_, CpuDescriptor(slot));
}

void ErRigRenderer::setDepthConvention(const mc::rig::DepthConvention& convention) { depth_ = convention; }

void ErRigRenderer::setHeldItem(const mc::HandItems& hands, mc::rig::GpuHandle atlas_table) {
    hands_ = hands;
    held_table_ = atlas_table;
}

void ErRigRenderer::setBlocks(const mc::rig::RigMesh& mesh) { renderer_.setBlocks(mesh); }

void ErRigRenderer::setHurtTint(float amount) {
    hurt_tint_ = amount < 0.f ? 0.f : (amount > 1.f ? 1.f : amount);
}

bool ErRigRenderer::initFirstPerson(mc::rig::HostHandle device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                                    const std::vector<SteveRenderer::FpItemCell>& cells) {
    return renderer_.initFirstPerson(static_cast<ID3D12Device*>(device), atlas_rgba, atlas_w, atlas_h, cells);
}

bool ErRigRenderer::initHeldItems(mc::rig::HostHandle device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                                  const std::vector<SteveRenderer::FpItemCell>& cells) {
    return renderer_.initHeldItems(static_cast<ID3D12Device*>(device), atlas_rgba, atlas_w, atlas_h, cells);
}

SteveParams ErRigRenderer::buildParams() const {
    return MakeSteveParams(depth_, scene_depth_ != nullptr, scene_depth_w_, scene_depth_h_, hands_, held_table_, hurt_tint_);
}

void ErRigRenderer::draw(const mc::rig::RigDraw& request) {
    if (request.part_world == nullptr) return;
    SteveParams params = buildParams();
    params.keep_depth = request.keep_depth;
    renderer_.draw(static_cast<ID3D12GraphicsCommandList*>(request.command_list),
                   static_cast<ID3D12DescriptorHeap*>(request.descriptor_heap), GpuDescriptor(request.depth_table),
                   request.width, request.height, request.view_projection, *request.part_world, params,
                   CpuDescriptor(request.render_target));
}

void ErRigRenderer::drawFirstPerson(const mc::rig::RigFirstPersonDraw& request) {
    renderer_.drawFirstPerson(static_cast<ID3D12GraphicsCommandList*>(request.command_list),
                              static_cast<ID3D12DescriptorHeap*>(request.descriptor_heap),
                              GpuDescriptor(request.skin_table), GpuDescriptor(request.atlas_table), request.width,
                              request.height, request.projection, request.arm_world, request.item_world, request.item,
                              CpuDescriptor(request.render_target));
}

} // namespace erov
