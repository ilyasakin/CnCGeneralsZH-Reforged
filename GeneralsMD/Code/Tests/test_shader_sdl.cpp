/*
 * test_shader_sdl - the POSIX twin of test_ffshadercompile and test_ffvertexcompile: every program
 * the generators write, under the SDL3 GPU target, taken the whole way to what SDL's GPU backends
 * accept (decision 4).
 *
 * For each program in shader_cases.h - the 49 decision 4 was measured on, and the extras - it:
 *   1. generates the SDL3_GPU text,
 *   2. compiles it to SPIR-V through SDL3_Compile_HLSL_To_SPIRV (the one place the HLSL front end is
 *      named, so this test follows the compiler if decision 4's exit is ever taken),
 *   3. validates the SPIR-V with spirv-val for Vulkan 1.0, when the build found one,
 *   4. translates it to MSL through SDL_shadercross, and checks the slot contract in that MSL: every
 *      [[texture(n)]] has its [[sampler(n)]], and n is below the slot count SDL3_Shader_Slots gives
 *      the device - SDL binds slots 0 to n - 1 as texture-sampler pairs, and a texture outside that
 *      range is one Metal accepts and nothing ever binds,
 *   5. on macOS, has Metal compile it: SDL_CreateGPUShader on a real device, which hands the MSL to
 *      the driver (newLibraryWithSource).  Not the offline `xcrun metal`, which is not installed.
 *
 * Steps 3 and 5 can be unavailable - no spirv-val on the machine, no GPU in the session - and then
 * they say SKIPPED on a line of their own, with the reason and the count they did not check.  A
 * skipped step is never folded into a pass.
 */
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "sdl3shadercompile.h"
#include "shader_cases.h"

namespace {

struct Tally
{
	unsigned programs;
	unsigned generated;
	unsigned compiled;
	unsigned validated;
	unsigned translated;
	unsigned slots_kept;
	unsigned accepted;
};

// The [[texture(n)]] or [[sampler(n)]] indices in an MSL entry point's signature.
std::vector<int> msl_indices(const std::string & msl, const char * kind)
{
	std::vector<int> indices;
	const std::string opening = std::string("[[") + kind + "(";
	for (size_t at = msl.find(opening); at != std::string::npos; at = msl.find(opening, at + 1)) {
		indices.push_back(atoi(msl.c_str() + at + opening.size()));
	}
	return indices;
}

// Every texture has the sampler of its own index, and every index is one SDL binds.
bool slots_kept(const std::string & msl, unsigned samplers, std::string & why)
{
	const std::vector<int> textures = msl_indices(msl, "texture");
	const std::vector<int> sampler_indices = msl_indices(msl, "sampler");
	for (size_t i = 0; i < textures.size(); ++i) {
		if (textures[i] < 0 || (unsigned)textures[i] >= samplers) {
			why = "texture " + std::to_string(textures[i]) + " is outside the " + std::to_string(samplers) + " slots SDL binds";
			return false;
		}
		bool paired = false;
		for (size_t k = 0; k < sampler_indices.size(); ++k) paired = paired || sampler_indices[k] == textures[i];
		if (!paired) {
			why = "texture " + std::to_string(textures[i]) + " has no sampler of its own index";
			return false;
		}
		for (size_t j = i + 1; j < textures.size(); ++j) {
			if (textures[j] == textures[i]) {
				why = "two textures share slot " + std::to_string(textures[i]);
				return false;
			}
		}
	}
	for (size_t k = 0; k < sampler_indices.size(); ++k) {
		if (sampler_indices[k] < 0 || (unsigned)sampler_indices[k] >= samplers) {
			why = "sampler " + std::to_string(sampler_indices[k]) + " is outside the slots SDL binds";
			return false;
		}
	}
	return true;
}

// spirv-val on one module, through a file, because it reads files.
bool validate(const char * spirv_val, const std::vector<unsigned char> & spirv, std::string & output)
{
	const char * temp = getenv("TMPDIR");
	char path[1024];
	snprintf(path, sizeof(path), "%s/test_shader_sdl_%d.spv", (temp != NULL && *temp) ? temp : "/tmp", (int)getpid());
	FILE * file = fopen(path, "wb");
	if (file == NULL) return false;
	fwrite(&spirv[0], 1, spirv.size(), file);
	fclose(file);

	std::string command = std::string("\"") + spirv_val + "\" --target-env vulkan1.0 \"" + path + "\" 2>&1";
	FILE * pipe = popen(command.c_str(), "r");
	output.clear();
	if (pipe == NULL) return false;
	char line[512];
	while (fgets(line, sizeof(line), pipe) != NULL) output += line;
	const int status = pclose(pipe);
	remove(path);
	return status == 0;
}

void print_tally(const char * what, const Tally & t, bool validating, bool metal)
{
	printf("  %-15s %3u programs: generated %u, compiled %u, spirv-val %s, MSL %u (slots kept %u), Metal %s\n", what,
		t.programs, t.generated, t.compiled,
		validating ? (std::to_string(t.validated) + "/" + std::to_string(t.compiled)).c_str() : "SKIPPED",
		t.translated, t.slots_kept,
		metal ? (std::to_string(t.accepted) + "/" + std::to_string(t.translated)).c_str() : "SKIPPED");
}

} // namespace

TEST(every_generated_program_reaches_sdl3_gpu)
{
	const char * spirv_val = getenv("ZH_SPIRV_VAL");
	const bool validating = spirv_val != NULL && *spirv_val != '\0';
	if (!validating) {
		printf("  SKIPPED spirv-val: ZH_SPIRV_VAL is not set (CMake found no spirv-val; install spirv-tools)\n");
	}

	// Metal only.  On Linux the SPIR-V is the program, and spirv-val is the check there is.
	SDL_GPUDevice * device = NULL;
#if defined(__APPLE__)
	if (SDL_Init(SDL_INIT_VIDEO)) {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, NULL);
	}
	if (device == NULL) {
		printf("  SKIPPED Metal: no GPU device in this session (%s)\n", SDL_GetError());
	}
#endif
	const bool metal = device != NULL;

	Tally reference, extra;
	memset(&reference, 0, sizeof(reference));
	memset(&extra, 0, sizeof(extra));

	const std::vector<ShaderCase> cases = Shader_Cases();
	for (size_t i = 0; i < cases.size(); ++i) {
		const ShaderCase & c = cases[i];
		Tally & t = c.Reference ? reference : extra;
		++t.programs;

		std::string hlsl;
		if (!Shader_Case_Generate(c, SHADER_CASE_SDL3_GPU, hlsl)) {
			printf("  %s: the generator refused the SDL3 target\n", c.Name.c_str());
			CHECK(false);
			continue;
		}
		++t.generated;

		std::vector<unsigned char> spirv;
		std::string log;
		if (!SDL3_Compile_HLSL_To_SPIRV(hlsl, c.VertexStage, spirv, log)) {
			printf("  %s: HLSL -> SPIR-V failed:\n%s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.compiled;

		if (validating) {
			std::string output;
			if (validate(spirv_val, spirv, output)) {
				++t.validated;
			}
			else {
				printf("  %s: spirv-val refused it:\n%s\n", c.Name.c_str(), output.c_str());
				CHECK(false);
			}
		}

		std::string msl;
		if (!SDL3_Translate_SPIRV_To_MSL(spirv, c.VertexStage, msl, log)) {
			printf("  %s: SPIR-V -> MSL failed: %s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.translated;

		unsigned samplers = 0, uniform_buffers = 0;
		SDL3_Shader_Slots(spirv, c.VertexStage, samplers, uniform_buffers);
		std::string why;
		if (slots_kept(msl, samplers, why)) {
			++t.slots_kept;
		}
		else {
			printf("  %s: slot contract broken in its MSL: %s\n", c.Name.c_str(), why.c_str());
			CHECK(false);
		}

		if (metal) {
			SDL_GPUShader * shader = SDL3_Create_Shader(device, spirv, c.VertexStage, log);
			if (shader != NULL) {
				++t.accepted;
				SDL_ReleaseGPUShader(device, shader);
			}
			else {
				printf("  %s: Metal refused it: %s\n", c.Name.c_str(), log.c_str());
				CHECK(false);
			}
		}
	}

	print_tally("reference set", reference, validating, metal);
	print_tally("beyond it", extra, validating, metal);
	CHECK_EQ(reference.programs, 49u);
	CHECK(extra.programs > 0);

	if (device != NULL) {
		SDL_DestroyGPUDevice(device);
	}
	SDL_Quit();
}
