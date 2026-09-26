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

// Float-to-unsigned conversions spelled out as the Windows build computes them.
//
// C++ leaves a float converted to an unsigned type undefined when the value does not fit, and this
// tree was written where that did not matter: MSVC for x86 and x64 lowers (unsigned char)f to
// cvttss2si, a truncation toward zero to a 32-bit int that answers 0x80000000 for anything out of
// int range or NaN, and then keeps the low byte.  So -11.3 became 245 and 300.2 became 44, and code
// came to depend on the wrap.  ARM64's clang lowers the same cast to fcvtzs or fcvtzu and, since the
// result "cannot" be out of range, uses it unmasked: a particle at about -0.28 rad (orientation -11)
// indexed its orientation table 190 GB past the end (docs/mac-port/README.md, the latent undefined
// behaviour list).
//
// Each function here is that MSVC result, computed with defined operations only, so every platform
// gets the Windows answer and Windows gets the answer it always had.  The MSVC lowering is the
// documented cvttss2si behaviour; it has not been measured on a Windows machine by this project
// (WINDOWS-DEBT.md has the row).  One spelling for the whole tree: a float-to-unsigned site that the
// ARM64 sweep finds uses these, not a local double cast.

#pragma once

#ifndef MSVCFLOATCASTS_H
#define MSVCFLOATCASTS_H

/// cvttss2si: toward zero into a 32-bit int, and INT_MIN (0x80000000) for NaN or anything outside
/// the int range.
inline int floatToIntAsMsvc(float value)
{
	return (value > -2147483648.0f && value < 2147483648.0f) ? (int)value : (int)(-2147483647 - 1);
}

/// (unsigned char)value as MSVC computes it: the low byte of floatToIntAsMsvc.
inline unsigned char floatToByteAsMsvc(float value)
{
	return (unsigned char)((unsigned int)floatToIntAsMsvc(value) & 0xFFu);
}

#endif // MSVCFLOATCASTS_H
