// w3d_view's vertex shader in HLSL, the language the game's own generators emit (ffshader.cpp,
// ffvertex.cpp, engineshader.h).  The same computation as model.vert / model.frag.  It exists to
// draw a real frame through the route D3 is being recommended, not only to compile through it:
// HLSL --glslang--> SPIR-V (Vulkan) --SPIRV-Cross--> MSL (Metal).  One file per stage, as the
// generators emit them.
//
// Registers carry SDL_gpu.h's spaces: vertex uniforms in space1; fragment textures and samplers in
// space2 and fragment uniforms in space3.  That is the one change the generators' output needs.
// Texture and sampler are declared apart, as the generators declare them.

cbuffer VertexUniforms : register(b0, space1)
{
	float4x4 viewProjection;
};

struct VertexInput
{
	[[vk::location(0)]] float3 position : TEXCOORD0;
	[[vk::location(1)]] float3 normal : TEXCOORD1;
	[[vk::location(2)]] float2 texCoord : TEXCOORD2;
};

struct VertexOutput
{
	float4 position : SV_Position;
	[[vk::location(0)]] float3 normal : TEXCOORD0;
	[[vk::location(1)]] float2 texCoord : TEXCOORD1;
};

VertexOutput vsMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(viewProjection, float4(input.position, 1.0));
	output.normal = input.normal;
	output.texCoord = input.texCoord;
	return output;
}
