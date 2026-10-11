#pragma once

#if defined(_WIN32)
#include <dxgiformat.h>
#else
enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R32G8X24_TYPELESS = 19,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
    DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
    DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
    DXGI_FORMAT_R32_TYPELESS = 39,
    DXGI_FORMAT_D32_FLOAT = 40,
    DXGI_FORMAT_R32_FLOAT = 42,
    DXGI_FORMAT_R24G8_TYPELESS = 44,
    DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
    DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
    DXGI_FORMAT_R16_TYPELESS = 54,
    DXGI_FORMAT_D16_UNORM = 55,
    DXGI_FORMAT_R16_UNORM = 56,
};
#endif

namespace eldenring::depth {

enum class DepthKind {
    None,
    Float32_Stencil8, // R32G8X24
    Float32,          // D32_FLOAT or R32_TYPELESS
    Unorm24_Stencil8, // D24_UNORM_S8 or R24G8
    Unorm16,          // D16_UNORM or R16_TYPELESS
};

inline DepthKind classifyDepthFormat(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            return DepthKind::Float32_Stencil8;

        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
            return DepthKind::Float32;

        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
            return DepthKind::Unorm24_Stencil8;

        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
            return DepthKind::Unorm16;

        default:
            return DepthKind::None;
    }
}

inline DXGI_FORMAT depthResourceToSrvFormat(DXGI_FORMAT res_format, DXGI_FORMAT dsv_format = DXGI_FORMAT_UNKNOWN) {
    DepthKind kind = classifyDepthFormat(res_format);
    if (kind == DepthKind::None && dsv_format != DXGI_FORMAT_UNKNOWN) {
        kind = classifyDepthFormat(dsv_format);
    }
    switch (kind) {
        case DepthKind::Float32_Stencil8:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DepthKind::Float32:
            return DXGI_FORMAT_R32_FLOAT;
        case DepthKind::Unorm24_Stencil8:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DepthKind::Unorm16:
            return DXGI_FORMAT_R16_UNORM;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

inline bool isTypelessDepthFormat(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R16_TYPELESS:
            return true;
        default:
            return false;
    }
}

struct DepthCandidateCriteria {
    unsigned int match_width{0};
    unsigned int match_height{0};
    unsigned int min_width{512};
    unsigned int min_height{256};
    bool allow_msaa{false};
};

inline bool isEligibleDepthCandidate(DXGI_FORMAT res_fmt,
                                     DXGI_FORMAT dsv_fmt,
                                     unsigned int width,
                                     unsigned int height,
                                     unsigned int depth_or_array_size,
                                     unsigned int sample_count,
                                     unsigned int flags,
                                     const DepthCandidateCriteria& criteria = {}) {
    if (depth_or_array_size != 1) return false;
    if (!criteria.allow_msaa && sample_count != 1) return false;
    if (width < criteria.min_width || height < criteria.min_height) return false;

    // Strict dimension matching if specified
    if (criteria.match_width > 0 && width != criteria.match_width) return false;
    if (criteria.match_height > 0 && height != criteria.match_height) return false;

    // Filter out typical shadow map and cascade atlas aspect ratios (1:1 square, 2:1 atlas, 3:2 atlas)
    if (criteria.match_width == 0 && criteria.match_height == 0) {
        if (width == height) return false;          // 1:1 square shadow map
        if (width == height * 2) return false;      // 2:1 cascade atlas (e.g. 2048x1024, 8192x4096)
        if (width * 2 == height * 3) return false;  // 3:2 cascade atlas (e.g. 6144x4096)
    }

    // D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE = 0x4
    constexpr unsigned int kDenyShaderResource = 0x4;
    if ((flags & kDenyShaderResource) != 0) return false;

    // In D3D12, a depth-stencil resource can only be sampled via an SRV if it was created
    // with a TYPELESS format (e.g. R32G8X24_TYPELESS, R32_TYPELESS). Strongly typed depth formats
    // (such as D32_FLOAT, D24_UNORM_S8_UINT) cannot create shader resource views.
    if (!isTypelessDepthFormat(res_fmt)) return false;

    return depthResourceToSrvFormat(res_fmt, dsv_fmt) != DXGI_FORMAT_UNKNOWN;
}

// Camera near plane empirically read from ChrCam+0x58 in Elden Ring (0.050067 m)
constexpr float kEldenRingNearPlane = 0.050067f;
constexpr float kEldenRingFarPlane = 10000.0f;

// Computes reverse-Z hardware depth for a given view-space distance z (in meters)
inline float linearZToReverseZ(float z, float near_plane = kEldenRingNearPlane, float far_plane = kEldenRingFarPlane) {
    if (z <= 0.0f) return 0.0f;
    return (near_plane * far_plane) / ((far_plane - near_plane) * z) - near_plane / (far_plane - near_plane);
}

// Reconstructs view-space distance z (in meters) from reverse-Z hardware depth gd
inline float reverseZToLinearZ(float gd, float depth_const = kEldenRingNearPlane) {
    if (gd <= 0.0f) return kEldenRingFarPlane;
    return depth_const / gd;
}

// Evaluates the shader discard condition exactly as executed in PSMain
// In D3D12 HLSL, SV_POSITION.w in the pixel shader is the interpolated view-space distance z (in meters)
inline bool shouldDiscardOccluded(float gd, float steve_z, float depth_const = kEldenRingNearPlane, float rel_bias = 0.05f, float abs_bias = 0.05f, float near_skip = 0.0f) {
    if (steve_z <= near_skip || gd <= 0.0f) return false;
    const float scene_z = depth_const / gd;
    return scene_z < steve_z * (1.0f - rel_bias) - abs_bias;
}

} // namespace eldenring::depth

