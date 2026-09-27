/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

#include "sdl3shadercompile.h"

#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <stdio.h>
#include <string.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace {

// Both libraries want one process-wide initialisation and neither says what happens on a second.
std::once_flag glslang_ready;
std::once_flag shadercross_ready;
bool shadercross_ok = false;

void start_shadercross()
{
	std::call_once(shadercross_ready, []() { shadercross_ok = SDL_ShaderCross_Init(); });
}

SDL_ShaderCross_SPIRV_Info spirv_info(const std::vector<unsigned char> & spirv, bool vertex_stage)
{
	SDL_ShaderCross_SPIRV_Info info;
	SDL_zero(info);
	info.bytecode = spirv.empty() ? NULL : &spirv[0];
	info.bytecode_size = spirv.size();
	info.entrypoint = "main";
	info.shader_stage = vertex_stage ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
	return info;
}

// The Direct3D 12 programs (-d3d12, X1), compiled once.  d3dcompiler_47 is Microsoft's on Windows and
// fast; under Wine it is vkd3d-shader, where one compile has taken minutes, so the bytecode is kept:
// d3d12shaders.shipped beside the executable (recorded on Windows with Microsoft's compiler, as
// dx11shaders.shipped is for the Direct3D 11 backend), and the player's own d3d12shaders.cache in the
// user data folder.  The format is the Direct3D 11 backend's (dx11backend.cpp) under its own marker: the
// marker, then one record a program - the key, the byte count, the bytes - whole or not at all.
//
// The key is FNV-1a over the profile and the HLSL SPIRV-Cross writes for the program at shader model 6.0.
// The bytecode is compiled from its shader model 5.1 text, which shadercross does not hand out; the two
// come from the same SPIR-V through the same SPIRV-Cross and differ in the model's own syntax only, so a
// SPIRV-Cross that writes different registers or bindings changes the key, and compiles again, rather
// than finding a program laid out for another.
const char DXBC_CACHE_MARKER[8] = { 'D', '3', 'D', '1', '2', 'S', 'C', '1' };
const unsigned DXBC_LARGEST_PROGRAM = 1u << 20;

struct DxbcCache
{
	std::mutex Lock;
	bool Loaded = false;
	std::string UserFile;
	std::unordered_map<unsigned long long, std::vector<unsigned char> > Programs;
	unsigned Shipped = 0, FromUser = 0, Compiled = 0;
};

DxbcCache & dxbc_cache()
{
	static DxbcCache cache;
	return cache;
}

unsigned read_dxbc_file(const std::string & path, std::unordered_map<unsigned long long, std::vector<unsigned char> > & programs)
{
	FILE * file = fopen(path.c_str(), "rb");
	if (file == NULL) {
		return 0;
	}
	unsigned count = 0;
	char marker[sizeof(DXBC_CACHE_MARKER)];
	if (fread(marker, sizeof(marker), 1, file) == 1 && memcmp(marker, DXBC_CACHE_MARKER, sizeof(marker)) == 0) {
		unsigned long long key = 0;
		unsigned int size = 0;
		while (fread(&key, sizeof(key), 1, file) == 1 && fread(&size, sizeof(size), 1, file) == 1
			&& size > 0 && size <= DXBC_LARGEST_PROGRAM) {
			std::vector<unsigned char> bytecode(size);
			if (fread(&bytecode[0], size, 1, file) != 1) {
				break;
			}
			programs[key].swap(bytecode);
			++count;
		}
	}
	fclose(file);
	return count;
}

/// One record onto the end of the player's cache, in one write: a run killed during it leaves a torn last
/// record, which the reader drops.  A file that is not the cache (another marker) is started again.
void append_dxbc_record(const std::string & path, unsigned long long key, const std::vector<unsigned char> & bytecode)
{
	if (path.empty()) {
		return;
	}
	bool valid = false;
	if (FILE * existing = fopen(path.c_str(), "rb")) {
		char marker[sizeof(DXBC_CACHE_MARKER)];
		valid = fread(marker, sizeof(marker), 1, existing) == 1 && memcmp(marker, DXBC_CACHE_MARKER, sizeof(marker)) == 0;
		fclose(existing);
	}
	FILE * file = fopen(path.c_str(), valid ? "ab" : "wb");
	if (file == NULL) {
		return;
	}
	std::vector<unsigned char> record;
	if (!valid) {
		record.insert(record.end(), DXBC_CACHE_MARKER, DXBC_CACHE_MARKER + sizeof(DXBC_CACHE_MARKER));
	}
	const unsigned int size = (unsigned int)bytecode.size();
	record.insert(record.end(), (const unsigned char *)&key, (const unsigned char *)&key + sizeof(key));
	record.insert(record.end(), (const unsigned char *)&size, (const unsigned char *)&size + sizeof(size));
	record.insert(record.end(), bytecode.begin(), bytecode.end());
	fwrite(&record[0], record.size(), 1, file);
	fclose(file);
}

/// Loads the player's cache and then the shipped file, so a shipped program wins, as in Direct3D 11's.
void load_dxbc_cache(DxbcCache & cache)
{
	if (cache.Loaded) {
		return;
	}
	cache.Loaded = true;
	if (!cache.UserFile.empty()) {
		cache.FromUser = read_dxbc_file(cache.UserFile, cache.Programs);
	}
	cache.Shipped = read_dxbc_file("d3d12shaders.shipped", cache.Programs);
	fprintf(stderr, "SDL3 shaders: Direct3D 12 programs: %u shipped with the game, %u in the player's cache (%s)\n",
		cache.Shipped, cache.FromUser, cache.UserFile.empty() ? "none named" : cache.UserFile.c_str());
}

unsigned long long dxbc_key(bool vertex_stage, const char * hlsl)
{
	unsigned long long hash = 14695981039346656037ULL;
	for (const char * cursor = vertex_stage ? "vs_5_1\n" : "ps_5_1\n"; *cursor != '\0'; ++cursor) {
		hash = (hash ^ (unsigned char)*cursor) * 1099511628211ULL;
	}
	for (const char * cursor = hlsl; *cursor != '\0'; ++cursor) {
		hash = (hash ^ (unsigned char)*cursor) * 1099511628211ULL;
	}
	return hash;
}

/// The DXBC for a program: kept, or compiled by shadercross and kept.
bool dxbc_program(const SDL_ShaderCross_SPIRV_Info & info, bool vertex_stage, std::vector<unsigned char> & dxbc)
{
	DxbcCache & cache = dxbc_cache();
	char * hlsl = static_cast<char *>(SDL_ShaderCross_TranspileHLSLFromSPIRV(&info));
	const bool keyed = hlsl != NULL;
	const unsigned long long key = keyed ? dxbc_key(vertex_stage, hlsl) : 0;
	SDL_free(hlsl);
	if (keyed) {
		std::lock_guard<std::mutex> hold(cache.Lock);
		load_dxbc_cache(cache);
		auto found = cache.Programs.find(key);
		if (found != cache.Programs.end()) {
			dxbc = found->second;
			return true;
		}
	}
	size_t size = 0;
	void * bytes = SDL_ShaderCross_CompileDXBCFromSPIRV(&info, &size);
	if (bytes == NULL) {
		return false;
	}
	dxbc.assign(static_cast<unsigned char *>(bytes), static_cast<unsigned char *>(bytes) + size);
	SDL_free(bytes);
	if (keyed) {
		std::lock_guard<std::mutex> hold(cache.Lock);
		cache.Programs[key] = dxbc;
		++cache.Compiled;
		append_dxbc_record(cache.UserFile, key, dxbc);
	}
	return true;
}

} // namespace

void SDL3_Set_DXBC_Cache_Directory(const char * directory)
{
	DxbcCache & cache = dxbc_cache();
	std::lock_guard<std::mutex> hold(cache.Lock);
	cache.UserFile.clear();
	if (directory != NULL && directory[0] != '\0') {
		cache.UserFile = directory;
		const char last = cache.UserFile[cache.UserFile.size() - 1];
		if (last != '/' && last != '\\') {
			cache.UserFile += '/';
		}
		cache.UserFile += "d3d12shaders.cache";
	}
}

void SDL3_DXBC_Cache_Statistics(unsigned & shipped, unsigned & from_user, unsigned & compiled)
{
	DxbcCache & cache = dxbc_cache();
	std::lock_guard<std::mutex> hold(cache.Lock);
	shipped = cache.Shipped;
	from_user = cache.FromUser;
	compiled = cache.Compiled;
}

// The one place the HLSL front end is named (see the header).  glslang's defaults are what the
// D-spike measured all 49 of the game's programs through: Vulkan 1.0, SPIR-V 1.0, default limits.
bool SDL3_Compile_HLSL_To_SPIRV(const std::string & hlsl, bool vertex_stage, std::vector<unsigned char> & spirv,
	std::string & log)
{
	std::call_once(glslang_ready, []() { glslang_initialize_process(); });
	spirv.clear();
	log.clear();

	const glslang_stage_t stage = vertex_stage ? GLSLANG_STAGE_VERTEX : GLSLANG_STAGE_FRAGMENT;
	glslang_input_t input;
	memset(&input, 0, sizeof(input));
	input.language = GLSLANG_SOURCE_HLSL;
	input.stage = stage;
	input.client = GLSLANG_CLIENT_VULKAN;
	input.client_version = GLSLANG_TARGET_VULKAN_1_0;
	input.target_language = GLSLANG_TARGET_SPV;
	input.target_language_version = GLSLANG_TARGET_SPV_1_0;
	input.code = hlsl.c_str();
	input.default_version = 100;
	input.default_profile = GLSLANG_NO_PROFILE;
	input.messages = GLSLANG_MSG_READ_HLSL_BIT;
	input.resource = glslang_default_resource();

	glslang_shader_t * shader = glslang_shader_create(&input);
	glslang_shader_set_entry_point(shader, "main");
	bool ok = glslang_shader_preprocess(shader, &input) && glslang_shader_parse(shader, &input);
	if (!ok) {
		log = glslang_shader_get_info_log(shader);
		glslang_shader_delete(shader);
		return false;
	}

	glslang_program_t * program = glslang_program_create();
	glslang_program_add_shader(program, shader);
	ok = glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT | GLSLANG_MSG_READ_HLSL_BIT);
	if (ok) {
		glslang_program_SPIRV_generate(program, stage);
		const size_t words = glslang_program_SPIRV_get_size(program);
		const unsigned char * bytes = reinterpret_cast<const unsigned char *>(glslang_program_SPIRV_get_ptr(program));
		spirv.assign(bytes, bytes + words * 4);
		const char * messages = glslang_program_SPIRV_get_messages(program);
		if (messages != NULL) log = messages;
		ok = !spirv.empty();
	}
	else {
		log = glslang_program_get_info_log(program);
	}
	glslang_program_delete(program);
	glslang_shader_delete(shader);
	return ok;
}

bool SDL3_Translate_SPIRV_To_MSL(const std::vector<unsigned char> & spirv, bool vertex_stage, std::string & msl,
	std::string & log)
{
	start_shadercross();
	msl.clear();
	log.clear();
	if (!shadercross_ok) {
		log = SDL_GetError();
		return false;
	}
	const SDL_ShaderCross_SPIRV_Info info = spirv_info(spirv, vertex_stage);
	char * text = static_cast<char *>(SDL_ShaderCross_TranspileMSLFromSPIRV(&info));
	if (text == NULL) {
		log = SDL_GetError();
		return false;
	}
	msl = text;
	SDL_free(text);
	return true;
}

bool SDL3_Translate_SPIRV_To_HLSL(const std::vector<unsigned char> & spirv, bool vertex_stage, std::string & hlsl,
	std::string & log)
{
	start_shadercross();
	hlsl.clear();
	log.clear();
	if (!shadercross_ok) {
		log = SDL_GetError();
		return false;
	}
	const SDL_ShaderCross_SPIRV_Info info = spirv_info(spirv, vertex_stage);
	char * text = static_cast<char *>(SDL_ShaderCross_TranspileHLSLFromSPIRV(&info));
	if (text == NULL) {
		log = SDL_GetError();
		return false;
	}
	hlsl = text;
	SDL_free(text);
	return true;
}

void SDL3_Shader_Slots(const std::vector<unsigned char> & spirv, bool vertex_stage, unsigned & samplers,
	unsigned & uniform_buffers)
{
	// SDL's sets: a vertex program's textures in 0 and constant buffers in 1, a pixel program's in 2
	// and 3 (SDL_CreateGPUShader).  Read straight from the OpDecorate instructions: the reflection
	// SDL_shadercross offers counts what is used, and the count is what SDL cannot take.
	const unsigned texture_set = vertex_stage ? 0 : 2;
	const unsigned uniform_set = vertex_stage ? 1 : 3;
	const size_t words = spirv.size() / 4;
	std::vector<unsigned> set_of(0), binding_of(0);
	const unsigned char * bytes = spirv.empty() ? NULL : &spirv[0];
	unsigned id_bound = words > 3 ? (unsigned)(bytes[12] | (bytes[13] << 8) | (bytes[14] << 16) | ((unsigned)bytes[15] << 24)) : 0;
	set_of.assign(id_bound, ~0u);
	binding_of.assign(id_bound, ~0u);
	for (size_t at = 5; at < words;) {
		const unsigned char * w = bytes + at * 4;
		const unsigned word = w[0] | (w[1] << 8) | (w[2] << 16) | ((unsigned)w[3] << 24);
		const unsigned opcode = word & 0xffff, length = word >> 16;
		if (length == 0) break;
		if (opcode == 71 && length >= 4 && at + 3 < words) {	// OpDecorate target decoration value
			const unsigned char * a = bytes + (at + 1) * 4;
			const unsigned char * b = bytes + (at + 2) * 4;
			const unsigned char * c = bytes + (at + 3) * 4;
			const unsigned target = a[0] | (a[1] << 8) | (a[2] << 16) | ((unsigned)a[3] << 24);
			const unsigned decoration = b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24);
			const unsigned value = c[0] | (c[1] << 8) | (c[2] << 16) | ((unsigned)c[3] << 24);
			if (target < id_bound && decoration == 34) set_of[target] = value;		// DescriptorSet
			if (target < id_bound && decoration == 33) binding_of[target] = value;	// Binding
		}
		at += length;
	}
	samplers = 0;
	uniform_buffers = 0;
	for (unsigned id = 0; id < id_bound; ++id) {
		if (binding_of[id] == ~0u) continue;
		if (set_of[id] == texture_set && binding_of[id] + 1 > samplers) samplers = binding_of[id] + 1;
		if (set_of[id] == uniform_set && binding_of[id] + 1 > uniform_buffers) uniform_buffers = binding_of[id] + 1;
	}
}

SDL_GPUShader * SDL3_Create_Shader(SDL_GPUDevice * device, const std::vector<unsigned char> & spirv,
	bool vertex_stage, std::string & log)
{
	start_shadercross();
	log.clear();
	if (!shadercross_ok) {
		log = SDL_GetError();
		return NULL;
	}
	const SDL_ShaderCross_SPIRV_Info info = spirv_info(spirv, vertex_stage);
	SDL_ShaderCross_GraphicsShaderResourceInfo resources;
	SDL_zero(resources);
	SDL3_Shader_Slots(spirv, vertex_stage, resources.num_samplers, resources.num_uniform_buffers);
	// shadercross leaves an error from its start-up (it looks for libraries it may not need) where a
	// later failure that sets none would report it.  Cleared, so a refusal says its own reason.
	SDL_ClearError();
	SDL_GPUShader * shader = NULL;
	const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
	if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) == 0 && (formats & SDL_GPU_SHADERFORMAT_DXBC) != 0) {
		// Direct3D 12 (-d3d12, X1).  shadercross creates a transpiled program with the samplers its own
		// reflection finds, not the counts it is given, and Direct3D 12's table runs from register 0 for that
		// many: a program with a gap (the terrain's t0-t3 and the shadow map at t5) reaches past it, and every
		// pipeline with it is refused.  So the DXBC comes from shadercross and the shader is made here, with
		// the slots read from the SPIR-V, as SPIR-V devices are given them.
		std::vector<unsigned char> dxbc;
		if (dxbc_program(info, vertex_stage, dxbc)) {
			SDL_GPUShaderCreateInfo create;
			SDL_zero(create);
			create.code = &dxbc[0];
			create.code_size = dxbc.size();
			create.entrypoint = "main";		// not read for DXBC
			create.format = SDL_GPU_SHADERFORMAT_DXBC;
			create.stage = vertex_stage ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
			create.num_samplers = resources.num_samplers;
			create.num_uniform_buffers = resources.num_uniform_buffers;
			shader = SDL_CreateGPUShader(device, &create);
		}
	}
	else {
		shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &info, &resources, 0);
	}
	if (shader == NULL) {
		log = SDL_GetError();
		if (log.empty()) log = "refused, and neither SDL nor SDL_shadercross said why";
	}
	return shader;
}
