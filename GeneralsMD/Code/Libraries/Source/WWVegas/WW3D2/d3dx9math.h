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

// The D3DX vector and matrix types for the native Direct3D 9 renderer.
//
// d3dx8math.h cannot come along, because it includes d3dx8.h, which includes d3d8.h,
// and the whole point of the phase is that d3d8.h is gone.  The types themselves owe
// nothing to either header: they are floats with constructors.  So they are declared
// here, laid out exactly as the D3DX ones are, and the eight functions the engine calls
// that are not inline come out of d3dx9_43.dll beside the texture and shader ones.
//
// Binding rather than reimplementing is deliberate.  D3DXVec4Transform and D3DXVec4Dot
// reach GameLogic through BezierSegment, which DumbProjectileBehavior steers a shell
// with, so their arithmetic is part of the network and replay CRC.  A hand-written 4x4
// inverse or transform would be a rounding difference nobody could see until a replay
// diverged.  D3DXVec4Dot and D3DXMatrixIdentity are the two the DirectX SDK inlined, so
// those two are written out here, matching d3dx8math.inl term for term.
//
// That holds on Windows, where there is a DLL to bind.  Elsewhere there is none, so the
// non-Windows branch at the bottom of this file declares the four names GameEngine reaches and
// nothing else: D3DXVECTOR4, D3DXMATRIX, D3DXVec4Transform and D3DXVec4Dot.  They go to
// d3dxportable.h, which copies the DLL's term order from Microsoft's machine code, and which
// explains why "the DLL's term order" turned out to be two orders chosen by CPU vendor.  That
// branch includes no d3d9.h, so GameLogic's path through BezierSegment.h stops pulling in the
// Direct3D header on macOS.  The renderer's names are absent on purpose: nothing outside WW3D2
// and GameEngineDevice uses them, neither compiles on macOS, and the Metal backend will decide
// what they become.

#ifndef D3DX9MATH_H
#define D3DX9MATH_H

#if defined(_WIN32)

#include <d3d9.h>

#define D3DX_PI	((FLOAT)3.141592654f)

struct D3DXVECTOR3 : public D3DVECTOR
{
public:
	D3DXVECTOR3() {}
	D3DXVECTOR3(FLOAT x_value, FLOAT y_value, FLOAT z_value)
	{
		x = x_value;
		y = y_value;
		z = z_value;
	}

	operator FLOAT * () { return &x; }
	operator const FLOAT * () const { return &x; }
};

struct D3DXVECTOR4
{
public:
	D3DXVECTOR4() {}
	D3DXVECTOR4(FLOAT x_value, FLOAT y_value, FLOAT z_value, FLOAT w_value)
		: x(x_value), y(y_value), z(z_value), w(w_value) {}

	operator FLOAT * () { return &x; }
	operator const FLOAT * () const { return &x; }

	FLOAT x;
	FLOAT y;
	FLOAT z;
	FLOAT w;
};

struct D3DXMATRIX : public D3DMATRIX
{
public:
	D3DXMATRIX() {}
	D3DXMATRIX(FLOAT m11, FLOAT m12, FLOAT m13, FLOAT m14,
	           FLOAT m21, FLOAT m22, FLOAT m23, FLOAT m24,
	           FLOAT m31, FLOAT m32, FLOAT m33, FLOAT m34,
	           FLOAT m41, FLOAT m42, FLOAT m43, FLOAT m44)
	{
		_11 = m11; _12 = m12; _13 = m13; _14 = m14;
		_21 = m21; _22 = m22; _23 = m23; _24 = m24;
		_31 = m31; _32 = m32; _33 = m33; _34 = m34;
		_41 = m41; _42 = m42; _43 = m43; _44 = m44;
	}

	operator FLOAT * () { return &_11; }
	operator const FLOAT * () const { return &_11; }

	FLOAT & operator()(UINT row, UINT column) { return m[row][column]; }
	FLOAT operator()(UINT row, UINT column) const { return m[row][column]; }

	// Defined below, once D3DXMatrixMultiply has been declared.
	D3DXMATRIX operator*(const D3DXMATRIX & right) const;
	D3DXMATRIX & operator*=(const D3DXMATRIX & right);
};

typedef HRESULT (WINAPI * D3DXMatrixInverseFunction)(D3DXMATRIX * out, FLOAT * determinant,
	const D3DXMATRIX * matrix);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixBinaryFunction)(D3DXMATRIX * out,
	const D3DXMATRIX * left, const D3DXMATRIX * right);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixUnaryFunction)(D3DXMATRIX * out,
	const D3DXMATRIX * matrix);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixTripleFunction)(D3DXMATRIX * out,
	FLOAT first, FLOAT second, FLOAT third);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixAngleFunction)(D3DXMATRIX * out, FLOAT angle);

typedef D3DXVECTOR4 * (WINAPI * D3DXVec4TransformFunction)(D3DXVECTOR4 * out,
	const D3DXVECTOR4 * vector, const D3DXMATRIX * matrix);

typedef D3DXVECTOR4 * (WINAPI * D3DXVec3TransformFunction)(D3DXVECTOR4 * out,
	const D3DXVECTOR3 * vector, const D3DXMATRIX * matrix);

// D3DXMatrixInverse returns null when the matrix is singular, so its result is not
// interchangeable with the others and it keeps its own signature.
extern D3DXMatrixInverseFunction	D3DXMatrixInverse;
extern D3DXMatrixBinaryFunction		D3DXMatrixMultiply;
extern D3DXMatrixUnaryFunction		D3DXMatrixTranspose;
extern D3DXMatrixTripleFunction		D3DXMatrixScaling;
extern D3DXMatrixTripleFunction		D3DXMatrixTranslation;
extern D3DXMatrixAngleFunction		D3DXMatrixRotationZ;
extern D3DXVec4TransformFunction	D3DXVec4Transform;
extern D3DXVec3TransformFunction	D3DXVec3Transform;

// These eight are bound by Bind_D3DX9_Runtime in d3dx9runtime.h, along with the texture
// and shader entry points: one place decides whether D3DX9 is present, and one answer
// covers all of it.  Every pointer is null until it succeeds.

// The D3DX matrix product, which is D3DXMatrixMultiply and nothing else: writing the
// sixty-four multiplies out here instead would put a second, differently rounded matrix
// product in the same renderer.
inline D3DXMATRIX D3DXMATRIX::operator*(const D3DXMATRIX & right) const
{
	D3DXMATRIX product;
	D3DXMatrixMultiply(&product, this, &right);
	return product;
}

inline D3DXMATRIX & D3DXMATRIX::operator*=(const D3DXMATRIX & right)
{
	D3DXMatrixMultiply(this, this, &right);
	return *this;
}

// The two the DirectX SDK inlined, copied from d3dx8math.inl so the arithmetic that
// reaches a replay CRC is the arithmetic that always reached it.
inline FLOAT D3DXVec4Dot(const D3DXVECTOR4 * left, const D3DXVECTOR4 * right)
{
	return left->x * right->x + left->y * right->y + left->z * right->z + left->w * right->w;
}

inline D3DXMATRIX * D3DXMatrixIdentity(D3DXMATRIX * out)
{
	out->m[0][1] = out->m[0][2] = out->m[0][3] =
	out->m[1][0] = out->m[1][2] = out->m[1][3] =
	out->m[2][0] = out->m[2][1] = out->m[2][3] =
	out->m[3][0] = out->m[3][1] = out->m[3][2] = 0.0f;

	out->m[0][0] = out->m[1][1] = out->m[2][2] = out->m[3][3] = 1.0f;
	return out;
}

#else // !_WIN32

#include "d3dxportable.h"

#include <string.h>

// Laid out exactly as the D3DX types are, so that a struct holding one of these has the same
// shape on both platforms.
struct D3DXVECTOR4
{
public:
	D3DXVECTOR4() {}
	D3DXVECTOR4(float x_value, float y_value, float z_value, float w_value)
		: x(x_value), y(y_value), z(z_value), w(w_value) {}

	operator float * () { return &x; }
	operator const float * () const { return &x; }

	float x;
	float y;
	float z;
	float w;
};

struct D3DXMATRIX
{
public:
	D3DXMATRIX() {}
	D3DXMATRIX(float m11, float m12, float m13, float m14,
	           float m21, float m22, float m23, float m24,
	           float m31, float m32, float m33, float m34,
	           float m41, float m42, float m43, float m44)
	{
		_11 = m11; _12 = m12; _13 = m13; _14 = m14;
		_21 = m21; _22 = m22; _23 = m23; _24 = m24;
		_31 = m31; _32 = m32; _33 = m33; _34 = m34;
		_41 = m41; _42 = m42; _43 = m43; _44 = m44;
	}

	operator float * () { return &_11; }
	operator const float * () const { return &_11; }

	float & operator()(unsigned int row, unsigned int column) { return m[row][column]; }
	float operator()(unsigned int row, unsigned int column) const { return m[row][column]; }

	// D3DMATRIX's own layout: the named elements and the array alias each other.
	union {
		struct {
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float m[4][4];
	};
};

static_assert(sizeof(D3DXVECTOR4) == 16, "D3DXVECTOR4 is four floats");
static_assert(sizeof(D3DXMATRIX) == 64, "D3DXMATRIX is sixteen floats");

// Plain functions where Windows has function pointers, so call sites read the same.  The values
// are copied through arrays so that the arithmetic in d3dxportable.h indexes real float[4] and
// float[16] objects.  Copying a float is exact.
inline D3DXVECTOR4 * D3DXVec4Transform(D3DXVECTOR4 * out, const D3DXVECTOR4 * vector,
	const D3DXMATRIX * matrix)
{
	const float in[4] = { vector->x, vector->y, vector->z, vector->w };
	float rows[16];
	memcpy(rows, matrix->m, sizeof(rows));
	float result[4];
	D3DXPortable::Vec4Transform(result, in, rows);
	out->x = result[0];
	out->y = result[1];
	out->z = result[2];
	out->w = result[3];
	return out;
}

inline float D3DXVec4Dot(const D3DXVECTOR4 * left, const D3DXVECTOR4 * right)
{
	const float l[4] = { left->x, left->y, left->z, left->w };
	const float r[4] = { right->x, right->y, right->z, right->w };
	return D3DXPortable::Vec4Dot(l, r);
}

#endif // _WIN32

#endif // D3DX9MATH_H
