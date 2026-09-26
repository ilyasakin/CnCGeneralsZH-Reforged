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

// a contributor's (decision 7, phase A2): the caps, the Check* answers and the adapter identifier a headless
// init reads.  These are placeholders that fail and say so, left by a contributor with the skeleton so that the
// device links; A2 replaces every one.  See PosixDevice9.h.

#include "PosixDevice9.h"

#include <stdio.h>

static RenderResult not_yet(const char *what)
{
	fprintf(stderr, "PosixDirect3D9::%s: not answered until A2\n", what);
	return D3DERR_NOTAVAILABLE;
}

RenderResult PosixDirect3D9::GetAdapterIdentifier(unsigned int, RenderUInt32, D3DADAPTER_IDENTIFIER9 *)
{
	return not_yet("GetAdapterIdentifier");
}

RenderResult PosixDirect3D9::CheckDeviceType(unsigned int, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, int)
{
	return not_yet("CheckDeviceType");
}

RenderResult PosixDirect3D9::CheckDeviceFormat(unsigned int, D3DDEVTYPE, D3DFORMAT, RenderUInt32, D3DRESOURCETYPE, D3DFORMAT)
{
	return not_yet("CheckDeviceFormat");
}

RenderResult PosixDirect3D9::CheckDeviceMultiSampleType(unsigned int, D3DDEVTYPE, D3DFORMAT, int, D3DMULTISAMPLE_TYPE,
	RenderUInt32 *)
{
	return not_yet("CheckDeviceMultiSampleType");
}

RenderResult PosixDirect3D9::CheckDepthStencilMatch(unsigned int, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT)
{
	return not_yet("CheckDepthStencilMatch");
}

RenderResult PosixDirect3D9::GetDeviceCaps(unsigned int, D3DDEVTYPE, D3DCAPS9 *)
{
	return not_yet("GetDeviceCaps");
}

RenderResult PosixDevice9::GetDeviceCaps(D3DCAPS9 *caps)
{
	return Adapter->GetDeviceCaps(0, D3DDEVTYPE_HAL, caps);
}
