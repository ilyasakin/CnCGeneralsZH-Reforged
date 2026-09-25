#version 450
// w3d_view's vertex shader: the one source for both Vulkan (SPIR-V, compiled by glslang) and Metal
// (MSL, translated from that SPIR-V).  Resource sets follow SDL_gpu.h's rule for SPIR-V vertex
// shaders: set 1 holds uniform buffers.  Vertices arrive already in model space, Z up.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;

layout(set = 1, binding = 0) uniform VertexUniforms
{
	mat4 viewProjection;
} vertexUniforms;

void main()
{
	gl_Position = vertexUniforms.viewProjection * vec4(inPosition, 1.0);
	outNormal = inNormal;
	outTexCoord = inTexCoord;
}
