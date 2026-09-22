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

// Float to long, rounding half to even, the same way on x64 and on arm64.
//
// This is dettrig.h's problem in one instruction, and it is here for the same reason: the
// simulation's arithmetic has to give the same answer on every machine in a network game, so the
// places where an architecture is free to choose get pinned to one choice by hand.
//
// What it replaces, and why it is not a cast.  EA wrote `fld` / `fistp`, which rounds in the FPU's
// current mode; the x64 port wrote that as SSE2's cvtss2si, which rounds in MXCSR's current mode.
// A C cast would truncate instead, and callers depend on the rounding - see the comment on
// WWMath::Float_To_Long and every WWMath::Float_To_Long(x - 0.499999f) in hrawanim.cpp, which is
// arithmetic built on top of round-to-nearest.
//
// Which mode "current" is.  GameLogic/FPUControl.cpp's setFPMode() sets _RC_NEAR and nothing else,
// and GameLogic::update re-asserts it at the top of every logic frame precisely because a graphics
// or audio driver may have left it somewhere else.  So the mode this conversion is designed to run
// in is round-to-nearest, ties to even.
//
// On arm64 that is FCVTNS - "convert to signed integer, rounding to nearest with ties to even" -
// which is what vcvtns_s32_f32 emits.  Two things follow, and the second is an improvement rather
// than a compromise:
//
//   1. It is bit-identical to cvtss2si under _RC_NEAR, which is the only mode the simulation is
//      ever supposed to be in.  test_detround.cpp checks the tie cases and sweeps the rest.
//   2. FCVTNS ignores FPCR's rounding field, so on arm64 this conversion cannot be perturbed by
//      anything that leaves the rounding mode somewhere else.  The whole class of bug that makes
//      setFPMode() get called every frame on Windows does not exist here for this operation.
//
// Out-of-range inputs are the one place the two architectures genuinely differ: cvtss2si yields
// the "integer indefinite" value 0x80000000 and FCVTNS saturates to 0x7FFFFFFF or 0x80000000.
// Nothing in the simulation converts an out-of-range float - the callers are frame numbers,
// scanline coordinates and pixel counts - so this is recorded rather than papered over.  If a
// caller ever could, it wants a range check of its own, not a different instruction here.

#pragma once

#ifndef DETROUND_H
#define DETROUND_H

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define DETROUND_ARM64 1
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define DETROUND_SSE2 1
#else
#error "DetRound has no implementation for this architecture; add one and extend test_detround.cpp."
#endif

namespace DetRound
{

	/// float -> long, rounding half to even.
	inline long To_Long(float f)
	{
#if defined(DETROUND_ARM64)
		return (long)vcvtns_s32_f32(f);
#else
		return (long)_mm_cvtss_si32(_mm_set_ss(f));
#endif
	}

	/// double -> long, rounding half to even.
	inline long To_Long(double f)
	{
#if defined(DETROUND_ARM64)
		// vcvtns_s32_f64 is not in the intrinsic set; the 64-bit form is, and narrowing afterwards
		// is exact for every value a long can hold, which is the whole of this function's domain.
		return (long)vcvtnd_s64_f64(f);
#else
		return (long)_mm_cvtsd_si32(_mm_set_sd(f));
#endif
	}

} // namespace DetRound

#endif // DETROUND_H
