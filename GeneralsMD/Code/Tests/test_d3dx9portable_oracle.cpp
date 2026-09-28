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
// d3dx9portable.cpp against d3dx9_43.dll itself (W-ARM64).  Windows on ARM64 has no d3dx9_43.dll and binds
// the port's own arithmetic in its place (d3dx9runtime.cpp); macOS and Linux use the same bodies as D3DX.
// Until this test they had been checked against independent answers only (test_d3dx9posix.cpp: "the DLL was
// not run").  This runs where the DLL binds, Windows x64, and compares every entry point the port replaces:
//
//   - the FVF vertex size, over every position type, the normal, point size, the two colours, every texture
//     set count and a spread of per-set formats, with the last-beta flags too: equal, every one;
//   - transpose, scaling and translation, which move numbers and compute none: bit for bit;
//   - multiply, rotation about z, Vec3Transform and the inverse, which compute: within 1e-5 of the operands'
//     scale, with how many came out bit-identical reported.  The DLL picks its bodies by CPU (dx9_smoke's
//     D3DXVec4Transform note), so bit-identity is not the claim; none of these reaches the simulation;
//   - an exactly singular matrix: both refuse it and leave the output alone.
//
// Where the DLL does not bind (ARM64, or a machine without the redistributable) there is nothing to compare
// against, and the test says so and exits 77, which CMake records as skipped rather than passed.

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "d3dx9portable.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static uint32_t random_state = 0x2545F491u;

static uint32_t next_random()
{
	// xorshift32: the same sequence on every run, so a failure names the same input again
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static float random_float(float range)
{
	return ((float)(next_random() & 0xFFFFFF) / (float)0xFFFFFF * 2.0f - 1.0f) * range;
}

static void random_matrix(D3DXMATRIX & m, float range)
{
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			m.m[row][column] = random_float(range);
		}
	}
}

static bool same_bits(const void * a, const void * b, size_t size)
{
	return memcmp(a, b, size) == 0;
}

struct Closeness
{
	const char * Name;
	unsigned Compared;
	unsigned Identical;
	double WorstRatio;		// the largest |port - dll| / scale seen
};

static void compare(Closeness & c, const float * port, const float * dll, const double * scale, int count)
{
	++c.Compared;
	if (same_bits(port, dll, sizeof(float) * count)) {
		++c.Identical;
		return;
	}
	for (int i = 0; i < count; ++i) {
		const double s = scale[i] > 1.0 ? scale[i] : 1.0;
		const double ratio = fabs((double)port[i] - (double)dll[i]) / s;
		if (ratio > c.WorstRatio) {
			c.WorstRatio = ratio;
		}
	}
}

static void report(const Closeness & c, double tolerance)
{
	printf("%-16s %6u compared, %6u bit-identical, worst difference %.3g of the operands' scale\n", c.Name,
		c.Compared, c.Identical, c.WorstRatio);
	if (c.WorstRatio > tolerance) {
		++failures;
		printf("FAIL: %s differs from d3dx9_43.dll by more than %.0e of the operands' scale\n", c.Name, tolerance);
	}
}

static void check_fvf_sizes()
{
	static const DWORD POSITIONS[] = { 0, D3DFVF_XYZ, D3DFVF_XYZRHW, D3DFVF_XYZW, D3DFVF_XYZB1, D3DFVF_XYZB2,
		D3DFVF_XYZB3, D3DFVF_XYZB4, D3DFVF_XYZB5 };
	static const DWORD LAST_BETA[] = { 0, D3DFVF_LASTBETA_UBYTE4, D3DFVF_LASTBETA_D3DCOLOR };
	unsigned compared = 0;
	for (size_t p = 0; p < sizeof(POSITIONS) / sizeof(POSITIONS[0]); ++p) {
		for (DWORD parts = 0; parts < 16; ++parts) {
			for (DWORD sets = 0; sets <= 8; ++sets) {
				for (int pattern = 0; pattern < 12; ++pattern) {
					DWORD fvf = POSITIONS[p] | (sets << D3DFVF_TEXCOUNT_SHIFT);
					if (parts & 1) fvf |= D3DFVF_NORMAL;
					if (parts & 2) fvf |= D3DFVF_PSIZE;
					if (parts & 4) fvf |= D3DFVF_DIFFUSE;
					if (parts & 8) fvf |= D3DFVF_SPECULAR;
					// patterns 0-3: every set the same format; 4-11: a random format per set
					for (DWORD set = 0; set < 8; ++set) {
						const DWORD format = pattern < 4 ? (DWORD)pattern : (next_random() & 3);
						fvf |= format << (16 + set * 2);
					}
					if (POSITIONS[p] >= D3DFVF_XYZB1 && POSITIONS[p] <= D3DFVF_XYZB5) {
						fvf |= LAST_BETA[pattern % 3];
					}
					const UINT dll = D3DXGetFVFVertexSize(fvf);
					const unsigned port = D3DXPortable_FVF_Vertex_Size(fvf);
					++compared;
					if (dll != port) {
						++failures;
						if (failures < 20) {
							printf("FAIL: FVF 0x%08lx: the DLL says %u bytes, the port %u\n", (unsigned long)fvf, dll, port);
						}
					}
				}
			}
		}
	}
	printf("FVF sizes        %6u compared against the DLL\n", compared);
}

static void check_exact()
{
	unsigned compared = 0;
	for (int round = 0; round < 2000; ++round) {
		D3DXMATRIX m, port, dll;
		random_matrix(m, 1000.0f);
		D3DXMatrixTranspose(&dll, &m);
		D3DXPortable_Matrix_Transpose(&port, &m);
		if (!same_bits(&port, &dll, sizeof(port))) { ++failures; printf("FAIL: transpose, round %d\n", round); }

		const float x = random_float(1000.0f), y = random_float(1000.0f), z = random_float(1000.0f);
		D3DXMatrixScaling(&dll, x, y, z);
		D3DXPortable_Matrix_Scaling(&port, x, y, z);
		if (!same_bits(&port, &dll, sizeof(port))) { ++failures; printf("FAIL: scaling, round %d\n", round); }

		D3DXMatrixTranslation(&dll, x, y, z);
		D3DXPortable_Matrix_Translation(&port, x, y, z);
		if (!same_bits(&port, &dll, sizeof(port))) { ++failures; printf("FAIL: translation, round %d\n", round); }
		compared += 3;
	}
	printf("moves            %6u compared, all bit-identical unless a FAIL says otherwise\n", compared);
}

static void check_computed()
{
	Closeness multiply = { "multiply", 0, 0, 0.0 };
	Closeness rotation = { "rotation z", 0, 0, 0.0 };
	Closeness vec3 = { "Vec3Transform", 0, 0, 0.0 };
	Closeness inverse = { "inverse", 0, 0, 0.0 };

	for (int round = 0; round < 20000; ++round) {
		D3DXMATRIX a, b, port, dll;
		random_matrix(a, 100.0f);
		random_matrix(b, 100.0f);

		D3DXMatrixMultiply(&dll, &a, &b);
		D3DXPortable_Matrix_Multiply(&port, &a, &b);
		double scale[16];
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				double s = 0.0;
				for (int k = 0; k < 4; ++k) {
					s += fabs((double)a.m[row][k] * (double)b.m[k][column]);
				}
				scale[row * 4 + column] = s;
			}
		}
		compare(multiply, &port._11, &dll._11, scale, 16);

		const float angle = random_float(20.0f);
		D3DXMatrixRotationZ(&dll, angle);
		D3DXPortable_Matrix_Rotation_Z(&port, angle);
		double unit[16];
		for (int i = 0; i < 16; ++i) unit[i] = 1.0;
		compare(rotation, &port._11, &dll._11, unit, 16);

		D3DXVECTOR3 v;
		v.x = random_float(100.0f);
		v.y = random_float(100.0f);
		v.z = random_float(100.0f);
		D3DXVECTOR4 vp, vd;
		D3DXVec3Transform(&vd, &v, &a);
		D3DXPortable_Vec3_Transform(&vp, &v, &a);
		double vscale[4];
		for (int column = 0; column < 4; ++column) {
			vscale[column] = fabs(v.x * a.m[0][column]) + fabs(v.y * a.m[1][column]) + fabs(v.z * a.m[2][column])
				+ fabs(a.m[3][column]);
		}
		compare(vec3, &vp.x, &vd.x, vscale, 4);

		// A well-conditioned matrix to invert: a rotation and scale with a translation, as the renderer's
		// view matrices are.  Its inverse's entries are all of order one.
		D3DXMATRIX r, s, t, m;
		D3DXPortable_Matrix_Rotation_Z(&r, random_float(3.0f));
		D3DXPortable_Matrix_Scaling(&s, 0.5f + random_float(0.4f) + 1.0f, 1.5f, 1.25f);
		D3DXPortable_Matrix_Translation(&t, random_float(500.0f), random_float(500.0f), random_float(500.0f));
		D3DXPortable_Matrix_Multiply(&m, &r, &s);
		D3DXPortable_Matrix_Multiply(&m, &m, &t);
		float det_dll = 0.0f, det_port = 0.0f;
		D3DXMatrixInverse(&dll, &det_dll, &m);
		D3DXPortable_Matrix_Inverse(&port, &det_port, &m);
		double iscale[16];
		for (int i = 0; i < 16; ++i) iscale[i] = 1000.0;	// the translation row carries the 500s
		compare(inverse, &port._11, &dll._11, iscale, 16);
	}

	report(multiply, 1e-5);
	report(rotation, 1e-5);
	report(vec3, 1e-5);
	report(inverse, 1e-5);
}

static void check_singular()
{
	D3DXMATRIX singular;
	memset(&singular, 0, sizeof(singular));
	singular._11 = 1.0f;
	singular._22 = 1.0f;
	singular._33 = 1.0f;	// _44 is zero: rank three

	D3DXMATRIX port, dll;
	memset(&port, 0x5A, sizeof(port));
	memset(&dll, 0x5A, sizeof(dll));
	float det = 0.0f;
	D3DXMatrixInverse(&dll, &det, &singular);
	const D3DXMATRIX * result = D3DXPortable_Matrix_Inverse(&port, &det, &singular);
	if (result != NULL) { ++failures; printf("FAIL: the port inverted a singular matrix\n"); }
	if (!same_bits(&port, &dll, sizeof(port))) {
		++failures;
		printf("FAIL: a singular matrix left different outputs behind (the DLL's and the port's)\n");
	}
}

int main()
{
	if (!Bind_D3DX9_Runtime()) {
		printf("SKIP: d3dx9_43.dll did not bind here (ARM64, or no DirectX redistributable): nothing to compare against\n");
		return 77;
	}

	check_fvf_sizes();
	check_exact();
	check_computed();
	check_singular();

	if (failures != 0) {
		printf("test_d3dx9portable_oracle: %d FAILED\n", failures);
		return 1;
	}
	printf("test_d3dx9portable_oracle: the port agrees with d3dx9_43.dll\n");
	return 0;
}
