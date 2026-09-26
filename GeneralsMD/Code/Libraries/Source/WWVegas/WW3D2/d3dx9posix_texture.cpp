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

// a contributor's (decision 7, phase A2): D3DX's texture helpers off Windows, which d3dx9posix.cpp's
// Bind_D3DX9_Runtime points D3DXCreateTexture and the rest at (d3dx9posix.h names them).  Until A2
// they fail, and say so once each; with a null device they fail with D3DERR_INVALIDCALL and a null
// texture, which is what -nodevice's sized textures need (TerrainTex.cpp skips a null one).

#include "d3dx9runtime.h"
#include "d3dx9posix.h"

#include <stdio.h>

static void say_once(bool & said, const char * what)
{
	if (!said) {
		said = true;
		fprintf(stderr, "D3DX: %s is not available off Windows yet; the call fails\n", what);
	}
}

#define D3DX9POSIX_TEXTURE_FAILURE(name, device)						\
	static bool said = false;											\
	if ((device) == NULL) {												\
		return D3DERR_INVALIDCALL;										\
	}																	\
	say_once(said, name " (A2 gives the device its resources)");		\
	return D3DERR_NOTAVAILABLE

RenderResult D3DX9Posix_Create_Texture(LPDIRECT3DDEVICE9 device, unsigned int, unsigned int,
	unsigned int, RenderUInt32, D3DFORMAT, D3DPOOL, LPDIRECT3DTEXTURE9 * texture)
{
	*texture = NULL;
	D3DX9POSIX_TEXTURE_FAILURE("D3DXCreateTexture", device);
}

RenderResult D3DX9Posix_Create_Cube_Texture(LPDIRECT3DDEVICE9 device, unsigned int, unsigned int,
	RenderUInt32, D3DFORMAT, D3DPOOL, LPDIRECT3DCUBETEXTURE9 * texture)
{
	*texture = NULL;
	D3DX9POSIX_TEXTURE_FAILURE("D3DXCreateCubeTexture", device);
}

RenderResult D3DX9Posix_Create_Volume_Texture(LPDIRECT3DDEVICE9 device, unsigned int, unsigned int,
	unsigned int, unsigned int, RenderUInt32, D3DFORMAT, D3DPOOL, LPDIRECT3DVOLUMETEXTURE9 * texture)
{
	*texture = NULL;
	D3DX9POSIX_TEXTURE_FAILURE("D3DXCreateVolumeTexture", device);
}

RenderResult D3DX9Posix_Create_Texture_From_File(LPDIRECT3DDEVICE9 device, const char *,
	unsigned int, unsigned int, unsigned int, RenderUInt32, D3DFORMAT, D3DPOOL, RenderUInt32,
	RenderUInt32, D3DCOLOR, D3DXIMAGE_INFO *, void *, LPDIRECT3DTEXTURE9 * texture)
{
	*texture = NULL;
	D3DX9POSIX_TEXTURE_FAILURE("D3DXCreateTextureFromFileExA", device);
}

RenderResult D3DX9Posix_Filter_Texture(LPDIRECT3DBASETEXTURE9 texture, const void *, unsigned int,
	RenderUInt32)
{
	D3DX9POSIX_TEXTURE_FAILURE("D3DXFilterTexture", texture);
}

RenderResult D3DX9Posix_Load_Surface_From_Surface(LPDIRECT3DSURFACE9 destination, const void *,
	const RenderRect *, LPDIRECT3DSURFACE9 source, const void *, const RenderRect *, RenderUInt32,
	D3DCOLOR)
{
	if (source == NULL) {
		return D3DERR_INVALIDCALL;
	}
	D3DX9POSIX_TEXTURE_FAILURE("D3DXLoadSurfaceFromSurface", destination);
}
