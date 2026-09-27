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
 * ffref_judge: FFReference's own replay of a capture set (L2's oracle side, and A3's on any backend).
 *
 * Every draw_NNNNN.cap in the folder is read by ffcapture.h (written from a contributor's description of the
 * format, not from the device or its harness), its textures decoded there, its programs decoded by
 * ffprogram.h, and drawn by FFReference into a target of the capture's size.  With --gpu, the GPU's own
 * pixels for the same capture (the device harness's dump, read as a black box) are compared with it,
 * and each capture gets a verdict:
 *   agree        every pixel exact or inside the envelope's documented freedoms
 *   DISAGREE     pixels outside it: counted, with the worst one and the zones
 *   refused      FFReference's documented refusals (it says which); nothing is judged
 *   unreadable   the file disagrees with the format's description (it says where)
 * Without --gpu it draws only, and reports what the reference did: a check of the reader itself.
 *
 * THE STARTING POINT, as a contributor describes the harness's replay: before each draw the whole target is cleared
 * to 0xFF3F2F1F, depth to 1.0 and stencil to 0 (a D24S8 depth-stencil), then the captured viewport is set;
 * a draw with no depth-stencil bound has depth and stencil off.  The GPU dump is <capture>.cap.bgra: the
 * back buffer read back after the draw, B, G, R, A bytes, rows top first, TargetWidth x TargetHeight.
 *
 * ANISOTROPIC is replayed as LINEAR, as FFReference's ruling on the harness accepted it: D3D9 defines no
 * anisotropic footprint, so an envelope around one would be no oracle.  Every run counts and prints the
 * draws it did this to ("N draws: ANISOTROPIC replayed as LINEAR"), so a pass never hides it; the GPU
 * side of the comparison must replay them the same way (the harness does, by that ruling).
 *
 *   ffref_judge <capture dir> [--gpu <dump dir>] [--only draw_NNNNN] [--mutate <bits>] [--shift-gpu]
 *
 * Armed controls, so an "agree" is a comparison that can fail: --mutate draws with FFReference's own
 * deliberate departures (ffreference.h's Mutation bits; D3D10's pixel centres, swapped blend factors, ...),
 * and --shift-gpu compares each capture with the NEXT capture's GPU picture.  Each line also says how
 * many pixels the reference wrote and how many the GPU changed from the clear colour, so a draw that
 * wrote nothing on either side is counted as such ("empty"), never as evidence.
 * Exit status: 0 no disagreement and nothing unreadable; 1 otherwise.
 */

#include "ffreference/ffcapture.h"

#include <dirent.h>
#include <algorithm>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace FFRef;
using namespace FFCapture;

namespace {

std::vector<std::string> captureNames( const std::string &dir )
{
	std::vector<std::string> names;
	DIR *d = opendir( dir.c_str() );
	if (d == NULL)
		return names;
	while (dirent *e = readdir( d ))
	{
		const std::string n = e->d_name;
		if (n.size() > 4 && n.compare( n.size() - 4, 4, ".cap" ) == 0 && n.compare( 0, 5, "draw_" ) == 0)
			names.push_back( n.substr( 0, n.size() - 4 ) );
	}
	closedir( d );
	std::sort( names.begin(), names.end() );
	return names;
}

const char *primitiveName( uint32_t t )
{
	static const char *names[] = { "?", "points", "lines", "linestrip", "triangles", "strip", "fan" };
	return t < 7 ? names[t] : "?";
}

}  // namespace

int main( int argc, char *argv[] )
{
	if (argc < 2)
	{
		fprintf( stderr, "usage: ffref_judge <capture dir> [--gpu <dump dir>] [--only draw_NNNNN]\n" );
		return 2;
	}
	const std::string dir = argv[1];
	std::string gpuDir, only;
	unsigned mutations = 0;
	bool shiftGpu = false;
	for (int i = 2; i < argc; ++i)
	{
		if (strcmp( argv[i], "--shift-gpu" ) == 0)
			shiftGpu = true;
		else if (i + 1 < argc && strcmp( argv[i], "--gpu" ) == 0)
			gpuDir = argv[++i];
		else if (i + 1 < argc && strcmp( argv[i], "--only" ) == 0)
			only = argv[++i];
		else if (i + 1 < argc && strcmp( argv[i], "--mutate" ) == 0)
			mutations = (unsigned)strtoul( argv[++i], NULL, 0 );
	}
	std::vector<std::string> names = captureNames( dir );
	if (names.empty())
	{
		fprintf( stderr, "ffref_judge: no draw_*.cap in %s\n", dir.c_str() );
		return 2;
	}
	std::map<std::string, Texture> textures;
	std::map<std::string, std::string> textureErrors;
	int drawn = 0, refused = 0, unreadable = 0, agree = 0, disagree = 0, anisotropic = 0, empty = 0;
	for (size_t n = 0; n < names.size(); ++n)
	{
		if (!only.empty() && names[n] != only)
			continue;
		Capture cap;
		std::string error;
		if (!readCapture( dir, names[n], cap, error ))
		{
			printf( "unreadable %s: %s\n", names[n].c_str(), error.c_str() );
			++unreadable;
			continue;
		}
		const Header &h = cap.header;
		VertexLayout layout;
		if (!layoutFromFVF( h.fvf, layout, error ) || layout.size != h.stride)
		{
			if (error.empty())
			{
				char what[ 96 ];
				snprintf( what, sizeof( what ), "FVF 0x%X describes %u bytes, the stride is %u", (unsigned)h.fvf,
					layout.size, (unsigned)h.stride );
				error = what;
			}
			printf( "unreadable %s: %s\n", names[n].c_str(), error.c_str() );
			++unreadable;
			continue;
		}
		const Texture *stageTextures[8] = { 0 };
		std::string textureProblem;
		for (int s = 0; s < 8; ++s)
		{
			const std::string &t = h.textures[s];
			if (t.empty())
				continue;
			if (!textures.count( t ) && !textureErrors.count( t ))
			{
				Texture tex;
				std::string terr;
				if (readTexture( dir + "/" + t, tex, terr ))
					textures[t] = tex;
				else
					textureErrors[t] = terr;
			}
			if (textureErrors.count( t ))
				textureProblem = textureErrors[t];
			else
				stageTextures[s] = &textures[t];
		}
		if (!textureProblem.empty())
		{
			printf( "unreadable %s: %s\n", names[n].c_str(), textureProblem.c_str() );
			++unreadable;
			continue;
		}
		Program vertexProgram, pixelProgram;
		const Program *vp = NULL, *pp = NULL;
		std::string programRefusal;
		if (cap.hasProg)
		{
			if (cap.prog.vertexPresent)
			{
				if (!decodeProgram( &cap.prog.vertexTokens[0], cap.prog.vertexTokens.size(), vertexProgram )
						|| !vertexProgram.refusals.empty())
					programRefusal = "the vertex program " + cap.prog.vertexName + ": "
						+ (vertexProgram.refusals.empty() ? std::string( "undecodable" ) : vertexProgram.refusals[0]);
				vp = &vertexProgram;
			}
			if (cap.prog.pixelPresent)
			{
				if (!decodeProgram( &cap.prog.pixelTokens[0], cap.prog.pixelTokens.size(), pixelProgram )
						|| !pixelProgram.refusals.empty())
					programRefusal = "the pixel program " + cap.prog.pixelName + ": "
						+ (pixelProgram.refusals.empty() ? std::string( "undecodable" ) : pixelProgram.refusals[0]);
				pp = &pixelProgram;
			}
		}
		DrawState state;
		if (!drawState( cap, layout, stageTextures, vp, pp, state, error ))
		{
			printf( "refused %s: %s\n", names[n].c_str(), error.c_str() );
			++refused;
			continue;
		}
		bool substituted = false;
		for (int s = 0; s < MAX_STAGES; ++s)
			for (int f = SAMP_MAGFILTER; f <= SAMP_MINFILTER; ++f)
				if (state.samplerState[s][f] == TEXF_ANISOTROPIC)
				{
					state.samplerState[s][f] = TEXF_LINEAR;
					substituted = true;
				}
		anisotropic += substituted ? 1 : 0;
		if (!programRefusal.empty())
		{
			printf( "refused %s: %s\n", names[n].c_str(), programRefusal.c_str() );
			++refused;
			continue;
		}
		std::vector<Vertex> vertices( h.vertexCount );
		for (uint32_t v = 0; v < h.vertexCount; ++v)
			decodeVertex( layout, &cap.vertices[(size_t)v * h.stride], vertices[v] );
		if (vp != NULL && !cap.prog.elements.empty())
		{
			// the vertex program reads the declaration's elements from each vertex's own bytes
			unsigned present = 0;
			for (uint32_t v = 0; v < h.vertexCount; ++v)
			{
				unsigned p = 0;
				if (!declarationInputs( &cap.vertices[(size_t)v * h.stride], &cap.prog.elements[0],
						(int)cap.prog.elements.size(), vertices[v].programInput, p, error ))
				{
					programRefusal = error;
					break;
				}
				present = p;
			}
			if (!programRefusal.empty())
			{
				printf( "refused %s: the declaration: %s\n", names[n].c_str(), programRefusal.c_str() );
				++refused;
				continue;
			}
			state.programInputsGiven = true;
			state.programInputPresent = present;
		}
		Target target;
		// A8R8G8B8 keeps alpha; X8R8G8B8 reads destination alpha as 1 (D3DFORMAT)
		target.create( (int)h.targetWidth, (int)h.targetHeight, h.targetFormat != 22 );
		target.clear( colorFromD3D( 0xFF3F2F1Fu ), 1.0, 0 );
		Report report;
		const int count = h.indexCount ? (int)h.indexCount : (int)h.vertexCount;
		const bool ok = draw( state, (int)h.primitiveType, vertices.empty() ? NULL : &vertices[0], (int)h.vertexCount,
			h.indexCount ? &cap.indices[0] : NULL, count, target, &report, mutations );
		if (!ok)
		{
			printf( "refused %s: %s\n", names[n].c_str(), report.refusals.empty() ? "?" : report.refusals[0].c_str() );
			++refused;
			continue;
		}
		++drawn;
		printf( "drawn %s: v%u %s x%u, fvf 0x%X%s%s, %ld triangles (%ld culled, %ld clipped away), %ld pixels written\n",
			names[n].c_str(), (unsigned)h.version, primitiveName( h.primitiveType ), (unsigned)h.primitiveCount,
			(unsigned)h.fvf, vp ? " vs" : "", pp ? " ps" : "", report.trianglesIn, report.trianglesCulled,
			report.trianglesClippedAway, report.pixelsWritten );
		if (!gpuDir.empty())
		{
			const std::string &gpuName = shiftGpu ? names[(n + 1) % names.size()] : names[n];
			const std::string dump = gpuDir + "/" + gpuName + ".cap.bgra";
			FILE *f = fopen( dump.c_str(), "rb" );
			const size_t bytes = (size_t)h.targetWidth * h.targetHeight * 4;
			std::vector<uint8_t> gpu( bytes );
			const size_t got = f ? fread( &gpu[0], 1, bytes, f ) : 0;
			const bool extra = f && fgetc( f ) != EOF;
			if (f)
				fclose( f );
			if (got != bytes || extra)
			{
				printf( "  no GPU picture: %s %s\n", dump.c_str(), f ? "has the wrong size" : "is missing" );
				++unreadable;
				continue;
			}
			long gpuChanged = 0;
			for (size_t i = 0; i < bytes; i += 4)
			{
				// the clear colour 0xFF3F2F1F is B 0x1F, G 0x2F, R 0x3F, A 0xFF in the dump's byte order
				if (gpu[i] != 0x1F || gpu[i + 1] != 0x2F || gpu[i + 2] != 0x3F || gpu[i + 3] != 0xFF)
					++gpuChanged;
				std::swap( gpu[i], gpu[i + 2] );		// B, G, R, A -> R, G, B, A
			}
			const Comparison c = compare( target, &gpu[0], (int)h.targetWidth * 4 );
			const bool nothing = report.pixelsWritten == 0 && gpuChanged == 0;
			if (!c.passed())
				++disagree;
			else if (nothing)
				++empty;
			else
				++agree;
			printf( "  %s: reference wrote %ld, GPU changed %ld; %ld pixels, %ld exact, %ld in a freedom, %ld outside",
				!c.passed() ? "DISAGREE" : nothing ? "empty" : "agree", report.pixelsWritten, gpuChanged,
				c.pixels, c.exact, c.inFreedom, c.outside );
			if (!c.passed())
				printf( ", worst %.1f/255 at (%d, %d)", c.worst * 255, c.worstX, c.worstY );
			printf( "\n" );
			if (!c.passed())
				print( c, stdout, ("    " + names[n]).c_str() );
		}
	}
	printf( "ffref_judge: %d drawn, %d refused, %d unreadable", drawn, refused, unreadable );
	if (!gpuDir.empty())
		printf( "; %d agree, %d disagree, %d empty (nothing written on either side)", agree, disagree, empty );
	printf( " (of %d captures)\n", (int)names.size() );
	printf( "  %d draws: ANISOTROPIC replayed as LINEAR\n", anisotropic );
	return (unreadable || disagree) ? 1 : 0;
}
