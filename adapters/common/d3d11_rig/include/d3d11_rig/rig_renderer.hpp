#pragma once

// D3D11 renderer for the 12-part Steve rig, drawn into the frame the game is about to present. Windows only.
// Game independent: the host supplies the meshes already in its own space (mc::rig::buildPartMesh with its
// HostBasis), row-vector matrices, and how its depth buffer works (mc::rig::DepthConvention). Reusable for any
// D3D11 title; see docs/PORTING_PLAYBOOK.md section 6.

#include "mc/rig.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <vector>

namespace mc::d3d11 {

class RigRenderer {
public:
    using PartMeshes = std::array<mc::rig::RigMesh, static_cast<size_t>(mc::StevePart::Count)>;
    using PartMatrices = std::array<mc::rig::Mat4, static_cast<size_t>(mc::StevePart::Count)>;

    // Compiles the shaders and uploads the static meshes (24 vertices / 36 indices per part, indices relative to
    // the part). Returns false (and logs why) on failure.
    bool init(ID3D11Device* device, const PartMeshes& meshes);
    // How the host's depth buffer relates to distance; without a calibrated reverse-Z convention the rig is
    // never occluded.
    void setDepthConvention(const mc::rig::DepthConvention& conv) { depth_conv_ = conv; }
    // Replaces the skin. `rgba` is width*height*4 bytes. Without a skin a neutral grey is used.
    bool setSkin(ID3D11Device* device, const std::vector<uint8_t>& rgba, UINT width, UINT height);
    // The game's depth buffer (reverse-Z R32G8X24_TYPELESS) so the rig is hidden behind the scene. Null or an
    // unsupported layout turns occlusion off. The texture is only referenced, not owned beyond this call chain.
    void setSceneDepth(ID3D11Texture2D* texture);
    // Where on screen (uv, 0..1) Steve stands: the frame around it lights him.
    void setAmbientProbe(float u, float v) { probe_u_ = u; probe_v_ = v; }
    // An item in the right hand: its mesh (built in the rest pose of the right arm, see mc/item_model.hpp) and
    // the sprite sheet its UVs address. It is drawn with the right arm's matrix, so it follows the arm. Pass an
    // empty mesh to take it away. The sheet is kept alive by the renderer.
    bool setHeldItem(const mc::rig::RigMesh& mesh, ID3D11ShaderResourceView* sprite_sheet);
    [[nodiscard]] bool hasHeldItem() const { return item_index_count_ > 0; }
    [[nodiscard]] bool sceneDepthActive() const { return scene_depth_srv_ != nullptr; }
    [[nodiscard]] bool ready() const { return ready_; }

    // Draws every part with its own world matrix. Saves and restores the pipeline state it touches.
    void draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, UINT width, UINT height,
              const mc::rig::Mat4& view_projection, const PartMatrices& part_world);

private:
    bool ensureDepth(UINT width, UINT height);
    bool captureFrame(ID3D11DeviceContext* context, ID3D11RenderTargetView* target);

    bool ready_{false};
    mc::rig::DepthConvention depth_conv_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indices_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> item_vertices_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> item_indices_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> item_sheet_;
    UINT item_index_count_{0};
    Microsoft::WRL::ComPtr<ID3D11Buffer> frame_cb_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> part_cb_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> scene_cb_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> linear_sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_tex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> frame_srv_;
    UINT frame_w_{0}, frame_h_{0};
    DXGI_FORMAT frame_fmt_{DXGI_FORMAT_UNKNOWN};
    float probe_u_{0.5f}, probe_v_{0.5f};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> scene_depth_tex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scene_depth_srv_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> skin_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_tex_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view_;
    UINT depth_width_{0};
    UINT depth_height_{0};
    UINT indices_per_part_{36};
    UINT vertices_per_part_{24};
};

} // namespace mc::d3d11
