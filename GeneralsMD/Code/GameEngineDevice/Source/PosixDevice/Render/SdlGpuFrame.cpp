/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// The device's frame on SDL3 GPU (decision 7, phase A3a).  See SdlGpuFrame.h.

#include "SdlGpuFrame.h"
#include "sdl3shadercompile.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The back buffer's format.  D3D9's X8R8G8B8 and A8R8G8B8 are these bytes in this order.
static const SDL_GPUTextureFormat BACK_BUFFER_FORMAT = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

// The gamma pass: one triangle over the whole target, and each channel looked up in its ramp.  Written
// for SDL3's register spaces (sdl3target.h: pixel textures and samplers in space2).  The ramp is read
// with a point sampler at its entry's centre, so the lookup is exact.
static const char GAMMA_VERTEX_HLSL[] =
	"struct Output\n"
	"{\n"
	"	float4 Position : SV_Position;\n"
	"	[[vk::location(0)]] float2 Uv : TEXCOORD0;\n"
	"};\n"
	"Output main(uint id : SV_VertexID)\n"
	"{\n"
	"	Output output;\n"
	"	float2 corner = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);\n"
	"	output.Position = float4(corner, 0.0, 1.0);\n"
	"	output.Uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);\n"
	"	return output;\n"
	"}\n";

static const char GAMMA_PIXEL_HLSL[] =
	"Texture2D BackBuffer : register(t0, space2);\n"
	"SamplerState BackSampler : register(s0, space2);\n"
	"Texture2D Ramp : register(t1, space2);\n"
	"SamplerState RampSampler : register(s1, space2);\n"
	"float entry(float value, int channel)\n"
	"{\n"
	"	float index = round(saturate(value) * 255.0);\n"
	"	return Ramp.SampleLevel(RampSampler, float2((index + 0.5) / 256.0, 0.5), 0.0)[channel];\n"
	"}\n"
	"float4 main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0\n"
	"{\n"
	"	float4 colour = BackBuffer.SampleLevel(BackSampler, uv, 0.0);\n"
	"	return float4(entry(colour.r, 0), entry(colour.g, 1), entry(colour.b, 2), colour.a);\n"
	"}\n";

bool Sdl_Gamma_Is_Identity(const uint16_t (*ramp)[256])
{
	if (ramp == NULL) {
		return true;
	}
	for (int channel = 0; channel < 3; ++channel) {
		for (int index = 0; index < 256; ++index) {
			if (ramp[channel][index] != (uint16_t)(index * 257)) {
				return false;
			}
		}
	}
	return true;
}

unsigned int SdlGpuFrame::Target_Format()
{
	return BACK_BUFFER_FORMAT;
}

SdlGpuFrame::SdlGpuFrame() :
	GpuDevice(NULL),
	Window(NULL),
	BackBuffer(NULL),
	DepthStencil(NULL),
	DepthFormat(SDL_GPU_TEXTUREFORMAT_INVALID),
	BackWidth(0),
	BackHeight(0),
	ClearColour(false),
	ClearDepth(false),
	ClearStencil(false),
	ClearArgb(0),
	ClearZ(1.0f),
	ClearStencilValue(0),
	GammaVertex(NULL),
	GammaPixel(NULL),
	GammaPipeline(NULL),
	GammaPipelineFormat(SDL_GPU_TEXTUREFORMAT_INVALID),
	RampTexture(NULL),
	PointSampler(NULL)
{
}

SdlGpuFrame * SdlGpuFrame::Create(RenderWindow window, unsigned int width, unsigned int height, std::string & error)
{
	SdlGpuFrame * frame = new SdlGpuFrame();
	// Debug validation only when asked: it is slow, and the game runs without it.
	const bool debug = getenv("ZH_GPU_DEBUG") != NULL;
	frame->GpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, debug, NULL);
	if (frame->GpuDevice == NULL) {
		error = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
		delete frame;
		return NULL;
	}
	// The one place a RenderWindow is taken back to what it is: C2 hands the device its SDL_Window.
	frame->Window = reinterpret_cast<SDL_Window *>(window);
	if (frame->Window != NULL && !SDL_ClaimWindowForGPUDevice(frame->GpuDevice, frame->Window)) {
		error = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
		frame->Window = NULL;
		delete frame;
		return NULL;
	}
	// D24S8 is what D3D9's auto depth-stencil almost always is; Apple's GPUs have none, and take D32S8.
	frame->DepthFormat = SDL_GPUTextureSupportsFormat(frame->GpuDevice, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
		SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
		? SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
	if (!frame->Create_Targets(width, height)) {
		error = std::string("the back buffer: ") + SDL_GetError();
		delete frame;
		return NULL;
	}
	return frame;
}

SdlGpuFrame::~SdlGpuFrame()
{
	if (GpuDevice == NULL) {
		return;
	}
	Release_Targets();
	if (GammaPipeline != NULL) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, GammaPipeline);
	if (GammaVertex != NULL) SDL_ReleaseGPUShader(GpuDevice, GammaVertex);
	if (GammaPixel != NULL) SDL_ReleaseGPUShader(GpuDevice, GammaPixel);
	if (RampTexture != NULL) SDL_ReleaseGPUTexture(GpuDevice, RampTexture);
	if (PointSampler != NULL) SDL_ReleaseGPUSampler(GpuDevice, PointSampler);
	if (Window != NULL) SDL_ReleaseWindowFromGPUDevice(GpuDevice, Window);
	SDL_DestroyGPUDevice(GpuDevice);
}

bool SdlGpuFrame::Create_Targets(unsigned int width, unsigned int height)
{
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = BACK_BUFFER_FORMAT;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	BackBuffer = SDL_CreateGPUTexture(GpuDevice, &info);
	info.format = (SDL_GPUTextureFormat)DepthFormat;
	info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	DepthStencil = SDL_CreateGPUTexture(GpuDevice, &info);
	BackWidth = width;
	BackHeight = height;
	return BackBuffer != NULL && DepthStencil != NULL;
}

void SdlGpuFrame::Release_Targets()
{
	if (BackBuffer != NULL) SDL_ReleaseGPUTexture(GpuDevice, BackBuffer);
	if (DepthStencil != NULL) SDL_ReleaseGPUTexture(GpuDevice, DepthStencil);
	BackBuffer = NULL;
	DepthStencil = NULL;
}

bool SdlGpuFrame::Resize(unsigned int width, unsigned int height)
{
	Release_Targets();
	ClearColour = ClearDepth = ClearStencil = false;
	return Create_Targets(width, height);
}

void SdlGpuFrame::Clear_Back_Buffer(bool colour, bool depth, bool stencil, uint32_t argb, float z, uint32_t stencil_value)
{
	if (colour) {
		ClearColour = true;
		ClearArgb = argb;
	}
	if (depth) {
		ClearDepth = true;
		ClearZ = z;
	}
	if (stencil) {
		ClearStencil = true;
		ClearStencilValue = stencil_value;
	}
}

// A pass over the back buffer whose only work is its load operations: the recorded clear.  In A3c the
// recorded draws follow in the same pass.
bool SdlGpuFrame::Record_Clear_Pass(SDL_GPUCommandBuffer * commands)
{
	if (!ClearColour && !ClearDepth && !ClearStencil) {
		return true;
	}
	SDL_GPUColorTargetInfo colour;
	SDL_zero(colour);
	colour.texture = BackBuffer;
	colour.clear_color.a = (float)((ClearArgb >> 24) & 0xFF) / 255.0f;
	colour.clear_color.r = (float)((ClearArgb >> 16) & 0xFF) / 255.0f;
	colour.clear_color.g = (float)((ClearArgb >> 8) & 0xFF) / 255.0f;
	colour.clear_color.b = (float)(ClearArgb & 0xFF) / 255.0f;
	colour.load_op = ClearColour ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	colour.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPUDepthStencilTargetInfo depth;
	SDL_zero(depth);
	depth.texture = DepthStencil;
	depth.clear_depth = ClearZ;
	depth.clear_stencil = (Uint8)ClearStencilValue;
	depth.load_op = ClearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depth.store_op = SDL_GPU_STOREOP_STORE;
	depth.stencil_load_op = ClearStencil ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depth.stencil_store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass * pass = SDL_BeginGPURenderPass(commands, &colour, 1, &depth);
	if (pass == NULL) {
		return false;
	}
	SDL_EndGPURenderPass(pass);
	ClearColour = ClearDepth = ClearStencil = false;
	return true;
}

bool SdlGpuFrame::Flush()
{
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	const bool recorded = Record_Clear_Pass(commands);
	return SDL_SubmitGPUCommandBuffer(commands) && recorded;
}

SDL_GPUGraphicsPipeline * SdlGpuFrame::Gamma_Pipeline(unsigned int format)
{
	if (GammaPipeline != NULL && GammaPipelineFormat == format) {
		return GammaPipeline;
	}
	std::string log;
	if (GammaVertex == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(GAMMA_VERTEX_HLSL, true, spirv, log)
			|| (GammaVertex = SDL3_Create_Shader(GpuDevice, spirv, true, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the gamma pass's vertex program: %s\n", log.c_str());
			return NULL;
		}
	}
	if (GammaPixel == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(GAMMA_PIXEL_HLSL, false, spirv, log)
			|| (GammaPixel = SDL3_Create_Shader(GpuDevice, spirv, false, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the gamma pass's pixel program: %s\n", log.c_str());
			return NULL;
		}
	}
	if (GammaPipeline != NULL) {
		SDL_ReleaseGPUGraphicsPipeline(GpuDevice, GammaPipeline);
		GammaPipeline = NULL;
	}
	SDL_GPUColorTargetDescription target;
	SDL_zero(target);
	target.format = (SDL_GPUTextureFormat)format;
	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_zero(info);
	info.vertex_shader = GammaVertex;
	info.fragment_shader = GammaPixel;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	GammaPipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice, &info);
	GammaPipelineFormat = format;
	if (GammaPipeline == NULL) {
		fprintf(stderr, "SdlGpuFrame: the gamma pass's pipeline: %s\n", SDL_GetError());
	}
	return GammaPipeline;
}

// The three ramps as a 256 x 1 R16G16B16A16 texture, uploaded only when they change.
bool SdlGpuFrame::Upload_Ramp(SDL_GPUCommandBuffer * commands, const uint16_t (*ramp)[256])
{
	std::vector<uint16_t> texels(256 * 4);
	for (int index = 0; index < 256; ++index) {
		texels[index * 4 + 0] = ramp[0][index];
		texels[index * 4 + 1] = ramp[1][index];
		texels[index * 4 + 2] = ramp[2][index];
		texels[index * 4 + 3] = 0xFFFF;
	}
	if (RampTexture != NULL && texels == UploadedRamp) {
		return true;
	}
	if (RampTexture == NULL) {
		SDL_GPUTextureCreateInfo info;
		SDL_zero(info);
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = 256;
		info.height = 1;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		RampTexture = SDL_CreateGPUTexture(GpuDevice, &info);
		SDL_GPUSamplerCreateInfo sampler;
		SDL_zero(sampler);
		sampler.min_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		PointSampler = SDL_CreateGPUSampler(GpuDevice, &sampler);
		if (RampTexture == NULL || PointSampler == NULL) {
			return false;
		}
	}
	const Uint32 bytes = (Uint32)(texels.size() * sizeof(uint16_t));
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
	if (mapped == NULL) {
		SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
		return false;
	}
	memcpy(mapped, &texels[0], bytes);
	SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureTransferInfo source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = RampTexture;
	region.w = 256;
	region.h = 1;
	region.d = 1;
	SDL_UploadToGPUTexture(copy, &source, &region, false);
	SDL_EndGPUCopyPass(copy);
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	UploadedRamp = texels;
	return true;
}

bool SdlGpuFrame::Present_Into(SDL_GPUCommandBuffer * commands, SDL_GPUTexture * target, unsigned int width,
	unsigned int height, unsigned int format, const uint16_t (*ramp)[256])
{
	if (Sdl_Gamma_Is_Identity(ramp)) {
		SDL_GPUBlitInfo blit;
		SDL_zero(blit);
		blit.source.texture = BackBuffer;
		blit.source.w = BackWidth;
		blit.source.h = BackHeight;
		blit.destination.texture = target;
		blit.destination.w = width;
		blit.destination.h = height;
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.filter = SDL_GPU_FILTER_LINEAR;
		SDL_BlitGPUTexture(commands, &blit);
		return true;
	}
	SDL_GPUGraphicsPipeline * pipeline = Gamma_Pipeline(format);
	if (pipeline == NULL || !Upload_Ramp(commands, ramp)) {
		return false;
	}
	SDL_GPUColorTargetInfo colour;
	SDL_zero(colour);
	colour.texture = target;
	colour.load_op = SDL_GPU_LOADOP_DONT_CARE;
	colour.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass * pass = SDL_BeginGPURenderPass(commands, &colour, 1, NULL);
	if (pass == NULL) {
		return false;
	}
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_GPUTextureSamplerBinding bindings[2];
	bindings[0].texture = BackBuffer;
	bindings[0].sampler = PointSampler;
	bindings[1].texture = RampTexture;
	bindings[1].sampler = PointSampler;
	SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	return true;
}

bool SdlGpuFrame::Present(const uint16_t (*ramp)[256])
{
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	bool ok = Record_Clear_Pass(commands);
	if (Window != NULL) {
		SDL_GPUTexture * swapchain = NULL;
		Uint32 width = 0;
		Uint32 height = 0;
		// No texture (a minimised window) is not a failure: there is nothing to show this frame.
		if (SDL_WaitAndAcquireGPUSwapchainTexture(commands, Window, &swapchain, &width, &height) && swapchain != NULL) {
			ok = Present_Into(commands, swapchain, width, height, SDL_GetGPUSwapchainTextureFormat(GpuDevice, Window), ramp)
				&& ok;
		}
	}
	return SDL_SubmitGPUCommandBuffer(commands) && ok;
}

bool SdlGpuFrame::Present_To(SDL_GPUTexture * target, unsigned int width, unsigned int height,
	const uint16_t (*ramp)[256])
{
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	bool ok = Record_Clear_Pass(commands);
	ok = Present_Into(commands, target, width, height, BACK_BUFFER_FORMAT, ramp) && ok;
	return SDL_SubmitGPUCommandBuffer(commands) && ok;
}

bool SdlGpuFrame::Upload_Back_Buffer(const std::vector<uint8_t> & bgra)
{
	const Uint32 bytes = BackWidth * BackHeight * 4;
	if (bgra.size() != bytes) {
		return false;
	}
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
	if (mapped == NULL) {
		SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
		return false;
	}
	memcpy(mapped, &bgra[0], bytes);
	SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureTransferInfo source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	source.pixels_per_row = BackWidth;
	source.rows_per_layer = BackHeight;
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = BackBuffer;
	region.w = BackWidth;
	region.h = BackHeight;
	region.d = 1;
	SDL_UploadToGPUTexture(copy, &source, &region, false);
	SDL_EndGPUCopyPass(copy);
	const bool ok = SDL_SubmitGPUCommandBuffer(commands);
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	return ok;
}

bool SdlGpuFrame::Read_Back(SDL_GPUTexture * texture, unsigned int width, unsigned int height, std::vector<uint8_t> & bgra)
{
	if (!Flush()) {
		return false;
	}
	const Uint32 bytes = width * height * 4;
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = texture;
	region.w = width;
	region.h = height;
	region.d = 1;
	SDL_GPUTextureTransferInfo destination;
	SDL_zero(destination);
	destination.transfer_buffer = transfer;
	destination.pixels_per_row = width;
	destination.rows_per_layer = height;
	SDL_DownloadFromGPUTexture(copy, &region, &destination);
	SDL_EndGPUCopyPass(copy);
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	bool ok = fence != NULL && SDL_WaitForGPUFences(GpuDevice, true, &fence, 1);
	if (fence != NULL) {
		SDL_ReleaseGPUFence(GpuDevice, fence);
	}
	if (ok) {
		const void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
		ok = mapped != NULL;
		if (ok) {
			bgra.assign((const uint8_t *)mapped, (const uint8_t *)mapped + bytes);
			SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
		}
	}
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	return ok;
}
