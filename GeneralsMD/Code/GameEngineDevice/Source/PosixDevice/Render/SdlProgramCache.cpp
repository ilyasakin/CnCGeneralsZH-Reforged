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

// The generated programs on SDL3 GPU (decision 7, phase A3c).  See SdlProgramCache.h.

#include "SdlProgramCache.h"
#include "sdl3shadercompile.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

void Sdl_Read_Slot_Lines(const std::string &hlsl, unsigned int slots, std::vector<int> &textures, std::vector<int> &samplers)
{
	textures.assign(slots, 0);
	samplers.assign(slots, 0);
	for (unsigned int slot = 0; slot < slots; ++slot) {
		textures[slot] = (int)slot;
		samplers[slot] = (int)slot;
	}
	static const char PREFIX[] = "// SDL3 slot ";
	for (size_t at = hlsl.find(PREFIX); at != std::string::npos; at = hlsl.find(PREFIX, at + 1)) {
		int slot = -1, texture = -1, sampler = -1;
		if (sscanf(hlsl.c_str() + at, "// SDL3 slot %d: texture t%d, sampler state s%d", &slot, &texture, &sampler) == 3
			&& slot >= 0 && (unsigned int)slot < slots) {
			textures[slot] = texture;
			samplers[slot] = sampler;
		}
	}
}

SdlProgramCache::SdlProgramCache(SDL_GPUDevice *device) :
	Device(device),
	Built(0),
	Refused(0),
	CompileMilliseconds(0.0)
{
}

SdlProgramCache::~SdlProgramCache()
{
	for (int stage = 0; stage < 2; ++stage) {
		std::map<std::string, SdlProgram> &cache = stage == 0 ? VertexPrograms : PixelPrograms;
		for (std::map<std::string, SdlProgram>::iterator it = cache.begin(); it != cache.end(); ++it) {
			if (it->second.Shader != NULL) {
				SDL_ReleaseGPUShader(Device, it->second.Shader);
			}
		}
	}
}

const SdlProgram &SdlProgramCache::Make(std::map<std::string, SdlProgram> &cache, const std::string &key, bool generated,
	const std::string &hlsl, bool vertex_stage)
{
	SdlProgram &program = cache[key];
	program.Shader = NULL;
	program.Key = key;
	program.SamplerSlots = 0;
	program.UniformBuffers = 0;
	program.Uses = 1;
	if (!generated) {
		program.Refusal = "the generator refuses the description";
	}
	else {
		const Uint64 start = SDL_GetTicksNS();
		std::vector<unsigned char> spirv;
		std::string log;
		if (!SDL3_Compile_HLSL_To_SPIRV(hlsl, vertex_stage, spirv, log)) {
			program.Refusal = "glslang: " + log;
		}
		else {
			SDL3_Shader_Slots(spirv, vertex_stage, program.SamplerSlots, program.UniformBuffers);
			program.Shader = SDL3_Create_Shader(Device, spirv, vertex_stage, log);
			if (program.Shader == NULL) {
				program.Refusal = "the device: " + log;
			}
		}
		CompileMilliseconds += (double)(SDL_GetTicksNS() - start) / 1.0e6;
	}
	if (program.Shader == NULL) {
		++Refused;
		// Once per key: the draws that ask for it again are counted by the caller, not logged.
		fprintf(stderr, "SdlProgramCache: %s program %s refused: %s\n", vertex_stage ? "vertex" : "pixel", key.c_str(),
			program.Refusal.c_str());
		return program;
	}
	Sdl_Read_Slot_Lines(hlsl, program.SamplerSlots, program.SlotTexture, program.SlotSampler);
	++Built;
	return program;
}

const SdlProgram &SdlProgramCache::Vertex_Program(const VertexPipelineDescription &description)
{
	const std::string key = VertexShader_Key(description);
	std::map<std::string, SdlProgram>::iterator existing = VertexPrograms.find(key);
	if (existing != VertexPrograms.end()) {
		++existing->second.Uses;
		return existing->second;
	}
	std::string hlsl;
	const bool generated = VertexShader_Generate(description, VERTEX_SHADER_TARGET_SDL3_GPU, hlsl);
	return Make(VertexPrograms, key, generated, hlsl, true);
}

const SdlProgram &SdlProgramCache::Pixel_Program(const CombinerDescription &description)
{
	const std::string key = CombinerShader_Key(description);
	std::map<std::string, SdlProgram>::iterator existing = PixelPrograms.find(key);
	if (existing != PixelPrograms.end()) {
		++existing->second.Uses;
		return existing->second;
	}
	std::string hlsl;
	const bool generated = CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, hlsl);
	return Make(PixelPrograms, key, generated, hlsl, false);
}
