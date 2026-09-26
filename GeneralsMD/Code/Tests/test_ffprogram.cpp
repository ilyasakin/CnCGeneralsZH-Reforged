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

// FFReference's programmable stages (A3e, Tests/ffreference/ffprogram.h): the decoder's census gate, and,
// with ZH_DATA_DIR, every program the game ships decoding with nothing refused.

#include "test_harness.h"
#include "ffreference/ffprogram.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace FFRef;
using namespace FFRef::Token;

namespace {

// ---- token builders, from the parameter-token layouts ---------------------------------------------
uint32_t ins( uint32_t op, bool coissue = false ) { return op | (coissue ? COISSUE : 0u); }
uint32_t dst( uint32_t type, int index, unsigned mask = 0xF, int shift = 0 )
{
	return PARAMETER | (type << REGTYPE_SHIFT) | (uint32_t)index | (mask << WRITEMASK_SHIFT)
		| ((uint32_t)(shift & 0xF) << RESULTSHIFT_SHIFT);
}
uint32_t src( uint32_t type, int index, uint32_t swizzle = 0xE4, uint32_t modifier = SRC_NONE, bool relative = false )
{
	return PARAMETER | (type << REGTYPE_SHIFT) | (uint32_t)index | (swizzle << SWIZZLE_SHIFT)
		| (modifier << SRCMOD_SHIFT) | (relative ? ADDRESS_RELATIVE : 0u);
}
const uint32_t PS11 = VERSION_PIXEL | 0x0101, VS11 = VERSION_VERTEX | 0x0101;

bool decodes( const std::vector<uint32_t> &t, Program &p ) { return decodeProgram( &t[0], t.size(), p ); }

bool refused( const std::vector<uint32_t> &t, const char *expect )
{
	Program p;
	const bool ok = decodeProgram( &t[0], t.size(), p );
	bool named = false;
	for (size_t i = 0; i < p.refusals.size(); ++i)
		named = named || p.refusals[i].find( expect ) != std::string::npos;
	if (ok || !named)
		printf( "  expected a refusal naming \"%s\"; got %s\n", expect, p.refusals.empty() ? "none" : p.refusals[0].c_str() );
	return !ok && named;
}

unsigned be32( const unsigned char *p ) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

/// A member of a BIG archive, by its lower-case name, or empty
std::string readBigEntry( const std::string &path, const std::string &entryName )
{
	std::string data;
	FILE *file = fopen( path.c_str(), "rb" );
	if (file == NULL)
		return data;
	unsigned char header[ 16 ];
	if (fread( header, 1, 16, file ) == 16 && memcmp( header, "BIGF", 4 ) == 0)
	{
		const unsigned count = be32( header + 8 );
		for (unsigned i = 0; i < count; ++i)
		{
			unsigned char pair[ 8 ];
			if (fread( pair, 1, 8, file ) != 8)
				break;
			std::string name;
			int c;
			while ((c = fgetc( file )) > 0)
				name += (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
			if (name == entryName)
			{
				data.resize( be32( pair + 4 ) );
				if (fseek( file, (long)be32( pair ), SEEK_SET ) != 0 || fread( &data[0], 1, data.size(), file ) != data.size())
					data.clear();
				break;
			}
		}
	}
	fclose( file );
	return data;
}

}	// namespace

TEST(ffprogram_decodes_the_census_forms)
{
	// ps.1.1: tex t0; mul_x2 r0.rgb, v0, t0; +mov r0.a, t0.a; lrp r0, v0.a, 1-t0, c1
	const std::vector<uint32_t> ps = { PS11,
		ins( OP_TEX ), dst( REG_TEXTURE, 0 ),
		ins( OP_MUL ), dst( REG_TEMP, 0, 0x7, 1 ), src( REG_INPUT, 0 ), src( REG_TEXTURE, 0 ),
		ins( OP_MOV, true ), dst( REG_TEMP, 0, 0x8 ), src( REG_TEXTURE, 0, 0xFF ),
		ins( OP_LRP ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0, 0xFF ), src( REG_TEXTURE, 0, 0xE4, SRC_COMPLEMENT ), src( REG_CONST, 1 ),
		END };
	Program p;
	CHECK( decodes( ps, p ) );
	CHECK_EQ( p.kind, PROGRAM_PIXEL );
	CHECK_EQ( p.code.size(), (size_t)4 );
	CHECK_EQ( p.code[1].shift, 1 );
	CHECK_EQ( p.code[1].dst.mask, 0x7u );
	CHECK( p.code[2].coissue );
	CHECK_EQ( p.code[2].src[0].swizzle[0], 3 );
	CHECK_EQ( p.code[3].src[1].modifier, (uint32_t)SRC_COMPLEMENT );

	// def c1, .5, .25, 0, 1 - the four floats as they are stored
	const float values[ 4 ] = { 0.5f, 0.25f, 0.0f, 1.0f };
	uint32_t raw[ 4 ];
	memcpy( raw, values, sizeof( raw ) );
	const std::vector<uint32_t> def = { PS11, ins( OP_DEF ), dst( REG_CONST, 1 ), raw[0], raw[1], raw[2], raw[3],
		ins( OP_TEX ), dst( REG_TEXTURE, 0 ), ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_TEXTURE, 0 ), END };
	CHECK( decodes( def, p ) );
	CHECK_NEAR( p.code[0].value[1], 0.25, 0.0 );

	// vs.1.1: mov a0.x, v1; mad r1, r2.zzzw, c8[a0.x], -v0; m4x4 oPos, r1, c4; mov oT1, r1; rcp r1.w, r0.w
	const std::vector<uint32_t> vs = { VS11,
		ins( OP_MOV ), dst( REG_TEMP, 2 ), src( REG_INPUT, 0 ),
		ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0 ),
		ins( OP_MOV ), dst( REG_ADDR, 0, 0x1 ), src( REG_INPUT, 1 ),
		ins( OP_MAD ), dst( REG_TEMP, 1 ), src( REG_TEMP, 2, 0xEA ), src( REG_CONST, 8, 0xE4, SRC_NONE, true ), src( REG_INPUT, 0, 0xE4, SRC_NEGATE ),
		ins( OP_M4x4 ), dst( REG_RASTOUT, RASTOUT_POSITION ), src( REG_TEMP, 1 ), src( REG_CONST, 4 ),
		ins( OP_MOV ), dst( REG_TEXCRDOUT, 1 ), src( REG_TEMP, 1 ),
		ins( OP_RCP ), dst( REG_TEMP, 1, 0x8 ), src( REG_TEMP, 0, 0xFF ),
		END };
	CHECK( decodes( vs, p ) );
	CHECK_EQ( p.kind, PROGRAM_VERTEX );
	CHECK( p.code[3].src[1].relative );
	CHECK_EQ( p.code[3].src[0].swizzle[0], 2 );
	CHECK_EQ( p.code[3].src[2].modifier, (uint32_t)SRC_NEGATE );
}

TEST(ffprogram_refuses_what_the_census_does_not_hold)
{
	const uint32_t PS14 = VERSION_PIXEL | 0x0104;
	CHECK( refused( { PS14, ins( OP_TEX ), dst( REG_TEMP, 0 ), src( REG_TEXTURE, 0 ), END }, "version other than 1_1" ) );
	CHECK( refused( { PS11, ins( OP_CND ), dst( REG_TEMP, 0 ), src( REG_TEMP, 0 ), src( REG_TEMP, 1 ), src( REG_TEXTURE, 0 ), END }, "not in the census" ) );
	CHECK( refused( { PS11, ins( OP_TEX ), dst( REG_TEXTURE, 0 ), ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_TEXTURE, 0, 0xE4, SRC_SIGN ), END }, "modifier other than complement" ) );
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0, 0x3 ), src( REG_INPUT, 0 ), END }, "write mask" ) );
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0, 0x1B ), END }, "swizzle" ) );
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0, 0xF, 2 ), src( REG_INPUT, 0 ), END }, "result shift" ) );
	CHECK( refused( { VS11, ins( OP_MOV ), dst( REG_RASTOUT, RASTOUT_FOG, 0x1 ), src( REG_INPUT, 0 ), END }, "oFog" ) );
	CHECK( refused( { VS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0, 0xE4, SRC_COMPLEMENT ), END }, "other than negate" ) );
	CHECK( refused( { VS11, ins( OP_MOV, true ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0 ), END }, "co-issue" ) );
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0 ) }, "no end token" ) );
	// the pages' own rules
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_TEMP, 1 ), END }, "no earlier instruction wrote" ) );
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_TEXTURE, 0 ), END }, "no earlier instruction wrote" ) );
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0, 0x7 ), src( REG_INPUT, 0 ), ins( OP_MOV ), dst( REG_TEMP, 1 ), src( REG_TEMP, 0 ), END },
		"no earlier instruction wrote" ) );		// r0.a was never written
	CHECK( refused( { PS11, ins( OP_TEX ), dst( REG_TEXTURE, 0 ), ins( OP_TEXBEM ), dst( REG_TEXTURE, 1 ), src( REG_TEXTURE, 0 ),
		ins( OP_ADD ), dst( REG_TEMP, 0 ), src( REG_TEXTURE, 1 ), src( REG_TEXTURE, 0 ), END }, "a texbem has read" ) );
	CHECK( refused( { PS11, ins( OP_TEX ), dst( REG_TEXTURE, 1 ), ins( OP_TEXBEM ), dst( REG_TEXTURE, 0 ), src( REG_TEXTURE, 1 ), END }, "m > n" ) );
	CHECK( refused( { PS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0 ),		// the rgb half reads r0.a, which its partner writes
		ins( OP_MUL ), dst( REG_TEMP, 1, 0x7 ), src( REG_TEMP, 0, 0xFF ), src( REG_INPUT, 0 ), ins( OP_MOV, true ), dst( REG_TEMP, 0, 0x8 ), src( REG_INPUT, 0 ), END },
		"partner writes" ) );
	CHECK( refused( { VS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0 ), ins( OP_RCP ), dst( REG_TEMP, 1 ), src( REG_TEMP, 0 ), END },
		"replicate swizzle" ) );
	CHECK( refused( { VS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_CONST, 0, 0xE4, SRC_NONE, true ), END }, "before a0 is loaded" ) );
	CHECK( refused( { VS11, ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0 ), ins( OP_M4x4 ), dst( REG_TEMP, 0 ), src( REG_TEMP, 0 ), src( REG_CONST, 0 ), END },
		"dest is its src0" ) );
}

TEST(ffprogram_decodes_every_shipped_program)
{
	const char *dir = getenv( "ZH_DATA_DIR" );
	if (dir == NULL || *dir == 0)
	{
		printf( "  skip: no game data (ZH_DATA_DIR) to read the shipped programs from\n" );
		return;
	}
	// The census, 2026-09-26: each program with the archive that holds it and its instruction count
	struct Shipped { const char *archive; const char *name; size_t instructions; };
	const Shipped shipped[] = {
		{ "zerohour/ShadersZH.big", "shaders\\trees.pso", 5 }, { "zerohour/ShadersZH.big", "shaders\\trees.vso", 11 },
		{ "zerohour/ShadersZH.big", "shaders\\fterrain.pso", 4 }, { "zerohour/ShadersZH.big", "shaders\\fterrain0.pso", 3 },
		{ "zerohour/ShadersZH.big", "shaders\\fterrainnoise.pso", 6 }, { "zerohour/ShadersZH.big", "shaders\\fterrainnoise2.pso", 8 },
		{ "zerohour/ShadersZH.big", "shaders\\motionblur.pso", 9 }, { "zerohour/ShadersZH.big", "shaders\\motionblur.vso", 6 },
		{ "generals/shaders.big", "shaders\\invmonochrome.pso", 4 }, { "generals/shaders.big", "shaders\\monochrome.pso", 4 },
		{ "generals/shaders.big", "shaders\\roadnoise2.pso", 6 }, { "generals/shaders.big", "shaders\\terrain.pso", 4 },
		{ "generals/shaders.big", "shaders\\terrainnoise.pso", 6 }, { "generals/shaders.big", "shaders\\terrainnoise2.pso", 8 },
		{ "generals/shaders.big", "shaders\\wave.pso", 3 }, { "generals/shaders.big", "shaders\\wave.vso", 10 } };
	int decoded = 0;
	for (size_t i = 0; i < sizeof( shipped ) / sizeof( shipped[0] ); ++i)
	{
		const std::string bytes = readBigEntry( std::string( dir ) + "/" + shipped[i].archive, shipped[i].name );
		CHECK( !bytes.empty() && bytes.size() % 4 == 0 );
		if (bytes.empty() || bytes.size() % 4 != 0)
			continue;
		std::vector<uint32_t> tokens( bytes.size() / 4 );
		memcpy( &tokens[0], bytes.data(), bytes.size() );
		Program p;
		const bool ok = decodeProgram( &tokens[0], tokens.size(), p );
		if (!ok)
			printf( "  %s: %s\n", shipped[i].name, p.refusals[0].c_str() );
		CHECK( ok );
		CHECK_EQ( p.code.size(), shipped[i].instructions );
		decoded += ok ? 1 : 0;
	}
	printf( "  %d of 16 shipped programs decode with nothing refused\n", decoded );
}

// ---- the vertex program, against the pages' pseudocode --------------------------------------------
namespace {
Program decoded( const std::vector<uint32_t> &t )
{
	Program p;
	if (!decodeProgram( &t[0], t.size(), p ))
		printf( "  did not decode: %s\n", p.refusals[0].c_str() );
	return p;
}
}

TEST(ffprogram_vertex_arithmetic_swizzles_and_negate)
{
	// mov r0, v0.wzyx; add r1, r0, -v1; mul r2, r1, c0; mad r3, r2, c0.y, v0; dp4 oT0.x, r3, v1; mov oD0, r3
	const Program p = decoded( { VS11,
		ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_INPUT, 0, 0x1B ),
		ins( OP_ADD ), dst( REG_TEMP, 1 ), src( REG_TEMP, 0 ), src( REG_INPUT, 1, 0xE4, SRC_NEGATE ),
		ins( OP_MUL ), dst( REG_TEMP, 2 ), src( REG_TEMP, 1 ), src( REG_CONST, 0 ),
		ins( OP_MAD ), dst( REG_TEMP, 3 ), src( REG_TEMP, 2 ), src( REG_CONST, 0, 0x55 ), src( REG_INPUT, 0 ),
		ins( OP_DP4 ), dst( REG_TEXCRDOUT, 0, 0x1 ), src( REG_TEMP, 3 ), src( REG_INPUT, 1 ),
		ins( OP_MOV ), dst( REG_ATTROUT, 0 ), src( REG_TEMP, 3 ), END } );
	double inputs[ 16 ][ 4 ] = {};
	const double v0[ 4 ] = { 1, 2, 3, 4 }, v1[ 4 ] = { 0.5, 0.25, 1, 2 };
	memcpy( inputs[0], v0, sizeof( v0 ) );
	memcpy( inputs[1], v1, sizeof( v1 ) );
	const double c[ 1 ][ 4 ] = { { 2, 3, 4, 5 } };
	VertexRun out;
	runVertexProgram( p, c, 1, inputs, out );
	// r0 = (4,3,2,1); r1 = r0 - v1 = (3.5, 2.75, 1, -1); r2 = r1 * c0 = (7, 8.25, 4, -5);
	// r3 = r2 * c0.y + v0 = (22, 26.75, 15, -11); oT0.x = r3 . v1 = 11 + 6.6875 + 15 - 22 = 10.6875
	const double r3[ 4 ] = { 22, 26.75, 15, -11 };
	for (int i = 0; i < 4; ++i)
		CHECK_NEAR( out.colour[0][i], r3[i], 0.0 );
	CHECK_NEAR( out.texture[0][0], 10.6875, 0.0 );
	CHECK_NEAR( out.texture[0][1], 0.0, 0.0 );		// only .x written
	CHECK_EQ( out.wroteColour, 1u );
	CHECK_EQ( out.wroteTexture, 1u );
}

TEST(ffprogram_vertex_m4x4_rcp_and_relative_constants)
{
	// mov a0.x, v1; mov r0, c8[a0.x]; m4x4 oPos, v0, c0; rcp r1, v2.x; mov oT1, r1; mov oT2, r0
	const Program p = decoded( { VS11,
		ins( OP_MOV ), dst( REG_ADDR, 0, 0x1 ), src( REG_INPUT, 1 ),
		ins( OP_MOV ), dst( REG_TEMP, 0 ), src( REG_CONST, 8, 0xE4, SRC_NONE, true ),
		ins( OP_M4x4 ), dst( REG_RASTOUT, RASTOUT_POSITION ), src( REG_INPUT, 0 ), src( REG_CONST, 0 ),
		ins( OP_RCP ), dst( REG_TEMP, 1 ), src( REG_INPUT, 2, 0x00 ),
		ins( OP_MOV ), dst( REG_TEXCRDOUT, 1 ), src( REG_TEMP, 1 ),
		ins( OP_MOV ), dst( REG_TEXCRDOUT, 2 ), src( REG_TEMP, 0 ), END } );
	double c[ 16 ][ 4 ] = {};
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 4; ++col)
			c[row][col] = row * 4 + col + 1;		// m4x4 reads c0..c3 as the rows it dots v0 with
	for (int k = 8; k < 16; ++k)
		c[k][0] = k;
	double inputs[ 16 ][ 4 ] = {};
	const double pos[ 4 ] = { 1, 0, -1, 2 };
	memcpy( inputs[0], pos, sizeof( pos ) );
	VertexRun out;

	inputs[1][0] = inputs[1][3] = 2.6;		// nearest: 3, so c11
	inputs[2][0] = 4;
	runVertexProgram( p, c, 16, inputs, out );
	CHECK_NEAR( out.texture[2][0], 11.0, 0.0 );
	CHECK( !out.addressAmbiguous );
	// oPos.j = v0 . c(j): (1*1 - 3 + 2*4, 5 - 7 + 16, 9 - 11 + 24, 13 - 15 + 32)
	const double expect[ 4 ] = { 6, 14, 22, 30 };
	for (int j = 0; j < 4; ++j)
		CHECK_NEAR( out.position[j], expect[j], 0.0 );
	for (int j = 0; j < 4; ++j)
		CHECK_NEAR( out.texture[1][j], 0.25, 0.0 );		// rcp, to every component

	inputs[1][0] = inputs[1][3] = 2.5;		// a tie goes away from zero (P1): c11
	runVertexProgram( p, c, 16, inputs, out );
	CHECK_NEAR( out.texture[2][0], 11.0, 0.0 );
	inputs[1][0] = inputs[1][3] = 9.0;		// c17: past the file, (0, 0, 0, 0)
	runVertexProgram( p, c, 16, inputs, out );
	CHECK_NEAR( out.texture[2][0], 0.0, 0.0 );
	inputs[1][0] = 1.0; inputs[1][3] = 2.0;	// P1: .x and .w differ, reported
	runVertexProgram( p, c, 16, inputs, out );
	CHECK_NEAR( out.texture[2][0], 9.0, 0.0 );
	CHECK( out.addressAmbiguous );

	inputs[2][0] = 1.0;		// "exactly 1.0 if the input is exactly 1.0"
	runVertexProgram( p, c, 16, inputs, out );
	CHECK_EQ( out.texture[1][0], 1.0 );
	CHECK( !out.reciprocalOfZero );
	inputs[2][0] = 0.0;		// P2
	runVertexProgram( p, c, 16, inputs, out );
	CHECK_EQ( out.texture[1][0], (double)FLT_MAX );
	CHECK( out.reciprocalOfZero );
}

// ---- the pixel program ----------------------------------------------------------------------------
namespace {
struct Sampled { int stage[ 4 ]; double du[ 4 ], dv[ 4 ]; int calls; double colour[ 4 ][ 4 ]; };
void sampler( void *context, int stage, double du, double dv, double rgba[4] )
{
	Sampled &s = *(Sampled *)context;
	s.stage[s.calls] = stage; s.du[s.calls] = du; s.dv[s.calls] = dv; ++s.calls;
	for (int c = 0; c < 4; ++c)
		rgba[c] = s.colour[stage][c];
}
PixelInputs pixelInputs( Sampled &sampled )
{
	PixelInputs in;
	memset( &in, 0, sizeof( in ) );
	in.sample = sampler;
	in.context = &sampled;
	return in;
}
const double P5 = 1.0 / 256.0;
}

TEST(ffprogram_pixel_texture_arithmetic_and_modifiers)
{
	// tex t0; tex t1; mul r0, t0, v0; lrp r1, v0.a, t1, 1-t0; add r0.rgb, r0, r1; +mov r0.a, t1
	const Program p = decoded( { PS11,
		ins( OP_TEX ), dst( REG_TEXTURE, 0 ), ins( OP_TEX ), dst( REG_TEXTURE, 1 ),
		ins( OP_MUL ), dst( REG_TEMP, 0 ), src( REG_TEXTURE, 0 ), src( REG_INPUT, 0 ),
		ins( OP_LRP ), dst( REG_TEMP, 1 ), src( REG_INPUT, 0, 0xFF ), src( REG_TEXTURE, 1 ), src( REG_TEXTURE, 0, 0xE4, SRC_COMPLEMENT ),
		ins( OP_ADD ), dst( REG_TEMP, 0, 0x7 ), src( REG_TEMP, 0 ), src( REG_TEMP, 1 ),
		ins( OP_MOV, true ), dst( REG_TEMP, 0, 0x8 ), src( REG_TEXTURE, 1 ), END } );
	Sampled sampled;
	memset( &sampled, 0, sizeof( sampled ) );
	const double t0[ 4 ] = { 0.5, 0.25, 0.75, 1.0 }, t1[ 4 ] = { 0.125, 0.5, 0.25, 0.375 };
	memcpy( sampled.colour[0], t0, sizeof( t0 ) );
	memcpy( sampled.colour[1], t1, sizeof( t1 ) );
	PixelInputs in = pixelInputs( sampled );
	const double v0[ 4 ] = { 0.5, 1.0, 0.25, 0.75 };
	memcpy( in.colour[0], v0, sizeof( v0 ) );
	Interval4 out;
	runPixelProgram( p, in, out );
	CHECK_EQ( sampled.calls, 2 );
	CHECK( sampled.stage[0] == 0 && sampled.stage[1] == 1 && sampled.du[0] == 0.0 );
	// r0 = t0*v0 = (.25, .25, .1875, .75); r1 = .75*t1 + .25*(1 - t0) = (.21875, .5625, .25, .28125)
	// r0.rgb = (.46875, .8125, .4375); r0.a = t1.a = .375
	const double expect[ 4 ] = { 0.46875, 0.8125, 0.4375, 0.375 };
	for (int ch = 0; ch < 4; ++ch)
		CHECK_NEAR( out.nominal[ch], expect[ch], 1e-12 );
	// P5: v0 iterated +-1/256 and three stored results on the rgb path; the moved alpha is exact
	CHECK( out.lo[0] < expect[0] - 2 * P5 && out.hi[0] > expect[0] + 2 * P5 );
	CHECK( out.hi[0] - out.lo[0] < 12 * P5 );
	CHECK_NEAR( out.lo[3], 0.375, 0.0 );
	CHECK_NEAR( out.hi[3], 0.375, 0.0 );
}

TEST(ffprogram_pixel_range_x2_dp3_and_saturated_inputs)
{
	// def c1, .5, .5, .5, 1; tex t0; mul_x2 r0, t0, c0; dp3 r1, t0, c1; mov r0.a, r1; mul r1, v0, c1
	float values[ 4 ] = { 0.5f, 0.5f, 0.5f, 1.0f };
	uint32_t raw[ 4 ];
	memcpy( raw, values, sizeof( raw ) );
	const Program p = decoded( { PS11, ins( OP_DEF ), dst( REG_CONST, 1 ), raw[0], raw[1], raw[2], raw[3],
		ins( OP_TEX ), dst( REG_TEXTURE, 0 ),
		ins( OP_MUL ), dst( REG_TEMP, 0, 0xF, 1 ), src( REG_TEXTURE, 0 ), src( REG_CONST, 0 ),
		ins( OP_DP3 ), dst( REG_TEMP, 1 ), src( REG_TEXTURE, 0 ), src( REG_CONST, 1 ),
		ins( OP_MOV ), dst( REG_TEMP, 0, 0x8 ), src( REG_TEMP, 1 ),
		ins( OP_MUL ), dst( REG_TEMP, 1 ), src( REG_INPUT, 0 ), src( REG_CONST, 1 ), END } );
	Sampled sampled;
	memset( &sampled, 0, sizeof( sampled ) );
	const double t0[ 4 ] = { 0.8, 0.4, 0.2, 0.6 };
	memcpy( sampled.colour[0], t0, sizeof( t0 ) );
	PixelInputs in = pixelInputs( sampled );
	const double c0[ 4 ] = { 1.0, 0.5, 2.0, 1.0 };		// c0.z past the -1..+1 a constant holds (P4)
	memcpy( in.constants[0], c0, sizeof( c0 ) );
	Interval4 out;
	runPixelProgram( p, in, out );
	// _x2: r0.r = 2 * .8 = 1.6, nominally clamped to 1 (P4), the interval reaching 1.6
	CHECK_NEAR( out.nominal[0], 1.0, 0.0 );
	CHECK( out.hi[0] >= 1.6 && out.lo[0] <= 1.0 );
	CHECK_NEAR( out.nominal[1], 0.4, 1e-12 );
	// c0.z = 2: nominally 1 (so r0.b = 2 * .2 * 1 = .4), and up to 2 in the interval (.8)
	CHECK_NEAR( out.nominal[2], 0.4, 1e-12 );
	CHECK( out.hi[2] >= 0.8 );
	// P6: dp3's sum reaches alpha: (.8 + .4 + .2) * .5 = .7
	CHECK_NEAR( out.nominal[3], 0.7, 1e-12 );
}

TEST(ffprogram_pixel_texbem_offsets_and_colour_saturation)
{
	// tex t0; texbem t1, t0; mul r0, t1, v0
	const Program p = decoded( { PS11, ins( OP_TEX ), dst( REG_TEXTURE, 0 ),
		ins( OP_TEXBEM ), dst( REG_TEXTURE, 1 ), src( REG_TEXTURE, 0 ),
		ins( OP_MUL ), dst( REG_TEMP, 0 ), src( REG_TEXTURE, 1 ), src( REG_INPUT, 0 ), END } );
	Sampled sampled;
	memset( &sampled, 0, sizeof( sampled ) );
	const double bump[ 4 ] = { 0.5, -0.25, 0, 0 }, colour[ 4 ] = { 1, 1, 1, 1 };
	memcpy( sampled.colour[0], bump, sizeof( bump ) );
	memcpy( sampled.colour[1], colour, sizeof( colour ) );
	PixelInputs in = pixelInputs( sampled );
	// stage 1's matrix (the stage sampled): 00 = 2, 01 = 3, 10 = 5, 11 = 7
	in.bumpMatrix[1][0] = 2; in.bumpMatrix[1][1] = 3; in.bumpMatrix[1][2] = 5; in.bumpMatrix[1][3] = 7;
	in.bumpMatrix[0][0] = 100;		// stage 0's must not be used
	const double v0[ 4 ] = { 1.5, 0.5, -0.5, 1 };		// saturated on the way in
	memcpy( in.colour[0], v0, sizeof( v0 ) );
	Interval4 out;
	runPixelProgram( p, in, out );
	CHECK_EQ( sampled.calls, 2 );
	CHECK_EQ( sampled.stage[1], 1 );
	// du = MAT00 * R + MAT10 * G = 2 * .5 + 5 * -.25 = -.25; dv = MAT01 * R + MAT11 * G = 1.5 - 1.75 = -.25
	CHECK_NEAR( sampled.du[1], -0.25, 1e-12 );
	CHECK_NEAR( sampled.dv[1], -0.25, 1e-12 );
	CHECK_NEAR( out.nominal[0], 1.0, 0.0 );
	CHECK_NEAR( out.nominal[2], 0.0, 0.0 );
}

// ---- the ps.1.1 assembler, on the game's own text -------------------------------------------------
namespace {
/// Every "ps.1.1..." string literal in a C++ file, as the compiler would read it
std::vector<std::string> pixelProgramsIn( const std::string &path )
{
	std::vector<std::string> programs;
	FILE *f = fopen( path.c_str(), "rb" );
	if (f == NULL)
		return programs;
	std::string source;
	char buffer[ 4096 ];
	size_t got;
	while ((got = fread( buffer, 1, sizeof( buffer ), f )) > 0)
		source.append( buffer, got );
	fclose( f );
	size_t at = 0;
	while ((at = source.find( "\"ps.1.1", at )) != std::string::npos)
	{
		std::string text;
		size_t i = at + 1;
		for (; i < source.size() && source[i] != '"'; ++i)
		{
			if (source[i] != '\\')
			{
				text += source[i];
				continue;
			}
			++i;
			if (source[i] == 'n') text += '\n';
			else if (source[i] == '\n') {}				// a continued line
			else if (source[i] == '\r' && source[i + 1] == '\n') ++i;
			else text += source[i];
		}
		programs.push_back( text );
		at = i;
	}
	return programs;
}
}

TEST(ffprogram_assembles_the_waters_text)
{
	const std::vector<std::string> programs = pixelProgramsIn(
		std::string( ZH_CODE_DIR ) + "/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp" );
	CHECK_EQ( programs.size(), (size_t)4 );		// river, environment, trapezoid, mirror (2026-09-26)
	const size_t instructions[ 4 ] = { 11, 6, 7, 9 };
	for (size_t k = 0; k < programs.size() && k < 4; ++k)
	{
		std::vector<uint32_t> tokens;
		std::string error;
		const bool assembled = assemblePixelProgram( programs[k], tokens, error );
		if (!assembled)
			printf( "  water program %zu: %s\n", k, error.c_str() );
		CHECK( assembled );
		Program p;
		const bool ok = assembled && decodeProgram( &tokens[0], tokens.size(), p );
		if (assembled && !ok)
			printf( "  water program %zu: %s\n", k, p.refusals[0].c_str() );
		CHECK( ok );
		CHECK_EQ( p.code.size(), instructions[k] );
	}
	// the text form of an instruction gives the tokens the builders give
	std::vector<uint32_t> tokens;
	std::string error;
	CHECK( assemblePixelProgram( "ps_1_1 // a comment\n tex t0\n mul_x2 r0.rgb, v0, t0 ; another\n +mov r0.a, 1-t0.a\n", tokens, error ) );
	const std::vector<uint32_t> expect = { PS11, ins( OP_TEX ), dst( REG_TEXTURE, 0 ),
		ins( OP_MUL ), dst( REG_TEMP, 0, 0x7, 1 ), src( REG_INPUT, 0 ), src( REG_TEXTURE, 0 ),
		ins( OP_MOV, true ), dst( REG_TEMP, 0, 0x8 ), src( REG_TEXTURE, 0, 0xFF, SRC_COMPLEMENT ), END };
	CHECK( tokens == expect );
	CHECK( !assemblePixelProgram( "ps.1.1\n cnd r0, r0.a, t0, t1\n", tokens, error ) );
	CHECK( !assemblePixelProgram( "ps.1.4\n texld r0, t0\n", tokens, error ) );

	// The mirror program (the fork's own) "four times and clamped": that clamp is the range cap (P4).
	// Over a mid-grey object, (1 - .5) * 4 = 2 is clamped to 1 at the documented minimum cap, so the water
	// darkens by the strength c0; a device whose cap is higher does not clamp, and darkens twice as much.
	if (programs.size() == 4)
	{
		CHECK( assemblePixelProgram( programs[3], tokens, error ) );
		Program mirror;
		CHECK( decodeProgram( &tokens[0], tokens.size(), mirror ) );
		Sampled sampled;
		memset( &sampled, 0, sizeof( sampled ) );
		for (int ch = 0; ch < 3; ++ch)
			sampled.colour[0][ch] = 0.5;
		PixelInputs in = pixelInputs( sampled );
		for (int ch = 0; ch < 4; ++ch)
			in.constants[0][ch] = 0.25;		// the strength
		Interval4 out;
		runPixelProgram( mirror, in, out );
		CHECK_NEAR( out.nominal[0], 0.75, 1e-9 );		// clamped at cap 1: 1 - 1 * .25
		CHECK( out.lo[0] <= 0.5 + 1e-9 );				// uncapped: 1 - 2 * .25
		printf( "  mirror water over mid grey: %.3f at the minimum cap, down to %.3f without it\n", out.nominal[0], out.lo[0] );
	}
}
