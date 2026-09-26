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

// The game's own draws against FFReference (A3's DONE: "every pipeline key checked").  ZH_GPU_CAPTURE
// writes the first draw of each signature a run makes (DrawCapture.h); this replays each one through the
// device on this machine's GPU and through FFReference, and compares them the way test_ffref_gpu does.
// ZH_FFREF_CAPTURE_DIR names the captures; without it there is nothing to replay, and the test skips (77).
// The captures come from the user's install, so they live in a scratch directory and never in the repository.
//
// Each draw is replayed alone, into a target of its own size and format cleared to one colour, with depth
// 1 and stencil 0.  Both sides get the same state, vertices and texels.  The reference's textures are
// decoded with A2's codec (Sdl_Convert_Level), the device's own: a codec error is invisible here, and
// test_posix_resources covers the codec.  The GPU samples the block formats natively, so for those the
// two sides decode separately.
//
// One substitution, on both sides and counted: an ANISOTROPIC filter is replayed as LINEAR.  The game
// asks for anisotropic filtering with MAXANISOTROPY 16 on nearly every draw, and FFReference refuses it
// above 1 (the pages define no footprint), so without this almost nothing is compared.  The device's
// anisotropic sampler is what goes unchecked; everything else in those draws is checked.
//
// A triage aid: FFREF_CAPTURE_NO_MIPS=1 replays with MIPFILTER NONE on both sides.  A failure that goes
// away with it is the level of detail's, which is what FFReference's LOD freedom (N15) is about.
//
// WHAT THIS DOES NOT PROVE: a draw that depends on what was under it.  The capture has no pixels, depth
// or stencil from before the draw, so an EQUAL depth test or a stencil test against the frame is
// checked against the clear, and it may draw nothing.  Those draws are counted as "drew nothing".

#include "DrawCapture.h"
#include "PosixDevice9.h"
#include "PosixResources9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
#include "ffreference/ffreference.h"

#include <SDL3/SDL.h>

#include <dirent.h>
#include <math.h>
#include <sys/stat.h>
#include <algorithm>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static const uint32_t CLEAR = 0xFF3F2F1F;

static int failures = 0;
static int compared = 0;
static int drew_nothing = 0;
static int anisotropic_as_linear = 0;
static int known_findings = 0;
static std::map<std::string, int> not_replayed;

/// Findings recorded in the task doc (tasks/A-posix-d3d9-device.md, "The capture layer") and waiting on a
/// ruling, by signature.  A known signature that fails is reported and not counted; one that passes fails
/// the run, so the list cannot outlive the finding.  A signature the captures do not have is not checked.
struct Known
{
	const char *Signature;
	const char *Finding;
};
static const Known KNOWN[] = {
	// C1: small, minified models: the texels differ past the LOD freedom, and match with mipmapping off.
	{ "274:101:0,0,0,0:L3:L3:L3:T0,0:V:F0,0 | 1:4,1,2,0,4,1,2,0,0,1:A1,7,F0 | pipeline 94988b3625682a23", "C1" },
	{ "274:101:0,0,0,0:L3:L3:L3:T0,0:V:F0,0 | 1:4,1,2,0,4,1,2,0,0,1:A0,0,F0 | pipeline 153437af07b1e83e", "C1" },
	{ "274:101:0,0,0,0:L3:L3:L3:T0,0:V:F0,0 | 1:4,1,2,0,4,1,2,0,0,1:A1,7,F0 | pipeline 1cf93c62fb232e81", "C1" },
	{ "594:101:0,0,0,0:L3:L3:L3:T0,0:V:F0,0 | 1:4,1,2,0,4,1,2,0,0,1:A0,0,F0 | pipeline cad7347369d97b18", "C1" },
	{ "594:101:0,0,0,0:L3:T0,0:V:F0,0 | 1:4,35,2,0,4,1,2,0,0,1:A0,0,F0 | pipeline a969c342bbda3198", "C1" },
	// C2: alpha-tested foliage with the cloud shadow's camera-space texture coordinates: a few pixels a few
	// levels past the envelope, and not the level of detail's (worse with mipmapping off).
	{ "338:001:1,0,0,0:T0,0:T131072,2:F0,0 | 2:4,1,2,0,4,1,2,0,0,1:4,1,2,1,3,1,2,1,0,1:A1,7,F0 | pipeline 562de4611b89f9e3", "C2" },
	{ "338:001:1,0,0,0:T0,0:T131072,2:F0,0 | 2:4,35,2,0,4,1,2,0,0,1:4,1,2,1,3,1,2,1,0,1:A1,7,F0 | pipeline 562de4611b89f9e3", "C2" },
};

static const char *known_finding(const char *signature)
{
	for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
		if (strcmp(KNOWN[i].Signature, signature) == 0) return KNOWN[i].Finding;
	}
	return NULL;
}

static bool read_file(const std::string &path, std::vector<uint8_t> &bytes)
{
	FILE *file = fopen(path.c_str(), "rb");
	if (file == NULL) {
		return false;
	}
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes.resize(size > 0 ? (size_t)size : 0);
	const bool ok = size > 0 && fread(&bytes[0], 1, bytes.size(), file) == bytes.size();
	fclose(file);
	return ok;
}

static void matrix_to_ref(const D3DMATRIX &m, FFRef::Matrix &out)
{
	const float *f = &m._11;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			out.m[r][c] = f[r * 4 + c];
}

static FFRef::Color colour(const D3DCOLORVALUE &c)
{
	FFRef::Color out = { c.r, c.g, c.b, c.a };
	return out;
}

static bool is_bump(D3DFORMAT format)
{
	return format == D3DFMT_V8U8 || format == D3DFMT_L6V5U5 || format == D3DFMT_X8L8V8U8 || format == D3DFMT_Q8W8V8U8;
}

/// A device drawing into a back buffer of one size and format, for the captures of one kind of target.
/// One lives at a time: the GPU copies of A2's objects are dropped through the one posixResourceDestroyed
/// hook, which the newest device's table holds, so an older device would keep the copy of a buffer that
/// died and hand it to the next buffer made at the same address.
struct Replayer
{
	IDirect3D9 *D3D;
	PosixDevice9 *Device;
};

static bool make_replayer(unsigned width, unsigned height, D3DFORMAT format, Replayer &replayer)
{
	replayer.D3D = Direct3DCreate9(D3D_SDK_VERSION);
	replayer.Device = NULL;
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = width;
	parameters.BackBufferHeight = height;
	parameters.BackBufferFormat = format;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *created = NULL;
	if (replayer.D3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &created) != D3D_OK) {
		replayer.D3D->Release();
		return false;
	}
	replayer.Device = static_cast<PosixDevice9 *>(created);
	if (replayer.Device->Create_Gpu_Frame(true) != D3D_OK) {
		replayer.Device->Release();
		replayer.D3D->Release();
		return false;
	}
	return true;
}

/// The kind of target a capture draws into, "WxH/format", or "" for a file that is not a capture.
static std::string target_kind(const std::string &path)
{
	DrawCaptureHeader header;
	FILE *file = fopen(path.c_str(), "rb");
	if (file == NULL) {
		return "";
	}
	const bool read = fread(&header, sizeof(header), 1, file) == 1;
	fclose(file);
	if (!read || memcmp(header.Magic, "ZHDC", 4) != 0) {
		return "";
	}
	char kind[64];
	snprintf(kind, sizeof(kind), "%ux%u/%u", header.TargetWidth, header.TargetHeight, header.TargetFormat);
	return kind;
}

/// The vertex format as the reference reads it; false, with the reason, for what it does not describe.
static bool describe_vertices(uint32_t fvf, FFRef::DrawState &state, int offsets[8], std::string &reason)
{
	const uint32_t position = fvf & D3DFVF_POSITION_MASK;
	if (position != D3DFVF_XYZ && position != D3DFVF_XYZRHW) {
		reason = "a position other than XYZ or XYZRHW";
		return false;
	}
	int offset = position == D3DFVF_XYZRHW ? 16 : 12;
	state.pretransformed = position == D3DFVF_XYZRHW;
	state.hasNormal = (fvf & D3DFVF_NORMAL) != 0;
	offsets[0] = offset;
	offset += state.hasNormal ? 12 : 0;
	offset += (fvf & D3DFVF_PSIZE) ? 4 : 0;
	state.hasDiffuse = (fvf & D3DFVF_DIFFUSE) != 0;
	offsets[1] = offset;
	offset += state.hasDiffuse ? 4 : 0;
	state.hasSpecular = (fvf & D3DFVF_SPECULAR) != 0;
	offsets[2] = offset;
	offset += state.hasSpecular ? 4 : 0;
	offsets[3] = offset;
	state.texCoordSets = (int)((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
	static const int SIZE_OF_BITS[4] = { 2, 3, 4, 1 };
	for (int set = 0; set < 8; ++set) {
		state.texCoordSize[set] = SIZE_OF_BITS[(fvf >> (16 + set * 2)) & 3];
	}
	return true;
}

static void read_vertices(const uint8_t *bytes, const DrawCaptureHeader &header, const FFRef::DrawState &state,
	const int offsets[8], std::vector<FFRef::Vertex> &out)
{
	out.resize(header.VertexCount);
	for (uint32_t i = 0; i < header.VertexCount; ++i) {
		const uint8_t *v = bytes + (size_t)i * header.Stride;
		FFRef::Vertex &r = out[i];
		memset(&r, 0, sizeof(r));
		float f[4] = { 0, 0, 0, 1 };
		memcpy(f, v, state.pretransformed ? 16 : 12);
		for (int k = 0; k < 4; ++k) r.position[k] = f[k];
		if (state.hasNormal) {
			float n[3];
			memcpy(n, v + offsets[0], 12);
			for (int k = 0; k < 3; ++k) r.normal[k] = n[k];
		}
		uint32_t argb = 0xFFFFFFFF;
		if (state.hasDiffuse) memcpy(&argb, v + offsets[1], 4);
		r.diffuse = FFRef::colorFromD3D(argb);
		argb = 0xFFFFFFFF;
		if (state.hasSpecular) memcpy(&argb, v + offsets[2], 4);
		r.specular = FFRef::colorFromD3D(argb);
		int offset = offsets[3];
		for (int set = 0; set < state.texCoordSets && set < 8; ++set) {
			float t[4] = { 0, 0, 0, 0 };
			memcpy(t, v + offset, (size_t)state.texCoordSize[set] * 4);
			for (int k = 0; k < 4; ++k) r.tex[set][k] = t[k];
			offset += state.texCoordSize[set] * 4;
		}
	}
}

/// A captured texture on the device and decoded for the reference.  False, with the reason, when it
/// cannot be replayed.
static bool load_texture(PosixDevice9 *device, const std::string &path, IDirect3DTexture9 *&texture,
	FFRef::Texture &reference, std::string &reason)
{
	std::vector<uint8_t> bytes;
	if (!read_file(path, bytes) || bytes.size() < sizeof(DrawCaptureTexture)) {
		reason = "a texture file missing or short";
		return false;
	}
	DrawCaptureTexture description;
	memcpy(&description, &bytes[0], sizeof(description));
	if (memcmp(description.Magic, "ZHTX", 4) != 0) {
		reason = "a texture file that is not one";
		return false;
	}
	if (is_bump((D3DFORMAT)description.Format)) {
		reason = "a bump-map texture (the reference takes it signed; the codec gives it unsigned)";
		return false;
	}
	if (device->CreateTexture(description.Width, description.Height, description.Levels, 0,
			(D3DFORMAT)description.Format, D3DPOOL_MANAGED, &texture, NULL) != D3D_OK) {
		reason = "a texture the device would not make";
		return false;
	}
	PosixTexture9 *posix = static_cast<PosixTexture9 *>(texture);
	reference.type = FFRef::TEXTURE_2D;
	reference.levels.clear();
	size_t at = sizeof(description);
	for (uint32_t level = 0; level < description.Levels; ++level) {
		uint32_t size = 0;
		if (at + 4 > bytes.size()) break;
		memcpy(&size, &bytes[at], 4);
		at += 4;
		PosixImage &image = posix->level(level);
		if (size != image.size() || at + size > bytes.size()) {
			reason = "a texture level of the wrong size";
			return false;
		}
		D3DLOCKED_RECT locked;
		texture->LockRect(level, &locked, NULL, 0);
		memcpy(locked.pBits, &bytes[at], size);
		texture->UnlockRect(level);
		at += size;

		std::vector<uint8_t> bgra((size_t)image.width() * image.height() * 4);
		if (!Sdl_Convert_Level(image, false, &bgra[0])) {
			reason = "a texture format the codec does not read";
			return false;
		}
		FFRef::TextureLevel decoded;
		decoded.width = (int)image.width();
		decoded.height = (int)image.height();
		decoded.texels.resize((size_t)decoded.width * decoded.height);
		for (size_t i = 0; i < decoded.texels.size(); ++i) {
			const uint8_t *p = &bgra[i * 4];
			decoded.texels[i] = FFRef::colorFromD3D(((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]);
		}
		reference.levels.push_back(decoded);
	}
	return true;
}

static void not_replayed_because(const std::string &file, const std::string &reason)
{
	++not_replayed[reason];
	printf("skip %s: %s\n", file.c_str(), reason.c_str());
}

/// Replays one capture and compares.  `mutations` arms the reference (a control, which must fail);
/// `picture` receives the device's, RGBA, when asked for.
static void replay(PosixDevice9 *device, const std::string &directory, const std::string &file, unsigned mutations = 0,
	std::vector<uint8_t> *picture = NULL)
{
	std::vector<uint8_t> bytes;
	if (!read_file(directory + "/" + file, bytes) || bytes.size() < sizeof(DrawCaptureHeader)) {
		not_replayed_because(file, "a capture file missing or short");
		return;
	}
	DrawCaptureHeader header;
	memcpy(&header, &bytes[0], sizeof(header));
	if (memcmp(header.Magic, "ZHDC", 4) != 0 || header.Version != DRAW_CAPTURE_VERSION) {
		not_replayed_because(file, "not a capture of this version");
		return;
	}
	const size_t vertex_bytes = (size_t)header.VertexCount * header.Stride;
	if (sizeof(header) + vertex_bytes + (size_t)header.IndexCount * 4 != bytes.size()) {
		not_replayed_because(file, "a capture of the wrong size");
		return;
	}
	const uint8_t *vertices = &bytes[sizeof(header)];
	const uint32_t *indices = header.IndexCount != 0 ? (const uint32_t *)(vertices + vertex_bytes) : NULL;
	if (header.TargetFormat != D3DFMT_A8R8G8B8 && header.TargetFormat != D3DFMT_X8R8G8B8) {
		not_replayed_because(file, "a target that is not 32-bit");
		return;
	}
	const int width = (int)header.TargetWidth, height = (int)header.TargetHeight;

	FFRef::DrawState state;
	state.setDefaults(width, height);
	int offsets[8];
	std::string reason;
	if (!describe_vertices(header.FVF, state, offsets, reason)) {
		not_replayed_because(file, reason);
		return;
	}

	// The textures, on both.
	IDirect3DTexture9 *textures[DRAW_CAPTURE_STAGES] = {};
	FFRef::Texture references[DRAW_CAPTURE_STAGES];
	for (int stage = 0; stage < DRAW_CAPTURE_STAGES; ++stage) {
		state.textures[stage] = NULL;
		if (header.Textures[stage][0] == '\0') {
			continue;
		}
		if (!load_texture(device, directory + "/" + header.Textures[stage], textures[stage], references[stage], reason)) {
			for (int s = 0; s <= stage; ++s) if (textures[s] != NULL) textures[s]->Release();
			not_replayed_because(file, reason);
			return;
		}
		state.textures[stage] = &references[stage];
	}

	// The state, as captured, on both, with ANISOTROPIC as LINEAR (see the top of the file).
	bool substituted = false;
	static const bool no_mips = getenv("FFREF_CAPTURE_NO_MIPS") != NULL;
	for (int stage = 0; stage < DRAW_CAPTURE_STAGES; ++stage) {
		if (no_mips) {
			header.SamplerStates[stage][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
		}
		for (int filter = D3DSAMP_MAGFILTER; filter <= D3DSAMP_MINFILTER; ++filter) {
			if (header.SamplerStates[stage][filter] == D3DTEXF_ANISOTROPIC) {
				header.SamplerStates[stage][filter] = D3DTEXF_LINEAR;
				substituted = true;
			}
		}
	}
	for (int s = 0; s < 256; ++s) {
		device->SetRenderState((D3DRENDERSTATETYPE)s, header.RenderStates[s]);
		state.renderState[s] = header.RenderStates[s];
	}
	for (int stage = 0; stage < DRAW_CAPTURE_STAGES; ++stage) {
		for (int t = 1; t < 33; ++t) {
			device->SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)t, header.StageStates[stage][t]);
			state.stageState[stage][t] = header.StageStates[stage][t];
		}
		for (int t = 1; t < 14; ++t) {
			device->SetSamplerState(stage, (D3DSAMPLERSTATETYPE)t, header.SamplerStates[stage][t]);
			state.samplerState[stage][t] = header.SamplerStates[stage][t];
		}
		device->SetTexture(stage, textures[stage]);
		device->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), &header.TextureMatrices[stage]);
		matrix_to_ref(header.TextureMatrices[stage], state.textureTransform[stage]);
	}
	device->SetTransform(D3DTS_WORLD, &header.World);
	device->SetTransform(D3DTS_VIEW, &header.View);
	device->SetTransform(D3DTS_PROJECTION, &header.Projection);
	matrix_to_ref(header.World, state.world);
	matrix_to_ref(header.View, state.view);
	matrix_to_ref(header.Projection, state.projection);
	device->SetMaterial(&header.Material);
	state.material.diffuse = colour(header.Material.Diffuse);
	state.material.ambient = colour(header.Material.Ambient);
	state.material.specular = colour(header.Material.Specular);
	state.material.emissive = colour(header.Material.Emissive);
	state.material.power = header.Material.Power;
	for (int i = 0; i < 8; ++i) {
		const D3DLIGHT9 &light = header.Lights[i];
		device->SetLight(i, &light);
		device->LightEnable(i, header.LightsEnabled[i]);
		FFRef::Light &l = state.lights[i];
		l.enabled = header.LightsEnabled[i] != 0;
		l.type = light.Type;
		l.diffuse = colour(light.Diffuse);
		l.specular = colour(light.Specular);
		l.ambient = colour(light.Ambient);
		l.position[0] = light.Position.x; l.position[1] = light.Position.y; l.position[2] = light.Position.z;
		l.direction[0] = light.Direction.x; l.direction[1] = light.Direction.y; l.direction[2] = light.Direction.z;
		l.range = light.Range;
		l.falloff = light.Falloff;
		l.attenuation0 = light.Attenuation0;
		l.attenuation1 = light.Attenuation1;
		l.attenuation2 = light.Attenuation2;
		l.theta = light.Theta;
		l.phi = light.Phi;
	}
	device->SetViewport(&header.Viewport);
	state.viewport.x = header.Viewport.X;
	state.viewport.y = header.Viewport.Y;
	state.viewport.width = header.Viewport.Width;
	state.viewport.height = header.Viewport.Height;
	state.viewport.minZ = header.Viewport.MinZ;
	state.viewport.maxZ = header.Viewport.MaxZ;
	device->SetFVF(header.FVF);

	// The cleared target on both.  Without a depth surface D3D9 draws with depth and stencil off, and
	// the reference is told so the same way the device decides it.
	IDirect3DSurface9 *depth = NULL;
	device->GetDepthStencilSurface(&depth);
	FFRef::Target target;
	target.create(width, height, header.TargetFormat == D3DFMT_A8R8G8B8);
	target.clear(FFRef::colorFromD3D(CLEAR), 1.0, 0);
	D3DVIEWPORT9 whole = { 0, 0, (unsigned)width, (unsigned)height, 0.0f, 1.0f };
	device->SetViewport(&whole);
	device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, CLEAR, 1.0f, 0);
	device->SetViewport(&header.Viewport);
	if (!header.DepthBound) {
		device->SetDepthStencilSurface(NULL);
		state.renderState[D3DRS_ZENABLE] = 0;
		state.renderState[D3DRS_ZWRITEENABLE] = 0;
		state.renderState[D3DRS_STENCILENABLE] = 0;
	}

	// The draw on the device, from buffers as the game's are.
	IDirect3DVertexBuffer9 *vertex_buffer = NULL;
	IDirect3DIndexBuffer9 *index_buffer = NULL;
	device->CreateVertexBuffer((unsigned)vertex_bytes, D3DUSAGE_WRITEONLY, header.FVF, D3DPOOL_MANAGED, &vertex_buffer, NULL);
	void *locked = NULL;
	vertex_buffer->Lock(0, 0, &locked, 0);
	memcpy(locked, vertices, vertex_bytes);
	vertex_buffer->Unlock();
	device->SetStreamSource(0, vertex_buffer, 0, header.Stride);
	if (indices != NULL) {
		device->CreateIndexBuffer(header.IndexCount * 4, D3DUSAGE_WRITEONLY, D3DFMT_INDEX32, D3DPOOL_MANAGED, &index_buffer, NULL);
		index_buffer->Lock(0, 0, &locked, 0);
		memcpy(locked, indices, (size_t)header.IndexCount * 4);
		index_buffer->Unlock();
		device->SetIndices(index_buffer);
		device->DrawIndexedPrimitive((D3DPRIMITIVETYPE)header.Primitive, 0, 0, header.VertexCount, 0, header.PrimitiveCount);
	}
	else {
		device->DrawPrimitive((D3DPRIMITIVETYPE)header.Primitive, 0, header.PrimitiveCount);
	}

	// The same draw on the reference.
	std::vector<FFRef::Vertex> reference_vertices;
	read_vertices(vertices, header, state, offsets, reference_vertices);
	FFRef::Report report;
	const bool drawn = FFRef::draw(state, (int)header.Primitive, &reference_vertices[0], (int)reference_vertices.size(),
		indices, indices != NULL ? (int)header.IndexCount : (int)header.VertexCount, target, &report, mutations);

	std::vector<uint8_t> bgra;
	SdlGpuFrame *gpu = device->Get_Gpu();
	const bool read = gpu->Read_Back(gpu->Back_Buffer(), width, height, bgra);

	device->SetStreamSource(0, NULL, 0, 0);
	device->SetIndices(NULL);
	for (int stage = 0; stage < DRAW_CAPTURE_STAGES; ++stage) {
		device->SetTexture(stage, NULL);
		if (textures[stage] != NULL) textures[stage]->Release();
	}
	vertex_buffer->Release();
	if (index_buffer != NULL) index_buffer->Release();
	device->SetDepthStencilSurface(depth);
	if (depth != NULL) depth->Release();

	const std::string label = file + " (" + std::string(header.Signature).substr(0, 60) + ")";
	if (!drawn) {
		not_replayed_because(file, "the reference refused: " + (report.refusals.empty() ? std::string("?") : report.refusals[0]));
		return;
	}
	if (!read) {
		++failures;
		printf("FAIL %s: no read-back\n", label.c_str());
		return;
	}
	std::vector<uint8_t> rgba(bgra.size());
	for (size_t i = 0; i < bgra.size(); i += 4) {
		rgba[i] = bgra[i + 2];
		rgba[i + 1] = bgra[i + 1];
		rgba[i + 2] = bgra[i];
		rgba[i + 3] = bgra[i + 3];
	}
	if (picture != NULL) {
		*picture = rgba;
	}
	const FFRef::Comparison comparison = FFRef::compare(target, &rgba[0], width * 4);
	if (mutations != 0) {
		// An armed control: the reference drawn with a deliberate departure must be found outside.
		if (comparison.passed()) {
			++failures;
			printf("FAIL %s: an armed control passed\n", label.c_str());
		}
		else {
			printf("ok   %s: the armed control is found outside (%ld pixels)\n", label.c_str(), comparison.outside);
		}
		return;
	}
	++compared;
	anisotropic_as_linear += substituted ? 1 : 0;
	if (report.pixelsWritten == 0) {
		++drew_nothing;
	}
	const char *known = known_finding(header.Signature);
	if (known != NULL) {
		if (comparison.passed()) {
			++failures;
			printf("FAIL %s: listed as known (%s) and now passes: take it off the list\n", label.c_str(), known);
		}
		else {
			++known_findings;
			printf("KNOWN %s %s: %ld outside of %ld written\n", label.c_str(), known, comparison.outside, report.pixelsWritten);
		}
		return;
	}
	if (!comparison.passed()) {
		++failures;
		const int x = comparison.worstX, y = comparison.worstY;
		printf("FAIL %s: %ld outside of %ld written", label.c_str(), comparison.outside, report.pixelsWritten);
		if (x >= 0) {
			const FFRef::Color &n = target.color[(size_t)y * width + x];
			const uint8_t *g = &rgba[((size_t)y * width + x) * 4];
			printf(", worst (%d, %d) gpu %u %u %u %u, reference %.1f %.1f %.1f %.1f", x, y, g[0], g[1], g[2], g[3],
				n.r * 255, n.g * 255, n.b * 255, n.a * 255);
		}
		printf("\n  signature: %s\n", header.Signature);
		printf("  reference: %ld triangles, %ld culled, %ld clipped away, %ld pixels covered\n", report.trianglesIn,
			report.trianglesCulled, report.trianglesClippedAway, report.pixelsCovered);
		if (getenv("FFREF_VERBOSE") != NULL) FFRef::print(comparison, stdout, label.c_str());
	}
	else {
		printf("ok   %s: %ld written, exact %ld, in a freedom %ld\n", label.c_str(), report.pixelsWritten, comparison.exact,
			comparison.inFreedom);
		if (report.pixelsWritten == 0) {
			printf("  drew nothing: %ld triangles, %ld culled, %ld clipped away, %ld pixels covered\n", report.trianglesIn,
				report.trianglesCulled, report.trianglesClippedAway, report.pixelsCovered);
		}
	}
}

/// The draw_*.cap files in a directory, in order.
static std::vector<std::string> list_captures(const std::string &directory)
{
	std::vector<std::string> files;
	DIR *listing = opendir(directory.c_str());
	if (listing == NULL) {
		return files;
	}
	while (struct dirent *entry = readdir(listing)) {
		const std::string name = entry->d_name;
		if (name.size() > 4 && name.compare(0, 5, "draw_") == 0 && name.compare(name.size() - 4, 4, ".cap") == 0) {
			files.push_back(name);
		}
	}
	closedir(listing);
	std::sort(files.begin(), files.end());
	return files;
}

/// Replays every capture, one kind of target at a time (see Replayer).  `pictures` receives each one's
/// GPU picture by file name, when asked for.
static void replay_all(const std::string &directory, const std::vector<std::string> &files,
	std::map<std::string, std::vector<uint8_t> > *pictures = NULL)
{
	std::map<std::string, std::vector<std::string> > kinds;
	for (size_t i = 0; i < files.size(); ++i) {
		kinds[target_kind(directory + "/" + files[i])].push_back(files[i]);
	}
	for (std::map<std::string, std::vector<std::string> >::iterator kind = kinds.begin(); kind != kinds.end(); ++kind) {
		unsigned width = 0, height = 0, format = 0;
		Replayer replayer;
		if (sscanf(kind->first.c_str(), "%ux%u/%u", &width, &height, &format) != 3
			|| !make_replayer(width, height, (D3DFORMAT)format, replayer)) {
			for (size_t i = 0; i < kind->second.size(); ++i) {
				not_replayed_because(kind->second[i], kind->first.empty() ? "not a capture" : "no device for its target");
			}
			continue;
		}
		for (size_t i = 0; i < kind->second.size(); ++i) {
			replay(replayer.Device, directory, kind->second[i], 0, pictures != NULL ? &(*pictures)[kind->second[i]] : NULL);
		}
		const std::map<std::string, unsigned int> &refused = replayer.Device->Draw_Refusals();
		for (std::map<std::string, unsigned int>::const_iterator r = refused.begin(); r != refused.end(); ++r) {
			++failures;
			printf("FAIL the device refused %u draws on %s: %s\n", r->second, kind->first.c_str(), r->first.c_str());
		}
		replayer.Device->Release();
		replayer.D3D->Release();
	}
}

static void summary(size_t captures)
{
	for (std::map<std::string, int>::const_iterator it = not_replayed.begin(); it != not_replayed.end(); ++it) {
		printf("  not replayed %d: %s\n", it->second, it->first.c_str());
	}
	printf("  %d draws: ANISOTROPIC replayed as LINEAR\n", anisotropic_as_linear);
	printf("ffref_capture: %zu captures, %d compared (%d drew nothing on the reference), %d failed, %d known findings\n",
		captures, compared, drew_nothing, failures, known_findings);
}

// ---- The round trip: without captures of the game's, this captures draws of its own and replays them.

static const int ROUND_TRIP_SIZE = 64;

static D3DMATRIX identity()
{
	D3DMATRIX m;
	memset(&m, 0, sizeof(m));
	m._11 = m._22 = m._33 = m._44 = 1.0f;
	return m;
}

/// The back buffer as RGBA.
static std::vector<uint8_t> picture_of(PosixDevice9 *device)
{
	std::vector<uint8_t> bgra, rgba;
	SdlGpuFrame *gpu = device->Get_Gpu();
	if (!gpu->Read_Back(gpu->Back_Buffer(), ROUND_TRIP_SIZE, ROUND_TRIP_SIZE, bgra)) {
		return rgba;
	}
	rgba.resize(bgra.size());
	for (size_t i = 0; i < bgra.size(); i += 4) {
		rgba[i] = bgra[i + 2];
		rgba[i + 1] = bgra[i + 1];
		rgba[i + 2] = bgra[i];
		rgba[i + 3] = bgra[i + 3];
	}
	return rgba;
}

/// Three draws with different signatures, each into a cleared target, each picture kept: a textured quad
/// from the caller's memory; a lit, textured (DXT1) grid drawn indexed with a base vertex, a start index
/// past junk and indices that do not start at 0, so the capture's rebasing does something; and a blended
/// fan from a buffer's middle.
static void draw_round_trip(PosixDevice9 *device, std::vector<std::vector<uint8_t> > &pictures)
{
	const D3DMATRIX id = identity();
	device->SetTransform(D3DTS_WORLD, &id);
	device->SetTransform(D3DTS_VIEW, &id);
	device->SetTransform(D3DTS_PROJECTION, &id);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	// 1: a pretransformed quad, modulated by an A8R8G8B8 texture with two levels.
	IDirect3DTexture9 *gradient = NULL;
	device->CreateTexture(8, 8, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &gradient, NULL);
	for (unsigned level = 0; level < 2; ++level) {
		D3DLOCKED_RECT locked;
		gradient->LockRect(level, &locked, NULL, 0);
		const unsigned size = 8 >> level;
		for (unsigned y = 0; y < size; ++y)
			for (unsigned x = 0; x < size; ++x)
				((uint32_t *)((uint8_t *)locked.pBits + y * locked.Pitch))[x] =
					0xFF000000 | ((x * 255 / size) << 16) | ((y * 255 / size) << 8) | (level ? 0xC0 : 0x40);
		gradient->UnlockRect(level);
	}
	struct Screen { float x, y, z, rhw; uint32_t diffuse; float u, v; };
	const Screen quad[6] = {
		{ 4.0f, 6.0f, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f }, { 58.0f, 6.0f, 0.5f, 1.0f, 0xFF80FF80, 1.0f, 0.0f },
		{ 4.0f, 60.0f, 0.5f, 1.0f, 0xFFFF8080, 0.0f, 1.0f }, { 58.0f, 6.0f, 0.5f, 1.0f, 0xFF80FF80, 1.0f, 0.0f },
		{ 58.0f, 60.0f, 0.5f, 1.0f, 0xFF8080FF, 1.0f, 1.0f }, { 4.0f, 60.0f, 0.5f, 1.0f, 0xFFFF8080, 0.0f, 1.0f } };
	device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, CLEAR, 1.0f, 0);
	device->SetTexture(0, gradient);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(Screen));
	pictures.push_back(picture_of(device));

	// 2: a lit grid in perspective, indexed, DXT1-textured, from buffers with junk before the draw's part.
	IDirect3DTexture9 *blocks = NULL;
	device->CreateTexture(8, 8, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &blocks, NULL);
	{
		D3DLOCKED_RECT locked;
		blocks->LockRect(0, &locked, NULL, 0);
		for (unsigned block = 0; block < 4; ++block) {
			uint8_t *b = (uint8_t *)locked.pBits + block * 8;
			const uint16_t colour0 = block & 1 ? 0xF800 : 0x07E0, colour1 = 0x001F;	// colour0 > colour1: four colours
			memcpy(b, &colour0, 2);
			memcpy(b + 2, &colour1, 2);
			const uint32_t codes = 0x1B1B1B1Bu ^ (block * 0x11111111u);
			memcpy(b + 4, &codes, 4);
		}
		blocks->UnlockRect(0);
	}
	struct Lit { float x, y, z, nx, ny, nz; uint32_t diffuse; float u, v; };
	// The grid's vertices start at 6: base vertex 4, and indices that start at 2.
	const unsigned JUNK_VERTICES = 6, BASE_VERTEX = 4, FIRST_INDEX = 2, JUNK_INDICES = 6, N = 4;
	std::vector<Lit> grid(JUNK_VERTICES + (N + 1) * (N + 1));
	for (unsigned i = 0; i < JUNK_VERTICES; ++i) {
		Lit junk = { 1e6f, 1e6f, 1e6f, 0, 0, -1, 0xFFFF00FF, 0, 0 };
		grid[i] = junk;
	}
	for (unsigned j = 0; j <= N; ++j) {
		for (unsigned i = 0; i <= N; ++i) {
			const float s = (float)i / N, t = (float)j / N;
			const float nx = 0.4f * sinf(s * 5.0f), ny = 0.3f * cosf(t * 4.0f), length = sqrtf(nx * nx + ny * ny + 1.0f);
			Lit v = { -1.5f + 3.0f * s, -1.2f + 2.4f * t, 2.0f + 1.5f * t, nx / length, ny / length, -1.0f / length,
				0xFF000000u | ((uint32_t)(255 * s) << 16) | ((uint32_t)(255 * t) << 8) | 0xA0, s * 2.0f, t * 2.0f };
			grid[JUNK_VERTICES + j * (N + 1) + i] = v;
		}
	}
	std::vector<uint16_t> indices(JUNK_INDICES, 9999);
	for (unsigned j = 0; j < N; ++j) {
		for (unsigned i = 0; i < N; ++i) {
			const uint16_t a = (uint16_t)(FIRST_INDEX + j * (N + 1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + N + 1), d = (uint16_t)(c + 1);
			const uint16_t cell[6] = { a, b, c, b, d, c };
			indices.insert(indices.end(), cell, cell + 6);
		}
	}
	IDirect3DVertexBuffer9 *vertex_buffer = NULL;
	IDirect3DIndexBuffer9 *index_buffer = NULL;
	void *locked = NULL;
	device->CreateVertexBuffer((unsigned)(grid.size() * sizeof(Lit)), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vertex_buffer, NULL);
	vertex_buffer->Lock(0, 0, &locked, 0);
	memcpy(locked, &grid[0], grid.size() * sizeof(Lit));
	vertex_buffer->Unlock();
	device->CreateIndexBuffer((unsigned)(indices.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &index_buffer, NULL);
	index_buffer->Lock(0, 0, &locked, 0);
	memcpy(locked, &indices[0], indices.size() * 2);
	index_buffer->Unlock();
	D3DMATRIX projection;
	memset(&projection, 0, sizeof(projection));
	projection._11 = 1.2f; projection._22 = 1.2f; projection._33 = 10.0f / 9.0f; projection._34 = 1.0f; projection._43 = -10.0f / 9.0f;
	device->SetTransform(D3DTS_PROJECTION, &projection);
	D3DLIGHT9 light;
	memset(&light, 0, sizeof(light));
	light.Type = D3DLIGHT_DIRECTIONAL;
	light.Diffuse.r = 0.9f; light.Diffuse.g = 0.8f; light.Diffuse.b = 0.7f; light.Diffuse.a = 1.0f;
	light.Direction.x = 0.3f; light.Direction.y = -0.2f; light.Direction.z = 1.0f;
	device->SetLight(0, &light);
	device->LightEnable(0, 1);
	D3DMATERIAL9 material;
	memset(&material, 0, sizeof(material));
	material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1.0f;
	material.Ambient.r = material.Ambient.g = material.Ambient.b = 0.2f;
	device->SetMaterial(&material);
	device->SetRenderState(D3DRS_LIGHTING, 1);
	device->SetRenderState(D3DRS_AMBIENT, 0xFF303030);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	device->SetTexture(0, blocks);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR);
	device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, CLEAR, 1.0f, 0);
	device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	device->SetStreamSource(0, vertex_buffer, 0, sizeof(Lit));
	device->SetIndices(index_buffer);
	device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, (int)BASE_VERTEX, FIRST_INDEX, (N + 1) * (N + 1), JUNK_INDICES, N * N * 2);
	pictures.push_back(picture_of(device));
	device->SetStreamSource(0, NULL, 0, 0);
	device->SetIndices(NULL);
	vertex_buffer->Release();
	index_buffer->Release();

	// 3: a blended fan in clip space, untextured, starting three vertices into its buffer.
	struct Plain { float x, y, z; uint32_t diffuse; };
	const Plain fan[9] = {
		{ 9, 9, 9, 0 }, { 9, 9, 9, 0 }, { 9, 9, 9, 0 },
		{ 0.0f, 0.0f, 0.3f, 0x80FFFFFF }, { -0.9f, -0.7f, 0.3f, 0x80FF0000 }, { 0.8f, -0.8f, 0.6f, 0x8000FF00 },
		{ 0.9f, 0.6f, 0.6f, 0x800000FF }, { -0.2f, 0.95f, 0.3f, 0x80FFFF00 }, { -0.9f, -0.7f, 0.3f, 0x80FF0000 } };
	device->SetTransform(D3DTS_PROJECTION, &id);
	device->SetRenderState(D3DRS_LIGHTING, 0);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetTexture(0, NULL);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	device->CreateVertexBuffer(sizeof(fan), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vertex_buffer, NULL);
	vertex_buffer->Lock(0, 0, &locked, 0);
	memcpy(locked, fan, sizeof(fan));
	vertex_buffer->Unlock();
	device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, CLEAR, 1.0f, 0);
	device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
	device->SetStreamSource(0, vertex_buffer, 0, sizeof(Plain));
	device->DrawPrimitive(D3DPT_TRIANGLEFAN, 3, 4);
	pictures.push_back(picture_of(device));
	device->SetStreamSource(0, NULL, 0, 0);
	vertex_buffer->Release();

	device->SetTexture(0, NULL);
	gradient->Release();
	blocks->Release();
}

static int round_trip()
{
	// ctest gives each test a directory of its own; a direct run has none, and nothing is written then.
	const char *user = getenv("ZH_USER_DATA_DIR");
	if (user == NULL || user[0] == '\0') {
		printf("ffref_capture: FAIL - run it through ctest, which gives it ZH_USER_DATA_DIR to capture into\n");
		return 1;
	}
	const std::string directory = std::string(user) + "/captures";
	mkdir(user, 0755);
	mkdir(directory.c_str(), 0755);
	// The last run's captures; this directory is the test's own.
	if (DIR *listing = opendir(directory.c_str())) {
		while (struct dirent *entry = readdir(listing)) {
			const std::string name = entry->d_name;
			if (name.size() > 4 && (name.compare(name.size() - 4, 4, ".cap") == 0 || name.compare(name.size() - 4, 4, ".tex") == 0)) {
				remove((directory + "/" + name).c_str());
			}
		}
		closedir(listing);
	}
	// The device reads ZH_GPU_CAPTURE at its first draw, which comes after this.
	setenv("ZH_GPU_CAPTURE", directory.c_str(), 1);

	Replayer original;
	if (!make_replayer(ROUND_TRIP_SIZE, ROUND_TRIP_SIZE, D3DFMT_A8R8G8B8, original)) {
		printf("ffref_capture: SKIP - no GPU device here: %s\n", SDL_GetError());
		return 77;
	}
	std::vector<std::vector<uint8_t> > drawn;
	draw_round_trip(original.Device, drawn);
	original.Device->Release();		// before the replay's device: one table of GPU copies at a time
	original.D3D->Release();

	const std::vector<std::string> files = list_captures(directory);
	if (files.size() != drawn.size()) {
		printf("ffref_capture: FAIL - %zu draws made %zu captures\n", drawn.size(), files.size());
		return 1;
	}
	std::map<std::string, std::vector<uint8_t> > replayed;
	replay_all(directory, files, &replayed);
	// The replay is the draw: the same device on the same state and bytes draws the same picture.
	for (size_t i = 0; i < files.size(); ++i) {
		const std::vector<uint8_t> &before = drawn[i], &after = replayed[files[i]];
		size_t differ = 0;
		if (before.empty() || before.size() != after.size()) {
			differ = ROUND_TRIP_SIZE * ROUND_TRIP_SIZE;		// a picture missing is every pixel wrong
		}
		else {
			for (size_t p = 0; p < before.size(); p += 4) {
				differ += memcmp(&before[p], &after[p], 4) != 0 ? 1 : 0;
			}
		}
		if (differ != 0) {
			++failures;
			printf("FAIL %s: the replay's picture is not the draw's (%zu pixels differ)\n", files[i].c_str(), differ);
		}
		else {
			printf("ok   %s: the replay's picture is the draw's\n", files[i].c_str());
		}
	}
	if (compared != (int)files.size() || drew_nothing != 0) {
		++failures;
		printf("FAIL every round-trip capture must be compared and draw: %d of %zu compared, %d drew nothing\n", compared,
			files.size(), drew_nothing);
	}
	// The armed control: the first capture against a reference with D3D10's pixel centres must fail.
	Replayer armed;
	if (make_replayer(ROUND_TRIP_SIZE, ROUND_TRIP_SIZE, D3DFMT_A8R8G8B8, armed)) {
		replay(armed.Device, directory, files[0], FFRef::MUTATE_RASTER_HALF_PIXEL);
		armed.Device->Release();
		armed.D3D->Release();
	}
	summary(files.size());
	return failures == 0 ? 0 : 1;
}

int main()
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("ffref_capture: SKIP - no video: %s\n", SDL_GetError());
		return 77;
	}
	const char *directory = getenv("ZH_FFREF_CAPTURE_DIR");
	int result = 0;
	if (directory == NULL || directory[0] == '\0') {
		result = round_trip();
	}
	else {
		const std::vector<std::string> files = list_captures(directory);
		if (files.empty()) {
			printf("ffref_capture: FAIL - no draw_*.cap in %s\n", directory);
			result = 1;
		}
		else {
			replay_all(directory, files);
			summary(files.size());
			result = failures == 0 && compared > 0 ? 0 : 1;
		}
	}
	SDL_Quit();
	return result;
}
