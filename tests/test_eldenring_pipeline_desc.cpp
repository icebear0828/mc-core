#include <gtest/gtest.h>

#ifdef _WIN32

#include "render/d3d12_pipeline_builder.hpp"

#include "mc/rig.hpp"

namespace {

erov::d3d12::PsoOptions BaseOptions() {
    erov::d3d12::PsoOptions o;
    o.rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    return o;
}

} // namespace

TEST(EldenRingPipelineDesc, RootSignatureHasThreeParams) {
    erov::d3d12::SteveRootDesc d = erov::d3d12::MakeSteveRootDesc();
    const D3D12_ROOT_SIGNATURE_DESC rd = erov::d3d12::SteveRootSignatureDesc(d);
    EXPECT_EQ(rd.NumParameters, 3u);
    EXPECT_EQ(rd.NumStaticSamplers, 1u);
    EXPECT_EQ(rd.pParameters, d.params);
    EXPECT_EQ(rd.pStaticSamplers, &d.sampler);
    EXPECT_TRUE((rd.Flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT) != 0);
}

TEST(EldenRingPipelineDesc, RootConstantsAre44Dwords) {
    const erov::d3d12::SteveRootDesc d = erov::d3d12::MakeSteveRootDesc();
    EXPECT_EQ(d.params[0].ParameterType, D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
    EXPECT_EQ(d.params[0].Constants.Num32BitValues, 44u);
    EXPECT_EQ(d.params[0].ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
}

TEST(EldenRingPipelineDesc, SrvTableHasDepthAndSkin) {
    const erov::d3d12::SteveRootDesc d = erov::d3d12::MakeSteveRootDesc();
    EXPECT_EQ(d.params[1].ParameterType, D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    EXPECT_EQ(d.params[1].DescriptorTable.NumDescriptorRanges, 1u);
    EXPECT_EQ(d.params[1].ShaderVisibility, D3D12_SHADER_VISIBILITY_PIXEL);
    EXPECT_EQ(d.srv_range.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
    EXPECT_EQ(d.srv_range.NumDescriptors, 2u);
}

TEST(EldenRingPipelineDesc, UavIsRegisterU1Pixel) {
    const erov::d3d12::SteveRootDesc d = erov::d3d12::MakeSteveRootDesc();
    EXPECT_EQ(d.params[2].ParameterType, D3D12_ROOT_PARAMETER_TYPE_UAV);
    EXPECT_EQ(d.params[2].Descriptor.ShaderRegister, 1u);
    EXPECT_EQ(d.params[2].ShaderVisibility, D3D12_SHADER_VISIBILITY_PIXEL);
}

TEST(EldenRingPipelineDesc, SamplerIsPointClamp) {
    const erov::d3d12::SteveRootDesc d = erov::d3d12::MakeSteveRootDesc();
    EXPECT_EQ(d.sampler.Filter, D3D12_FILTER_MIN_MAG_MIP_POINT);
    EXPECT_EQ(d.sampler.AddressU, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    EXPECT_EQ(d.sampler.AddressV, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    EXPECT_EQ(d.sampler.AddressW, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    EXPECT_EQ(d.sampler.ComparisonFunc, D3D12_COMPARISON_FUNC_NEVER);
    EXPECT_EQ(d.sampler.ShaderRegister, 0u);
    EXPECT_EQ(d.sampler.ShaderVisibility, D3D12_SHADER_VISIBILITY_PIXEL);
}

TEST(EldenRingPipelineDesc, FigurePsoWritesDepthWithLess) {
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = erov::d3d12::MakePsoDesc(BaseOptions());
    EXPECT_EQ(pd.DepthStencilState.DepthEnable, TRUE);
    EXPECT_EQ(pd.DepthStencilState.DepthWriteMask, D3D12_DEPTH_WRITE_MASK_ALL);
    EXPECT_EQ(pd.DepthStencilState.DepthFunc, D3D12_COMPARISON_FUNC_LESS);
    EXPECT_EQ(pd.DSVFormat, DXGI_FORMAT_D32_FLOAT);
    EXPECT_EQ(pd.RasterizerState.CullMode, D3D12_CULL_MODE_NONE);
    EXPECT_EQ(pd.BlendState.RenderTarget[0].BlendEnable, FALSE);
}

TEST(EldenRingPipelineDesc, DepthViewPsoHasNoDepthAndAlphaBlends) {
    erov::d3d12::PsoOptions o = BaseOptions();
    o.depth_enable = false;
    o.dsv_format = DXGI_FORMAT_UNKNOWN;
    o.alpha_blend = true;
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = erov::d3d12::MakePsoDesc(o);
    EXPECT_EQ(pd.DepthStencilState.DepthEnable, FALSE);
    EXPECT_EQ(pd.DSVFormat, DXGI_FORMAT_UNKNOWN);
    EXPECT_EQ(pd.InputLayout.NumElements, 0u);
    EXPECT_EQ(pd.BlendState.RenderTarget[0].BlendEnable, TRUE);
    EXPECT_EQ(pd.BlendState.RenderTarget[0].SrcBlend, D3D12_BLEND_SRC_ALPHA);
    EXPECT_EQ(pd.BlendState.RenderTarget[0].DestBlend, D3D12_BLEND_INV_SRC_ALPHA);
}

TEST(EldenRingPipelineDesc, ShadowPsoWritesNoDepthAndAlphaBlends) {
    erov::d3d12::PsoOptions o = BaseOptions();
    o.depth_write = D3D12_DEPTH_WRITE_MASK_ZERO;
    o.alpha_blend = true;
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = erov::d3d12::MakePsoDesc(o);
    EXPECT_EQ(pd.DepthStencilState.DepthEnable, TRUE);
    EXPECT_EQ(pd.DepthStencilState.DepthWriteMask, D3D12_DEPTH_WRITE_MASK_ZERO);
    EXPECT_EQ(pd.BlendState.RenderTarget[0].BlendEnable, TRUE);
}

TEST(EldenRingPipelineDesc, InputLayoutMatchesRigVertex) {
    UINT count = 0;
    const D3D12_INPUT_ELEMENT_DESC* layout = erov::d3d12::SteveInputLayout(count);
    ASSERT_EQ(count, 2u);
    EXPECT_STREQ(layout[0].SemanticName, "POSITION");
    EXPECT_EQ(layout[0].Format, DXGI_FORMAT_R32G32B32_FLOAT);
    EXPECT_EQ(layout[0].AlignedByteOffset, 0u);
    EXPECT_STREQ(layout[1].SemanticName, "TEXCOORD");
    EXPECT_EQ(layout[1].Format, DXGI_FORMAT_R32G32_FLOAT);
    EXPECT_EQ(layout[1].AlignedByteOffset, 12u);
    EXPECT_EQ(sizeof(mc::rig::RigVertex), 20u);
}

#endif // _WIN32
