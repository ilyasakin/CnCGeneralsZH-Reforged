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

/*
 * FFReference's programmable stages: see ffprogram.h for the rules, the sources and the scope.
 * Token layouts: "Instruction Token", "Destination Parameter Token", "Source Parameter Token"
 * (learn.microsoft.com, windows-hardware/drivers/display).
 */

#include "ffreference/ffprogram.h"

#include <stdio.h>
#include <string.h>

namespace FFRef {

namespace {

using namespace Token;

std::string at( size_t i, const char *what )
{
	char buffer[ 160 ];
	snprintf( buffer, sizeof( buffer ), "token %zu: %s", i, what );
	return buffer;
}

Operand destination( uint32_t t )
{
	Operand o;
	memset( &o, 0, sizeof( o ) );
	o.type = (t & REGTYPE_MASK) >> REGTYPE_SHIFT;
	o.index = (int)(t & REGNUM_MASK);
	o.mask = (t & WRITEMASK_MASK) >> WRITEMASK_SHIFT;
	for (int i = 0; i < 4; ++i)
		o.swizzle[i] = i;
	return o;
}

Operand source( uint32_t t )
{
	Operand o;
	memset( &o, 0, sizeof( o ) );
	o.type = (t & REGTYPE_MASK) >> REGTYPE_SHIFT;
	o.index = (int)(t & REGNUM_MASK);
	o.mask = 0xF;
	const uint32_t swizzle = (t & SWIZZLE_MASK) >> SWIZZLE_SHIFT;
	for (int i = 0; i < 4; ++i)
		o.swizzle[i] = (int)((swizzle >> (2 * i)) & 3);
	o.modifier = (t & SRCMOD_MASK) >> SRCMOD_SHIFT;
	o.relative = (t & ADDRESS_RELATIVE) != 0;
	return o;
}

/// The census: the operations each kind may use, and how many sources each takes
int censusSources( ProgramKind kind, uint32_t op )
{
	if (kind == PROGRAM_PIXEL)
		switch (op)
		{
			case OP_TEX: return 0;
			case OP_TEXBEM: case OP_MOV: return 1;
			case OP_MUL: case OP_ADD: case OP_DP3: return 2;
			case OP_MAD: case OP_LRP: return 3;
		}
	else
		switch (op)
		{
			case OP_MOV: case OP_RCP: return 1;
			case OP_ADD: case OP_MUL: case OP_DP4: case OP_M4x4: return 2;
			case OP_MAD: return 3;
		}
	return -1;
}

bool identitySwizzle( const Operand &o )
{
	return o.swizzle[0] == 0 && o.swizzle[1] == 1 && o.swizzle[2] == 2 && o.swizzle[3] == 3;
}

bool alphaReplicate( const Operand &o )
{
	return o.swizzle[0] == 3 && o.swizzle[1] == 3 && o.swizzle[2] == 3 && o.swizzle[3] == 3;
}

/// Everything about one instruction the census does not hold, into `refusals`
void checkCensus( const Program &p, const Instruction &in, size_t i, std::vector<std::string> &refusals )
{
	const bool ps = p.kind == PROGRAM_PIXEL;
	if (in.saturate)
		refusals.push_back( at( i, "the _sat result modifier (not in the census)" ) );
	if (in.shift != 0 && !(ps && in.shift == 1))
		refusals.push_back( at( i, "a result shift other than _x2" ) );
	if (in.coissue && !ps)
		refusals.push_back( at( i, "co-issue in a vertex shader" ) );
	const Operand &d = in.dst;
	if (ps)
	{
		const bool texture = in.opcode == OP_TEX || in.opcode == OP_TEXBEM;
		if (texture ? d.type != REG_TEXTURE : d.type != REG_TEMP)
			refusals.push_back( at( i, texture ? "a texture instruction writing other than t" : "an arithmetic instruction writing other than r" ) );
		if (d.mask != 0xF && d.mask != 0x7 && d.mask != 0x8)
			refusals.push_back( at( i, "a write mask other than .rgba, .rgb or .a" ) );
		if (d.index > 3 || (d.type == REG_TEMP && d.index > 1))
			refusals.push_back( at( i, "a register past ps_1_1's (r0-r1, t0-t3)" ) );
	}
	else
	{
		const bool ok = d.type == REG_TEMP || d.type == REG_ATTROUT || d.type == REG_TEXCRDOUT
			|| (d.type == REG_RASTOUT && d.index == RASTOUT_POSITION) || (d.type == REG_ADDR && in.opcode == OP_MOV);
		if (!ok)
			refusals.push_back( at( i, "a vertex destination outside the census (oFog, oPts, or a0 other than by mov)" ) );
		if (d.type == REG_ADDR && (d.index != 0 || d.mask != 0x1))
			refusals.push_back( at( i, "an address register other than a0.x" ) );
		if (d.type == REG_ATTROUT && d.index > 1)
			refusals.push_back( at( i, "a colour output past oD1" ) );
		if (d.type == REG_TEXCRDOUT && d.index > 7)
			refusals.push_back( at( i, "a texture coordinate output past oT7" ) );
		if (d.type == REG_TEMP && d.index > 11)
			refusals.push_back( at( i, "a temporary past r11" ) );
	}
	for (int s = 0; s < in.sources; ++s)
	{
		const Operand &o = in.src[s];
		if (ps)
		{
			if (o.type != REG_TEMP && o.type != REG_INPUT && o.type != REG_CONST && o.type != REG_TEXTURE)
				refusals.push_back( at( i, "a pixel source outside r, v, c, t" ) );
			if (o.modifier != SRC_NONE && o.modifier != SRC_COMPLEMENT)
				refusals.push_back( at( i, "a pixel source modifier other than complement (not in the census)" ) );
			if (!identitySwizzle( o ) && !alphaReplicate( o ))
				refusals.push_back( at( i, "a pixel source swizzle other than none or .a" ) );
			if (o.relative)
				refusals.push_back( at( i, "relative addressing in a pixel shader" ) );
			if ((o.type == REG_TEMP && o.index > 1) || (o.type == REG_INPUT && o.index > 1)
					|| (o.type == REG_CONST && o.index > 7) || (o.type == REG_TEXTURE && o.index > 3))
				refusals.push_back( at( i, "a register past ps_1_1's" ) );
		}
		else
		{
			if (o.type != REG_TEMP && o.type != REG_INPUT && o.type != REG_CONST)
				refusals.push_back( at( i, "a vertex source outside r, v, c" ) );
			if (o.modifier != SRC_NONE && o.modifier != SRC_NEGATE)
				refusals.push_back( at( i, "a vertex source modifier other than negate" ) );
			if (o.relative && o.type != REG_CONST)
				refusals.push_back( at( i, "relative addressing of other than a constant" ) );
			if ((o.type == REG_TEMP && o.index > 11) || (o.type == REG_INPUT && o.index > 15) || (o.type == REG_CONST && o.index > 95))
				refusals.push_back( at( i, "a register past vs_1_1's (r0-r11, v0-v15, c0-c95)" ) );
		}
	}
}

}	// namespace

bool decodeProgram( const uint32_t *tokens, size_t count, Program &out )
{
	out = Program();
	std::vector<std::string> &refusals = out.refusals;
	if (count == 0)
	{
		refusals.push_back( "an empty program" );
		return false;
	}
	const uint32_t version = tokens[0];
	if ((version & 0xFFFF0000u) == VERSION_PIXEL)
		out.kind = PROGRAM_PIXEL;
	else if ((version & 0xFFFF0000u) == VERSION_VERTEX)
		out.kind = PROGRAM_VERTEX;
	else
	{
		refusals.push_back( "the first token is not a version token" );
		return false;
	}
	out.major = (int)((version >> 8) & 0xFF);
	out.minor = (int)(version & 0xFF);
	if (out.major != 1 || out.minor != 1)
		refusals.push_back( out.kind == PROGRAM_PIXEL ? "a pixel shader version other than 1_1" : "a vertex shader version other than 1_1" );

	bool ended = false;
	size_t i = 1;
	while (i < count && !ended)
	{
		const uint32_t t = tokens[i];
		const uint32_t op = t & 0xFFFFu;
		if (t == END)
		{
			ended = true;
			++i;
			break;
		}
		if (t & PARAMETER)
		{
			refusals.push_back( at( i, "a parameter token where an instruction was due" ) );
			return false;
		}
		if (op == COMMENT)
		{
			i += 1 + ((t & COMMENTSIZE_MASK) >> COMMENTSIZE_SHIFT);
			continue;
		}
		Instruction in;
		memset( &in, 0, sizeof( in ) );
		in.opcode = op;
		in.coissue = (t & COISSUE) != 0;
		const size_t start = i++;
		if (op == OP_DEF)
		{
			if (out.kind != PROGRAM_PIXEL || i + 5 > count || !(tokens[i] & PARAMETER))
			{
				refusals.push_back( at( start, "a def outside a pixel shader, or cut short" ) );
				return false;
			}
			in.dst = destination( tokens[i] );
			for (int k = 0; k < 4; ++k)
			{
				float f;
				memcpy( &f, &tokens[i + 1 + k], 4 );
				in.value[k] = f;
			}
			i += 5;
			if (in.dst.type != REG_CONST || in.dst.index > 7)
				refusals.push_back( at( start, "a def of other than c0-c7" ) );
			out.code.push_back( in );
			continue;
		}
		const int wanted = censusSources( out.kind, op );
		if (wanted < 0)
		{
			char what[ 96 ];
			snprintf( what, sizeof( what ), "opcode %u, not in the census", (unsigned)op );
			refusals.push_back( at( start, what ) );
		}
		std::vector<uint32_t> params;
		while (i < count && (tokens[i] & PARAMETER))
			params.push_back( tokens[i++] );
		if (params.empty())
		{
			refusals.push_back( at( start, "an instruction with no destination" ) );
			return false;
		}
		in.dst = destination( params[0] );
		const uint32_t resultMod = (params[0] & RESULTMOD_MASK) >> RESULTMOD_SHIFT;
		in.saturate = (resultMod & RESULTMOD_SATURATE) != 0;
		if (resultMod & ~RESULTMOD_SATURATE)
			refusals.push_back( at( start, "a result modifier other than _sat" ) );
		const int shift = (int)((params[0] & RESULTSHIFT_MASK) >> RESULTSHIFT_SHIFT);
		in.shift = shift >= 8 ? shift - 16 : shift;		// a signed four-bit shift
		in.sources = (int)params.size() - 1;
		if (in.sources > 3)
		{
			refusals.push_back( at( start, "more than three sources" ) );
			return false;
		}
		for (int s = 0; s < in.sources; ++s)
			in.src[s] = source( params[1 + s] );
		if (wanted >= 0 && in.sources != wanted)
			refusals.push_back( at( start, "the wrong number of sources for its operation" ) );
		if (wanted >= 0)
			checkCensus( out, in, start, refusals );
		out.code.push_back( in );
	}
	if (!ended)
		refusals.push_back( "no end token" );
	if (!out.code.empty() && out.code[0].coissue)
		refusals.push_back( "the first instruction is co-issued" );
	return refusals.empty();
}

}	// namespace FFRef
