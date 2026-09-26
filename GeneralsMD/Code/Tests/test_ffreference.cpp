/*
 * A3b: FFReference's own tests.  Every expected value below is worked by hand from the formula on the
 * named learn.microsoft.com page (the arithmetic is in the comments), not taken from FFReference's
 * output.  Each Mutation is an armed control: a test that applies it and checks the result MOVES, so
 * a check that could not fail would show.
 *
 * What these cannot see: whether a GPU agrees (A3's harness, against this); whether the NAMED CHOICES
 * in ffreference.h are what D3D9 drivers did (only a Windows machine could say); game draws (A3's
 * harness feeds them).
 */

#include "test_harness.h"

#include "ffreference/ffreference.h"

#include <math.h>
#include <vector>

using namespace FFRef;

namespace {

const double EPS = 1e-9;

Color rgba( double r, double g, double b, double a ) { Color c = { r, g, b, a }; return c; }

Light directional( double dx, double dy, double dz )
{
	Light l;
	memset( &l, 0, sizeof( l ) );
	l.enabled = true;
	l.type = LIGHT_DIRECTIONAL;
	l.direction[0] = dx; l.direction[1] = dy; l.direction[2] = dz;
	l.range = 1000;
	l.attenuation0 = 1;
	return l;
}

Vertex screenVertex( double x, double y, double z, double rhw, Color diffuse )
{
	Vertex v;
	memset( &v, 0, sizeof( v ) );
	v.position[0] = x; v.position[1] = y; v.position[2] = z; v.position[3] = rhw;
	v.diffuse = diffuse;
	return v;
}

Vertex worldVertex( double x, double y, double z, Color diffuse )
{
	Vertex v;
	memset( &v, 0, sizeof( v ) );
	v.position[0] = x; v.position[1] = y; v.position[2] = z;
	v.diffuse = diffuse;
	return v;
}

/// Pretransformed, untextured, unlit, uncull'd: the plainest draw
DrawState screenState( int w, int h )
{
	DrawState s;
	s.setDefaults( w, h );
	s.pretransformed = true;
	s.hasDiffuse = true;
	s.renderState[RS_CULLMODE] = CULL_NONE;
	return s;
}

Target exactTarget( int w, int h )
{
	Target t;
	t.create( w, h );
	t.colorBits = 0;		// hand values are exact; no 8-bit rounding
	t.clear( rgba( 0, 0, 0, 0 ) );
	return t;
}

Color at( const Target &t, int x, int y ) { return t.color[(size_t)y * t.width + x]; }

/// A triangle far larger than the target, so every pixel is well inside it
void fill( const DrawState &s, Target &t, double z, Color c, Report *report = 0, unsigned mutations = 0 )
{
	const Vertex v[3] = { screenVertex( -100, -100, z, 1, c ), screenVertex( 300, -100, z, 1, c ), screenVertex( -100, 300, z, 1, c ) };
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, report, mutations ) );
}

std::vector<uint8_t> toRGBA8( const Target &t )
{
	std::vector<uint8_t> out( (size_t)t.width * t.height * 4 );
	for (size_t i = 0; i < t.color.size(); ++i)
	{
		const Color &c = t.color[i];
		const double ch[4] = { c.r, c.g, c.b, c.a };
		for (int k = 0; k < 4; ++k)
			out[i * 4 + k] = (uint8_t)floor( ch[k] * 255.0 + 0.5 );
	}
	return out;
}

Texture solidLevels( const double *reds, int levels, int size )
{
	Texture t;
	t.type = TEXTURE_2D;
	for (int l = 0; l < levels; ++l)
	{
		TextureLevel level;
		level.width = level.height = size >> l;
		level.texels.assign( (size_t)level.width * level.height, rgba( reds[l], 0, 0, 1 ) );
		t.levels.push_back( level );
	}
	return t;
}

/// 4x4, texel (i, j) = (i/4, j/4, 0, 1)
Texture ramp4()
{
	Texture t;
	t.type = TEXTURE_2D;
	TextureLevel level;
	level.width = level.height = 4;
	for (int j = 0; j < 4; ++j)
		for (int i = 0; i < 4; ++i)
			level.texels.push_back( rgba( i / 4.0, j / 4.0, 0, 1 ) );
	t.levels.push_back( level );
	return t;
}

}  // namespace

// ---- lighting: "Mathematics of Lighting" and subpages ---------------------------------------------

TEST(ffref_lighting_directional_ambient_diffuse)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_AMBIENT] = 0x00202020;		// Ga = 32/255
	s.material.ambient = rgba( 1, 1, 1, 1 );
	s.material.diffuse = rgba( 0.5, 0.5, 0.5, 0.8 );
	s.lights[0] = directional( 0, 0, 1 );			// Ldir = -(0,0,1)
	s.lights[0].diffuse = rgba( 1, 0.5, 0.25, 1 );
	s.lights[0].ambient = rgba( 0.1, 0.1, 0.1, 1 );
	const double P[3] = { 0, 0, 10 }, N[3] = { 0, 0, -1 };	// N.Ldir = 1
	const LitVertex lit = light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) );
	// Ca(Ga + La) + Cd Ld (N.L): r = (32/255 + .1) + .5 * 1, g = .. + .5 * .5, b = .. + .5 * .25
	const double amb = 32.0 / 255.0 + 0.1;
	CHECK_NEAR( lit.diffuse.r, amb + 0.5, EPS );
	CHECK_NEAR( lit.diffuse.g, amb + 0.25, EPS );
	CHECK_NEAR( lit.diffuse.b, amb + 0.125, EPS );
	CHECK_NEAR( lit.diffuse.a, 0.8, EPS );		// the diffuse source's alpha (N5)
	// no normal: N.L = 0, only ambient remains (D3DRS_LIGHTING)
	const LitVertex bare = light( s, P, N, false, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) );
	CHECK_NEAR( bare.diffuse.r, amb, EPS );
}

TEST(ffref_lighting_point_attenuation_and_range)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	Light &l = s.lights[0];
	memset( &l, 0, sizeof( l ) );
	l.enabled = true;
	l.type = LIGHT_POINT;
	l.diffuse = rgba( 1, 1, 1, 1 );
	l.range = 100;
	l.attenuation0 = 1; l.attenuation1 = 0.1; l.attenuation2 = 0.01;
	const double P[3] = { 0, 0, 10 }, N[3] = { 0, 0, -1 };
	// d = 10: 1 / (1 + .1*10 + .01*100) = 1/3; Ldir = norm(Lp - P) = (0,0,-1), N.L = 1
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).diffuse.r, 1.0 / 3.0, EPS );
	// armed: without attenuation it would be 1
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ), MUTATE_NO_ATTENUATION ).diffuse.r, 1.0, EPS );
	l.range = 5;		// beyond the range: Atten = 0
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).diffuse.r, 0.0, EPS );
}

TEST(ffref_lighting_spot_cone)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	Light &l = s.lights[0];
	memset( &l, 0, sizeof( l ) );
	l.enabled = true;
	l.type = LIGHT_SPOT;
	l.diffuse = rgba( 1, 1, 1, 1 );
	l.direction[2] = 1;
	l.range = 100;
	l.attenuation0 = 1;
	l.theta = 0.5; l.phi = 1.0; l.falloff = 1;
	const double cosTheta = cos( 0.25 ), cosPhi = cos( 0.5 );
	struct At
	{
		static double diffuse( const DrawState &s, double rho )
		{
			const double a = acos( rho );
			double P[3] = { 10 * tan( a ), 0, 10 };
			const double len = sqrt( P[0] * P[0] + P[2] * P[2] );
			const double N[3] = { -P[0] / len, 0, -P[2] / len };	// facing the light: N.L = 1
			return light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).diffuse.r;
		}
	};
	// halfway between cos(phi/2) and cos(theta/2): ((rho - cos(phi/2)) / (cos(theta/2) - cos(phi/2)))^1 = .5
	CHECK_NEAR( At::diffuse( s, (cosTheta + cosPhi) / 2 ), 0.5, 1e-9 );
	CHECK_NEAR( At::diffuse( s, cos( 0.1 ) ), 1.0, 1e-9 );		// inside theta/2
	CHECK_NEAR( At::diffuse( s, cos( 0.6 ) ), 0.0, 1e-9 );		// outside phi/2
	l.falloff = 2;
	CHECK_NEAR( At::diffuse( s, (cosTheta + cosPhi) / 2 ), 0.25, 1e-9 );
}

TEST(ffref_lighting_specular_halfway)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_SPECULARENABLE] = 1;
	s.renderState[RS_LOCALVIEWER] = 0;
	const double r2 = sqrt( 0.5 );
	s.lights[0] = directional( -r2, 0, -r2 );		// Ldir = (r2, 0, r2)
	s.lights[0].specular = rgba( 1, 1, 1, 1 );
	s.material.specular = rgba( 0.5, 0.5, 0.5, 1 );
	s.material.power = 2;
	const double P[3] = { 0, 0, 10 }, up[3] = { 0, 0, 1 }, down[3] = { 0, 0, -1 };
	// non-local: H = norm((0,0,1) + Ldir), N.H = cos(22.5 deg); .5 * cos^2 = .5 * .853553...
	const double c = cos( M_PI / 8 );
	CHECK_NEAR( light( s, P, up, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).specular.r, 0.5 * c * c, EPS );
	// local: E = norm(-P) = (0,0,-1), H = norm(E + Ldir) = (.92388, 0, -.38268); N = (0,0,-1): N.H = sin(22.5)
	// and not gated by N.L (which is negative here), N3
	s.renderState[RS_LOCALVIEWER] = 1;
	const double sn = sin( M_PI / 8 );
	const LitVertex local = light( s, P, down, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) );
	CHECK_NEAR( local.specular.r, 0.5 * sn * sn, EPS );
	CHECK_NEAR( local.diffuse.r, 0.0, EPS );
	// SPECULARENABLE off: no specular lighting (N6)
	s.renderState[RS_SPECULARENABLE] = 0;
	CHECK_NEAR( light( s, P, down, true, rgba( 1, 1, 1, 1 ), rgba( 1, 1, 1, 1 ) ).specular.r, 0.0, EPS );
}

TEST(ffref_lighting_material_sources)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.hasDiffuse = s.hasSpecular = true;
	s.renderState[RS_EMISSIVEMATERIALSOURCE] = MCS_COLOR2;
	const double P[3] = { 0, 0, 10 }, N[3] = { 0, 0, -1 };
	// no lights, no ambient: the lit diffuse is the emissive, here the vertex specular colour
	LitVertex lit = light( s, P, N, true, rgba( 0.9, 0.9, 0.9, 0.6 ), rgba( 0.2, 0.3, 0.4, 1 ) );
	CHECK_NEAR( lit.diffuse.r, 0.2, EPS );
	CHECK_NEAR( lit.diffuse.g, 0.3, EPS );
	CHECK_NEAR( lit.diffuse.b, 0.4, EPS );
	CHECK_NEAR( lit.diffuse.a, 0.6, EPS );		// diffuse source COLOR1: the vertex's alpha
	// COLORVERTEX off: every source is the material's (emissive 0, diffuse alpha 1)
	s.renderState[RS_COLORVERTEX] = 0;
	lit = light( s, P, N, true, rgba( 0.9, 0.9, 0.9, 0.6 ), rgba( 0.2, 0.3, 0.4, 1 ) );
	CHECK_NEAR( lit.diffuse.r, 0.0, EPS );
	CHECK_NEAR( lit.diffuse.a, 1.0, EPS );
	// a source naming a colour the vertex lacks falls back to the material ("Specular Lighting")
	s.renderState[RS_COLORVERTEX] = 1;
	s.hasSpecular = false;
	s.material.emissive = rgba( 0.7, 0, 0, 1 );
	CHECK_NEAR( light( s, P, N, true, rgba( 1, 1, 1, 1 ), rgba( 0.2, 0.3, 0.4, 1 ) ).diffuse.r, 0.7, EPS );
}

TEST(ffref_normals_by_the_inverse_transpose)
{
	// world shears x by y (x' = x + y): the plane x = 0 becomes x' = y', whose normal is (1,-1,0)/r2.
	// The inverse-transpose takes (1,0,0) there; the plain matrix would not.  Light along that normal.
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	s.renderState[RS_NORMALIZENORMALS] = 1;
	s.world.m[1][0] = 1.0;
	const double r2 = sqrt( 0.5 );
	s.lights[0] = directional( -r2, r2, 0 );		// Ldir = (r2, -r2, 0)
	s.lights[0].diffuse = rgba( 1, 1, 1, 1 );
	s.hasNormal = true;
	Vertex v[3] = { worldVertex( -10, -10, 0.5, rgba( 0, 0, 0, 0 ) ), worldVertex( -10, 30, 0.5, rgba( 0, 0, 0, 0 ) ),
		worldVertex( 30, -10, 0.5, rgba( 0, 0, 0, 0 ) ) };
	for (int i = 0; i < 3; ++i)
		v[i].normal[0] = 1.0;
	Target t = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 2, 2 ).r, 1.0, EPS );		// N.L = 1; the plain matrix's normal would give r2
}

// ---- fog: "Fog Formulas", "Vertex Fog", "Pixel Fog" -----------------------------------------------

TEST(ffref_fog_formulas)
{
	CHECK_NEAR( fogFactor( FOG_LINEAR, 35, 10, 110, 0 ), 0.75, EPS );		// (110 - 35) / 100
	CHECK_NEAR( fogFactor( FOG_LINEAR, 200, 10, 110, 0 ), 0.0, EPS );		// clamped
	CHECK_NEAR( fogFactor( FOG_EXP, 2, 0, 0, 0.5 ), exp( -1.0 ), EPS );
	CHECK_NEAR( fogFactor( FOG_EXP2, 1, 0, 0, 0.5 ), exp( -0.25 ), EPS );
	CHECK_NEAR( fogFactor( FOG_LINEAR, 5, 5, 5, 0 ), 1.0, EPS );			// end == start, N8
	CHECK_NEAR( fogFactor( FOG_NONE, 5, 0, 1, 1 ), 1.0, EPS );
}

TEST(ffref_fog_vertex_and_table)
{
	DrawState s;
	s.setDefaults( 4, 4 );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	s.renderState[RS_LIGHTING] = 0;
	s.hasDiffuse = true;
	s.renderState[RS_FOGENABLE] = 1;
	s.renderState[RS_FOGSTART] = floatBits( 0.0f );
	s.renderState[RS_FOGEND] = floatBits( 1.0f );
	s.renderState[RS_FOGVERTEXMODE] = FOG_LINEAR;
	const Color white = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { worldVertex( -10, -10, 0.25, white ), worldVertex( -10, 30, 0.25, white ), worldVertex( 30, -10, 0.25, white ) };

	// vertex fog: |z camera| = .25 -> f = .75, fog colour black
	Target t = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.75, EPS );
	CHECK_NEAR( at( t, 1, 1 ).a, 1.0, EPS );		// fog leaves alpha alone (N9)
	// armed: f and 1 - f swapped
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, 0, MUTATE_FOG_REVERSED ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.25, EPS );

	// table fog, affine projection: z-based, z/w = .25
	s.renderState[RS_FOGVERTEXMODE] = FOG_NONE;
	s.renderState[RS_FOGTABLEMODE] = FOG_LINEAR;
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.75, EPS );

	// table fog, perspective projection: w-based, eye distance 10 against end 40 -> .75
	s.projection.m[2][2] = 100.0 / 99.0; s.projection.m[3][2] = -100.0 / 99.0;
	s.projection.m[2][3] = 1.0; s.projection.m[3][3] = 0.0;
	s.renderState[RS_FOGEND] = floatBits( 40.0f );
	const Vertex far[3] = { worldVertex( -300, -300, 10, white ), worldVertex( -300, 900, 10, white ), worldVertex( 900, -300, 10, white ) };
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, far, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.75, 1e-6 );
}

// ---- the cascade: D3DTEXTUREOP, D3DTA ---------------------------------------------------------------

TEST(ffref_texture_ops)
{
	const Color a0 = rgba( 0.25, 0, 0, 0 ), a1 = rgba( 0.2, 0.4, 0.6, 0.8 ), a2 = rgba( 0.5, 0.25, 1.0, 0.5 );
	struct Op { int op; double r; };
	// red channel, arg1 .2, arg2 .5, arg0 .25, blend alpha .25
	const Op ops[] = {
		{ TOP_SELECTARG1, 0.2 }, { TOP_SELECTARG2, 0.5 }, { TOP_MODULATE, 0.1 }, { TOP_MODULATE2X, 0.2 },
		{ TOP_MODULATE4X, 0.4 }, { TOP_ADD, 0.7 }, { TOP_ADDSIGNED, 0.2 }, { TOP_ADDSIGNED2X, 0.4 },
		{ TOP_SUBTRACT, -0.3 }, { TOP_ADDSMOOTH, 0.6 },				// .2 + .5 * .8
		{ TOP_BLENDDIFFUSEALPHA, 0.425 }, { TOP_BLENDTEXTUREALPHA, 0.425 }, { TOP_BLENDFACTORALPHA, 0.425 },
		{ TOP_BLENDCURRENTALPHA, 0.425 },							// .2 * .25 + .5 * .75
		{ TOP_BLENDTEXTUREALPHAPM, 0.575 },							// .2 + .5 * .75
		{ TOP_MODULATEALPHA_ADDCOLOR, 0.6 },						// .2 + .8 * .5
		{ TOP_MODULATECOLOR_ADDALPHA, 0.9 },						// .2 * .5 + .8
		{ TOP_MODULATEINVALPHA_ADDCOLOR, 0.3 },						// .2 * .5 + .2
		{ TOP_MODULATEINVCOLOR_ADDALPHA, 1.2 },						// .8 * .5 + .8
		{ TOP_DOTPRODUCT3, 0.3 },		// 4 * ((-.3)(0) + (-.1)(-.25) + (.1)(.5)) = 4 * .075
		{ TOP_MULTIPLYADD, 0.35 },		// .25 + .2 * .5
		{ TOP_LERP, 0.425 } };			// .25 * .2 + .75 * .5
	for (size_t i = 0; i < sizeof( ops ) / sizeof( ops[0] ); ++i)
	{
		const double got = textureOp( ops[i].op, 0, a0, a1, a2, 0.25 );
		if (fabs( got - ops[i].r ) > EPS)
			printf( "  D3DTOP %d: %.6f, want %.6f\n", ops[i].op, got, ops[i].r );
		CHECK_NEAR( got, ops[i].r, EPS );
	}
	// armed: LERP with ARG1 and ARG2 swapped: .25 * .5 + .75 * .2
	CHECK_NEAR( textureOp( TOP_LERP, 0, a0, a1, a2, 0.25, MUTATE_LERP_SWAPPED ), 0.275, EPS );
}

TEST(ffref_cascade_through_draws)
{
	DrawState s = screenState( 2, 2 );
	Target t = exactTarget( 2, 2 );
	Texture red;
	red.type = TEXTURE_2D;
	TextureLevel one;
	one.width = one.height = 1;
	one.texels.push_back( rgba( 1, 0.5, 0, 0.25 ) );
	red.levels.push_back( one );
	s.texCoordSets = 1;

	// N20: stage 0 MODULATE of TEXTURE with no texture: the cascade is the diffuse
	fill( s, t, 0.5, rgba( 0.3, 0.6, 0.9, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).g, 0.6, EPS );

	// stage 0 texture into TEMP, stage 1 TFACTOR * TEMP into CURRENT
	s.textures[0] = &red;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_RESULTARG] = TA_TEMP;
	s.stageState[1][TSS_COLOROP] = TOP_MODULATE;
	s.stageState[1][TSS_COLORARG1] = TA_TFACTOR;
	s.stageState[1][TSS_COLORARG2] = TA_TEMP;
	s.stageState[1][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[1][TSS_ALPHAARG1] = TA_TEMP | TA_COMPLEMENT;
	s.renderState[RS_TEXTUREFACTOR] = 0xFF808080;
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 0.3, 0.6, 0.9, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 128.0 / 255.0, EPS );			// 128/255 * 1
	CHECK_NEAR( at( t, 0, 0 ).g, 0.5 * 128.0 / 255.0, EPS );
	CHECK_NEAR( at( t, 0, 0 ).a, 0.75, EPS );					// 1 - .25

	// DOTPRODUCT3 as a COLOROP writes alpha too (N17): diffuse (.75, .5, .5) . TFACTOR (1, 128/255, 128/255)
	// = 4 * ((.75-.5)(1-.5) + 0 * (128/255-.5) + 0 * (128/255-.5)) = .5, in RGB and in alpha
	DrawState d = screenState( 2, 2 );
	d.stageState[0][TSS_COLOROP] = TOP_DOTPRODUCT3;
	d.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	d.stageState[0][TSS_COLORARG2] = TA_TFACTOR;
	d.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	d.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;
	d.renderState[RS_TEXTUREFACTOR] = 0xFFFF8080;		// (1, 128/255, 128/255)
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( d, t, 0.5, rgba( 0.75, 0.5, 0.5, 0.1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );
	CHECK_NEAR( at( t, 0, 0 ).b, 0.5, EPS );
	CHECK_NEAR( at( t, 0, 0 ).a, 0.5, EPS );		// not the alpha op's .1

	// PREMODULATE: stage 0 outputs ARG1 (diffuse .5); stage 1's CURRENT is premultiplied by its texture
	DrawState p = screenState( 2, 2 );
	p.texCoordSets = 1;
	p.stageState[0][TSS_COLOROP] = TOP_PREMODULATE;
	p.stageState[0][TSS_COLORARG1] = TA_DIFFUSE;
	p.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	p.stageState[0][TSS_ALPHAARG1] = TA_DIFFUSE;
	p.textures[1] = &red;
	p.stageState[1][TSS_COLOROP] = TOP_SELECTARG1;
	p.stageState[1][TSS_COLORARG1] = TA_CURRENT;
	p.stageState[1][TSS_ALPHAOP] = TOP_SELECTARG1;
	p.stageState[1][TSS_ALPHAARG1] = TA_CURRENT;
	p.stageState[1][TSS_TEXCOORDINDEX] = 0;
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( p, t, 0.5, rgba( 0.5, 0.5, 0.5, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );		// .5 * 1
	CHECK_NEAR( at( t, 0, 0 ).g, 0.25, EPS );		// .5 * .5
	CHECK_NEAR( at( t, 0, 0 ).a, 0.25, EPS );		// 1 * .25
}

TEST(ffref_bump_env_map_offsets_the_next_stage)
{
	DrawState s = screenState( 2, 2 );
	s.texCoordSets = 1;
	Texture bump;
	bump.type = TEXTURE_2D;
	TextureLevel b;
	b.width = b.height = 1;
	b.texels.push_back( rgba( 0.5, 0.25, 0.8, 1 ) );		// du .5, dv .25, L .8
	bump.levels.push_back( b );
	Texture env = ramp4();
	s.textures[0] = &bump;
	s.stageState[0][TSS_COLOROP] = TOP_BUMPENVMAPLUMINANCE;
	s.stageState[0][TSS_BUMPENVMAT00] = floatBits( 0.5f );		// du' = du M00 + dv M10 = .25 + .25 = .5
	s.stageState[0][TSS_BUMPENVMAT10] = floatBits( 1.0f );		// dv' = du M01 + dv M11 = 0
	s.stageState[0][TSS_BUMPENVLSCALE] = floatBits( 1.0f );
	s.stageState[0][TSS_BUMPENVLOFFSET] = floatBits( 0.1f );	// L' = .8 + .1 = .9
	s.textures[1] = &env;
	s.stageState[1][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[1][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[1][TSS_ALPHAARG1] = TA_DIFFUSE;
	s.stageState[1][TSS_TEXCOORDINDEX] = 0;
	Target t = exactTarget( 2, 2 );
	Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
		screenVertex( -100, 300, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
	for (int i = 0; i < 3; ++i)
	{
		v[i].tex[0][0] = 0.0625;	// + .5 = .5625: texel 2 (M01 in M10's place would give .3125, texel 1)
		v[i].tex[0][1] = 0.125;
	}
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5 * 0.9, EPS );		// texel 2 (r = 2/4), times L'
}

// ---- sampling ---------------------------------------------------------------------------------------

TEST(ffref_sampling_point_linear_and_address_modes)
{
	const Texture t = ramp4();
	DrawState s;
	s.setDefaults( 1, 1 );
	uint32_t *sp = s.samplerState[0];
	// point: texel floor(u * 4)
	CHECK_NEAR( sample( t, sp, 0.375, 0.1, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, 0.49, 0.1, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, 0.5, 0.1, -1 ).r, 0.5, EPS );
	// armed: texel centres at i, not i + .5: floor(1.5 + .5) = 2
	CHECK_NEAR( sample( t, sp, 0.375, 0.1, -1, MUTATE_TEXEL_CORNER ).r, 0.5, EPS );
	// linear: at a centre, the texel; halfway between centres .375 and .625, the mean
	sp[SAMP_MAGFILTER] = sp[SAMP_MINFILTER] = TEXF_LINEAR;
	CHECK_NEAR( sample( t, sp, 0.375, 0.375, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, 0.5, 0.375, -1 ).r, 0.375, EPS );
	// at u = 0 the taps are texels -1 and 0, half each
	sp[SAMP_ADDRESSU] = TADDRESS_WRAP;
	CHECK_NEAR( sample( t, sp, 0.0, 0.375, -1 ).r, (0.75 + 0.0) / 2, EPS );
	sp[SAMP_ADDRESSU] = TADDRESS_CLAMP;
	CHECK_NEAR( sample( t, sp, 0.0, 0.375, -1 ).r, 0.0, EPS );
	sp[SAMP_ADDRESSU] = TADDRESS_MIRROR;
	CHECK_NEAR( sample( t, sp, 0.0, 0.375, -1 ).r, 0.0, EPS );
	CHECK_NEAR( sample( t, sp, 1.0, 0.375, -1 ).r, 0.75, EPS );		// taps 3 and 4 -> 3, 3
	sp[SAMP_ADDRESSU] = TADDRESS_BORDER;
	sp[SAMP_BORDERCOLOR] = 0xFF0000FF;		// blue
	const Color border = sample( t, sp, 1.0, 0.375, -1 );		// texel 3 and the border, half each
	CHECK_NEAR( border.r, 0.375, EPS );
	CHECK_NEAR( border.b, 0.5, EPS );
	// MIRRORONCE, point: floor(-1.2) = -2 mirrors to 1; far past the end clamps to 3
	sp[SAMP_ADDRESSU] = TADDRESS_MIRRORONCE;
	sp[SAMP_MAGFILTER] = sp[SAMP_MINFILTER] = TEXF_POINT;
	CHECK_NEAR( sample( t, sp, -0.3, 0.1, -1 ).r, 0.25, EPS );
	CHECK_NEAR( sample( t, sp, -3.0, 0.1, -1 ).r, 0.75, EPS );
	// rows run down from v = 0
	CHECK_NEAR( sample( t, sp, 0.1, 0.9, -1 ).g, 0.75, EPS );
}

TEST(ffref_mip_selection)
{
	const double reds[3] = { 0.0, 0.5, 1.0 };
	const Texture t = solidLevels( reds, 3, 4 );
	DrawState s;
	s.setDefaults( 1, 1 );
	uint32_t *sp = s.samplerState[0];
	sp[SAMP_MIPFILTER] = TEXF_NONE;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 1.5 ).r, 0.0, EPS );		// no mipmapping: level 0
	sp[SAMP_MIPFILTER] = TEXF_POINT;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 0.4 ).r, 0.0, EPS );		// nearest: 0
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 0.6 ).r, 0.5, EPS );		// nearest: 1
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 1.6 ).r, 1.0, EPS );		// nearest: 2
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 9.0 ).r, 1.0, EPS );		// clamped to the last
	sp[SAMP_MIPFILTER] = TEXF_LINEAR;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 0.25 ).r, 0.125, EPS );	// .75 * 0 + .25 * .5
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, 1.5 ).r, 0.75, EPS );
	sp[SAMP_MAXMIPLEVEL] = 1;
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, -1.0 ).r, 0.5, EPS );		// magnified, from level MAXMIPLEVEL
	sp[SAMP_MAXMIPLEVEL] = 0;
	sp[SAMP_MIPFILTER] = TEXF_POINT;
	sp[SAMP_MIPMAPLODBIAS] = floatBits( 1.0f );
	CHECK_NEAR( sample( t, sp, 0.5, 0.5, -0.5 ).r, 0.5, EPS );		// -.5 + 1 = .5: minified, level round(.5) = 1
}

TEST(ffref_lod_from_screen_derivatives)
{
	// An 8x8 texture over an 8x8-pixel square: one texel a pixel, lambda 0.  Over a 4x4 square the
	// same texture is minified 2x: lambda 1, mip level 1.
	Texture t;
	t.type = TEXTURE_2D;
	const double reds[4] = { 0.0, 0.5, 1.0, 1.0 };
	t = solidLevels( reds, 4, 8 );
	DrawState s = screenState( 8, 8 );
	s.texCoordSets = 1;
	s.textures[0] = &t;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.samplerState[0][SAMP_MIPFILTER] = TEXF_POINT;
	struct Quad
	{
		static void draw( const DrawState &s, Target &t, double size, Report *report )
		{
			Vertex v[4];
			const double xy[4][2] = { { 0, 0 }, { size, 0 }, { size, size }, { 0, size } };
			for (int i = 0; i < 4; ++i)
			{
				v[i] = screenVertex( xy[i][0], xy[i][1], 0.5, 1, rgba( 1, 1, 1, 1 ) );
				v[i].tex[0][0] = xy[i][0] / size;
				v[i].tex[0][1] = xy[i][1] / size;
			}
			const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
			CHECK( FFRef::draw( s, PT_TRIANGLELIST, v, 4, idx, 6, t, report ) );
		}
	};
	Target tg = exactTarget( 8, 8 );
	Report r;
	Quad::draw( s, tg, 8, &r );
	CHECK_NEAR( r.minLod, 0.0, 1e-6 );
	CHECK_NEAR( r.maxLod, 0.0, 1e-6 );
	tg.clear( rgba( 0, 0, 0, 0 ) );
	Report r2;
	Quad::draw( s, tg, 4, &r2 );
	CHECK_NEAR( r2.minLod, 1.0, 1e-6 );
	CHECK_NEAR( at( tg, 1, 1 ).r, 0.5, EPS );
}

// ---- rasterisation: "Rasterization Rules", "Directly Mapping Texels to Pixels" ---------------------

TEST(ffref_raster_centres_and_top_left_rule)
{
	// The square [0,2]x[0,2] as two triangles sharing the diagonal, added ONE/ONE at .25: centres
	// (0,0), (1,0), (0,1), (1,1) each exactly once (left and top edges in, right and bottom out, and
	// the diagonal's centres go to one triangle only).
	DrawState s = screenState( 4, 4 );
	s.renderState[RS_ALPHABLENDENABLE] = 1;
	s.renderState[RS_SRCBLEND] = BLEND_ONE;
	s.renderState[RS_DESTBLEND] = BLEND_ONE;
	s.renderState[RS_ZENABLE] = ZB_FALSE;
	const Color q = rgba( 0.25, 0.25, 0.25, 0.25 );
	const Vertex v[6] = { screenVertex( 0, 0, 0.5, 1, q ), screenVertex( 2, 0, 0.5, 1, q ), screenVertex( 2, 2, 0.5, 1, q ),
		screenVertex( 0, 0, 0.5, 1, q ), screenVertex( 2, 2, 0.5, 1, q ), screenVertex( 0, 2, 0.5, 1, q ) };
	Target t = exactTarget( 4, 4 );
	Report r;
	CHECK( draw( s, PT_TRIANGLELIST, v, 6, 0, 6, t, &r ) );
	int wrong = 0;
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
		{
			const double want = (x < 2 && y < 2) ? 0.25 : 0.0;
			if (fabs( at( t, x, y ).r - want ) > EPS)
			{
				printf( "  pixel %d,%d: %.3f, want %.3f\n", x, y, at( t, x, y ).r, want );
				++wrong;
			}
		}
	CHECK_EQ( wrong, 0 );
	CHECK_EQ( r.pixelsWritten, 4L );
	// armed: centres on any edge counted in: column 2 and row 2 join, the diagonal's twice
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 6, 0, 6, t, 0, MUTATE_TOP_LEFT_OFF ) );
	CHECK_NEAR( at( t, 2, 1 ).r, 0.25, EPS );
	CHECK_NEAR( at( t, 1, 1 ).r, 0.5, EPS );
}

TEST(ffref_raster_half_pixel_is_detected)
{
	// [0.25, 2.25]^2: D3D9's centres 1 and 2 are inside, D3D10's .5 and 1.5 (pixels 0 and 1) instead.
	// Split on the diagonal x + y = 2.5, which no centre lies on, so no pixel is an edge freedom.
	DrawState s = screenState( 4, 4 );
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[4] = { screenVertex( 0.25, 0.25, 0.5, 1, w ), screenVertex( 2.25, 0.25, 0.5, 1, w ),
		screenVertex( 2.25, 2.25, 0.5, 1, w ), screenVertex( 0.25, 2.25, 0.5, 1, w ) };
	const uint32_t idx[6] = { 0, 1, 3, 1, 2, 3 };
	Target t = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, t ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK_NEAR( at( t, 2, 2 ).r, 1.0, EPS );
	Target m = exactTarget( 4, 4 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, m, 0, MUTATE_RASTER_HALF_PIXEL ) );
	CHECK_NEAR( at( m, 0, 0 ).r, 1.0, EPS );
	CHECK_NEAR( at( m, 2, 2 ).r, 0.0, EPS );
	// and compare() calls it a failure, not a freedom
	t.colorBits = 8;
	const std::vector<uint8_t> gpu = toRGBA8( m );
	const Comparison c = compare( t, &gpu[0], 16 );
	print( c, stdout, "half-pixel raster against D3D9's" );
	CHECK( !c.passed() );
	CHECK_EQ( c.outside, 6L );		// the two L-shapes that differ: 3 + 3 pixels
}

TEST(ffref_perspective_correct_interpolation)
{
	// (0,0) w 1 red 0, (8,0) w 4 red 1, (0,8) w 1 red 0.  At pixel (4,1) the screen weights are
	// .375, .5, .125; divided by w: .375, .125, .125 of .625, so red = .125 / .625 = .2 (linear: .5)
	DrawState s = screenState( 8, 8 );
	const Vertex v[3] = { screenVertex( 0, 0, 0.5, 1, rgba( 0, 0, 0, 1 ) ), screenVertex( 8, 0, 0.5, 0.25, rgba( 1, 0, 0, 1 ) ),
		screenVertex( 0, 8, 0.5, 1, rgba( 0, 0, 0, 1 ) ) };
	Target t = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 4, 1 ).r, 0.2, EPS );
	Target m = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, m, 0, MUTATE_SCREEN_LINEAR ) );	// armed
	CHECK_NEAR( at( m, 4, 1 ).r, 0.5, EPS );
}

TEST(ffref_flat_shading_first_vertex)
{
	DrawState s = screenState( 8, 8 );
	s.renderState[RS_SHADEMODE] = SHADE_FLAT;
	const Vertex v[4] = { screenVertex( 0, 0, 0.5, 1, rgba( 0.1, 0, 0, 1 ) ), screenVertex( 8, 0, 0.5, 1, rgba( 0.9, 0, 0, 1 ) ),
		screenVertex( 0, 8, 0.5, 1, rgba( 0.7, 0, 0, 1 ) ), screenVertex( 8, 8, 0.5, 1, rgba( 0.5, 0, 0, 1 ) ) };
	Target t = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 2, 2 ).r, 0.1, EPS );		// list: vertex 3i
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t ) );
	CHECK_NEAR( at( t, 6, 6 ).r, 0.9, EPS );		// strip triangle 1 (v1, v2, v3): vertex 1
	t.clear( rgba( 0, 0, 0, 0 ) );
	const Vertex fan[4] = { v[0], v[1], v[3], v[2] };
	CHECK( draw( s, PT_TRIANGLEFAN, fan, 4, 0, 4, t ) );
	CHECK_NEAR( at( t, 2, 6 ).r, 0.5, EPS );		// fan triangle 1 (0, 2, 3): vertex 2 is fan[2] = v3
	// a strip's odd triangles are wound the other way round and still face the same way (N12): with
	// counterclockwise culling both triangles of this clockwise strip are drawn
	s.renderState[RS_CULLMODE] = CULL_CCW;
	t.clear( rgba( 0, 0, 0, 0 ) );
	Report r;
	CHECK( draw( s, PT_TRIANGLESTRIP, v, 4, 0, 4, t, &r ) );
	CHECK_EQ( r.trianglesCulled, 0L );
	CHECK_NEAR( at( t, 6, 6 ).r, 0.9, EPS );
}

TEST(ffref_near_plane_clipping_and_culling)
{
	// clip z = .5 x (w = 1): z >= 0 keeps x >= 0, which is screen columns 4..7 of an 8-wide viewport
	DrawState s;
	s.setDefaults( 8, 8 );
	s.renderState[RS_LIGHTING] = 0;
	s.hasDiffuse = true;
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { worldVertex( -1, -1, -0.5, w ), worldVertex( 3, -1, 1.5, w ), worldVertex( -1, 3, -0.5, w ) };
	Target t = exactTarget( 8, 8 );
	Report culled;
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &culled ) );	// counterclockwise on screen: culled by default
	CHECK_EQ( culled.trianglesCulled, 1L );
	CHECK_EQ( culled.pixelsWritten, 0L );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	Report r;
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &r ) );
	CHECK_EQ( r.pixelsWritten, 32L );
	CHECK_NEAR( at( t, 3, 4 ).r, 0.0, EPS );
	CHECK_NEAR( at( t, 4, 4 ).r, 1.0, EPS );		// on the clip edge x = 0: a left edge, in
	CHECK_NEAR( t.depth[4 * 8 + 6], 0.25, EPS );	// X = 6: x = .5, z = .25
}

// ---- the frame buffer ------------------------------------------------------------------------------

TEST(ffref_specular_add)
{
	DrawState s = screenState( 2, 2 );
	s.hasSpecular = true;
	s.renderState[RS_SPECULARENABLE] = 1;
	Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 0.5, 0.5, 0.5, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 0.5, 0.5, 0.5, 1 ) ),
		screenVertex( -100, 300, 0.5, 1, rgba( 0.5, 0.5, 0.5, 1 ) ) };
	for (int i = 0; i < 3; ++i)
		v[i].specular = rgba( 0.25, 0.75, 0, 0.3 );
	Target t = exactTarget( 2, 2 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.75, EPS );
	CHECK_NEAR( at( t, 0, 0 ).g, 1.0, EPS );		// saturated
	CHECK_NEAR( at( t, 0, 0 ).a, 1.0, EPS );		// RGB only
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, 0, MUTATE_NO_SPECULAR_ADD ) );	// armed
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );
}

TEST(ffref_blend_factors_and_ops)
{
	DrawState s;
	s.setDefaults( 1, 1 );
	uint32_t *rs = s.renderState;
	const Color src = rgba( 0.8, 0.6, 0.4, 0.25 ), dst = rgba( 0.2, 0.4, 0.6, 0.5 );
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8, EPS );		// blending off: the source
	rs[RS_ALPHABLENDENABLE] = 1;
	rs[RS_SRCBLEND] = BLEND_SRCALPHA; rs[RS_DESTBLEND] = BLEND_INVSRCALPHA;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 0.25 + 0.2 * 0.75, EPS );
	CHECK_NEAR( blend( s, src, dst, true ).a, 0.25 * 0.25 + 0.5 * 0.75, EPS );
	CHECK_NEAR( blend( s, src, dst, true, MUTATE_BLEND_SWAPPED ).r, 0.8 * 0.75 + 0.2 * 0.25, EPS );	// armed
	rs[RS_SRCBLEND] = BLEND_BOTHSRCALPHA; rs[RS_DESTBLEND] = BLEND_ZERO;		// DESTBLEND overridden
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 0.25 + 0.2 * 0.75, EPS );
	rs[RS_SRCBLEND] = BLEND_SRCALPHASAT; rs[RS_DESTBLEND] = BLEND_ZERO;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 0.25, EPS );			// min(.25, 1 - .5)
	CHECK_NEAR( blend( s, src, dst, true ).a, 0.25, EPS );				// alpha factor 1
	rs[RS_SRCBLEND] = BLEND_DESTCOLOR;
	CHECK_NEAR( blend( s, src, dst, true ).g, 0.6 * 0.4, EPS );
	rs[RS_SRCBLEND] = BLEND_DESTALPHA;
	CHECK_NEAR( blend( s, src, dst, false ).r, 0.8, EPS );		// X8 target: destination alpha 1
	rs[RS_SRCBLEND] = BLEND_BLENDFACTOR; rs[RS_BLENDFACTOR] = 0x00800000;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8 * 128.0 / 255.0, EPS );
	rs[RS_SRCBLEND] = BLEND_ONE; rs[RS_DESTBLEND] = BLEND_ONE;
	rs[RS_BLENDOP] = BLENDOP_SUBTRACT;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.6, EPS );
	rs[RS_BLENDOP] = BLENDOP_REVSUBTRACT;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.0, EPS );		// clamped
	CHECK_NEAR( blend( s, src, dst, true ).b, 0.2, EPS );
	rs[RS_BLENDOP] = BLENDOP_MIN;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.2, EPS );
	rs[RS_BLENDOP] = BLENDOP_MAX;
	CHECK_NEAR( blend( s, src, dst, true ).r, 0.8, EPS );
	rs[RS_BLENDOP] = BLENDOP_ADD;
	rs[RS_SEPARATEALPHABLENDENABLE] = 1;
	rs[RS_SRCBLENDALPHA] = BLEND_ZERO; rs[RS_DESTBLENDALPHA] = BLEND_ONE;
	CHECK_NEAR( blend( s, src, dst, true ).a, 0.5, EPS );
	CHECK_NEAR( blend( s, src, dst, true ).r, 1.0, EPS );		// .8 + .2
}

TEST(ffref_compare_funcs)
{
	CHECK( !compareFunc( CMP_NEVER, 1, 0 ) );
	CHECK( compareFunc( CMP_LESS, 0.2, 0.3 ) && !compareFunc( CMP_LESS, 0.3, 0.3 ) );
	CHECK( compareFunc( CMP_EQUAL, 0.3, 0.3 ) && !compareFunc( CMP_EQUAL, 0.3, 0.4 ) );
	CHECK( compareFunc( CMP_LESSEQUAL, 0.3, 0.3 ) && !compareFunc( CMP_LESSEQUAL, 0.4, 0.3 ) );
	CHECK( compareFunc( CMP_GREATER, 0.4, 0.3 ) && !compareFunc( CMP_GREATER, 0.3, 0.3 ) );
	CHECK( compareFunc( CMP_NOTEQUAL, 0.4, 0.3 ) && !compareFunc( CMP_NOTEQUAL, 0.3, 0.3 ) );
	CHECK( compareFunc( CMP_GREATEREQUAL, 0.3, 0.3 ) && !compareFunc( CMP_GREATEREQUAL, 0.2, 0.3 ) );
	CHECK( compareFunc( CMP_ALWAYS, 0, 1 ) );
}

TEST(ffref_alpha_test)
{
	DrawState s = screenState( 2, 2 );
	s.renderState[RS_ALPHATESTENABLE] = 1;
	s.renderState[RS_ALPHAFUNC] = CMP_GREATER;
	s.renderState[RS_ALPHAREF] = 0x7780;		// low 8 bits: 128
	Target t = exactTarget( 2, 2 );
	fill( s, t, 0.5, rgba( 1, 1, 1, 0.6 ) );		// 153 > 128
	CHECK_NEAR( at( t, 0, 0 ).r, 1.0, EPS );
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 1, 1, 1, 0.4 ) );		// 102: rejected
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK_EQ( t.zones[0], 0u );
	// at the reference exactly: nominally rejected, but inside the alpha-test freedom
	t.clear( rgba( 0, 0, 0, 0 ) );
	fill( s, t, 0.5, rgba( 1, 1, 1, 128.0 / 255.0 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK( (t.zones[0] & ZONE_ALPHA_TEST) != 0 );
	CHECK_NEAR( t.hi[0].r, 1.0, EPS );
}

TEST(ffref_depth_and_stencil)
{
	DrawState s = screenState( 2, 2 );
	Target t = exactTarget( 2, 2 );
	fill( s, t, 0.5, rgba( 0.5, 0, 0, 1 ) );
	CHECK_NEAR( t.depth[0], 0.5, EPS );
	s.renderState[RS_ZFUNC] = CMP_LESS;
	fill( s, t, 0.7, rgba( 0.9, 0, 0, 1 ) );		// behind: rejected
	CHECK_NEAR( at( t, 0, 0 ).r, 0.5, EPS );
	fill( s, t, 0.3, rgba( 0.1, 0, 0, 1 ) );		// in front
	CHECK_NEAR( at( t, 0, 0 ).r, 0.1, EPS );
	s.renderState[RS_ZWRITEENABLE] = 0;
	fill( s, t, 0.2, rgba( 0.2, 0, 0, 1 ) );
	CHECK_NEAR( t.depth[0], 0.3, EPS );			// tested, not written
	s.renderState[RS_ZWRITEENABLE] = 1;
	// depth bias: .9 + (-.8) = .1 passes against .3
	s.renderState[RS_DEPTHBIAS] = floatBits( -0.8f );
	fill( s, t, 0.9, rgba( 0.3, 0, 0, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.3, EPS );
	CHECK_NEAR( t.depth[0], 0.9 + (double)-0.8f, 1e-7 );
	s.renderState[RS_DEPTHBIAS] = 0;

	// stencil: REPLACE 3, then EQUAL 3 passes and EQUAL 2 does not
	s.renderState[RS_ZENABLE] = ZB_FALSE;
	s.renderState[RS_STENCILENABLE] = 1;
	s.renderState[RS_STENCILPASS] = STENCILOP_REPLACE;
	s.renderState[RS_STENCILREF] = 3;
	fill( s, t, 0.5, rgba( 0.4, 0, 0, 1 ) );
	CHECK_EQ( t.stencil[0], 3u );
	s.renderState[RS_STENCILPASS] = STENCILOP_KEEP;
	s.renderState[RS_STENCILFUNC] = CMP_EQUAL;
	s.renderState[RS_STENCILREF] = 2;
	s.renderState[RS_STENCILFAIL] = STENCILOP_INVERT;
	fill( s, t, 0.5, rgba( 0.6, 0, 0, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.4, EPS );
	CHECK_EQ( t.stencil[0], 252u );		// ~3 in 8 bits
	struct Op { int op; uint32_t from, to; };
	const Op ops[] = { { STENCILOP_INCRSAT, 255, 255 }, { STENCILOP_INCR, 255, 0 }, { STENCILOP_DECRSAT, 0, 0 },
		{ STENCILOP_DECR, 0, 255 }, { STENCILOP_ZERO, 9, 0 }, { STENCILOP_INVERT, 0x0F, 0xF0 } };
	s.renderState[RS_STENCILFUNC] = CMP_ALWAYS;
	for (size_t i = 0; i < sizeof( ops ) / sizeof( ops[0] ); ++i)
	{
		t.clear( rgba( 0, 0, 0, 0 ), 1.0, ops[i].from );
		s.renderState[RS_STENCILPASS] = ops[i].op;
		fill( s, t, 0.5, rgba( 1, 0, 0, 1 ) );
		CHECK_EQ( t.stencil[0], ops[i].to );
	}
	// write mask: only the low nibble changes
	t.clear( rgba( 0, 0, 0, 0 ), 1.0, 0xA5 );
	s.renderState[RS_STENCILPASS] = STENCILOP_ZERO;
	s.renderState[RS_STENCILWRITEMASK] = 0x0F;
	fill( s, t, 0.5, rgba( 1, 0, 0, 1 ) );
	CHECK_EQ( t.stencil[0], 0xA0u );
	// two-sided: a counterclockwise triangle takes the CCW operations
	s.renderState[RS_STENCILWRITEMASK] = 0xFFFFFFFF;
	s.renderState[RS_TWOSIDEDSTENCILMODE] = 1;
	s.renderState[RS_STENCILPASS] = STENCILOP_INCR;
	s.renderState[RS_CCW_STENCILFUNC] = CMP_ALWAYS;
	s.renderState[RS_CCW_STENCILPASS] = STENCILOP_DECR;
	t.clear( rgba( 0, 0, 0, 0 ), 1.0, 5 );
	const Color c = rgba( 1, 0, 0, 1 );
	const Vertex ccw[3] = { screenVertex( -100, -100, 0.5, 1, c ), screenVertex( -100, 300, 0.5, 1, c ), screenVertex( 300, -100, 0.5, 1, c ) };
	CHECK( draw( s, PT_TRIANGLELIST, ccw, 3, 0, 3, t ) );
	CHECK_EQ( t.stencil[0], 4u );
	fill( s, t, 0.5, c );		// clockwise
	CHECK_EQ( t.stencil[0], 5u );
}

TEST(ffref_write_mask_scissor_viewport)
{
	DrawState s = screenState( 4, 4 );
	Target t = exactTarget( 4, 4 );
	s.renderState[RS_COLORWRITEENABLE] = 0x2 | 0x8;	// green, alpha
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ) );
	CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	CHECK_NEAR( at( t, 0, 0 ).g, 1.0, EPS );
	s.renderState[RS_COLORWRITEENABLE] = 0xF;
	t.clear( rgba( 0, 0, 0, 0 ) );
	s.renderState[RS_SCISSORTESTENABLE] = 1;
	s.scissor.left = 1; s.scissor.top = 1; s.scissor.right = 3; s.scissor.bottom = 2;
	Report r;
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ), &r );
	CHECK_EQ( r.pixelsWritten, 2L );
	CHECK_NEAR( at( t, 1, 1 ).r, 1.0, EPS );
	s.renderState[RS_SCISSORTESTENABLE] = 0;
	t.clear( rgba( 0, 0, 0, 0 ) );
	s.viewport.x = 2; s.viewport.width = 2;
	Report rv;
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ), &rv );
	CHECK_EQ( rv.pixelsWritten, 8L );
	CHECK_NEAR( at( t, 1, 0 ).r, 0.0, EPS );
}

// ---- texture coordinates: D3DTSS_TCI, "Texture Coordinate Transformations" -------------------------

TEST(ffref_texcoord_generation_transform_and_projection)
{
	// camera-space position, scaled by .5 and moved by .5 + 1/16: u = X/8 + 1/16, so pixel X reads
	// texel X of an 8x1 ramp, well clear of the texel edges
	Texture ramp;
	ramp.type = TEXTURE_2D;
	TextureLevel l;
	l.width = 8; l.height = 1;
	for (int i = 0; i < 8; ++i)
		l.texels.push_back( rgba( i / 8.0, 0, 0, 1 ) );
	ramp.levels.push_back( l );
	DrawState s;
	s.setDefaults( 8, 8 );
	s.renderState[RS_CULLMODE] = CULL_NONE;
	s.renderState[RS_LIGHTING] = 0;
	s.textures[0] = &ramp;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.stageState[0][TSS_TEXCOORDINDEX] = TSS_TCI_CAMERASPACEPOSITION;
	s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT2;
	s.textureTransform[0].m[0][0] = 0.5;
	s.textureTransform[0].m[3][0] = 0.5 + 1.0 / 16.0;
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { worldVertex( -10, -10, 0.5, w ), worldVertex( -10, 30, 0.5, w ), worldVertex( 30, -10, 0.5, w ) };
	Target t = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );		// x = X/4 - 1, u = x/2 + .5 + 1/16
	CHECK_NEAR( at( t, 5, 3 ).r, 5.0 / 8.0, EPS );
	CHECK_NEAR( at( t, 2, 3 ).r, 2.0 / 8.0, EPS );

	// projected: (s, t, q) = (.6, .2, 2) passed through, COUNT3 | PROJECTED: u = .3 -> texel 2
	s.texCoordSets = 1;
	s.texCoordSize[0] = 3;
	s.stageState[0][TSS_TEXCOORDINDEX] = 0;
	s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT3 | TTFF_PROJECTED;
	s.textureTransform[0] = Matrix::identity();
	Vertex p[3] = { v[0], v[1], v[2] };
	for (int i = 0; i < 3; ++i)
	{
		p[i].tex[0][0] = 0.6; p[i].tex[0][1] = 0.2; p[i].tex[0][2] = 2.0;
	}
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, p, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 5, 3 ).r, 2.0 / 8.0, EPS );
	// without PROJECTED the first element is u = .6 -> texel 4
	s.stageState[0][TSS_TEXTURETRANSFORMFLAGS] = TTFF_COUNT3;
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, p, 3, 0, 3, t ) );
	CHECK_NEAR( at( t, 5, 3 ).r, 4.0 / 8.0, EPS );
}

// ---- refusals, and compare()'s classification ------------------------------------------------------

TEST(ffref_refusals_draw_nothing)
{
	struct Case { const char *what; void (*set)( DrawState &, Texture & ); };
	struct Sets
	{
		static void shader( DrawState &s, Texture & ) { s.vertexShaderBound = true; }
		static void spheremap( DrawState &s, Texture &t ) { s.textures[0] = &t; s.stageState[0][TSS_TEXCOORDINDEX] = TSS_TCI_SPHEREMAP; }
		static void alphaDisable( DrawState &s, Texture & ) { s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE; s.stageState[0][TSS_ALPHAOP] = TOP_DISABLE; }
		static void aniso( DrawState &s, Texture &t ) { s.textures[0] = &t; s.samplerState[0][SAMP_MINFILTER] = TEXF_ANISOTROPIC; s.samplerState[0][SAMP_MAXANISOTROPY] = 4; }
		static void cube( DrawState &s, Texture &t ) { t.type = TEXTURE_CUBE; s.textures[0] = &t; }
		static void clipPlane( DrawState &s, Texture & ) { s.renderState[RS_CLIPPLANEENABLE] = 1; }
		static void wrap( DrawState &s, Texture & ) { s.renderState[RS_WRAP0] = 1; }
		static void colourOnlyAlpha( DrawState &s, Texture & ) { s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE; s.stageState[0][TSS_ALPHAOP] = TOP_MODULATEALPHA_ADDCOLOR; }
		static void textureless( DrawState &s, Texture & ) { s.stageState[0][TSS_COLORARG1] = TA_DIFFUSE; s.stageState[0][TSS_COLORARG2] = TA_TEXTURE; }
	};
	const Case cases[] = { { "shader", Sets::shader }, { "spheremap", Sets::spheremap }, { "alpha disable", Sets::alphaDisable },
		{ "anisotropic 4", Sets::aniso }, { "cube", Sets::cube }, { "clip plane", Sets::clipPlane }, { "wrap", Sets::wrap },
		{ "colour-only alpha op", Sets::colourOnlyAlpha }, { "texture read without texture", Sets::textureless } };
	for (size_t i = 0; i < sizeof( cases ) / sizeof( cases[0] ); ++i)
	{
		DrawState s = screenState( 2, 2 );
		s.texCoordSets = 1;
		Texture tex = ramp4();
		cases[i].set( s, tex );
		Target t = exactTarget( 2, 2 );
		const Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
			screenVertex( -100, 300, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
		Report r;
		const bool drew = draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t, &r );
		if (drew || r.refusals.empty())
			printf( "  %s: not refused\n", cases[i].what );
		CHECK( !drew );
		CHECK( !r.refusals.empty() );
		CHECK_NEAR( at( t, 0, 0 ).r, 0.0, EPS );
	}
	// and the plainest draw is not refused
	DrawState s = screenState( 2, 2 );
	Target t = exactTarget( 2, 2 );
	Report r;
	fill( s, t, 0.5, rgba( 1, 1, 1, 1 ), &r );
	CHECK( r.refusals.empty() );
}

TEST(ffref_compare_classifies_freedoms)
{
	// A triangle whose hypotenuse runs through pixel centres: those are ZONE_EDGE; a GPU that drew or
	// skipped them is inside the freedom, one that changed an interior pixel is not.
	DrawState s = screenState( 8, 8 );
	const Color w = rgba( 1, 1, 1, 1 );
	const Vertex v[3] = { screenVertex( 0, 0, 0.5, 1, w ), screenVertex( 8, 0, 0.5, 1, w ), screenVertex( 0, 8, 0.5, 1, w ) };
	Target ref;
	ref.create( 8, 8 );
	ref.clear( rgba( 0, 0, 0, 1 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, ref ) );
	std::vector<uint8_t> gpu = toRGBA8( ref );
	Comparison same = compare( ref, &gpu[0], 32 );
	CHECK_EQ( same.outside, 0L );
	CHECK_EQ( same.exact, 64L );
	// flip every hypotenuse centre (x + y = 8 is outside by the rule; x + y = 7 is not on it)
	int flipped = 0;
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			if (x + y == 8)
			{
				uint8_t *p = &gpu[((size_t)y * 8 + x) * 4];
				p[0] = p[1] = p[2] = 255;
				++flipped;
			}
	// and skip a top-edge centre the rule draws
	uint8_t *top = &gpu[((size_t)0 * 8 + 3) * 4];
	top[0] = top[1] = top[2] = 0;
	++flipped;
	Comparison edge = compare( ref, &gpu[0], 32 );
	print( edge, stdout, "hypotenuse centres drawn, a top-edge centre skipped" );
	CHECK_EQ( edge.outside, 0L );
	CHECK_EQ( edge.inFreedom, (long)flipped );
	CHECK_EQ( edge.zoneCounts[0], (long)flipped );
	int outsideOwnEnvelope = 0;
	for (size_t i = 0; i < ref.color.size(); ++i)
		if (ref.color[i].r < ref.lo[i].r || ref.color[i].r > ref.hi[i].r)
			++outsideOwnEnvelope;
	CHECK_EQ( outsideOwnEnvelope, 0 );
	// an interior pixel off by 10 levels is outside
	gpu[((size_t)2 * 8 + 2) * 4] = 245;
	Comparison bad = compare( ref, &gpu[0], 32 );
	CHECK_EQ( bad.outside, 1L );
	CHECK_EQ( bad.worstX, 2 );
	CHECK_EQ( bad.worstY, 2 );
	// base tolerance: off by 2 levels is exact, by 3 is not
	gpu[((size_t)2 * 8 + 2) * 4] = 253;
	CHECK_EQ( compare( ref, &gpu[0], 32 ).outside, 0L );
	gpu[((size_t)2 * 8 + 2) * 4] = 252;
	CHECK_EQ( compare( ref, &gpu[0], 32 ).outside, 1L );
}

TEST(ffref_bilinear_and_lod_freedoms_widen_the_envelope)
{
	// Bilinear between a black and a white texel: a weight-precision shift of 1/128 texel moves the
	// colour by 1/128, so the envelope spans about +-2 levels around the nominal; ZONE_TEXEL marks it.
	Texture bw;
	bw.type = TEXTURE_2D;
	TextureLevel l;
	l.width = 2; l.height = 1;
	l.texels.push_back( rgba( 0, 0, 0, 1 ) );
	l.texels.push_back( rgba( 1, 1, 1, 1 ) );
	bw.levels.push_back( l );
	DrawState s = screenState( 1, 1 );
	s.texCoordSets = 1;
	s.textures[0] = &bw;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.samplerState[0][SAMP_MAGFILTER] = s.samplerState[0][SAMP_MINFILTER] = TEXF_LINEAR;
	s.samplerState[0][SAMP_ADDRESSU] = TADDRESS_CLAMP;
	Vertex v[3] = { screenVertex( -100, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ), screenVertex( 300, -100, 0.5, 1, rgba( 1, 1, 1, 1 ) ),
		screenVertex( -100, 300, 0.5, 1, rgba( 1, 1, 1, 1 ) ) };
	for (int i = 0; i < 3; ++i)
	{
		v[i].tex[0][0] = 0.5; v[i].tex[0][1] = 0.5;		// halfway between the centres .25 and .75
	}
	Target t;
	t.create( 1, 1 );
	t.colorBits = 0;
	t.clear( rgba( 0, 0, 0, 0 ) );
	CHECK( draw( s, PT_TRIANGLELIST, v, 3, 0, 3, t ) );
	CHECK_NEAR( t.color[0].r, 0.5, EPS );
	CHECK( (t.zones[0] & ZONE_TEXEL) != 0 );
	CHECK_NEAR( t.lo[0].r, 0.5 - 1.0 / 128.0, 1e-9 );
	CHECK_NEAR( t.hi[0].r, 0.5 + 1.0 / 128.0, 1e-9 );
}

TEST(ffref_lod_freedom_reaches_every_level_inside_its_window)
{
	// Trilinear at lambda 1.5 over levels whose red is 0, 1, 0, 0, 0: nominal .5.  The +-0.6 window
	// [0.9, 2.1] has red .9 and 0 at its ends, but passes through level 1 itself at lambda 1, so the
	// envelope must reach 1.0 (F11: endpoints alone were not a superset of the narrower window).
	const double reds[5] = { 0.0, 1.0, 0.0, 0.0, 0.0 };
	Texture t = solidLevels( reds, 5, 16 );
	DrawState s = screenState( 8, 8 );
	s.texCoordSets = 1;
	s.textures[0] = &t;
	s.stageState[0][TSS_COLOROP] = TOP_SELECTARG1;
	s.stageState[0][TSS_ALPHAOP] = TOP_SELECTARG1;
	s.samplerState[0][SAMP_MAGFILTER] = s.samplerState[0][SAMP_MINFILTER] = TEXF_LINEAR;
	s.samplerState[0][SAMP_MIPFILTER] = TEXF_LINEAR;
	const double size = 16.0 / pow( 2.0, 1.5 );		// 2^1.5 texels a pixel: lambda 1.5 everywhere
	Vertex v[4];
	const double xy[4][2] = { { 0, 0 }, { size, 0 }, { size, size }, { 0, size } };
	for (int i = 0; i < 4; ++i)
	{
		v[i] = screenVertex( xy[i][0], xy[i][1], 0.5, 1, rgba( 1, 1, 1, 1 ) );
		v[i].tex[0][0] = xy[i][0] / size;
		v[i].tex[0][1] = xy[i][1] / size;
	}
	const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	Target tg = exactTarget( 8, 8 );
	Report r;
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, tg, &r ) );
	const size_t i = 2 * 8 + 3;		// pixel (3, 2): inside, off the diagonal
	CHECK_NEAR( r.minLod, 1.5, 1e-6 );
	CHECK_NEAR( tg.color[i].r, 0.5, 1e-9 );
	CHECK_NEAR( tg.hi[i].r, 1.0, 1e-9 );
	CHECK_NEAR( tg.lo[i].r, 0.0, 1e-9 );
	CHECK( (tg.zones[i] & ZONE_LOD) != 0 );
	// armed: at the old +-0.2 the window [1.3, 1.7] holds no integer, and the envelope is its ends
	Freedoms narrow;
	narrow.lodDelta = 0.2;
	Target tn = exactTarget( 8, 8 );
	CHECK( draw( s, PT_TRIANGLELIST, v, 4, idx, 6, tn, 0, 0, narrow ) );
	CHECK_NEAR( tn.hi[i].r, 0.7, 1e-9 );		// lambda 1.3: .7 of level 1
	CHECK_NEAR( tn.lo[i].r, 0.3, 1e-9 );
}
