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

// What zh_d3d12.dll exports (zh_d3d12.def): the device, and the ten D3DX entry points the engine binds.
//
// Interoperability: the D3DX functions are exported under the names of Microsoft's D3DX9 API, which is how
// the engine's binder (d3dx9runtime.cpp) finds its entry points in a module.  The bodies are this project's
// own implementations (d3dx9posix, through D3D12ShimBridge.cpp), written from the API's documented
// behaviour; nothing of Microsoft's D3DX is in them, and d3dx9_43.dll is not needed under -d3d12.  Each one
// is checked below against the engine's own function type, so the binder's cast is to the right signature.

#include "D3D12Adapter.h"
#include "d3dx9runtime.h"

#include <stdlib.h>
#include <string.h>
#include <type_traits>

using namespace zhd3d12;

namespace {

// The shader bytes D3DXAssembleShader hands back, as the engine's ID3DXBuffer (d3dx9runtime.h).
class ShaderBuffer : public ID3DXBuffer
{
public:
	ShaderBuffer(void * bytes, DWORD size) : References(1), Bytes(bytes), Size(size) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID * out) override
	{
		if (out == NULL)
			return E_POINTER;
		if (IsEqualGUID(iid, IID_IUnknown)) {
			AddRef();
			*out = static_cast<IUnknown *>(this);
			return S_OK;
		}
		*out = NULL;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&References); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const LONG left = InterlockedDecrement(&References);
		if (left == 0) {
			free(Bytes);
			delete this;
		}
		return (ULONG)left;
	}
	LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return Bytes; }
	DWORD STDMETHODCALLTYPE GetBufferSize() override { return Size; }

private:
	volatile LONG References;
	void * const Bytes;
	const DWORD Size;
};

DeviceAdapter * adapter_of(LPDIRECT3DDEVICE9 device) { return static_cast<DeviceAdapter *>(device); }

}  // namespace

extern "C" IDirect3D9 * WINAPI ZH_D3D12_Direct3DCreate9(UINT sdk_version)
{
	zhposix::IDirect3D9 * direct3d = static_cast<zhposix::IDirect3D9 *>(ZH_PosixDirect3DCreate9(sdk_version));
	if (direct3d == NULL)
		return NULL;
	return new Direct3DAdapter(direct3d);
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXAssembleShader(LPCSTR source, UINT source_length, const D3DXMACRO *, LPD3DXINCLUDE,
	DWORD, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors)
{
	if (errors != NULL)
		*errors = NULL;
	if (shader == NULL)
		return D3DERR_INVALIDCALL;
	*shader = NULL;
	void * bytes = NULL;
	unsigned int size = 0;
	const HRESULT result = ZHP_D3DX_Assemble_Shader(source, source_length, &bytes, &size);
	if (FAILED(result))
		return result;
	*shader = new ShaderBuffer(bytes, size);
	return D3D_OK;
}

// d3dx9posix has no compiler or disassembler: the SDL3 GPU draw compiles its own programs, as off Windows.
extern "C" HRESULT WINAPI ZH_D3D12_D3DXCompileShader(LPCSTR, UINT, const D3DXMACRO *, LPD3DXINCLUDE, LPCSTR, LPCSTR, DWORD,
	LPD3DXBUFFER * shader, LPD3DXBUFFER * errors, void ** constant_table)
{
	if (shader != NULL)
		*shader = NULL;
	if (errors != NULL)
		*errors = NULL;
	if (constant_table != NULL)
		*constant_table = NULL;
	return D3DERR_NOTAVAILABLE;
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXDisassembleShader(const DWORD *, BOOL, LPCSTR, LPD3DXBUFFER * disassembly)
{
	if (disassembly != NULL)
		*disassembly = NULL;
	return D3DERR_NOTAVAILABLE;
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXCreateTexture(LPDIRECT3DDEVICE9 device, UINT width, UINT height, UINT mip_levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9 * texture)
{
	if (device == NULL || texture == NULL)
		return D3DERR_INVALIDCALL;
	void * made = NULL;
	const HRESULT result = ZHP_D3DX_Create_Texture(Shim_Of(device), width, height, mip_levels, usage, format, pool, &made);
	*texture = Adopt_Texture(static_cast<zhposix::IDirect3DTexture9 *>(made), adapter_of(device));
	return result;
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXCreateCubeTexture(LPDIRECT3DDEVICE9 device, UINT edge_length, UINT mip_levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DCUBETEXTURE9 * texture)
{
	if (device == NULL || texture == NULL)
		return D3DERR_INVALIDCALL;
	void * made = NULL;
	const HRESULT result = ZHP_D3DX_Create_Cube_Texture(Shim_Of(device), edge_length, mip_levels, usage, format, pool, &made);
	*texture = Adopt_Cube_Texture(static_cast<zhposix::IDirect3DCubeTexture9 *>(made), adapter_of(device));
	return result;
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXCreateVolumeTexture(LPDIRECT3DDEVICE9 device, UINT width, UINT height, UINT depth,
	UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DVOLUMETEXTURE9 * texture)
{
	if (device == NULL || texture == NULL)
		return D3DERR_INVALIDCALL;
	void * made = NULL;
	const HRESULT result = ZHP_D3DX_Create_Volume_Texture(Shim_Of(device), width, height, depth, mip_levels, usage, format,
		pool, &made);
	*texture = Adopt_Volume_Texture(static_cast<zhposix::IDirect3DVolumeTexture9 *>(made), adapter_of(device));
	return result;
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE9 device, LPCSTR file_name, UINT width,
	UINT height, UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mip_filter,
	D3DCOLOR colour_key, D3DXIMAGE_INFO * info, PALETTEENTRY * palette, LPDIRECT3DTEXTURE9 * texture)
{
	if (device == NULL || texture == NULL)
		return D3DERR_INVALIDCALL;
	void * made = NULL;
	const HRESULT result = ZHP_D3DX_Create_Texture_From_File(Shim_Of(device), file_name, width, height, mip_levels, usage,
		format, pool, filter, mip_filter, colour_key, info, palette, &made);
	*texture = Adopt_Texture(static_cast<zhposix::IDirect3DTexture9 *>(made), adapter_of(device));
	return result;
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXFilterTexture(LPDIRECT3DBASETEXTURE9 texture, const PALETTEENTRY * palette,
	UINT source_level, DWORD filter)
{
	return ZHP_D3DX_Filter_Texture(Shim_Of(texture), palette, source_level, filter);
}

extern "C" HRESULT WINAPI ZH_D3D12_D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9 destination,
	const PALETTEENTRY * destination_palette, const RECT * destination_rect, LPDIRECT3DSURFACE9 source,
	const PALETTEENTRY * source_palette, const RECT * source_rect, DWORD filter, D3DCOLOR colour_key)
{
	return ZHP_D3DX_Load_Surface_From_Surface(Shim_Of(destination), destination_palette, destination_rect, Shim_Of(source),
		source_palette, source_rect, filter, colour_key);
}

extern "C" UINT WINAPI ZH_D3D12_D3DXGetFVFVertexSize(DWORD fvf)
{
	return ZHP_D3DX_FVF_Vertex_Size(fvf);
}

// The engine's two hooks into the device (d3d12runtime.h): a shader's name, for the draw to find D3's
// transcription by, and the D3D8 declaration a vertex declaration was decoded from.
extern "C" void WINAPI ZH_D3D12_Name_Shader(const void * shader, const char * name)
{
	const void * ours = Shim_Of_Shader(shader);
	if (ours != NULL)
		ZHP_Name_Shader(ours, name);
}

extern "C" void WINAPI ZH_D3D12_Keep_D3D8_Declaration(IDirect3DVertexDeclaration9 * declaration, const DWORD * d3d8_tokens)
{
	static_assert(sizeof(*d3d8_tokens) == sizeof(unsigned int), "the D3D8 tokens are 32-bit words");
	if (declaration != NULL)
		ZHP_Keep_D3D8_Declaration(Shim_Of(declaration), reinterpret_cast<const unsigned int *>(d3d8_tokens));
}

// The binder casts what GetProcAddress finds to these types; each export must be exactly one.
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXAssembleShader), D3DXAssembleShaderFunction>::value, "D3DXAssembleShader");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXCompileShader), D3DXCompileShaderFunction>::value, "D3DXCompileShader");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXDisassembleShader), D3DXDisassembleShaderFunction>::value, "D3DXDisassembleShader");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXCreateTexture), D3DXCreateTextureFunction>::value, "D3DXCreateTexture");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXCreateCubeTexture), D3DXCreateCubeTextureFunction>::value, "D3DXCreateCubeTexture");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXCreateVolumeTexture), D3DXCreateVolumeTextureFunction>::value, "D3DXCreateVolumeTexture");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXCreateTextureFromFileExA), D3DXCreateTextureFromFileExFunction>::value,
	"D3DXCreateTextureFromFileExA");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXFilterTexture), D3DXFilterTextureFunction>::value, "D3DXFilterTexture");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXLoadSurfaceFromSurface), D3DXLoadSurfaceFromSurfaceFunction>::value,
	"D3DXLoadSurfaceFromSurface");
static_assert(std::is_same<decltype(&ZH_D3D12_D3DXGetFVFVertexSize), D3DXGetFVFVertexSizeFunction>::value, "D3DXGetFVFVertexSize");
