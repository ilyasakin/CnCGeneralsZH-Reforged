/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

// X1's vertical slice: zh_d3d12.dll loaded as the game will load it, and one draw through it on D3D12.
//
// Only the SDK's d3d9.h is seen here, as in the engine: the DLL is found by path (argv[1]), its device made
// through ZH_D3D12_Direct3DCreate9 with no window (ZH_OFFSCREEN_FRAMES: the device draws into its own
// target), a clear and one triangle drawn, and the target read back through GetRenderTargetData.  The pixels
// are exact: a flat colour, no blending, no filtering.  Also checked: the wrappers keep a pointer's identity
// and the references come out even, and the D3DX exports resolve and work.
//
//   test_d3d12_slice <path to zh_d3d12.dll>    exit 0 on a pass; 77 when this machine has no D3D12 device

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

typedef IDirect3D9 * (WINAPI * CreateFunction)(UINT);
typedef HRESULT (WINAPI * CreateTextureFunction)(IDirect3DDevice9 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
	IDirect3DTexture9 **);
typedef UINT (WINAPI * VertexSizeFunction)(DWORD);

struct ScreenVertex
{
	float X, Y, Z, RHW;
	DWORD Colour;
};
static const DWORD SCREEN_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
static const UINT TARGET_SIZE = 64;
static const D3DCOLOR CLEAR_COLOUR = D3DCOLOR_XRGB(10, 20, 30);
static const D3DCOLOR DRAW_COLOUR = D3DCOLOR_XRGB(64, 128, 191);

static DWORD pixel_at(const D3DLOCKED_RECT & locked, UINT x, UINT y)
{
	const BYTE * row = (const BYTE *)locked.pBits + y * locked.Pitch;
	DWORD value;
	memcpy(&value, row + x * 4, sizeof(value));
	return value & 0x00FFFFFFu;
}

int main(int argc, char ** argv)
{
	if (argc < 2) {
		printf("usage: test_d3d12_slice <zh_d3d12.dll>\n");
		return 2;
	}
	// The device reads this through SDL's hints, which fall back to the environment.
	SetEnvironmentVariableA("ZH_OFFSCREEN_FRAMES", "1");
	_putenv_s("ZH_OFFSCREEN_FRAMES", "1");

	HMODULE module = LoadLibraryA(argv[1]);
	if (module == NULL) {
		printf("d3d12_slice: FAIL - LoadLibrary(%s): error %lu\n", argv[1], GetLastError());
		return 1;
	}
	CreateFunction create = (CreateFunction)GetProcAddress(module, "ZH_D3D12_Direct3DCreate9");
	CreateTextureFunction create_texture = (CreateTextureFunction)GetProcAddress(module, "D3DXCreateTexture");
	VertexSizeFunction vertex_size = (VertexSizeFunction)GetProcAddress(module, "D3DXGetFVFVertexSize");
	static const char * const D3DX_NAMES[] = { "D3DXAssembleShader", "D3DXCompileShader", "D3DXDisassembleShader",
		"D3DXCreateTexture", "D3DXCreateCubeTexture", "D3DXCreateVolumeTexture", "D3DXCreateTextureFromFileExA",
		"D3DXFilterTexture", "D3DXLoadSurfaceFromSurface", "D3DXGetFVFVertexSize",
		"ZH_D3D12_Name_Shader", "ZH_D3D12_Keep_D3D8_Declaration" };
	for (size_t index = 0; index < sizeof(D3DX_NAMES) / sizeof(D3DX_NAMES[0]); ++index) {
		if (GetProcAddress(module, D3DX_NAMES[index]) == NULL) {
			++failures;
			printf("FAIL: zh_d3d12.dll exports no %s\n", D3DX_NAMES[index]);
		}
	}
	if (create == NULL || create_texture == NULL || vertex_size == NULL) {
		printf("d3d12_slice: FAIL - the exports are missing\n");
		return 1;
	}
	CHECK(vertex_size(SCREEN_FVF) == sizeof(ScreenVertex));

	IDirect3D9 * d3d = create(D3D_SDK_VERSION);
	if (d3d == NULL) {
		printf("d3d12_slice: FAIL - ZH_D3D12_Direct3DCreate9 gave nothing\n");
		return 1;
	}
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = TARGET_SIZE;
	parameters.BackBufferHeight = TARGET_SIZE;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = TRUE;
	parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
	parameters.EnableAutoDepthStencil = TRUE;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 * device = NULL;
	const HRESULT made = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
		&parameters, &device);
	if (FAILED(made) || device == NULL) {
		printf("d3d12_slice: SKIP - no device here (0x%08lx)\n", (unsigned long)made);
		d3d->Release();
		return 77;
	}

	// One pointer per device object, and the counts even: two GetRenderTargets are the same wrapper.
	IDirect3DSurface9 * target = NULL;
	IDirect3DSurface9 * again = NULL;
	CHECK(device->GetRenderTarget(0, &target) == D3D_OK && target != NULL);
	CHECK(device->GetRenderTarget(0, &again) == D3D_OK && again == target);
	if (again != NULL)
		again->Release();

	CHECK(device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, CLEAR_COLOUR, 1.0f, 0) == D3D_OK);
	CHECK(device->BeginScene() == D3D_OK);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTexture(0, NULL);
	CHECK(device->SetFVF(SCREEN_FVF) == D3D_OK);
	// The upper-left half of the target: (8, 8) is inside, (56, 56) outside.
	const ScreenVertex triangle[3] = {
		{ 0.0f, 0.0f, 0.5f, 1.0f, DRAW_COLOUR },
		{ (float)TARGET_SIZE, 0.0f, 0.5f, 1.0f, DRAW_COLOUR },
		{ 0.0f, (float)TARGET_SIZE, 0.5f, 1.0f, DRAW_COLOUR },
	};
	CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(ScreenVertex)) == D3D_OK);
	CHECK(device->EndScene() == D3D_OK);

	IDirect3DSurface9 * copy = NULL;
	CHECK(device->CreateOffscreenPlainSurface(TARGET_SIZE, TARGET_SIZE, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &copy, NULL) == D3D_OK);
	DWORD inside = 0, outside = 0;
	if (target != NULL && copy != NULL && device->GetRenderTargetData(target, copy) == D3D_OK) {
		D3DLOCKED_RECT locked;
		if (copy->LockRect(&locked, NULL, D3DLOCK_READONLY) == D3D_OK) {
			inside = pixel_at(locked, 8, 8);
			outside = pixel_at(locked, 56, 56);
			copy->UnlockRect();
		} else {
			++failures;
			printf("FAIL: LockRect on the copy\n");
		}
	} else {
		++failures;
		printf("FAIL: GetRenderTargetData\n");
	}
	CHECK(inside == (DRAW_COLOUR & 0x00FFFFFFu));
	CHECK(outside == (CLEAR_COLOUR & 0x00FFFFFFu));

	// D3DX through the DLL: a texture made on this device, with the levels asked for.
	IDirect3DTexture9 * texture = NULL;
	CHECK(create_texture(device, 16, 16, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture) == D3D_OK && texture != NULL);
	if (texture != NULL) {
		CHECK(texture->GetLevelCount() == 3);
		IDirect3DDevice9 * owner = NULL;
		CHECK(texture->GetDevice(&owner) == D3D_OK && owner == device);
		if (owner != NULL)
			owner->Release();
		CHECK(texture->Release() == 0);
	}

	if (copy != NULL)
		CHECK(copy->Release() == 0);
	if (target != NULL)
		target->Release();
	CHECK(device->Release() == 0);
	CHECK(d3d->Release() == 0);

	printf("d3d12_slice: inside %06lx (want %06lx), outside %06lx (want %06lx)\n", (unsigned long)inside,
		(unsigned long)(DRAW_COLOUR & 0x00FFFFFFu), (unsigned long)outside, (unsigned long)(CLEAR_COLOUR & 0x00FFFFFFu));
	if (failures != 0) {
		printf("d3d12_slice: %d FAILED\n", failures);
		return 1;
	}
	printf("D3D12 SLICE PASS\n");
	return 0;
}
