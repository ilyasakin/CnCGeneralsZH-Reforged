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

// The device's fixed-function draws on this machine's GPU (A3c), through D3D9 calls only, read back
// from the back buffer:
//   - a pre-transformed quad whose edges sit a quarter pixel past an integer lands on the columns D3D9
//     covers (the half-pixel viewport), and its clockwise triangles are front-facing (D3D9's CULL_CCW);
//   - a transformed, unlit quad through the generated vertex program;
//   - textures: A8R8G8B8 as it is (four texels to four quadrants, point sampled), DXT1 as BC1,
//     X8R8G8B8 expanded with alpha 1, and an unbound slot sampling white;
//   - a fan (DrawPrimitiveUP and indexed), static and dynamic buffers, and a static buffer written
//     again after a draw used it, which must flush so both draws show what D3D9 would have shown;
//   - a clear between draws, and the depth test ordering two quads whatever order they are drawn in;
//   - partial clears as clear draws: one cut to a smaller viewport, a list of rectangles, and a depth-only
//     rectangle that a following depth-tested draw sees (and whose own state does not leak into it);
//   - a texture's GPU copy goes when the texture does (posixResourceDestroyed);
//   - a cube texture is refused, counted, and draws nothing.
//
// WHAT THIS DOES NOT PROVE: that the shading matches D3D9's beyond these solid colours.  That is the
// reference comparison's (A3b).

#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>
#include <vector>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static const unsigned SIZE = 32;

struct ScreenVertex
{
	float x, y, z, rhw;
	uint32_t colour;
	float u, v;
};
static const RenderUInt32 SCREEN_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

// Two clockwise triangles (D3D9's front face under CULL_CCW), as a list.
static void quad(ScreenVertex *out, float left, float top, float right, float bottom, float z, uint32_t colour)
{
	const ScreenVertex corners[4] = {
		{ left, top, z, 1.0f, colour, 0.0f, 0.0f }, { right, top, z, 1.0f, colour, 1.0f, 0.0f },
		{ left, bottom, z, 1.0f, colour, 0.0f, 1.0f }, { right, bottom, z, 1.0f, colour, 1.0f, 1.0f } };
	out[0] = corners[0]; out[1] = corners[1]; out[2] = corners[2];
	out[3] = corners[1]; out[4] = corners[3]; out[5] = corners[2];
}

static std::vector<uint8_t> pixels;

static void read_back(PosixDevice9 *device)
{
	SdlGpuFrame *gpu = device->Get_Gpu();
	CHECK(gpu->Read_Back(gpu->Back_Buffer(), SIZE, SIZE, pixels));
}

// The pixel as D3DCOLOR (A8R8G8B8).
static uint32_t at(unsigned x, unsigned y)
{
	const uint8_t *p = &pixels[(y * SIZE + x) * 4];
	return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static bool near_colour(uint32_t a, uint32_t b)
{
	for (int shift = 0; shift < 32; shift += 8) {
		const int d = (int)((a >> shift) & 0xFF) - (int)((b >> shift) & 0xFF);
		if (d > 2 || d < -2) return false;
	}
	return true;
}

#define CHECK_PIXEL(x, y, expected) \
	do { const uint32_t got_ = at(x, y); if (!near_colour(got_ | 0xFF000000u, (expected) | 0xFF000000u)) { ++failures; \
		printf("FAIL %s:%d: pixel (%u, %u) is %08x, expected %08x\n", __FILE__, __LINE__, (unsigned)(x), (unsigned)(y), got_, (unsigned)(expected)); } } while (0)

static const uint32_t BLUE = 0xFF0000FF, RED = 0xFFFF0000, GREEN = 0xFF00FF00, WHITE = 0xFFFFFFFF, YELLOW = 0xFFFFFF00;

static void begin(PosixDevice9 *device, uint32_t colour)
{
	device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0f, 0);
	device->SetFVF(SCREEN_FVF);
	device->SetTexture(0, NULL);
	device->SetRenderState(D3DRS_LIGHTING, 0);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
}

static void check_half_pixel_and_winding(PosixDevice9 *device)
{
	begin(device, BLUE);
	ScreenVertex v[6];
	quad(v, 8.25f, 8.25f, 24.25f, 24.25f, 0.5f, RED);
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex)) == D3D_OK);
	read_back(device);
	// D3D9 covers the pixels whose integer centres lie inside: 9 to 24.
	CHECK_PIXEL(8, 16, BLUE);
	CHECK_PIXEL(9, 16, RED);
	CHECK_PIXEL(24, 16, RED);
	CHECK_PIXEL(25, 16, BLUE);
	CHECK_PIXEL(16, 8, BLUE);
	CHECK_PIXEL(16, 9, RED);
	CHECK_PIXEL(16, 24, RED);
	CHECK_PIXEL(16, 25, BLUE);

	// The same quad wound the other way is culled.
	begin(device, BLUE);
	std::swap(v[1], v[2]);
	std::swap(v[4], v[5]);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, BLUE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, RED);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
}

static void check_transformed(PosixDevice9 *device)
{
	begin(device, BLUE);
	struct Vertex { float x, y, z; uint32_t colour; };
	// Clip space straight through: identity world, view and projection.  y is up, so the clockwise
	// order on screen is (-0.5, 0.5), (0.5, 0.5), (-0.5, -0.5).
	const Vertex v[6] = {
		{ -0.5f, 0.5f, 0.5f, GREEN }, { 0.5f, 0.5f, 0.5f, GREEN }, { -0.5f, -0.5f, 0.5f, GREEN },
		{ 0.5f, 0.5f, 0.5f, GREEN }, { 0.5f, -0.5f, 0.5f, GREEN }, { -0.5f, -0.5f, 0.5f, GREEN } };
	device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(Vertex));
	read_back(device);
	CHECK_PIXEL(16, 16, GREEN);
	CHECK_PIXEL(2, 2, BLUE);
	CHECK_PIXEL(29, 29, BLUE);
}

static IDirect3DTexture9 *make_texture(PosixDevice9 *device, unsigned w, unsigned h, D3DFORMAT format, const void *bytes,
	unsigned size)
{
	IDirect3DTexture9 *texture = NULL;
	CHECK(device->CreateTexture(w, h, 1, 0, format, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	D3DLOCKED_RECT locked;
	CHECK(texture->LockRect(0, &locked, NULL, 0) == D3D_OK);
	memcpy(locked.pBits, bytes, size);
	texture->UnlockRect(0);
	return texture;
}

static void check_textures(PosixDevice9 *device)
{
	ScreenVertex v[6];
	// The classic D3D9 quad: edges on half pixels, so texel centres fall on pixel centres.
	quad(v, 7.5f, 7.5f, 23.5f, 23.5f, 0.5f, WHITE);

	// A8R8G8B8, four texels.
	const uint32_t texels[4] = { RED, GREEN, BLUE, YELLOW };
	IDirect3DTexture9 *argb = make_texture(device, 2, 2, D3DFMT_A8R8G8B8, texels, sizeof(texels));
	begin(device, 0xFF000000);
	device->SetTexture(0, argb);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(10, 10, RED);
	CHECK_PIXEL(20, 10, GREEN);
	CHECK_PIXEL(10, 20, BLUE);
	CHECK_PIXEL(20, 20, YELLOW);

	// DXT1: one block, colour 0 pure red (RGB565 0xF800), every index 0.
	const uint8_t block[8] = { 0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0 };
	IDirect3DTexture9 *dxt = make_texture(device, 4, 4, D3DFMT_DXT1, block, sizeof(block));
	begin(device, 0xFF000000);
	device->SetTexture(0, dxt);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, RED);

	// X8R8G8B8 with its unused byte 0: alpha must read 1, so blending over the clear shows the texel.
	const uint32_t x8[1] = { 0x0000FF00 };
	IDirect3DTexture9 *xrgb = make_texture(device, 1, 1, D3DFMT_X8R8G8B8, x8, sizeof(x8));
	begin(device, BLUE);
	device->SetTexture(0, xrgb);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
	read_back(device);
	CHECK_PIXEL(16, 16, GREEN);

	// A stage that reads a texture with none bound samples white.
	begin(device, BLUE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	quad(v, 7.5f, 7.5f, 23.5f, 23.5f, 0.5f, YELLOW);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(16, 16, YELLOW);

	// The copies go with their textures.
	const size_t live = device->Get_Mirrors()->Live_Copies();
	device->SetTexture(0, NULL);
	argb->Release();
	dxt->Release();
	xrgb->Release();
	CHECK(device->Get_Mirrors()->Live_Copies() == live - 3);
}

static void check_fans_and_buffers(PosixDevice9 *device)
{
	// A fan through DrawPrimitiveUP: centre and four corners, clockwise.
	begin(device, BLUE);
	const ScreenVertex fan[6] = {
		{ 16, 16, 0.5f, 1, RED, 0, 0 }, { 4, 4, 0.5f, 1, RED, 0, 0 }, { 28, 4, 0.5f, 1, RED, 0, 0 },
		{ 28, 28, 0.5f, 1, RED, 0, 0 }, { 4, 28, 0.5f, 1, RED, 0, 0 }, { 4, 4, 0.5f, 1, RED, 0, 0 } };
	device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 4, fan, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(10, 16, RED);
	CHECK_PIXEL(22, 16, RED);
	CHECK_PIXEL(16, 6, RED);
	CHECK_PIXEL(16, 26, RED);
	CHECK_PIXEL(1, 1, BLUE);

	// A static vertex buffer, drawn, written, and drawn again in one batch: two quads, each as it was.
	IDirect3DVertexBuffer9 *buffer = NULL;
	CHECK(device->CreateVertexBuffer(6 * sizeof(ScreenVertex), D3DUSAGE_WRITEONLY, SCREEN_FVF, D3DPOOL_MANAGED, &buffer, NULL) == D3D_OK);
	void *data = NULL;
	buffer->Lock(0, 0, &data, 0);
	quad((ScreenVertex *)data, 0, 0, 16, 16, 0.5f, RED);
	buffer->Unlock();
	begin(device, BLUE);
	device->SetStreamSource(0, buffer, 0, sizeof(ScreenVertex));
	const unsigned int stale_before = device->Get_Mirrors()->Stale_Flushes();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
	buffer->Lock(0, 0, &data, 0);
	quad((ScreenVertex *)data, 16, 16, 32, 32, 0.5f, GREEN);
	buffer->Unlock();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
	read_back(device);
	CHECK_PIXEL(8, 8, RED);
	CHECK_PIXEL(24, 24, GREEN);
	CHECK_PIXEL(24, 8, BLUE);
	CHECK(device->Get_Mirrors()->Stale_Flushes() == stale_before + 1);

	// A dynamic buffer, written between draws with DISCARD, staged at each draw instead: no flush.
	IDirect3DVertexBuffer9 *dynamic = NULL;
	CHECK(device->CreateVertexBuffer(12 * sizeof(ScreenVertex), D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, SCREEN_FVF,
		D3DPOOL_DEFAULT, &dynamic, NULL) == D3D_OK);
	begin(device, BLUE);
	device->SetStreamSource(0, dynamic, 0, sizeof(ScreenVertex));
	dynamic->Lock(0, 0, &data, D3DLOCK_DISCARD);
	quad((ScreenVertex *)data + 6, 16, 0, 32, 16, 0.5f, YELLOW);
	dynamic->Unlock();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 6, 2);
	dynamic->Lock(0, 0, &data, D3DLOCK_DISCARD);
	quad((ScreenVertex *)data + 6, 0, 16, 16, 32, 0.5f, GREEN);
	dynamic->Unlock();
	device->DrawPrimitive(D3DPT_TRIANGLELIST, 6, 2);
	read_back(device);
	CHECK_PIXEL(24, 8, YELLOW);
	CHECK_PIXEL(8, 24, GREEN);
	CHECK_PIXEL(8, 8, BLUE);
	CHECK(device->Get_Mirrors()->Stale_Flushes() == stale_before + 1);

	// An indexed fan over a static buffer with a base vertex: the fan's vertices from the UP draw above,
	// placed after two padding vertices.
	IDirect3DVertexBuffer9 *fan_buffer = NULL;
	IDirect3DIndexBuffer9 *indices = NULL;
	CHECK(device->CreateVertexBuffer(8 * sizeof(ScreenVertex), 0, SCREEN_FVF, D3DPOOL_MANAGED, &fan_buffer, NULL) == D3D_OK);
	CHECK(device->CreateIndexBuffer(8 * 2, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices, NULL) == D3D_OK);
	fan_buffer->Lock(0, 0, &data, 0);
	memset(data, 0, 2 * sizeof(ScreenVertex));
	memcpy((ScreenVertex *)data + 2, fan, sizeof(fan));
	for (int i = 0; i < 6; ++i) ((ScreenVertex *)data)[2 + i].colour = GREEN;
	fan_buffer->Unlock();
	indices->Lock(0, 0, &data, 0);
	const uint16_t fan_indices[8] = { 99, 99, 0, 1, 2, 3, 4, 5 };
	memcpy(data, fan_indices, sizeof(fan_indices));
	indices->Unlock();
	begin(device, BLUE);
	device->SetStreamSource(0, fan_buffer, 0, sizeof(ScreenVertex));
	device->SetIndices(indices);
	device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 2, 0, 6, 2, 4);
	read_back(device);
	CHECK_PIXEL(10, 16, GREEN);
	CHECK_PIXEL(16, 26, GREEN);
	CHECK_PIXEL(1, 1, BLUE);

	device->SetStreamSource(0, NULL, 0, 0);
	device->SetIndices(NULL);
	buffer->Release();
	dynamic->Release();
	fan_buffer->Release();
	indices->Release();
}

static void check_clears_and_depth(PosixDevice9 *device)
{
	// A clear between draws hides the first.
	begin(device, BLUE);
	ScreenVertex v[6];
	quad(v, 0, 0, 16, 32, 0.5f, RED);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	device->Clear(0, NULL, D3DCLEAR_TARGET, GREEN, 1.0f, 0);
	quad(v, 16, 0, 32, 32, 0.5f, RED);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(8, 16, GREEN);
	CHECK_PIXEL(24, 16, RED);

	// The depth test: the nearer quad wins whichever is drawn first.
	for (int order = 0; order < 2; ++order) {
		begin(device, BLUE);
		device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
		ScreenVertex near_quad[6], far_quad[6];
		quad(near_quad, 0, 0, 20, 20, 0.25f, RED);
		quad(far_quad, 10, 10, 32, 32, 0.75f, GREEN);
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, order == 0 ? near_quad : far_quad, sizeof(ScreenVertex));
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, order == 0 ? far_quad : near_quad, sizeof(ScreenVertex));
		read_back(device);
		CHECK_PIXEL(15, 15, RED);
		CHECK_PIXEL(25, 25, GREEN);
		CHECK_PIXEL(5, 5, RED);
	}
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

	// A clear with a smaller viewport clears only the viewport.
	begin(device, BLUE);
	D3DVIEWPORT9 whole, part = { 8, 8, 16, 16, 0.0f, 1.0f };
	device->GetViewport(&whole);
	device->SetViewport(&part);
	device->Clear(0, NULL, D3DCLEAR_TARGET, GREEN, 1.0f, 0);
	device->SetViewport(&whole);
	read_back(device);
	CHECK_PIXEL(7, 7, BLUE);
	CHECK_PIXEL(8, 8, GREEN);
	CHECK_PIXEL(23, 23, GREEN);
	CHECK_PIXEL(24, 24, BLUE);

	// Rectangles, each cut to the viewport.
	begin(device, BLUE);
	const D3DRECT rects[2] = { { 0, 0, 8, 8 }, { 24, 24, 40, 40 } };
	device->Clear(2, rects, D3DCLEAR_TARGET, RED, 1.0f, 0);
	read_back(device);
	CHECK_PIXEL(0, 0, RED);
	CHECK_PIXEL(7, 7, RED);
	CHECK_PIXEL(8, 8, BLUE);
	CHECK_PIXEL(31, 31, RED);
	CHECK_PIXEL(16, 16, BLUE);

	// Depth only, over the left half: depth 0 everywhere, then 1 on the left, then a quad at 0.5 over
	// everything shows on the left alone, and in its own colour.
	begin(device, BLUE);
	device->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 0.0f, 0);
	const D3DRECT left_half = { 0, 0, 16, 32 };
	device->Clear(1, &left_half, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	quad(v, 0, 0, 32, 32, 0.5f, YELLOW);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	read_back(device);
	CHECK_PIXEL(8, 16, YELLOW);
	CHECK_PIXEL(24, 16, BLUE);
}

// A3d: a render-target texture drawn into on the GPU and then sampled, a StretchRect out of it, and a
// draw that would sample the target it draws into refused.
static void check_render_targets(PosixDevice9 *device)
{
	IDirect3DTexture9 *target_texture = NULL;
	CHECK(device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &target_texture, NULL) == D3D_OK);
	IDirect3DSurface9 *target = NULL, *back = NULL;
	CHECK(target_texture->GetSurfaceLevel(0, &target) == D3D_OK);
	CHECK(device->GetRenderTarget(0, &back) == D3D_OK);
	CHECK(device->Gpu_Owns(target) && device->Gpu_Owns(back));

	// Into the target: red all over, then green on its left half.  The implicit depth surface is bigger
	// than the target, so the pass takes a matching scratch one.  The back buffer's own clear, with no
	// draw after it there, must still happen, on the back buffer and not on the target.
	begin(device, BLUE);
	device->Clear(0, NULL, D3DCLEAR_TARGET, YELLOW, 1.0f, 0);
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	device->Clear(0, NULL, D3DCLEAR_TARGET, RED, 1.0f, 0);
	ScreenVertex v[6];
	quad(v, 0, 0, 8, 16, 0.5f, GREEN);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));

	// Back to the back buffer: its yellow clear happened, and only there.
	CHECK(device->SetRenderTarget(0, back) == D3D_OK);
	read_back(device);
	CHECK_PIXEL(16, 16, YELLOW);

	// And the target drawn over all of it, point sampled.
	device->Clear(0, NULL, D3DCLEAR_TARGET, BLUE, 1.0f, 0);
	device->SetTexture(0, target_texture);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	quad(v, -0.5f, -0.5f, 31.5f, 31.5f, 0.5f, WHITE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	read_back(device);
	CHECK_PIXEL(4, 16, GREEN);
	CHECK_PIXEL(27, 16, RED);

	// A draw into the target that samples it is refused, and leaves the target as it was.
	const unsigned int recorded = device->Draws_Recorded();
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex));
	CHECK(device->Draws_Recorded() == recorded);
	CHECK(device->Draw_Refusals().count("sampling the render target it draws into") == 1);

	// The target's right half into the back buffer's top-left quarter, on the GPU.  Through the seam's
	// Gpu_StretchRect until a contributor's StretchRect calls it (then through StretchRect itself).
	CHECK(device->SetRenderTarget(0, back) == D3D_OK);
	device->SetTexture(0, NULL);
	device->Clear(0, NULL, D3DCLEAR_TARGET, BLUE, 1.0f, 0);
	const RenderRect from = { 8, 0, 16, 16 };
	const RenderRect to = { 0, 0, 16, 16 };
	CHECK(device->Gpu_StretchRect(target, &from, back, &to, D3DTEXF_POINT) == D3D_OK);
	read_back(device);
	CHECK_PIXEL(8, 8, RED);
	CHECK_PIXEL(24, 24, BLUE);

	// The target cleared on its own: nothing else moves.
	CHECK(device->SetRenderTarget(0, target) == D3D_OK);
	device->Clear(0, NULL, D3DCLEAR_TARGET, YELLOW, 1.0f, 0);
	CHECK(device->SetRenderTarget(0, back) == D3D_OK);
	read_back(device);
	CHECK_PIXEL(24, 24, BLUE);

	target->Release();
	back->Release();
	target_texture->Release();
}

static void check_refusal(PosixDevice9 *device)
{
	IDirect3DCubeTexture9 *cube = NULL;
	CHECK(device->CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube, NULL) == D3D_OK);
	begin(device, BLUE);
	device->SetTexture(0, cube);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	ScreenVertex v[6];
	quad(v, 0, 0, 32, 32, 0.5f, RED);
	const unsigned int recorded = device->Draws_Recorded();
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex)) == D3D_OK);
	CHECK(device->Draws_Recorded() == recorded);
	unsigned int refused = 0;
	for (std::map<std::string, unsigned int>::const_iterator it = device->Draw_Refusals().begin();
		it != device->Draw_Refusals().end(); ++it) {
		if (it->first.find("cube") != std::string::npos) refused += it->second;
	}
	CHECK(refused == 1);
	read_back(device);
	CHECK_PIXEL(16, 16, BLUE);
	device->SetTexture(0, NULL);
	cube->Release();
}

int main()
{
	IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = SIZE;
	parameters.BackBufferHeight = SIZE;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *created = NULL;
	CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &created) == D3D_OK);
	PosixDevice9 *device = static_cast<PosixDevice9 *>(created);

	// Headless first: a draw succeeds and records nothing.
	ScreenVertex v[6];
	quad(v, 0, 0, 8, 8, 0.5f, RED);
	device->SetFVF(SCREEN_FVF);
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(ScreenVertex)) == D3D_OK);
	CHECK(device->Draws_Recorded() == 0);

	if (!SDL_Init(SDL_INIT_VIDEO) || device->Create_Gpu_Frame(true) != D3D_OK) {
		printf("posix_gpu_draw_selfcheck: SKIP - no GPU device here: %s\n", SDL_GetError());
		device->Release();
		d3d->Release();
		return failures != 0 ? 1 : 77;
	}
	check_half_pixel_and_winding(device);
	check_transformed(device);
	check_textures(device);
	check_fans_and_buffers(device);
	check_clears_and_depth(device);
	check_render_targets(device);
	check_refusal(device);

	printf("posix_gpu_draw_selfcheck: %s: %u draws recorded, %u textures and %u buffers uploaded, %u stale flushes\n",
		SDL_GetGPUDeviceDriver(device->Get_Gpu()->Device()), device->Draws_Recorded(),
		device->Get_Mirrors()->Textures_Uploaded(), device->Get_Mirrors()->Buffers_Uploaded(),
		device->Get_Mirrors()->Stale_Flushes());
	device->Release();
	d3d->Release();
	SDL_Quit();
	if (failures != 0) {
		printf("posix_gpu_draw_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	return 0;
}
