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

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#if defined(_M_ARM64)
#include "d3dx9portable.h"
#endif

#include <stdio.h>

D3DXAssembleShaderFunction			D3DXAssembleShader = NULL;
D3DXCompileShaderFunction			D3DXCompileShader = NULL;
D3DXDisassembleShaderFunction		D3DXDisassembleShader = NULL;
D3DXCreateTextureFunction			D3DXCreateTexture = NULL;
D3DXCreateCubeTextureFunction		D3DXCreateCubeTexture = NULL;
D3DXCreateVolumeTextureFunction		D3DXCreateVolumeTexture = NULL;
D3DXCreateTextureFromFileExFunction	D3DXCreateTextureFromFileExA = NULL;
D3DXFilterTextureFunction			D3DXFilterTexture = NULL;
D3DXLoadSurfaceFromSurfaceFunction	D3DXLoadSurfaceFromSurface = NULL;
D3DXGetFVFVertexSizeFunction		D3DXGetFVFVertexSize = NULL;

D3DXMatrixInverseFunction	D3DXMatrixInverse = NULL;
D3DXMatrixBinaryFunction	D3DXMatrixMultiply = NULL;
D3DXMatrixUnaryFunction		D3DXMatrixTranspose = NULL;
D3DXMatrixTripleFunction	D3DXMatrixScaling = NULL;
D3DXMatrixTripleFunction	D3DXMatrixTranslation = NULL;
D3DXMatrixAngleFunction		D3DXMatrixRotationZ = NULL;
D3DXVec4TransformFunction	D3DXVec4TransformFromDLL = NULL;
D3DXVec3TransformFunction	D3DXVec3Transform = NULL;

// The last D3DX9 release, and the one d3d8to9 binds, so a machine that runs this fork
// today already has it.  There is no fallback to an earlier d3dx9_NN.dll on purpose:
// the earlier ones differ in behaviour, and a renderer that silently landed on one
// would be the hardest kind of bug to see.
static const char D3DX9_MODULE_NAME[] = "d3dx9_43.dll";

static HMODULE D3DX9Module = NULL;
static bool BindAttempted = false;
static bool BindSucceeded = false;

static void release_module(void);

#if defined(_M_ARM64)
/* Windows on Arm: Microsoft shipped d3dx9_43.dll for x86 and x64 only, and an ARM64 process cannot load
	 either.  The arithmetic is bound to the port's own bodies instead (d3dx9portable.cpp, what macOS and
	 Linux use, held against the DLL by test_d3dx9portable_oracle on x64), so the renderer's matrices and
	 vertex sizes are right.  The three texture constructors are the device's own Create* calls without
	 D3DX's size and format fitting, which is enough for the engine to start (DX8Wrapper::_Create_DX8_Texture
	 called through null here, on the ARM64 VM).  Everything else needs what only the DLL has - image
	 decoding, filtering, an assembler, a compiler - and refuses as a failed DLL call would: file textures
	 become MissingTexture, the water keeps its fixed-function path.  The bind still reports failure.
	 Native ARM64 rendering is -d3d12's (X1), where d3dx9posix serves whole; until then ARM64 players run
	 the x64 build under Windows' emulation. */
static HRESULT WINAPI portable_matrix_inverse(D3DXMATRIX * out, FLOAT * determinant, const D3DXMATRIX * matrix)
{
	// No caller reads the result; the DLL's is the matrix pointer, or null when it is singular.
	return D3DXPortable_Matrix_Inverse(out, determinant, matrix) != NULL ? S_OK : E_FAIL;
}

static UINT WINAPI portable_fvf_vertex_size(DWORD fvf)
{
	return D3DXPortable_FVF_Vertex_Size(fvf);
}

static HRESULT WINAPI device_create_texture(LPDIRECT3DDEVICE9 device, UINT width, UINT height,
	UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9 * texture)
{
	if (device == NULL || texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	return device->CreateTexture(width, height, mip_levels, usage, format, pool, texture, NULL);
}

static HRESULT WINAPI device_create_cube_texture(LPDIRECT3DDEVICE9 device, UINT edge_length,
	UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DCUBETEXTURE9 * texture)
{
	if (device == NULL || texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	return device->CreateCubeTexture(edge_length, mip_levels, usage, format, pool, texture, NULL);
}

static HRESULT WINAPI device_create_volume_texture(LPDIRECT3DDEVICE9 device, UINT width, UINT height,
	UINT depth, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
	LPDIRECT3DVOLUMETEXTURE9 * texture)
{
	if (device == NULL || texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	return device->CreateVolumeTexture(width, height, depth, mip_levels, usage, format, pool, texture, NULL);
}

static HRESULT WINAPI unavailable_assemble_shader(LPCSTR, UINT, const D3DXMACRO *, LPD3DXINCLUDE, DWORD,
	LPD3DXBUFFER * shader, LPD3DXBUFFER * errors)
{
	if (shader != NULL) *shader = NULL;
	if (errors != NULL) *errors = NULL;
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_compile_shader(LPCSTR, UINT, const D3DXMACRO *, LPD3DXINCLUDE, LPCSTR,
	LPCSTR, DWORD, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors, void ** constant_table)
{
	if (shader != NULL) *shader = NULL;
	if (errors != NULL) *errors = NULL;
	if (constant_table != NULL) *constant_table = NULL;
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_disassemble_shader(const DWORD *, BOOL, LPCSTR, LPD3DXBUFFER * disassembly)
{
	if (disassembly != NULL) *disassembly = NULL;
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_texture_from_file(LPDIRECT3DDEVICE9, LPCSTR, UINT, UINT, UINT, DWORD,
	D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO *, PALETTEENTRY *, LPDIRECT3DTEXTURE9 * texture)
{
	if (texture != NULL) *texture = NULL;
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_filter_texture(LPDIRECT3DBASETEXTURE9, const PALETTEENTRY *, UINT, DWORD)
{
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_load_surface(LPDIRECT3DSURFACE9, const PALETTEENTRY *, const RECT *,
	LPDIRECT3DSURFACE9, const PALETTEENTRY *, const RECT *, DWORD, D3DCOLOR)
{
	return D3DERR_NOTAVAILABLE;
}

static void bind_arm64_runtime(void)
{
	D3DXCreateTexture = device_create_texture;
	D3DXCreateCubeTexture = device_create_cube_texture;
	D3DXCreateVolumeTexture = device_create_volume_texture;
	D3DXAssembleShader = unavailable_assemble_shader;
	D3DXCompileShader = unavailable_compile_shader;
	D3DXDisassembleShader = unavailable_disassemble_shader;
	D3DXCreateTextureFromFileExA = unavailable_texture_from_file;
	D3DXFilterTexture = unavailable_filter_texture;
	D3DXLoadSurfaceFromSurface = unavailable_load_surface;
	D3DXGetFVFVertexSize = portable_fvf_vertex_size;
	D3DXMatrixInverse = portable_matrix_inverse;
	D3DXMatrixMultiply = (D3DXMatrixBinaryFunction)D3DXPortable_Matrix_Multiply;
	D3DXMatrixTranspose = (D3DXMatrixUnaryFunction)D3DXPortable_Matrix_Transpose;
	D3DXMatrixScaling = (D3DXMatrixTripleFunction)D3DXPortable_Matrix_Scaling;
	D3DXMatrixTranslation = (D3DXMatrixTripleFunction)D3DXPortable_Matrix_Translation;
	D3DXMatrixRotationZ = (D3DXMatrixAngleFunction)D3DXPortable_Matrix_Rotation_Z;
	D3DXVec3Transform = (D3DXVec3TransformFunction)D3DXPortable_Vec3_Transform;
}
#endif

struct ErrorName
{
	HRESULT Result;
	const char * Name;
};

// Every code the renderer's own paths return.  D3DERR values are D3D_OK plus the
// Direct3D facility, so they cannot be written as plain integers here.
static const ErrorName ERROR_NAMES[] =
{
	{ D3D_OK,							"D3D_OK" },
	{ D3DERR_DEVICELOST,				"D3DERR_DEVICELOST" },
	{ D3DERR_DEVICENOTRESET,			"D3DERR_DEVICENOTRESET" },
	{ D3DERR_DRIVERINTERNALERROR,		"D3DERR_DRIVERINTERNALERROR" },
	{ D3DERR_INVALIDCALL,				"D3DERR_INVALIDCALL" },
	{ D3DERR_INVALIDDEVICE,				"D3DERR_INVALIDDEVICE" },
	{ D3DERR_NOTAVAILABLE,				"D3DERR_NOTAVAILABLE" },
	{ D3DERR_NOTFOUND,					"D3DERR_NOTFOUND" },
	{ D3DERR_OUTOFVIDEOMEMORY,			"D3DERR_OUTOFVIDEOMEMORY" },
	{ D3DERR_TOOMANYOPERATIONS,			"D3DERR_TOOMANYOPERATIONS" },
	{ D3DERR_UNSUPPORTEDTEXTUREFILTER,	"D3DERR_UNSUPPORTEDTEXTUREFILTER" },
	{ D3DERR_WRONGTEXTUREFORMAT,		"D3DERR_WRONGTEXTUREFORMAT" },
	{ E_OUTOFMEMORY,					"E_OUTOFMEMORY" },
	{ E_INVALIDARG,						"E_INVALIDARG" },
	{ E_FAIL,							"E_FAIL" }
};

static const int ERROR_NAME_COUNT = sizeof(ERROR_NAMES) / sizeof(ERROR_NAMES[0]);
static const int ERROR_TEXT_SIZE = 32;

static char UnknownErrorText[ERROR_TEXT_SIZE] = "";

bool Bind_D3DX9_Runtime(void)
{
	if (BindAttempted) {
		return BindSucceeded;
	}
	BindAttempted = true;

#if defined(_M_ARM64)
	bind_arm64_runtime();
	return false;
#else
	D3DX9Module = LoadLibraryA(D3DX9_MODULE_NAME);
	if (D3DX9Module == NULL) {
		return false;
	}

	D3DXAssembleShader = (D3DXAssembleShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXAssembleShader");
	D3DXCompileShader = (D3DXCompileShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXCompileShader");
	D3DXDisassembleShader = (D3DXDisassembleShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXDisassembleShader");
	D3DXCreateTexture = (D3DXCreateTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateTexture");
	D3DXCreateCubeTexture = (D3DXCreateCubeTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateCubeTexture");
	D3DXCreateVolumeTexture = (D3DXCreateVolumeTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateVolumeTexture");
	D3DXCreateTextureFromFileExA = (D3DXCreateTextureFromFileExFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateTextureFromFileExA");
	D3DXFilterTexture = (D3DXFilterTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXFilterTexture");
	D3DXLoadSurfaceFromSurface = (D3DXLoadSurfaceFromSurfaceFunction)
		GetProcAddress(D3DX9Module, "D3DXLoadSurfaceFromSurface");
	D3DXGetFVFVertexSize = (D3DXGetFVFVertexSizeFunction)
		GetProcAddress(D3DX9Module, "D3DXGetFVFVertexSize");

	D3DXMatrixInverse = (D3DXMatrixInverseFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixInverse");
	D3DXMatrixMultiply = (D3DXMatrixBinaryFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixMultiply");
	D3DXMatrixTranspose = (D3DXMatrixUnaryFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixTranspose");
	D3DXMatrixScaling = (D3DXMatrixTripleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixScaling");
	D3DXMatrixTranslation = (D3DXMatrixTripleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixTranslation");
	D3DXMatrixRotationZ = (D3DXMatrixAngleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixRotationZ");
	D3DXVec4TransformFromDLL = (D3DXVec4TransformFunction)
		GetProcAddress(D3DX9Module, "D3DXVec4Transform");
	D3DXVec3Transform = (D3DXVec3TransformFunction)
		GetProcAddress(D3DX9Module, "D3DXVec3Transform");

	BindSucceeded = D3DXAssembleShader != NULL
		&& D3DXCompileShader != NULL
		&& D3DXDisassembleShader != NULL
		&& D3DXCreateTexture != NULL
		&& D3DXCreateCubeTexture != NULL
		&& D3DXCreateVolumeTexture != NULL
		&& D3DXCreateTextureFromFileExA != NULL
		&& D3DXFilterTexture != NULL
		&& D3DXLoadSurfaceFromSurface != NULL
		&& D3DXGetFVFVertexSize != NULL
		&& D3DXMatrixInverse != NULL
		&& D3DXMatrixMultiply != NULL
		&& D3DXMatrixTranspose != NULL
		&& D3DXMatrixScaling != NULL
		&& D3DXMatrixTranslation != NULL
		&& D3DXMatrixRotationZ != NULL
		&& D3DXVec4TransformFromDLL != NULL
		&& D3DXVec3Transform != NULL;

	if (!BindSucceeded) {
		release_module();
	}
	return BindSucceeded;
#endif
}

void Unbind_D3DX9_Runtime(void)
{
	release_module();
	BindAttempted = false;
	BindSucceeded = false;
}

static void release_module(void)
{
	D3DXAssembleShader = NULL;
	D3DXCompileShader = NULL;
	D3DXDisassembleShader = NULL;
	D3DXCreateTexture = NULL;
	D3DXCreateCubeTexture = NULL;
	D3DXCreateVolumeTexture = NULL;
	D3DXCreateTextureFromFileExA = NULL;
	D3DXFilterTexture = NULL;
	D3DXLoadSurfaceFromSurface = NULL;
	D3DXGetFVFVertexSize = NULL;

	D3DXMatrixInverse = NULL;
	D3DXMatrixMultiply = NULL;
	D3DXMatrixTranspose = NULL;
	D3DXMatrixScaling = NULL;
	D3DXMatrixTranslation = NULL;
	D3DXMatrixRotationZ = NULL;
	D3DXVec4TransformFromDLL = NULL;
	D3DXVec3Transform = NULL;

	if (D3DX9Module != NULL) {
		FreeLibrary(D3DX9Module);
		D3DX9Module = NULL;
	}
}

UINT Get_FVF_Vertex_Size(DWORD fvf)
{
#if defined(_M_ARM64)
	return D3DXPortable_FVF_Vertex_Size(fvf);
#else
	if (!Bind_D3DX9_Runtime()) {
		return 0;
	}
	return D3DXGetFVFVertexSize(fvf);
#endif
}

const char * Get_D3D_Error_String(HRESULT result)
{
	for (int index = 0; index < ERROR_NAME_COUNT; ++index) {
		if (ERROR_NAMES[index].Result == result) {
			return ERROR_NAMES[index].Name;
		}
	}
	snprintf(UnknownErrorText, ERROR_TEXT_SIZE, "0x%08lx", (unsigned long)result);
	UnknownErrorText[ERROR_TEXT_SIZE - 1] = '\0';
	return UnknownErrorText;
}
