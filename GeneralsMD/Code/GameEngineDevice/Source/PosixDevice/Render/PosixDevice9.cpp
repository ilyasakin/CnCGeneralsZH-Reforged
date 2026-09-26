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

// The device off Windows (decision 7): its state, bindings, scenes, presentation, shaders and draws.
// The resource and surface methods, Clear and the caps are -18's (PosixDevice9Resources.cpp,
// PosixD3D9Caps.cpp).  See PosixDevice9.h for who owns what and how a device without a window behaves.

#include "PosixDevice9.h"
#include "SdlGpuFrame.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <vector>

//-------------------------------------------------------------------------------------------------
// Shaders and vertex declarations: held as given.  A3 translates them at the draw.
//-------------------------------------------------------------------------------------------------

namespace
{

/// A vertex or pixel shader's tokens, up to and including D3D's end token.
template <class Interface>
class PosixShader9 : public PosixRefCounted<Interface>
{
public:
	explicit PosixShader9(const RenderUInt32 *function)
	{
		const RenderUInt32 END_TOKEN = 0x0000FFFF;
		do {
			Tokens.push_back(*function);
		} while (*function++ != END_TOKEN);
	}
	std::vector<RenderUInt32> Tokens;
};

class PosixVertexDeclaration9 : public PosixRefCounted<IDirect3DVertexDeclaration9>
{
public:
	explicit PosixVertexDeclaration9(const D3DVERTEXELEMENT9 *elements)
	{
		// D3DDECL_END is the element whose stream is 0xFF.
		do {
			Elements.push_back(*elements);
		} while ((elements++)->Stream != 0xFF);
	}
	std::vector<D3DVERTEXELEMENT9> Elements;
};

}

//-------------------------------------------------------------------------------------------------
// Construction and teardown.
//-------------------------------------------------------------------------------------------------

PosixDevice9::PosixDevice9(PosixDirect3D9 *adapter, RenderWindow window, const D3DPRESENT_PARAMETERS &parameters) :
	Adapter(adapter),
	Gpu(NULL),
	Window(window),
	Parameters(parameters),
	BackBuffer(NULL),
	DepthSurface(NULL),
	DepthStencil(NULL),
	InScene(false),
	Indices(NULL),
	Declaration(NULL),
	FVF(0),
	VertexShader(NULL),
	PixelShader(NULL)
{
	Adapter->AddRef();

	memset(RenderTargets, 0, sizeof(RenderTargets));
	// State starts zeroed.  D3D9's documented defaults (CULLMODE CCW, ZENABLE with an auto depth
	// surface, the stage-0 MODULATE ops, ...) are A3's to set, with the draw that reads them; the engine
	// sets every state it relies on explicitly (DX8Wrapper::Invalidate_Cached_Render_States) before it
	// draws.
	memset(RenderStates, 0, sizeof(RenderStates));
	memset(TextureStageStates, 0, sizeof(TextureStageStates));
	memset(SamplerStates, 0, sizeof(SamplerStates));
	// Every transform starts as the identity, as D3D9's do.
	memset(Transforms, 0, sizeof(Transforms));
	for (int index = 0; index < TRANSFORM_COUNT; ++index) {
		Transforms[index]._11 = Transforms[index]._22 = Transforms[index]._33 = Transforms[index]._44 = 1.0f;
	}
	// The viewport starts as the whole back buffer.
	Viewport.X = 0;
	Viewport.Y = 0;
	Viewport.Width = Parameters.BackBufferWidth;
	Viewport.Height = Parameters.BackBufferHeight;
	Viewport.MinZ = 0.0f;
	Viewport.MaxZ = 1.0f;
	memset(&Material, 0, sizeof(Material));
	memset(Lights, 0, sizeof(Lights));
	memset(LightsEnabled, 0, sizeof(LightsEnabled));
	memset(ClipPlanes, 0, sizeof(ClipPlanes));
	memset(Textures, 0, sizeof(Textures));
	memset(Streams, 0, sizeof(Streams));
	memset(StreamOffsets, 0, sizeof(StreamOffsets));
	memset(StreamStrides, 0, sizeof(StreamStrides));
	memset(VertexShaderConstants, 0, sizeof(VertexShaderConstants));
	memset(PixelShaderConstants, 0, sizeof(PixelShaderConstants));
	for (int index = 0; index < 256; ++index) {
		GammaRamp.red[index] = GammaRamp.green[index] = GammaRamp.blue[index] = (uint16_t)(index * 257);
	}
}

PosixDevice9::~PosixDevice9()
{
	Release_Surfaces();
	for (int index = 0; index < SAMPLER_COUNT; ++index) {
		Posix_Bind(Textures[index], (IDirect3DBaseTexture9 *)NULL);
	}
	for (int index = 0; index < STREAM_COUNT; ++index) {
		Posix_Bind(Streams[index], (IDirect3DVertexBuffer9 *)NULL);
	}
	Posix_Bind(Indices, (IDirect3DIndexBuffer9 *)NULL);
	Posix_Bind(Declaration, (IDirect3DVertexDeclaration9 *)NULL);
	Posix_Bind(VertexShader, (IDirect3DVertexShader9 *)NULL);
	Posix_Bind(PixelShader, (IDirect3DPixelShader9 *)NULL);
	delete Gpu;
	Adapter->Release();
}

RenderResult PosixDevice9::Create_Gpu_Frame()
{
	if (Window == NULL) {
		return D3D_OK;		// -headless: no GPU device at all
	}
	std::string error;
	Gpu = SdlGpuFrame::Create(Window, Parameters.BackBufferWidth, Parameters.BackBufferHeight, error);
	if (Gpu == NULL) {
		fprintf(stderr, "PosixDevice9: a window, and no SDL3 GPU device for it: %s\n", error.c_str());
		return D3DERR_NOTAVAILABLE;
	}
	return D3D_OK;
}

RenderResult PosixDevice9::Gpu_Clear(RenderUInt32 count, const D3DRECT *rects, RenderUInt32 flags, D3DCOLOR color,
	float z, RenderUInt32 stencil)
{
	if (Gpu == NULL) {
		return D3DERR_INVALIDCALL;
	}
	const bool whole_target = (count == 0 || rects == NULL)
		&& RenderTargets[0] == BackBuffer
		&& Viewport.X == 0 && Viewport.Y == 0
		&& Viewport.Width == Parameters.BackBufferWidth && Viewport.Height == Parameters.BackBufferHeight;
	if (!whole_target) {
		static bool said = false;
		if (!said) {
			said = true;
			fprintf(stderr, "PosixDevice9::Clear: part of a target, or a target not the back buffer, waits for A3c's clear draw; refused\n");
		}
		return D3DERR_INVALIDCALL;
	}
	Gpu->Clear_Back_Buffer((flags & D3DCLEAR_TARGET) != 0, (flags & D3DCLEAR_ZBUFFER) != 0,
		(flags & D3DCLEAR_STENCIL) != 0, color, z, stencil);
	return D3D_OK;
}

void PosixDevice9::Release_Surfaces()
{
	for (int index = 0; index < RENDER_TARGET_COUNT; ++index) {
		Posix_Bind(RenderTargets[index], (IDirect3DSurface9 *)NULL);
	}
	Posix_Bind(DepthStencil, (IDirect3DSurface9 *)NULL);
	Posix_Bind(BackBuffer, (IDirect3DSurface9 *)NULL);
	Posix_Bind(DepthSurface, (IDirect3DSurface9 *)NULL);
}

//-------------------------------------------------------------------------------------------------
// The device as a whole.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::TestCooperativeLevel()
{
	return D3D_OK;		// never lost: there is no exclusive mode to lose
}

unsigned int PosixDevice9::GetAvailableTextureMem()
{
	return 512u * 1024u * 1024u;	// what the engine sizes its texture reduction by; memory is the host's
}

RenderResult PosixDevice9::EvictManagedResources()
{
	return D3D_OK;
}

RenderResult PosixDevice9::GetDisplayMode(unsigned int swap_chain, D3DDISPLAYMODE *mode)
{
	if (swap_chain != 0 || mode == NULL) {
		return D3DERR_INVALIDCALL;
	}
	if (Parameters.Windowed) {
		return Adapter->GetAdapterDisplayMode(0, mode);
	}
	mode->Width = Parameters.BackBufferWidth;
	mode->Height = Parameters.BackBufferHeight;
	mode->RefreshRate = Parameters.FullScreen_RefreshRateInHz;
	mode->Format = Parameters.BackBufferFormat;
	return D3D_OK;
}

// The hardware cursor is C3's (W3DMouse), and it does not go through the device off Windows.
RenderResult PosixDevice9::SetCursorProperties(unsigned int, unsigned int, IDirect3DSurface9 *)
{
	return D3D_OK;
}

void PosixDevice9::SetCursorPosition(int, int, RenderUInt32)
{
}

int PosixDevice9::ShowCursor(int)
{
	return 0;
}

RenderResult PosixDevice9::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **swap_chain)
{
	if (swap_chain != NULL) {
		*swap_chain = NULL;
	}
	return D3DERR_NOTAVAILABLE;	// the engine makes none
}

RenderResult PosixDevice9::Reset(D3DPRESENT_PARAMETERS *parameters)
{
	if (parameters == NULL || parameters->BackBufferWidth == 0 || parameters->BackBufferHeight == 0) {
		return D3DERR_INVALIDCALL;
	}
	Release_Surfaces();
	Parameters = *parameters;
	if (Parameters.hDeviceWindow != NULL) {
		Window = Parameters.hDeviceWindow;
	}
	Viewport.X = 0;
	Viewport.Y = 0;
	Viewport.Width = Parameters.BackBufferWidth;
	Viewport.Height = Parameters.BackBufferHeight;
	Viewport.MinZ = 0.0f;
	Viewport.MaxZ = 1.0f;
	if (Gpu != NULL && !Gpu->Resize(Parameters.BackBufferWidth, Parameters.BackBufferHeight)) {
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	return Create_Implicit_Surfaces();
}

RenderResult PosixDevice9::Present(const RenderRect *, const RenderRect *, RenderWindow, const void *)
{
	// With no window there is nothing to present to.  With one, the back buffer goes to it through the
	// gamma ramp (SdlGpuFrame::Present); D3DGAMMARAMP is the three 256-entry ramps, red, green, blue.
	if (Gpu == NULL) {
		return D3D_OK;
	}
	static_assert(offsetof(D3DGAMMARAMP, green) == 512 && offsetof(D3DGAMMARAMP, blue) == 1024,
		"D3DGAMMARAMP is the three ramps back to back, as SdlGpuFrame::Present reads it");
	if (!Gpu->Present(reinterpret_cast<const uint16_t (*)[256]>(&GammaRamp))) {
		static bool said = false;
		if (!said) {
			said = true;
			fprintf(stderr, "PosixDevice9::Present: the GPU refused the frame\n");
		}
		return D3DERR_DRIVERINTERNALERROR;
	}
	return D3D_OK;
}

void PosixDevice9::SetGammaRamp(unsigned int, RenderUInt32, const D3DGAMMARAMP *ramp)
{
	if (ramp != NULL) {
		GammaRamp = *ramp;	// kept for A3, which applies it to what it presents
	}
}

//-------------------------------------------------------------------------------------------------
// Scenes and state.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::BeginScene()
{
	if (InScene) {
		return D3DERR_INVALIDCALL;
	}
	InScene = true;
	return D3D_OK;
}

RenderResult PosixDevice9::EndScene()
{
	if (!InScene) {
		return D3DERR_INVALIDCALL;
	}
	InScene = false;
	return D3D_OK;
}

RenderResult PosixDevice9::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
	if ((unsigned int)state >= TRANSFORM_COUNT || matrix == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Transforms[state] = *matrix;
	return D3D_OK;
}

RenderResult PosixDevice9::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned int)state >= TRANSFORM_COUNT || matrix == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*matrix = Transforms[state];
	return D3D_OK;
}

RenderResult PosixDevice9::SetViewport(const D3DVIEWPORT9 *viewport)
{
	if (viewport == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Viewport = *viewport;
	return D3D_OK;
}

RenderResult PosixDevice9::GetViewport(D3DVIEWPORT9 *viewport)
{
	if (viewport == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*viewport = Viewport;
	return D3D_OK;
}

RenderResult PosixDevice9::SetMaterial(const D3DMATERIAL9 *material)
{
	if (material == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Material = *material;
	return D3D_OK;
}

RenderResult PosixDevice9::SetLight(RenderUInt32 index, const D3DLIGHT9 *light)
{
	if (index >= LIGHT_COUNT || light == NULL) {
		return D3DERR_INVALIDCALL;
	}
	Lights[index] = *light;
	return D3D_OK;
}

RenderResult PosixDevice9::LightEnable(RenderUInt32 index, int enable)
{
	if (index >= LIGHT_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	LightsEnabled[index] = enable != 0;
	return D3D_OK;
}

RenderResult PosixDevice9::SetClipPlane(RenderUInt32 index, const float *plane)
{
	if (index >= CLIP_PLANE_COUNT || plane == NULL) {
		return D3DERR_INVALIDCALL;
	}
	memcpy(ClipPlanes[index], plane, sizeof(ClipPlanes[index]));
	return D3D_OK;
}

RenderResult PosixDevice9::SetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 value)
{
	if ((unsigned int)state >= RENDER_STATE_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	RenderStates[state] = value;
	return D3D_OK;
}

RenderResult PosixDevice9::GetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 *value)
{
	if ((unsigned int)state >= RENDER_STATE_COUNT || value == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*value = RenderStates[state];
	return D3D_OK;
}

RenderResult PosixDevice9::GetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 **texture)
{
	if (stage >= SAMPLER_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	return Posix_Hand_Out(Textures[stage], texture);
}

RenderResult PosixDevice9::SetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 *texture)
{
	if (stage >= SAMPLER_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	Posix_Bind(Textures[stage], texture);
	return D3D_OK;
}

RenderResult PosixDevice9::GetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 *value)
{
	if (stage >= TEXTURE_STAGE_COUNT || (unsigned int)type >= TEXTURE_STAGE_STATE_COUNT || value == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*value = TextureStageStates[stage][type];
	return D3D_OK;
}

RenderResult PosixDevice9::SetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 value)
{
	if (stage >= TEXTURE_STAGE_COUNT || (unsigned int)type >= TEXTURE_STAGE_STATE_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	TextureStageStates[stage][type] = value;
	return D3D_OK;
}

RenderResult PosixDevice9::SetSamplerState(RenderUInt32 sampler, D3DSAMPLERSTATETYPE type, RenderUInt32 value)
{
	if (sampler >= SAMPLER_COUNT || (unsigned int)type >= SAMPLER_STATE_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	SamplerStates[sampler][type] = value;
	return D3D_OK;
}

RenderResult PosixDevice9::ValidateDevice(RenderUInt32 *passes)
{
	if (passes != NULL) {
		*passes = 1;
	}
	return D3D_OK;
}

RenderResult PosixDevice9::SetSoftwareVertexProcessing(int)
{
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
// Draws.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::Draw_Unavailable(const char *what)
{
	// -headless: Windows draws into a hidden window nobody sees, so succeeding and drawing nothing is
	// the same effect (decision 8's refinement, 2026-09-26).
	if (Window == NULL) {
		return D3D_OK;
	}
	static bool said = false;
	if (!said) {
		said = true;
		fprintf(stderr, "PosixDevice9::%s: there is no draw into a window until the SDL3 GPU draw (A3); refused\n", what);
	}
	return D3DERR_INVALIDCALL;
}

RenderResult PosixDevice9::DrawPrimitive(D3DPRIMITIVETYPE, unsigned int, unsigned int)
{
	return Draw_Unavailable("DrawPrimitive");
}

RenderResult PosixDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE, int, unsigned int, unsigned int, unsigned int,
	unsigned int)
{
	return Draw_Unavailable("DrawIndexedPrimitive");
}

RenderResult PosixDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE, unsigned int, const void *vertices, unsigned int)
{
	if (vertices == NULL) {
		return D3DERR_INVALIDCALL;
	}
	return Draw_Unavailable("DrawPrimitiveUP");
}

RenderResult PosixDevice9::ProcessVertices(unsigned int, unsigned int, unsigned int, IDirect3DVertexBuffer9 *,
	IDirect3DVertexDeclaration9 *, RenderUInt32)
{
	return Draw_Unavailable("ProcessVertices");
}

//-------------------------------------------------------------------------------------------------
// Vertex input and shaders.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration)
{
	if (elements == NULL || declaration == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*declaration = new PosixVertexDeclaration9(elements);
	return D3D_OK;
}

RenderResult PosixDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration)
{
	Posix_Bind(Declaration, declaration);
	return D3D_OK;
}

RenderResult PosixDevice9::SetFVF(RenderUInt32 fvf)
{
	FVF = fvf;
	return D3D_OK;
}

RenderResult PosixDevice9::CreateVertexShader(const RenderUInt32 *function, IDirect3DVertexShader9 **shader)
{
	if (function == NULL || shader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*shader = new PosixShader9<IDirect3DVertexShader9>(function);
	return D3D_OK;
}

RenderResult PosixDevice9::SetVertexShader(IDirect3DVertexShader9 *shader)
{
	Posix_Bind(VertexShader, shader);
	return D3D_OK;
}

RenderResult PosixDevice9::GetVertexShader(IDirect3DVertexShader9 **shader)
{
	return Posix_Hand_Out(VertexShader, shader);
}

RenderResult PosixDevice9::SetVertexShaderConstantF(unsigned int start, const float *data, unsigned int count)
{
	if (data == NULL || start > VERTEX_SHADER_CONSTANT_COUNT || count > VERTEX_SHADER_CONSTANT_COUNT - start) {
		return D3DERR_INVALIDCALL;
	}
	memcpy(VertexShaderConstants[start], data, count * sizeof(VertexShaderConstants[0]));
	return D3D_OK;
}

RenderResult PosixDevice9::SetStreamSource(unsigned int stream, IDirect3DVertexBuffer9 *buffer, unsigned int offset,
	unsigned int stride)
{
	if (stream >= STREAM_COUNT) {
		return D3DERR_INVALIDCALL;
	}
	Posix_Bind(Streams[stream], buffer);
	StreamOffsets[stream] = offset;
	StreamStrides[stream] = stride;
	return D3D_OK;
}

RenderResult PosixDevice9::SetIndices(IDirect3DIndexBuffer9 *buffer)
{
	Posix_Bind(Indices, buffer);
	return D3D_OK;
}

RenderResult PosixDevice9::GetIndices(IDirect3DIndexBuffer9 **buffer)
{
	return Posix_Hand_Out(Indices, buffer);
}

RenderResult PosixDevice9::CreatePixelShader(const RenderUInt32 *function, IDirect3DPixelShader9 **shader)
{
	if (function == NULL || shader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*shader = new PosixShader9<IDirect3DPixelShader9>(function);
	return D3D_OK;
}

RenderResult PosixDevice9::SetPixelShader(IDirect3DPixelShader9 *shader)
{
	Posix_Bind(PixelShader, shader);
	return D3D_OK;
}

RenderResult PosixDevice9::GetPixelShader(IDirect3DPixelShader9 **shader)
{
	return Posix_Hand_Out(PixelShader, shader);
}

RenderResult PosixDevice9::SetPixelShaderConstantF(unsigned int start, const float *data, unsigned int count)
{
	if (data == NULL || start > PIXEL_SHADER_CONSTANT_COUNT || count > PIXEL_SHADER_CONSTANT_COUNT - start) {
		return D3DERR_INVALIDCALL;
	}
	memcpy(PixelShaderConstants[start], data, count * sizeof(PixelShaderConstants[0]));
	return D3D_OK;
}
