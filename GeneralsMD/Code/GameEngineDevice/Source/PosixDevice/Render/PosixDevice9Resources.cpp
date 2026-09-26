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

// -18's (decision 7, phase A2): the device's resources and surfaces, Clear, and the implicit back buffer
// and depth surface.  These are placeholders that fail and say so, left by -a9 with the skeleton so
// that the device links; A2 replaces every one.  See PosixDevice9.h.

#include "PosixDevice9.h"

#include <stdio.h>

static RenderResult not_yet(const char *what)
{
	fprintf(stderr, "PosixDevice9::%s: not available until the device has resources (A2)\n", what);
	return D3DERR_NOTAVAILABLE;
}

template <class Object>
static void none(Object **out)
{
	if (out != NULL) {
		*out = NULL;
	}
}

RenderResult PosixDevice9::Create_Implicit_Surfaces()
{
	return not_yet("Create_Implicit_Surfaces");
}

RenderResult PosixDevice9::CreateTexture(unsigned int, unsigned int, unsigned int, RenderUInt32, D3DFORMAT, D3DPOOL,
	IDirect3DTexture9 **texture, void **)
{
	none(texture);
	return not_yet("CreateTexture");
}

RenderResult PosixDevice9::CreateVolumeTexture(unsigned int, unsigned int, unsigned int, unsigned int, RenderUInt32,
	D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9 **texture, void **)
{
	none(texture);
	return not_yet("CreateVolumeTexture");
}

RenderResult PosixDevice9::CreateCubeTexture(unsigned int, unsigned int, RenderUInt32, D3DFORMAT, D3DPOOL,
	IDirect3DCubeTexture9 **texture, void **)
{
	none(texture);
	return not_yet("CreateCubeTexture");
}

RenderResult PosixDevice9::CreateVertexBuffer(unsigned int, RenderUInt32, RenderUInt32, D3DPOOL,
	IDirect3DVertexBuffer9 **buffer, void **)
{
	none(buffer);
	return not_yet("CreateVertexBuffer");
}

RenderResult PosixDevice9::CreateIndexBuffer(unsigned int, RenderUInt32, D3DFORMAT, D3DPOOL,
	IDirect3DIndexBuffer9 **buffer, void **)
{
	none(buffer);
	return not_yet("CreateIndexBuffer");
}

RenderResult PosixDevice9::CreateRenderTarget(unsigned int, unsigned int, D3DFORMAT, D3DMULTISAMPLE_TYPE, RenderUInt32,
	int, IDirect3DSurface9 **surface, void **)
{
	none(surface);
	return not_yet("CreateRenderTarget");
}

RenderResult PosixDevice9::CreateDepthStencilSurface(unsigned int, unsigned int, D3DFORMAT, D3DMULTISAMPLE_TYPE,
	RenderUInt32, int, IDirect3DSurface9 **surface, void **)
{
	none(surface);
	return not_yet("CreateDepthStencilSurface");
}

RenderResult PosixDevice9::UpdateSurface(IDirect3DSurface9 *, const RenderRect *, IDirect3DSurface9 *, const RenderPoint *)
{
	return not_yet("UpdateSurface");
}

RenderResult PosixDevice9::UpdateTexture(IDirect3DBaseTexture9 *, IDirect3DBaseTexture9 *)
{
	return not_yet("UpdateTexture");
}

RenderResult PosixDevice9::GetRenderTargetData(IDirect3DSurface9 *, IDirect3DSurface9 *)
{
	return not_yet("GetRenderTargetData");
}

RenderResult PosixDevice9::GetFrontBufferData(unsigned int, IDirect3DSurface9 *)
{
	return not_yet("GetFrontBufferData");
}

RenderResult PosixDevice9::StretchRect(IDirect3DSurface9 *, const RenderRect *, IDirect3DSurface9 *, const RenderRect *,
	D3DTEXTUREFILTERTYPE)
{
	return not_yet("StretchRect");
}

RenderResult PosixDevice9::CreateOffscreenPlainSurface(unsigned int, unsigned int, D3DFORMAT, D3DPOOL,
	IDirect3DSurface9 **surface, void **)
{
	none(surface);
	return not_yet("CreateOffscreenPlainSurface");
}

RenderResult PosixDevice9::SetRenderTarget(RenderUInt32, IDirect3DSurface9 *)
{
	return not_yet("SetRenderTarget");
}

RenderResult PosixDevice9::GetRenderTarget(RenderUInt32, IDirect3DSurface9 **surface)
{
	none(surface);
	return not_yet("GetRenderTarget");
}

RenderResult PosixDevice9::SetDepthStencilSurface(IDirect3DSurface9 *)
{
	return not_yet("SetDepthStencilSurface");
}

RenderResult PosixDevice9::GetDepthStencilSurface(IDirect3DSurface9 **surface)
{
	none(surface);
	return not_yet("GetDepthStencilSurface");
}

RenderResult PosixDevice9::GetBackBuffer(unsigned int, unsigned int, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **surface)
{
	none(surface);
	return not_yet("GetBackBuffer");
}

RenderResult PosixDevice9::Clear(RenderUInt32, const D3DRECT *, RenderUInt32, D3DCOLOR, float, RenderUInt32)
{
	return not_yet("Clear");
}
