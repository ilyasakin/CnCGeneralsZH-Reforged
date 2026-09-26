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
		ins( OP_MOV ), dst( REG_ADDR, 0, 0x1 ), src( REG_INPUT, 1 ),
		ins( OP_MAD ), dst( REG_TEMP, 1 ), src( REG_TEMP, 2, 0xEA ), src( REG_CONST, 8, 0xE4, SRC_NONE, true ), src( REG_INPUT, 0, 0xE4, SRC_NEGATE ),
		ins( OP_M4x4 ), dst( REG_RASTOUT, RASTOUT_POSITION ), src( REG_TEMP, 1 ), src( REG_CONST, 4 ),
		ins( OP_MOV ), dst( REG_TEXCRDOUT, 1 ), src( REG_TEMP, 1 ),
		ins( OP_RCP ), dst( REG_TEMP, 1, 0x8 ), src( REG_TEMP, 0, 0xFF ),
		END };
	CHECK( decodes( vs, p ) );
	CHECK_EQ( p.kind, PROGRAM_VERTEX );
	CHECK( p.code[1].src[1].relative );
	CHECK_EQ( p.code[1].src[0].swizzle[0], 2 );
	CHECK_EQ( p.code[1].src[2].modifier, (uint32_t)SRC_NEGATE );
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
