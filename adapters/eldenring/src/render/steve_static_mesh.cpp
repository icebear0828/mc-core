#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include "render/d3d12_util.hpp"
#include "render/steve_mesh_factory.hpp"
#include "steve_renderer_d3d12.hpp"

namespace erov {
using d3d12::Rel;

// Lives apart from steve_renderer_d3d12.cpp, whose line budget the shader guard test keeps under 600.
bool SteveRenderer::setStaticMesh(ID3D12Device* device, std::string_view name, const mc::model::StaticMesh& mesh) {
    if (!device || name.empty() || mesh.vertices.empty() || mesh.indices.empty()) return false;
    const steve::MobCpuMesh cpu = steve::BuildStaticMesh(mesh);
    if (cpu.vertices.empty() || cpu.indices.empty() || cpu.bone_ranges.empty()) return false;

    MobGpuMesh gpu;
    unsigned index_count = 0;
    if (!d3d12::UploadMesh(device, cpu.vertices, cpu.indices, gpu.vertices, gpu.indices, gpu.vbv, gpu.ibv, index_count)) {
        Rel(gpu.vertices);
        Rel(gpu.indices);
        return false;
    }
    gpu.ranges = cpu.bone_ranges;

    auto it = mob_meshes_.find(std::string(name));
    if (it != mob_meshes_.end()) {
        if (it->second.vertices != mob_vertices_) Rel(it->second.vertices);
        if (it->second.indices != mob_indices_) Rel(it->second.indices);
    }
    mob_meshes_[std::string(name)] = gpu;
    return true;
}

} // namespace erov
