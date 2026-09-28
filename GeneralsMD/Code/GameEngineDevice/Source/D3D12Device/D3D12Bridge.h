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

// The two Direct3D 9s zh_d3d12.dll (X1, -d3d12) stands between.
//
// The engine on Windows speaks COM: the SDK's d3d9.h, whose interfaces the adapter implements.  The device
// it forwards to is posixd3d9, the SDL3 GPU device macOS and Linux draw with, which speaks Platform/
// D3D9Posix.h: the same names, the same values and structure layouts (Tools/d3d9posix_check.py, and the
// MSVC layout check of X1 1b), but abstract classes instead of COM.  Both are included here, the POSIX one
// inside namespace zhposix, so zhposix::IDirect3DDevice9 is the device and ::IDirect3DDevice9 is COM's.
// The POSIX headers' macros are undefined in between (D3D12ShimUndefs.h) and redefined by them with the
// same values; the adapter builds with C4005 as an error, so a macro that slipped through stops it.
//
// posixd3d9 and d3dx9posix are compiled against the POSIX header outside any namespace, so their free
// functions cannot be named from here; the few the adapter needs have C names, declared at the bottom.

#ifndef D3D12DEVICE_D3D12BRIDGE_H
#define D3D12DEVICE_D3D12BRIDGE_H

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <d3d9.h>

#include "D3D12ShimUndefs.h"

#define D3D9POSIX_CHECKER
#include "Platform/RenderTypes.h"
namespace zhposix {
#include "Platform/D3D9Posix.h"
}

// Everything the adapter reinterprets between the two, checked here as well as by the layout checks.
#define ZH_D3D12_SAME_LAYOUT(name) \
	static_assert(sizeof(::name) == sizeof(zhposix::name) && alignof(::name) == alignof(zhposix::name), #name)
ZH_D3D12_SAME_LAYOUT(D3DSURFACE_DESC);
ZH_D3D12_SAME_LAYOUT(D3DVOLUME_DESC);
ZH_D3D12_SAME_LAYOUT(D3DLOCKED_RECT);
ZH_D3D12_SAME_LAYOUT(D3DLOCKED_BOX);
ZH_D3D12_SAME_LAYOUT(D3DBOX);
ZH_D3D12_SAME_LAYOUT(D3DVERTEXBUFFER_DESC);
ZH_D3D12_SAME_LAYOUT(D3DINDEXBUFFER_DESC);
ZH_D3D12_SAME_LAYOUT(D3DDISPLAYMODE);
ZH_D3D12_SAME_LAYOUT(D3DCAPS9);
ZH_D3D12_SAME_LAYOUT(D3DPRESENT_PARAMETERS);
ZH_D3D12_SAME_LAYOUT(D3DGAMMARAMP);
ZH_D3D12_SAME_LAYOUT(D3DRECT);
ZH_D3D12_SAME_LAYOUT(D3DMATRIX);
ZH_D3D12_SAME_LAYOUT(D3DVIEWPORT9);
ZH_D3D12_SAME_LAYOUT(D3DMATERIAL9);
ZH_D3D12_SAME_LAYOUT(D3DLIGHT9);
ZH_D3D12_SAME_LAYOUT(D3DVERTEXELEMENT9);
ZH_D3D12_SAME_LAYOUT(D3DADAPTER_IDENTIFIER9);
#undef ZH_D3D12_SAME_LAYOUT
static_assert(sizeof(GUID) == sizeof(zhposix::D3D9PosixGuid), "GUID");

// A structure pointer from one side as the other's: the same bytes (checked above).
template <class To, class From> inline To * Same_Layout(From * from) { return reinterpret_cast<To *>(from); }
template <class To, class From> inline const To * Same_Layout(const From * from) { return reinterpret_cast<const To *>(from); }

extern "C" {

// PosixDirect3D9.cpp: Direct3DCreate9, a zhposix::IDirect3D9 or NULL.
void * ZH_PosixDirect3DCreate9(unsigned int sdk_version);

// D3D12ShimBridge.cpp: d3dx9posix's D3DX, bound once.  Interface pointers are the POSIX device's objects;
// results are D3D9's.  A shader's bytes come back malloc'd, for the caller to free.
long ZHP_D3DX_Assemble_Shader(const char * source, unsigned int source_length, void ** bytes, unsigned int * size);
long ZHP_D3DX_Create_Texture(void * device, unsigned int width, unsigned int height, unsigned int mip_levels,
	unsigned long usage, int format, int pool, void ** texture);
long ZHP_D3DX_Create_Cube_Texture(void * device, unsigned int edge_length, unsigned int mip_levels,
	unsigned long usage, int format, int pool, void ** texture);
long ZHP_D3DX_Create_Volume_Texture(void * device, unsigned int width, unsigned int height, unsigned int depth,
	unsigned int mip_levels, unsigned long usage, int format, int pool, void ** texture);
long ZHP_D3DX_Create_Texture_From_File(void * device, const char * file_name, unsigned int width,
	unsigned int height, unsigned int mip_levels, unsigned long usage, int format, int pool, unsigned long filter,
	unsigned long mip_filter, unsigned long colour_key, void * info, void * palette, void ** texture);
long ZHP_D3DX_Filter_Texture(void * texture, const void * palette, unsigned int source_level, unsigned long filter);
long ZHP_D3DX_Load_Surface_From_Surface(void * destination, const void * destination_palette,
	const RECT * destination_rect, void * source, const void * source_palette, const RECT * source_rect,
	unsigned long filter, unsigned long colour_key);
unsigned int ZHP_D3DX_FVF_Vertex_Size(unsigned long fvf);

// The two engine hooks the device reads off Windows too (Platform/EngineShaderName.h): a shader's registered
// name, keyed by the device's own interface pointer, and the D3D8 tokens a vertex declaration came from.
void ZHP_Name_Shader(const void * shader, const char * name);
void ZHP_Keep_D3D8_Declaration(void * declaration, const unsigned int * d3d8_tokens);

}

#endif // D3D12DEVICE_D3D12BRIDGE_H
