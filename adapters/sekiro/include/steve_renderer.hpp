#pragma once

// D3D11 renderer for the 12-part Steve rig, drawn into the frame the game is about to present.
// Windows only. Geometry and matrices come from sekiro_steve.hpp (Dantelion space, row vectors).

#include "sekiro_steve.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <vector>

namespace sekiro::render {

class SteveRenderer {
public:
    // Compiles the shaders and uploads the static mesh. Returns false (and logs why) on failure.
    bool init(ID3D11Device* device);
    // Replaces the skin. `rgba` is width*height*4 bytes. Without a skin a neutral grey is used.
    bool setSkin(ID3D11Device* device, const std::vector<uint8_t>& rgba, UINT width, UINT height);
    [[nodiscard]] bool ready() const { return ready_; }

    // Draws every part with its own world matrix. Saves and restores the pipeline state it touches.
    void draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, UINT width, UINT height,
              const Mat4& view_projection, const std::array<Mat4, static_cast<size_t>(mc::StevePart::Count)>& part_world);

private:
    bool ensureDepth(UINT width, UINT height);

    bool ready_{false};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indices_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> frame_cb_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> part_cb_;
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

} // namespace sekiro::render
