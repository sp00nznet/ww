// =============================================================================
// D3D11 Shader Compilation
// Compiles TEV-generated HLSL shaders at runtime using D3DCompile
// Caches compiled shaders by TEV configuration hash
// =============================================================================

#ifdef _WIN32

#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace ww::gx {

// Basic vertex shader for transformed geometry
static const char* g_vs_source = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color0   : COLOR0;
    float4 color1   : COLOR1;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 color0   : COLOR0;
    float4 color1   : COLOR1;
    float2 texcoord[8] : TEXCOORD0;
};

cbuffer Matrices : register(b0) {
    float4x4 model_view;
    float4x4 projection;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 world_pos = mul(model_view, float4(input.position, 1.0));
    output.position = mul(projection, world_pos);
    output.color0 = input.color0;
    output.color1 = input.color1;
    output.texcoord[0] = input.texcoord0;
    output.texcoord[1] = input.texcoord1;
    // Zero out unused
    for (int i = 2; i < 8; i++) output.texcoord[i] = float2(0, 0);
    return output;
}
)";

struct CompiledShader {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader*  ps = nullptr;
    ID3D11InputLayout*  layout = nullptr;
};

static std::unordered_map<uint64_t, CompiledShader> g_shader_cache;

bool compile_shader(const std::string& source, const char* target,
                    ID3DBlob** blob_out, ID3DBlob** error_out) {
    HRESULT hr = D3DCompile(
        source.c_str(), source.size(),
        nullptr, nullptr, nullptr,
        "main", target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        blob_out, error_out
    );
    if (FAILED(hr)) {
        if (*error_out) {
            fprintf(stderr, "[D3D11] Shader compile error: %s\n",
                    (const char*)(*error_out)->GetBufferPointer());
        }
        return false;
    }
    return true;
}

} // namespace ww::gx

#endif // _WIN32
