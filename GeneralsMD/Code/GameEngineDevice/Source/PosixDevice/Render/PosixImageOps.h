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

// FILE: PosixImageOps.h //////////////////////////////////////////////////////////////////////////
// Desc:   Copies, conversions, scaling and fills between in-memory images (decision 7, phase A2).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* What D3D9's device and D3DX do to surfaces on the CPU, over PosixImage:

	 - posixCopyImage: a rectangle of one image into a rectangle of another, converting the format and
		 scaling as asked.  A copy of the same format and size is a straight byte copy of whole blocks, so
		 compressed data is moved, never decoded.  Anything else goes through PosixPixelCodec, and fails
		 where it cannot write the destination's format (compressing to DXT).
	 - The filters are D3DX's names for what they do: NONE copies without scaling and leaves what the
		 source does not cover transparent black; POINT takes the nearest pixel; LINEAR interpolates
		 between the four nearest; BOX and TRIANGLE average the source pixels each destination pixel
		 covers (for the 2:1 reductions mip chains are made of, TRIANGLE and BOX agree).
	 - posixFillImage and posixFillDepth: ColorFill, and Clear's colour and depth. */

#pragma once

#ifndef POSIXIMAGEOPS_H
#define POSIXIMAGEOPS_H

#include "PosixPixelCodec.h"
#include "PosixResources9.h"

enum PosixFilter
{
	POSIX_FILTER_NONE,
	POSIX_FILTER_POINT,
	POSIX_FILTER_LINEAR,
	POSIX_FILTER_BOX,
};

/** A rectangle inside an image, in pixels; right and bottom exclusive. */
struct PosixRegion
{
	unsigned int left, top, right, bottom;
};

/** The whole of an image, or `rect` checked against it.  False if the rectangle is empty or outside. */
bool posixRegionOf( const PosixImage &image, const RenderRect *rect, PosixRegion *region );

/** Copies `sourceRegion` of `source` into `destRegion` of `dest`.  D3D_OK, or D3DERR_INVALIDCALL for
	* a format that cannot be read or written, or a region a block format cannot address. */
RenderResult posixCopyImage( PosixImage &dest, const PosixRegion &destRegion,
	const PosixImage &source, const PosixRegion &sourceRegion, PosixFilter filter );

/** Fills a region with one colour.  Fails for a format that cannot be written. */
RenderResult posixFillImage( PosixImage &dest, const PosixRegion &region, const PosixColor &color );

/** Fills a region of a depth surface: the depth and the stencil as `flags` (D3DCLEAR_ZBUFFER,
	* D3DCLEAR_STENCIL) ask, keeping the other where the format has both. */
RenderResult posixFillDepth( PosixImage &dest, const PosixRegion &region, RenderUInt32 flags, float z,
	RenderUInt32 stencil );

#endif // POSIXIMAGEOPS_H
