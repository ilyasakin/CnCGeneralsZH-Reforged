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
 * FFReference's programmable stages (A3e): vs_1_1 and ps_1_1 programs, run from their ORIGINAL tokens.
 *
 * The same rule as FFReference: written from Microsoft's documentation alone - the shader reference pages
 * for what each instruction, register and modifier does (one page cited per instruction in ffprogram.cpp),
 * the driver pages "Instruction Token", "Destination Parameter Token" and "Source Parameter Token" for
 * the bit layouts.  It shares nothing with the game's own shader translation, which it exists to check.
 * The numbers the pages leave out (opcodes, register types, modifiers, version tokens) are values from
 * Microsoft's d3d8types.h, the header the reference pages name; they are written here as this file's own
 * constants, and Tools/ffprogram-values-check.sh static_asserts them against MinGW-w64's d3d8types.h.
 *
 * SCOPE: exactly what the game ships (the census, 2026-09-26: 16 programs in ShadersZH.big and
 * shaders.big, 4 ps.1.1 text programs in W3DWater.cpp).
 *   ps_1_1: tex, texbem, mov, mul, mad, add, dp3, lrp, def; registers r, v, c, t; the complement source
 *           modifier; the _x2 result shift; write masks; the alpha-replicate source swizzle; co-issue.
 *   vs_1_1: mov, add, mad, mul, dp4, m4x4, rcp; registers r, v, c (relative through a0.x), a0; the
 *           negate source modifier; any swizzle and write mask; outputs oPos, oD0, oD1, oT0-oT7.
 * Everything else is REFUSED by name when the program is decoded (Program::refusals), as FFReference
 * refuses the fixed-function states it does not model.
 */

#ifndef FFPROGRAM_H
#define FFPROGRAM_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace FFRef {

// ---- token values (Microsoft's d3d8types.h, as this file's own names) ------------------------------
namespace Token {
	enum : uint32_t {
		VERSION_PIXEL = 0xFFFF0000u,		///< the high half of a pixel shader's version token
		VERSION_VERTEX = 0xFFFE0000u,		///< and of a vertex shader's; major in bits 15:8, minor in 7:0
		END = 0x0000FFFFu,
		COMMENT = 0xFFFEu,					///< opcode of a comment; its length in bits 30:16
		COISSUE = 0x40000000u,				///< instruction token bit 30 (ps_1_x)
		PARAMETER = 0x80000000u				///< bit 31, set on every parameter token
	};
	enum Opcode : uint32_t {
		OP_NOP = 0, OP_MOV = 1, OP_ADD = 2, OP_SUB = 3, OP_MAD = 4, OP_MUL = 5, OP_RCP = 6, OP_RSQ = 7,
		OP_DP3 = 8, OP_DP4 = 9, OP_MIN = 10, OP_MAX = 11, OP_SLT = 12, OP_SGE = 13, OP_EXP = 14, OP_LOG = 15,
		OP_LIT = 16, OP_DST = 17, OP_LRP = 18, OP_FRC = 19, OP_M4x4 = 20, OP_M4x3 = 21, OP_M3x4 = 22,
		OP_M3x3 = 23, OP_M3x2 = 24,
		OP_TEXCOORD = 64, OP_TEXKILL = 65, OP_TEX = 66, OP_TEXBEM = 67, OP_TEXBEML = 68,
		OP_TEXREG2AR = 69, OP_TEXREG2GB = 70, OP_TEXM3x2PAD = 71, OP_TEXM3x2TEX = 72, OP_TEXM3x3PAD = 73,
		OP_TEXM3x3TEX = 74, OP_TEXM3x3DIFF = 75, OP_TEXM3x3SPEC = 76, OP_TEXM3x3VSPEC = 77, OP_EXPP = 78,
		OP_LOGP = 79, OP_CND = 80, OP_DEF = 81, OP_TEXREG2RGB = 82, OP_TEXDP3TEX = 83, OP_TEXM3x2DEPTH = 84,
		OP_TEXDP3 = 85, OP_TEXM3x3 = 86, OP_TEXDEPTH = 87, OP_CMP = 88, OP_BEM = 89, OP_PHASE = 0xFFFD
	};
	/// Register types: bits 30:28 of a parameter token (the only three bits shader model 1 uses)
	enum Register : uint32_t {
		REG_TEMP = 0, REG_INPUT = 1, REG_CONST = 2, REG_ADDR = 3, REG_TEXTURE = 3,	///< a0 in a vertex shader, t in a pixel shader
		REG_RASTOUT = 4, REG_ATTROUT = 5, REG_TEXCRDOUT = 6
	};
	enum : uint32_t { RASTOUT_POSITION = 0, RASTOUT_FOG = 1, RASTOUT_POINTSIZE = 2 };
	/// Source modifiers, bits 27:24 of a source parameter token
	enum SourceModifier : uint32_t {
		SRC_NONE = 0, SRC_NEGATE = 1, SRC_BIAS = 2, SRC_BIASNEGATE = 3, SRC_SIGN = 4, SRC_SIGNNEGATE = 5,
		SRC_COMPLEMENT = 6, SRC_X2 = 7, SRC_X2NEGATE = 8, SRC_DZ = 9, SRC_DW = 10
	};
	enum : uint32_t {
		REGNUM_MASK = 0x7FFu, REGTYPE_SHIFT = 28, REGTYPE_MASK = 0x70000000u,
		WRITEMASK_SHIFT = 16, WRITEMASK_MASK = 0x000F0000u,
		RESULTMOD_SHIFT = 20, RESULTMOD_MASK = 0x00F00000u, RESULTMOD_SATURATE = 1,
		RESULTSHIFT_SHIFT = 24, RESULTSHIFT_MASK = 0x0F000000u,
		SWIZZLE_SHIFT = 16, SWIZZLE_MASK = 0x00FF0000u,
		SRCMOD_SHIFT = 24, SRCMOD_MASK = 0x0F000000u,
		ADDRESS_RELATIVE = 0x00002000u,		///< bit 13: relative addressing (vertex shader sources); not RELATIVE, a Windows macro
		COMMENTSIZE_SHIFT = 16, COMMENTSIZE_MASK = 0x7FFF0000u
	};
}

// ---- a decoded program ----------------------------------------------------------------------------
enum ProgramKind { PROGRAM_VERTEX, PROGRAM_PIXEL };

struct Operand
{
	uint32_t type;			///< Token::Register
	int index;				///< register number
	unsigned mask;			///< destinations: the write mask, bit 0 = x/r .. bit 3 = w/a
	int swizzle[4];			///< sources: the component each channel reads, 0..3
	uint32_t modifier;		///< sources: Token::SourceModifier
	bool relative;			///< sources: c[a0.x + index]
};

struct Instruction
{
	uint32_t opcode;		///< Token::Opcode
	bool coissue;			///< ps_1_x '+': runs with the instruction before it
	bool saturate;			///< the _sat result modifier
	int shift;				///< the result shift, as a power of two: 1 is _x2, -1 is _d2 (ps_1_x)
	Operand dst;
	Operand src[3];
	int sources;
	double value[4];		///< def's constant
};

struct Program
{
	ProgramKind kind;
	int major, minor;
	std::vector<Instruction> code;
	std::vector<std::string> refusals;	///< non-empty: outside the census; it must not be run
};

/// Decodes a token stream (version token first, END last) into `out`.  FALSE, with out.refusals saying
/// why, for anything malformed or outside the census.
bool decodeProgram( const uint32_t *tokens, size_t count, Program &out );

/// Assembles ps.1.1 text, as the water's programs are written, into tokens.  FALSE with `error` for
/// anything the census does not hold.  (A second reading of what D3DXAssembleShader produces.)
bool assemblePixelProgram( const std::string &text, std::vector<uint32_t> &tokens, std::string &error );

}	// namespace FFRef

#endif
