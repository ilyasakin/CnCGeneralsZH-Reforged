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

// d3dx9posix's D3DX under C names, for zh_d3d12.dll's COM adapter (D3D12Bridge.h declares them).
//
// This unit is on the POSIX device's side: <d3d9.h> is Platform/PosixD3D9's and ZH_D3D12_DEVICE is defined,
// so d3dx9runtime.h is the POSIX half and the D3DX pointers are d3dx9posix.cpp's, bound here once.  What
// crosses is untyped: interface pointers are the device's own objects, which the adapter wraps and unwraps.

#include <d3d9.h>
#include <stdlib.h>
#include <string.h>

#include "d3dx9runtime.h"
#include "Platform/EngineShaderName.h"

static bool bind_once()
{
	static const bool bound = Bind_D3DX9_Runtime();
	return bound;
}

extern "C" long ZHP_D3DX_Assemble_Shader(const char * source, unsigned int source_length, void ** bytes, unsigned int * size)
{
	*bytes = NULL;
	*size = 0;
	if (!bind_once())
		return D3DERR_NOTAVAILABLE;
	LPD3DXBUFFER shader = NULL;
	const RenderResult result = D3DXAssembleShader(source, source_length, NULL, NULL, 0, &shader, NULL);
	if (result != D3D_OK || shader == NULL)
		return result != D3D_OK ? result : D3DERR_INVALIDCALL;
	const unsigned int count = shader->GetBufferSize();
	void * copy = malloc(count != 0 ? count : 1);
	if (copy == NULL) {
		shader->Release();
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	memcpy(copy, shader->GetBufferPointer(), count);
	shader->Release();
	*bytes = copy;
	*size = count;
	return D3D_OK;
}

extern "C" long ZHP_D3DX_Create_Texture(void * device, unsigned int width, unsigned int height, unsigned int mip_levels,
	unsigned long usage, int format, int pool, void ** texture)
{
	*texture = NULL;
	if (!bind_once())
		return D3DERR_NOTAVAILABLE;
	LPDIRECT3DTEXTURE9 made = NULL;
	const RenderResult result = D3DXCreateTexture((LPDIRECT3DDEVICE9)device, width, height, mip_levels, usage,
		(D3DFORMAT)format, (D3DPOOL)pool, &made);
	*texture = made;
	return result;
}

extern "C" long ZHP_D3DX_Create_Cube_Texture(void * device, unsigned int edge_length, unsigned int mip_levels,
	unsigned long usage, int format, int pool, void ** texture)
{
	*texture = NULL;
	if (!bind_once())
		return D3DERR_NOTAVAILABLE;
	LPDIRECT3DCUBETEXTURE9 made = NULL;
	const RenderResult result = D3DXCreateCubeTexture((LPDIRECT3DDEVICE9)device, edge_length, mip_levels, usage,
		(D3DFORMAT)format, (D3DPOOL)pool, &made);
	*texture = made;
	return result;
}

extern "C" long ZHP_D3DX_Create_Volume_Texture(void * device, unsigned int width, unsigned int height, unsigned int depth,
	unsigned int mip_levels, unsigned long usage, int format, int pool, void ** texture)
{
	*texture = NULL;
	if (!bind_once())
		return D3DERR_NOTAVAILABLE;
	LPDIRECT3DVOLUMETEXTURE9 made = NULL;
	const RenderResult result = D3DXCreateVolumeTexture((LPDIRECT3DDEVICE9)device, width, height, depth, mip_levels,
		usage, (D3DFORMAT)format, (D3DPOOL)pool, &made);
	*texture = made;
	return result;
}

extern "C" long ZHP_D3DX_Create_Texture_From_File(void * device, const char * file_name, unsigned int width,
	unsigned int height, unsigned int mip_levels, unsigned long usage, int format, int pool, unsigned long filter,
	unsigned long mip_filter, unsigned long colour_key, void * info, void * palette, void ** texture)
{
	*texture = NULL;
	if (!bind_once())
		return D3DERR_NOTAVAILABLE;
	LPDIRECT3DTEXTURE9 made = NULL;
	const RenderResult result = D3DXCreateTextureFromFileExA((LPDIRECT3DDEVICE9)device, file_name, width, height,
		mip_levels, usage, (D3DFORMAT)format, (D3DPOOL)pool, filter, mip_filter, colour_key, (D3DXIMAGE_INFO *)info,
		palette, &made);
	*texture = made;
	return result;
}

extern "C" long ZHP_D3DX_Filter_Texture(void * texture, const void * palette, unsigned int source_level, unsigned long filter)
{
	if (!bind_once())
		return D3DERR_NOTAVAILABLE;
	return D3DXFilterTexture((LPDIRECT3DBASETEXTURE9)texture, palette, source_level, filter);
}

extern "C" long ZHP_D3DX_Load_Surface_From_Surface(void * destination, const void * destination_palette,
	const RECT * destination_rect, void * source, const void * source_palette, const RECT * source_rect,
	unsigned long filter, unsigned long colour_key)
{
	if (!bind_once())
		return D3DERR_NOTAVAILABLE;
	return D3DXLoadSurfaceFromSurface((LPDIRECT3DSURFACE9)destination, destination_palette, destination_rect,
		(LPDIRECT3DSURFACE9)source, source_palette, source_rect, filter, colour_key);
}

extern "C" unsigned int ZHP_D3DX_FVF_Vertex_Size(unsigned long fvf)
{
	return Get_FVF_Vertex_Size(fvf);
}

extern "C" void ZHP_Name_Shader(const void * shader, const char * name)
{
	PosixDevice_Name_Shader(shader, name);
}

extern "C" void ZHP_Keep_D3D8_Declaration(void * declaration, const unsigned int * d3d8_tokens)
{
	PosixDevice_Keep_D3D8_Declaration(declaration, d3d8_tokens);
}
