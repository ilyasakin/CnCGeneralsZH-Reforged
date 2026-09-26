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
#include "PosixResources9.h"
#include "SdlConstants.h"
#include "SdlGpuFrame.h"
#include "SdlPipelineCache.h"
#include "SdlProgramCache.h"
#include "SdlResourceMirror.h"

#include "ffshader.h"
#include "ffvertex.h"

#include <math.h>
#include <stdio.h>
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

bool PosixDevice9::Stage_Ends_Cascade(unsigned int stage) const
{
	// A disabled stage ends it; so does one whose COLORARG1 is the texture when none is bound.  That
	// second case is D3D9's documented one (FFReference's N20, from the D3DTA and texture blending
	// pages); ffshader's own answer for a stage with no texture is opaque white, which D3 left for a
	// Windows measurement to settle, and a stage that reads the texture only elsewhere still gets it.
	const RenderUInt32 *ts = TextureStageStates[stage];
	return ts[D3DTSS_COLOROP] == D3DTOP_DISABLE
		|| ((ts[D3DTSS_COLORARG1] & D3DTA_SELECTMASK) == D3DTA_TEXTURE && Textures[stage] == NULL);
}

void PosixDevice9::Build_Combiner_Description(CombinerDescription &description) const
{
	// memset first, as dx11backend's does: descriptions are compared with memcmp, padding included.
	memset(&description, 0, sizeof(description));

	// The alpha test and the fog are pipeline state under D3D9 and instructions in the program here.
	description.PixelPipeline.AlphaTestEnabled = RenderStates[D3DRS_ALPHATESTENABLE] != 0;
	description.PixelPipeline.AlphaFunction = RenderStates[D3DRS_ALPHAFUNC];
	description.PixelPipeline.FogEnabled = RenderStates[D3DRS_FOGENABLE] != 0;

	if (Stage_Ends_Cascade(0)) {
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
		if (Stage_Ends_Cascade(index)) {
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

// A material source as D3D9 reads it: COLOR1 or COLOR2 names the vertex's diffuse or specular colour,
// and with COLORVERTEX off, or a vertex that has not got that colour, the material's own is used
// ("D3DMATERIALCOLORSOURCE").  The generator reads no vertex specular, so COLOR2 it can take only as that
// fallback.
static RenderUInt32 material_source(RenderUInt32 source, RenderUInt32 fvf, bool colour_vertex)
{
	if (source == D3DMCS_COLOR1 && (!colour_vertex || (fvf & D3DFVF_DIFFUSE) == 0)) {
		return D3DMCS_MATERIAL;
	}
	if (source == D3DMCS_COLOR2 && (!colour_vertex || (fvf & D3DFVF_SPECULAR) == 0)) {
		return D3DMCS_MATERIAL;
	}
	return source;
}

bool PosixDevice9::Build_Vertex_Description(VertexPipelineDescription &description, std::string *refusal) const
{
	memset(&description, 0, sizeof(description));
	description.FVF = FVF;
	// Pretransformed vertices skip transform and lighting altogether.
	description.LightingEnabled = RenderStates[D3DRS_LIGHTING] != 0 && (FVF & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW;
	description.SpecularEnabled = RenderStates[D3DRS_SPECULARENABLE] != 0;
	description.ColourVertexEnabled = RenderStates[D3DRS_COLORVERTEX] != 0;
	const bool colour_vertex = description.ColourVertexEnabled;
	description.DiffuseMaterialSource = material_source(RenderStates[D3DRS_DIFFUSEMATERIALSOURCE], FVF, colour_vertex);
	description.AmbientMaterialSource = material_source(RenderStates[D3DRS_AMBIENTMATERIALSOURCE], FVF, colour_vertex);
	description.EmissiveMaterialSource = material_source(RenderStates[D3DRS_EMISSIVEMATERIALSOURCE], FVF, colour_vertex);
	description.SpecularMaterialSource = material_source(RenderStates[D3DRS_SPECULARMATERIALSOURCE], FVF, colour_vertex);

	// The enabled lights, packed down in order: the program declares them contiguously, and
	// Build_Constants packs their fields the same way.  Unlit, the program has none.
	description.LightCount = 0;
	for (unsigned index = 0; index < LIGHT_COUNT && description.LightingEnabled; ++index) {
		if (!LightsEnabled[index]) {
			continue;
		}
		if (description.LightCount == MAXIMUM_VERTEX_LIGHTS) {
			if (refusal != NULL) *refusal = "more lights enabled than the generator carries";
			return false;
		}
		description.Lights[description.LightCount].Type = Lights[index].Type;
		++description.LightCount;
	}

	// The same stages the combiner walk takes: the program only carries coordinates for those.  A
	// disabled stage 0 is the one-stage diffuse combiner, which reads no coordinates but has a stage.
	description.StageCount = 0;
	for (unsigned index = 0; index < MAXIMUM_VERTEX_STAGES; ++index) {
		if (index > 0 && Stage_Ends_Cascade(index)) {
			break;
		}
		if (index == 0 && Stage_Ends_Cascade(0)) {
			description.StageCount = 1;
			break;
		}
		description.Stages[index].TextureCoordinateIndex = TextureStageStates[index][D3DTSS_TEXCOORDINDEX];
		description.Stages[index].TextureTransformFlags = TextureStageStates[index][D3DTSS_TEXTURETRANSFORMFLAGS];
		description.StageCount = index + 1;
	}

	description.FogEnabled = RenderStates[D3DRS_FOGENABLE] != 0;
	description.FogVertexMode = RenderStates[D3DRS_FOGVERTEXMODE];
	// The engine fogs per vertex, LINEAR (dx8wrapper's defaults); the generator has no table fog, and no
	// fog factor taken from the specular alpha, which is D3D9's answer with both modes NONE.
	if (description.FogEnabled && RenderStates[D3DRS_FOGTABLEMODE] != D3DFOG_NONE) {
		if (refusal != NULL) *refusal = "table fog";
		return false;
	}
	if (description.FogEnabled && description.FogVertexMode == D3DFOG_NONE) {
		if (refusal != NULL) *refusal = "fog from the specular alpha (both fog modes NONE)";
		return false;
	}
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

//-------------------------------------------------------------------------------------------------
// The draws (A3c): recorded on the GPU with a window, nothing without one.
//-------------------------------------------------------------------------------------------------

// The vertices a primitive count reads; for an indexed draw, the indices.
static unsigned vertices_of(D3DPRIMITIVETYPE type, unsigned count)
{
	switch (type) {
		case D3DPT_POINTLIST:		return count;
		case D3DPT_LINELIST:		return count * 2;
		case D3DPT_LINESTRIP:		return count + 1;
		case D3DPT_TRIANGLELIST:	return count * 3;
		case D3DPT_TRIANGLESTRIP:	return count + 2;
		case D3DPT_TRIANGLEFAN:		return count + 2;
		default:					return 0;
	}
}

// A fan as a list: triangle k is (0, k + 1, k + 2) of the fan's vertices, D3D9's winding.
template <class Index>
static void expand_fan(const Index *fan, unsigned first, unsigned count, Index *list)
{
	for (unsigned k = 0; k < count; ++k) {
		list[k * 3] = fan != NULL ? fan[0] : (Index)first;
		list[k * 3 + 1] = fan != NULL ? fan[k + 1] : (Index)(first + k + 1);
		list[k * 3 + 2] = fan != NULL ? fan[k + 2] : (Index)(first + k + 2);
	}
}

static bool is_dynamic(IDirect3DVertexBuffer9 *buffer)
{
	D3DVERTEXBUFFER_DESC desc;
	return buffer->GetDesc(&desc) == D3D_OK && (desc.Usage & D3DUSAGE_DYNAMIC) != 0;
}

static bool is_dynamic(IDirect3DIndexBuffer9 *buffer)
{
	D3DINDEXBUFFER_DESC desc;
	return buffer->GetDesc(&desc) == D3D_OK && (desc.Usage & D3DUSAGE_DYNAMIC) != 0;
}

void PosixDevice9::Refuse_Draw(const std::string &reason)
{
	unsigned int &count = DrawRefusals[reason];
	if (count++ == 0) {
		fprintf(stderr, "PosixDevice9: a draw refused: %s\n", reason.c_str());
	}
}

RenderResult PosixDevice9::DrawPrimitive(D3DPRIMITIVETYPE type, unsigned int start_vertex, unsigned int primitive_count)
{
	DrawCall call;
	memset(&call, 0, sizeof(call));
	call.Type = type;
	call.PrimitiveCount = primitive_count;
	call.StartVertex = start_vertex;
	return Gpu_Draw(call);
}

RenderResult PosixDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, int base_vertex, unsigned int min_vertex,
	unsigned int vertex_count, unsigned int start_index, unsigned int primitive_count)
{
	DrawCall call;
	memset(&call, 0, sizeof(call));
	call.Type = type;
	call.PrimitiveCount = primitive_count;
	call.Indexed = true;
	call.BaseVertex = base_vertex;
	call.MinVertex = min_vertex;
	call.VertexCount = vertex_count;
	call.StartIndex = start_index;
	return Gpu_Draw(call);
}

RenderResult PosixDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE type, unsigned int primitive_count, const void *vertices,
	unsigned int stride)
{
	if (vertices == NULL) {
		return D3DERR_INVALIDCALL;
	}
	DrawCall call;
	memset(&call, 0, sizeof(call));
	call.Type = type;
	call.PrimitiveCount = primitive_count;
	call.UserVertices = vertices;
	call.UserStride = stride;
	const RenderResult result = Gpu_Draw(call);
	// D3D9: "After calling DrawPrimitiveUP, the stream 0 settings ... are set to NULL."
	Posix_Bind(Streams[0], (IDirect3DVertexBuffer9 *)NULL);
	StreamOffsets[0] = 0;
	StreamStrides[0] = 0;
	return result;
}

RenderResult PosixDevice9::Gpu_Draw(const DrawCall &call)
{
	const unsigned int reads = vertices_of(call.Type, call.PrimitiveCount);
	if (reads == 0 && call.PrimitiveCount != 0) {
		return D3DERR_INVALIDCALL;
	}
	if (Gpu == NULL || call.PrimitiveCount == 0) {
		return D3D_OK;		// -headless: drawn into a window nobody sees
	}

	// What A3c does not draw yet.
	if (VertexShader != NULL || PixelShader != NULL) {
		Refuse_Draw("a programmable draw (A3e)");
		return D3D_OK;
	}
	if (FVF == 0) {
		Refuse_Draw("a vertex declaration and no FVF (A3e)");
		return D3D_OK;
	}
	if (RenderTargets[0] != BackBuffer || RenderTargets[1] != NULL || RenderTargets[2] != NULL || RenderTargets[3] != NULL) {
		Refuse_Draw("a render target other than the back buffer (A3d)");
		return D3D_OK;
	}
	if (DepthStencil != NULL && DepthStencil != DepthSurface) {
		Refuse_Draw("a depth surface other than the implicit one (A3d)");
		return D3D_OK;
	}
	if (RenderStates[D3DRS_CLIPPLANEENABLE] != 0) {
		Refuse_Draw("user clip planes");
		return D3D_OK;
	}

	Mirrors->Collect_Dead();
	if (Gpu->Batch_Is_Full()) {
		Gpu->Flush();
	}

	// The programs and the pipeline.
	std::string refusal;
	SdlVertexLayout layout;
	if (!Sdl_Vertex_Layout(FVF, layout, refusal)) {
		Refuse_Draw("the vertex format: " + refusal);
		return D3D_OK;
	}
	CombinerDescription combiner;
	VertexPipelineDescription vertex;
	Build_Combiner_Description(combiner);
	if (!Build_Vertex_Description(vertex, &refusal)) {
		Refuse_Draw(refusal);
		return D3D_OK;
	}
	const SdlProgram &vertex_program = Programs->Vertex_Program(vertex);
	const SdlProgram &pixel_program = Programs->Pixel_Program(combiner);
	if (vertex_program.Shader == NULL || pixel_program.Shader == NULL) {
		Refuse_Draw(vertex_program.Shader == NULL ? "a vertex program refused" : "a pixel program refused");
		return D3D_OK;
	}
	if (pixel_program.SamplerSlots > SdlRecordedDraw::MAXIMUM_SAMPLERS || vertex_program.SamplerSlots != 0) {
		Refuse_Draw("more sampler slots than a draw binds");
		return D3D_OK;
	}
	// Every pass has the frame's depth-stencil, so every pipeline has its format; a draw with no depth
	// surface bound has D3D9's answer instead, depth and stencil off.
	const RenderUInt32 *states = RenderStates;
	RenderUInt32 without_depth[RENDER_STATE_COUNT];
	if (DepthStencil == NULL) {
		memcpy(without_depth, RenderStates, sizeof(without_depth));
		without_depth[D3DRS_ZENABLE] = D3DZB_FALSE;
		without_depth[D3DRS_ZWRITEENABLE] = 0;
		without_depth[D3DRS_STENCILENABLE] = 0;
		states = without_depth;
	}
	SdlPipelineKey key;
	if (!Sdl_Pipeline_Key(states, call.Type, vertex_program.Shader, pixel_program.Shader, FVF,
		SdlGpuFrame::Target_Format(), Gpu->Depth_Format(), key, refusal)) {
		Refuse_Draw("the pipeline: " + refusal);
		return D3D_OK;
	}
	SdlRecordedDraw draw;
	memset(&draw, 0, sizeof(draw));
	draw.Pipeline = Pipelines->Pipeline(key);
	if (draw.Pipeline == NULL) {
		Refuse_Draw("the GPU refused a pipeline");
		return D3D_OK;
	}

	// The vertex and index sources.
	const bool user = call.UserVertices != NULL;
	const unsigned int stride = user ? call.UserStride : StreamStrides[0];
	PosixVertexBuffer9 *vertex_buffer = user ? NULL : static_cast<PosixVertexBuffer9 *>(Streams[0]);
	PosixIndexBuffer9 *index_buffer = call.Indexed ? static_cast<PosixIndexBuffer9 *>(Indices) : NULL;
	if ((!user && vertex_buffer == NULL) || (call.Indexed && index_buffer == NULL)) {
		return D3DERR_INVALIDCALL;
	}
	if (stride != layout.Stride) {
		Refuse_Draw("a stream stride that is not the FVF's");
		return D3D_OK;
	}
	const bool fan = call.Type == D3DPT_TRIANGLEFAN;
	const bool stage_vertices = user || is_dynamic(vertex_buffer);
	const bool stage_indices = call.Indexed && (fan || is_dynamic(index_buffer));
	const unsigned int index_size = (call.Indexed && index_buffer->format() == D3DFMT_INDEX32) ? 4 : 2;

	// The GPU copies.  Bringing one up to date can flush the batch, which leaves the ones already asked
	// for marked as used by the batch before: so ask again until a round flushes nothing.
	for (int round = 0; round < 3; ++round) {
		const uint64_t batch = Gpu->Batch();
		draw.SamplerCount = pixel_program.SamplerSlots;
		for (unsigned int slot = 0; slot < pixel_program.SamplerSlots; ++slot) {
			const int texture_stage = pixel_program.SlotTexture[slot];
			const int sampler_stage = pixel_program.SlotSampler[slot];
			if (texture_stage < 0 || texture_stage >= SAMPLER_COUNT || sampler_stage < 0 || sampler_stage >= SAMPLER_COUNT) {
				Refuse_Draw("a program slot past the device's stages");
				return D3D_OK;
			}
			IDirect3DBaseTexture9 *texture = Textures[texture_stage];
			draw.Textures[slot] = texture != NULL ? Mirrors->Texture(texture, refusal) : Mirrors->White();
			if (draw.Textures[slot] == NULL) {
				Refuse_Draw("the texture: " + refusal);
				return D3D_OK;
			}
			draw.Samplers[slot] = Samplers->Sampler(SamplerStates[sampler_stage]);
			if (draw.Samplers[slot] == NULL) {
				Refuse_Draw("the sampler: " + Samplers->Refusal());
				return D3D_OK;
			}
		}
		if (!stage_vertices) {
			draw.VertexBuffer = Mirrors->Buffer(vertex_buffer, vertex_buffer->storage(), refusal);
		}
		if (call.Indexed && !stage_indices) {
			draw.IndexBuffer = Mirrors->Buffer(index_buffer, index_buffer->storage(), refusal);
		}
		if ((!stage_vertices && draw.VertexBuffer == NULL) || (call.Indexed && !stage_indices && draw.IndexBuffer == NULL)) {
			Refuse_Draw("the buffer: " + refusal);
			return D3D_OK;
		}
		if (Gpu->Batch() == batch) {
			break;
		}
	}

	// The vertices: a static buffer's copy is bound where the stream points, and a dynamic buffer's or
	// the caller's bytes are staged, from the first vertex the draw can read.
	if (stage_vertices) {
		unsigned int first = 0;
		unsigned int count = reads;
		if (!user) {
			if (call.Indexed && call.BaseVertex < 0) {
				Refuse_Draw("a negative base vertex into a dynamic buffer");
				return D3D_OK;
			}
			first = call.Indexed ? (unsigned int)call.BaseVertex : call.StartVertex;
			count = call.Indexed ? call.MinVertex + call.VertexCount : reads;
		}
		const size_t start = user ? 0 : (size_t)StreamOffsets[0] + (size_t)first * stride;
		const size_t size = (size_t)count * stride;
		if (!user && start + size > vertex_buffer->storage().length()) {
			return D3DERR_INVALIDCALL;
		}
		const uint8_t *source = user ? (const uint8_t *)call.UserVertices : vertex_buffer->storage().bytes() + start;
		memcpy(Gpu->Stage((uint32_t)size, draw.VertexOffset), source, size);
		draw.First = 0;
		draw.BaseVertex = 0;
	}
	else {
		draw.VertexOffset = StreamOffsets[0];
		draw.First = call.StartVertex;
		draw.BaseVertex = call.BaseVertex;
	}

	// The indices.
	if (call.Indexed) {
		const size_t start = (size_t)call.StartIndex * index_size;
		if (start + (size_t)reads * index_size > index_buffer->storage().length()) {
			return D3DERR_INVALIDCALL;
		}
		const uint8_t *source = index_buffer->storage().bytes() + start;
		draw.IndexSize = index_size;
		if (fan) {
			draw.Count = call.PrimitiveCount * 3;
			uint8_t *list = Gpu->Stage(draw.Count * index_size, draw.IndexOffset);
			if (index_size == 4) {
				expand_fan((const uint32_t *)source, 0, call.PrimitiveCount, (uint32_t *)list);
			}
			else {
				expand_fan((const uint16_t *)source, 0, call.PrimitiveCount, (uint16_t *)list);
			}
			draw.First = 0;
		}
		else if (stage_indices) {
			draw.Count = reads;
			memcpy(Gpu->Stage(reads * index_size, draw.IndexOffset), source, (size_t)reads * index_size);
			draw.First = 0;
		}
		else {
			draw.Count = reads;
			draw.IndexOffset = 0;
			draw.First = call.StartIndex;
		}
	}
	else if (fan) {
		// Staged vertices start at the fan's first; a static buffer's copy is indexed from its start.
		const unsigned int first = stage_vertices ? 0 : call.StartVertex;
		draw.Count = call.PrimitiveCount * 3;
		if (first + reads > 0xFFFF) {
			draw.IndexSize = 4;
			expand_fan((const uint32_t *)NULL, first, call.PrimitiveCount, (uint32_t *)Gpu->Stage(draw.Count * 4, draw.IndexOffset));
		}
		else {
			draw.IndexSize = 2;
			expand_fan((const uint16_t *)NULL, first, call.PrimitiveCount, (uint16_t *)Gpu->Stage(draw.Count * 2, draw.IndexOffset));
		}
		draw.First = 0;
	}
	else {
		draw.Count = reads;
	}

	// The constants, pushed only when they change.
	SdlVertexConstants vertex_constants;
	SdlPixelConstants pixel_constants;
	Build_Constants(vertex_constants, pixel_constants);
	if (vertex_program.UniformBuffers != 0) {
		draw.VertexConstants = Gpu->Constants(0, &vertex_constants, sizeof(vertex_constants));
		draw.VertexConstantsSize = sizeof(vertex_constants);
	}
	if (pixel_program.UniformBuffers != 0) {
		draw.PixelConstants = Gpu->Constants(1, &pixel_constants, sizeof(pixel_constants));
		draw.PixelConstantsSize = sizeof(pixel_constants);
	}

	// D3D9's pixel centres are at integer coordinates, SDL3 GPU's (Metal's, Vulkan's) at half-integers:
	// the viewport moves half a pixel right and down, as dx11backend's Set_Viewport does.  The scissor
	// keeps the draw inside the viewport D3D9 would have clipped it to.
	draw.Viewport[0] = (float)Viewport.X + 0.5f;
	draw.Viewport[1] = (float)Viewport.Y + 0.5f;
	draw.Viewport[2] = (float)Viewport.Width;
	draw.Viewport[3] = (float)Viewport.Height;
	draw.Viewport[4] = Viewport.MinZ;
	draw.Viewport[5] = Viewport.MaxZ;
	draw.Scissor[0] = (int32_t)Viewport.X;
	draw.Scissor[1] = (int32_t)Viewport.Y;
	draw.Scissor[2] = (int32_t)Viewport.Width;
	draw.Scissor[3] = (int32_t)Viewport.Height;
	draw.StencilReference = RenderStates[D3DRS_STENCILREF] & 0xFF;
	draw.BlendFactor = RenderStates[D3DRS_BLENDFACTOR];
	Gpu->Record_Draw(draw);
	++DrawsRecorded;
	return D3D_OK;
}
