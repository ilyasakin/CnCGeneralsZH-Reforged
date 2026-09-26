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

// The draw's resolve (decision 7, phase A3c): the device's D3D9 state, as set, into D3's generator
// descriptions and the constants their programs read.  It reads the state the way dx11backend reads its
// mirror of the same state (Build_Combiner_Description, Build_Vertex_Description, Upload_Constants), so
// the two backends hand the generators the same thing; dx11backend's own additions (normal maps, the
// shadow map) are not A3's.  And D3D9's documented initial states, which the draw is the first to read.

#include "PosixDevice9.h"
#include "SdlConstants.h"

#include "ffshader.h"
#include "ffvertex.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
// D3D9's initial state, from its documentation of each state.  A float state holds its float's bits.
//-------------------------------------------------------------------------------------------------

static RenderUInt32 float_bits(float value)
{
	RenderUInt32 bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

void PosixDevice9::Set_Default_States()
{
	RenderUInt32 *rs = RenderStates;
	rs[D3DRS_ZENABLE] = Parameters.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
	rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
	rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
	rs[D3DRS_ZWRITEENABLE] = 1;
	rs[D3DRS_ALPHATESTENABLE] = 0;
	rs[D3DRS_LASTPIXEL] = 1;
	rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	rs[D3DRS_CULLMODE] = D3DCULL_CCW;
	rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	rs[D3DRS_ALPHAREF] = 0;
	rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_DITHERENABLE] = 0;
	rs[D3DRS_ALPHABLENDENABLE] = 0;
	rs[D3DRS_FOGENABLE] = 0;
	rs[D3DRS_SPECULARENABLE] = 0;
	rs[D3DRS_FOGCOLOR] = 0;
	rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
	rs[D3DRS_FOGSTART] = float_bits(0.0f);
	rs[D3DRS_FOGEND] = float_bits(1.0f);
	rs[D3DRS_FOGDENSITY] = float_bits(1.0f);
	rs[D3DRS_RANGEFOGENABLE] = 0;
	rs[D3DRS_STENCILENABLE] = 0;
	rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_STENCILREF] = 0;
	rs[D3DRS_STENCILMASK] = 0xFFFFFFFFu;
	rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFFu;
	rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFFu;
	rs[D3DRS_CLIPPING] = 1;
	rs[D3DRS_LIGHTING] = 1;
	rs[D3DRS_AMBIENT] = 0;
	rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
	rs[D3DRS_COLORVERTEX] = 1;
	rs[D3DRS_LOCALVIEWER] = 1;
	rs[D3DRS_NORMALIZENORMALS] = 0;
	rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
	rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
	rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
	rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
	rs[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
	rs[D3DRS_CLIPPLANEENABLE] = 0;
	rs[D3DRS_POINTSIZE] = float_bits(1.0f);
	rs[D3DRS_POINTSIZE_MIN] = float_bits(1.0f);
	rs[D3DRS_POINTSPRITEENABLE] = 0;
	rs[D3DRS_POINTSCALEENABLE] = 0;
	rs[D3DRS_MULTISAMPLEANTIALIAS] = 1;
	rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFFu;
	rs[D3DRS_POINTSIZE_MAX] = float_bits(64.0f);
	rs[D3DRS_COLORWRITEENABLE] = 0x0000000Fu;
	rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
	rs[D3DRS_SCISSORTESTENABLE] = 0;
	rs[D3DRS_SLOPESCALEDEPTHBIAS] = 0;
	rs[D3DRS_DEPTHBIAS] = 0;
	rs[D3DRS_TWOSIDEDSTENCILMODE] = 0;
	rs[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
	rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_SEPARATEALPHABLENDENABLE] = 0;
	rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
	rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
	rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
	rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFFu;

	// Stage 0 modulates the texture with the diffuse colour and takes the texture's alpha; every other
	// stage is disabled.  Each stage reads the coordinate set of its own number.
	for (int stage = 0; stage < TEXTURE_STAGE_COUNT; ++stage) {
		RenderUInt32 *ts = TextureStageStates[stage];
		ts[D3DTSS_COLOROP] = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		ts[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		ts[D3DTSS_COLORARG2] = D3DTA_CURRENT;
		ts[D3DTSS_ALPHAOP] = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		ts[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		ts[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		ts[D3DTSS_TEXCOORDINDEX] = (RenderUInt32)stage;
		ts[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
		ts[D3DTSS_COLORARG0] = D3DTA_CURRENT;
		ts[D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
		ts[D3DTSS_RESULTARG] = D3DTA_CURRENT;
	}
	for (int sampler = 0; sampler < SAMPLER_COUNT; ++sampler) {
		RenderUInt32 *ss = SamplerStates[sampler];
		ss[D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
		ss[D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
		ss[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
		ss[D3DSAMP_BORDERCOLOR] = 0;
		ss[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
		ss[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
		ss[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
		ss[D3DSAMP_MIPMAPLODBIAS] = 0;
		ss[D3DSAMP_MAXMIPLEVEL] = 0;
		ss[D3DSAMP_MAXANISOTROPY] = 1;
	}
}

//-------------------------------------------------------------------------------------------------
// The descriptions.
//-------------------------------------------------------------------------------------------------

void PosixDevice9::Build_Combiner_Description(CombinerDescription &description) const
{
	// memset first, as dx11backend's does: descriptions are compared with memcmp, padding included.
	memset(&description, 0, sizeof(description));

	// The alpha test and the fog are pipeline state under D3D9 and instructions in the program here.
	description.PixelPipeline.AlphaTestEnabled = RenderStates[D3DRS_ALPHATESTENABLE] != 0;
	description.PixelPipeline.AlphaFunction = RenderStates[D3DRS_ALPHAFUNC];
	description.PixelPipeline.FogEnabled = RenderStates[D3DRS_FOGENABLE] != 0;

	if (TextureStageStates[0][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
		// No texturing: D3D9 draws the diffuse colour and its alpha.  The generator ends its chain at a
		// disabled stage and would refuse, so this is the one-stage combiner that means the same.
		CombinerStage &stage = description.Stages[0];
		stage.ColourOperation = D3DTOP_SELECTARG1;
		stage.ColourArgument1 = D3DTA_DIFFUSE;
		stage.ColourArgument2 = D3DTA_CURRENT;
		stage.ColourArgument0 = D3DTA_CURRENT;
		stage.AlphaOperation = D3DTOP_SELECTARG1;
		stage.AlphaArgument1 = D3DTA_DIFFUSE;
		stage.AlphaArgument2 = D3DTA_CURRENT;
		stage.AlphaArgument0 = D3DTA_CURRENT;
		stage.TextureCoordinateIndex = 0;
		stage.TextureBound = false;
		description.StageCount = 1;
		return;
	}

	for (unsigned index = 0; index < MAXIMUM_COMBINER_STAGES; ++index) {
		const RenderUInt32 *ts = TextureStageStates[index];
		if (ts[D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}
		CombinerStage &stage = description.Stages[index];
		stage.ColourOperation = ts[D3DTSS_COLOROP];
		stage.ColourArgument0 = ts[D3DTSS_COLORARG0];
		stage.ColourArgument1 = ts[D3DTSS_COLORARG1];
		stage.ColourArgument2 = ts[D3DTSS_COLORARG2];
		stage.AlphaOperation = ts[D3DTSS_ALPHAOP];
		stage.AlphaArgument0 = ts[D3DTSS_ALPHAARG0];
		stage.AlphaArgument1 = ts[D3DTSS_ALPHAARG1];
		stage.AlphaArgument2 = ts[D3DTSS_ALPHAARG2];
		stage.TextureCoordinateIndex = ts[D3DTSS_TEXCOORDINDEX];
		stage.TextureBound = Textures[index] != NULL;
		description.StageCount = index + 1;
	}
}

bool PosixDevice9::Build_Vertex_Description(VertexPipelineDescription &description) const
{
	memset(&description, 0, sizeof(description));
	description.FVF = FVF;
	description.LightingEnabled = RenderStates[D3DRS_LIGHTING] != 0;
	description.SpecularEnabled = RenderStates[D3DRS_SPECULARENABLE] != 0;
	description.ColourVertexEnabled = RenderStates[D3DRS_COLORVERTEX] != 0;
	description.DiffuseMaterialSource = RenderStates[D3DRS_DIFFUSEMATERIALSOURCE];
	description.AmbientMaterialSource = RenderStates[D3DRS_AMBIENTMATERIALSOURCE];
	description.EmissiveMaterialSource = RenderStates[D3DRS_EMISSIVEMATERIALSOURCE];
	description.SpecularMaterialSource = RenderStates[D3DRS_SPECULARMATERIALSOURCE];

	// The enabled lights, packed down in order: the program declares them contiguously, and
	// Build_Constants packs their fields the same way.
	description.LightCount = 0;
	for (unsigned index = 0; index < LIGHT_COUNT; ++index) {
		if (!LightsEnabled[index]) {
			continue;
		}
		if (description.LightCount == MAXIMUM_VERTEX_LIGHTS) {
			return false;
		}
		description.Lights[description.LightCount].Type = Lights[index].Type;
		++description.LightCount;
	}

	// The same stages the combiner walk takes: the program only carries coordinates for those.  A
	// disabled stage 0 is the one-stage diffuse combiner, which reads no coordinates but has a stage.
	description.StageCount = 0;
	for (unsigned index = 0; index < MAXIMUM_VERTEX_STAGES; ++index) {
		if (index > 0 && TextureStageStates[index][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}
		if (index == 0 && TextureStageStates[0][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			description.StageCount = 1;
			break;
		}
		description.Stages[index].TextureCoordinateIndex = TextureStageStates[index][D3DTSS_TEXCOORDINDEX];
		description.Stages[index].TextureTransformFlags = TextureStageStates[index][D3DTSS_TEXTURETRANSFORMFLAGS];
		description.StageCount = index + 1;
	}

	description.FogEnabled = RenderStates[D3DRS_FOGENABLE] != 0;
	description.FogVertexMode = RenderStates[D3DRS_FOGVERTEXMODE];
	return true;
}

//-------------------------------------------------------------------------------------------------
// The constants.  Row major, D3D9's order, which is how the programs declare their matrices.
//-------------------------------------------------------------------------------------------------

static void multiply(const float left[16], const float right[16], float result[16])
{
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) {
				sum += left[row * 4 + index] * right[index * 4 + column];
			}
			result[row * 4 + column] = sum;
		}
	}
}

// A light is set in world space and lit in camera space: its position and direction go through the
// view matrix once per light, as the fixed-function pipeline carries them.
static void transform_point(const float source[3], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column] + source[1] * matrix[4 + column]
			+ source[2] * matrix[8 + column] + matrix[12 + column];
	}
}

static void transform_direction(const float source[3], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column] + source[1] * matrix[4 + column] + source[2] * matrix[8 + column];
	}
}

// The inverse transpose of the upper three by three, for the normals: the cofactors over the
// determinant.  A singular matrix gives the identity, as dx11backend's does.
static void inverse_transpose(const float s[16], float result[16])
{
	const float a = s[0], b = s[1], c = s[2];
	const float d = s[4], e = s[5], f = s[6];
	const float g = s[8], h = s[9], i = s[10];
	const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	memset(result, 0, 16 * sizeof(float));
	result[0] = result[5] = result[10] = result[15] = 1.0f;
	if (determinant == 0.0f) {
		return;
	}
	const float scale = 1.0f / determinant;
	result[0] = (e * i - f * h) * scale;
	result[1] = (f * g - d * i) * scale;
	result[2] = (d * h - e * g) * scale;
	result[4] = (c * h - b * i) * scale;
	result[5] = (a * i - c * g) * scale;
	result[6] = (b * g - a * h) * scale;
	result[8] = (b * f - c * e) * scale;
	result[9] = (c * d - a * f) * scale;
	result[10] = (a * e - b * d) * scale;
}

static void colour_of(RenderUInt32 argb, float out[4])
{
	out[0] = (float)((argb >> 16) & 0xFF) / 255.0f;
	out[1] = (float)((argb >> 8) & 0xFF) / 255.0f;
	out[2] = (float)(argb & 0xFF) / 255.0f;
	out[3] = (float)((argb >> 24) & 0xFF) / 255.0f;
}

static void colour_value(const D3DCOLORVALUE &colour, float out[4])
{
	out[0] = colour.r;
	out[1] = colour.g;
	out[2] = colour.b;
	out[3] = colour.a;
}

void PosixDevice9::Build_Constants(SdlVertexConstants &vertex, SdlPixelConstants &pixel) const
{
	memset(&vertex, 0, sizeof(vertex));
	memset(&pixel, 0, sizeof(pixel));

	const float *world = &Transforms[D3DTS_WORLD]._11;
	const float *view = &Transforms[D3DTS_VIEW]._11;
	const float *projection = &Transforms[D3DTS_PROJECTION]._11;
	float world_view[16];
	multiply(world, view, world_view);
	multiply(world_view, projection, vertex.WorldViewProjection);
	memcpy(vertex.WorldView, world_view, sizeof(world_view));
	inverse_transpose(world_view, vertex.NormalTransform);
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		memcpy(vertex.TextureMatrix[stage], &Transforms[D3DTS_TEXTURE0 + stage]._11, 16 * sizeof(float));
	}

	colour_value(Material.Ambient, vertex.MaterialAmbient);
	colour_value(Material.Diffuse, vertex.MaterialDiffuse);
	colour_value(Material.Specular, vertex.MaterialSpecular);
	colour_value(Material.Emissive, vertex.MaterialEmissive);
	vertex.MaterialPower[0] = Material.Power;
	colour_of(RenderStates[D3DRS_AMBIENT], vertex.GlobalAmbient);

	// Fog start, end and density are floats carried in DWORD states.
	memcpy(&vertex.FogParameters[0], &RenderStates[D3DRS_FOGSTART], sizeof(float));
	memcpy(&vertex.FogParameters[1], &RenderStates[D3DRS_FOGEND], sizeof(float));
	memcpy(&vertex.FogParameters[2], &RenderStates[D3DRS_FOGDENSITY], sizeof(float));
	vertex.ViewportInverse[0] = Viewport.Width != 0 ? 1.0f / (float)Viewport.Width : 0.0f;
	vertex.ViewportInverse[1] = Viewport.Height != 0 ? 1.0f / (float)Viewport.Height : 0.0f;

	// The enabled lights in the order Build_Vertex_Description packed them.  The attenuation and the
	// spot cone as DX8Wrapper hands them to the Direct3D 11 backend: {a0, a1, a2, range} and
	// {cos(theta / 2), cos(phi / 2), falloff, 0}.
	unsigned slot = 0;
	for (unsigned index = 0; index < LIGHT_COUNT && slot < MAXIMUM_VERTEX_LIGHTS; ++index) {
		if (!LightsEnabled[index]) {
			continue;
		}
		const D3DLIGHT9 &light = Lights[index];
		const float position[3] = { light.Position.x, light.Position.y, light.Position.z };
		const float direction[3] = { light.Direction.x, light.Direction.y, light.Direction.z };
		float (*fields)[4] = vertex.LightFields[slot];
		transform_point(position, view, fields[0]);
		transform_direction(direction, view, fields[1]);
		colour_value(light.Diffuse, fields[2]);
		colour_value(light.Specular, fields[3]);
		fields[4][0] = light.Attenuation0;
		fields[4][1] = light.Attenuation1;
		fields[4][2] = light.Attenuation2;
		fields[4][3] = light.Range;
		fields[5][0] = cosf(light.Theta * 0.5f);
		fields[5][1] = cosf(light.Phi * 0.5f);
		fields[5][2] = light.Falloff;
		fields[5][3] = 0.0f;
		++slot;
	}

	colour_of(RenderStates[D3DRS_TEXTUREFACTOR], pixel.TextureFactor);
	colour_of(RenderStates[D3DRS_FOGCOLOR], pixel.FogColour);
	// A whole level, as D3D9 compares it: the program rounds the pixel's alpha to a level first.
	pixel.AlphaReference[0] = (float)(RenderStates[D3DRS_ALPHAREF] & 0xFF);
}
