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

// Platform/MsvcFloatCasts.h against the bytes MSVC's cvttss2si-then-low-byte gives, worked by hand:
// truncation toward zero, the wrap modulo 256 both ways, and 0x80000000's low byte (0) for NaN and
// for values past the int range.  Then the particle case that crashed ARM64: an angle of -0.28 rad
// through W3DParticleSys's orientation expression is table index 245, inside the table's 256.

#include "Platform/MsvcFloatCasts.h"

#include <limits>
#include <stdio.h>

static int failures = 0;

#define CHECK_BYTE(value, expected) \
	do { const unsigned got_ = floatToByteAsMsvc(value); if (got_ != (unsigned)(expected)) { ++failures; \
		printf("FAIL %s:%d: floatToByteAsMsvc(%s) = %u, expected %u\n", __FILE__, __LINE__, #value, got_, (unsigned)(expected)); } } while (0)

int main()
{
	CHECK_BYTE(0.0f, 0);
	CHECK_BYTE(0.99f, 0);				// toward zero
	CHECK_BYTE(-0.99f, 0);				// toward zero from below, not -1
	CHECK_BYTE(255.7f, 255);
	CHECK_BYTE(256.0f, 0);				// the wrap
	CHECK_BYTE(300.2f, 44);
	CHECK_BYTE(-1.0f, 255);
	CHECK_BYTE(-11.3f, 245);
	CHECK_BYTE(-256.5f, 0);
	CHECK_BYTE(65535.9f, 255);
	CHECK_BYTE(2147483520.0f, 128);		// the largest float below 2^31: 0x7FFFFF80
	CHECK_BYTE(2147483648.0f, 0);		// out of range: 0x80000000
	CHECK_BYTE(-2147483648.0f, 0);		// INT_MIN exactly
	CHECK_BYTE(1.0e20f, 0);
	CHECK_BYTE(-1.0e20f, 0);
	CHECK_BYTE(std::numeric_limits<float>::quiet_NaN(), 0);
	CHECK_BYTE(std::numeric_limits<float>::infinity(), 0);

	// The crash: orientation -11, a particle between -0.2956 and -0.2710 rad; -0.28 rad here.
	const float PI_AS_BASETYPE = 3.14159265359f;
	const unsigned orientation = floatToByteAsMsvc(-0.28f * 255.0f / (2.0f * PI_AS_BASETYPE));
	if (orientation != 245) {
		++failures;
		printf("FAIL: the particle at -0.28 rad has orientation %u, expected 245\n", orientation);
	}

#if defined(_MSC_VER)
	// Where the claim can be measured: MSVC's own cast, for values in the int range (outside it the
	// cast is undefined even to MSVC's optimiser), against the helper.
	for (float value = -1000.0f; value <= 1000.0f; value += 0.37f) {
		const unsigned char raw = (unsigned char)value;
		if (raw != floatToByteAsMsvc(value)) {
			++failures;
			printf("FAIL: MSVC casts %f to %u, the helper says %u\n", value, (unsigned)raw, (unsigned)floatToByteAsMsvc(value));
			break;
		}
	}
#endif

	if (failures != 0) {
		printf("test_msvc_float_casts: %d FAILED\n", failures);
		return 1;
	}
	printf("test_msvc_float_casts: every conversion is MSVC's byte\n");
	return 0;
}
