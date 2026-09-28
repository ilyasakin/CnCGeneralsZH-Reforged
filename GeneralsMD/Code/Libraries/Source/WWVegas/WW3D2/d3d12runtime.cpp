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

// -d3d12's switch (d3d12runtime.h).

#include "d3d12runtime.h"
#include "d3dx9runtime.h"

#include <stdio.h>

typedef IDirect3D9 * (WINAPI * Direct3D12CreateFunction)(UINT sdk_version);
typedef void (WINAPI * Direct3D12NameShaderFunction)(const void * shader, const char * name);
typedef void (WINAPI * Direct3D12KeepDeclarationFunction)(IDirect3DVertexDeclaration9 * declaration, const DWORD * d3d8_tokens);

static HMODULE Module = NULL;
static Direct3D12CreateFunction Create = NULL;
static Direct3D12NameShaderFunction NameShader = NULL;
static Direct3D12KeepDeclarationFunction KeepDeclaration = NULL;

bool Direct3D12_Activate(char * why, size_t why_size)
{
	if (Module != NULL) {
		return true;
	}
	// Only the executable's folder and System32: never the working directory.  A module that is there but
	// cannot load (a missing dependency, the wrong architecture) fails the call rather than showing the
	// loader's dialog, as a player's missing file must not stop a run on a message box.
	DWORD previous = 0;
	SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previous);
	HMODULE module = LoadLibraryExA("zh_d3d12.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	const DWORD error = module == NULL ? GetLastError() : 0;
	SetThreadErrorMode(previous, NULL);
	if (module == NULL) {
		_snprintf_s(why, why_size, _TRUNCATE, "zh_d3d12.dll did not load (Windows error %lu)", (unsigned long)error);
		return false;
	}
	Direct3D12CreateFunction create = (Direct3D12CreateFunction)GetProcAddress(module, "ZH_D3D12_Direct3DCreate9");
	Direct3D12NameShaderFunction name_shader = (Direct3D12NameShaderFunction)GetProcAddress(module, "ZH_D3D12_Name_Shader");
	Direct3D12KeepDeclarationFunction keep_declaration =
		(Direct3D12KeepDeclarationFunction)GetProcAddress(module, "ZH_D3D12_Keep_D3D8_Declaration");
	if (create == NULL || name_shader == NULL || keep_declaration == NULL) {
		_snprintf_s(why, why_size, _TRUNCATE, "zh_d3d12.dll is not this build's (an entry point is missing)");
		FreeLibrary(module);
		return false;
	}
	Module = module;
	Create = create;
	NameShader = name_shader;
	KeepDeclaration = keep_declaration;
	// Anything bound before now (Get_FVF_Vertex_Size binds on its own) was d3dx9_43.dll's, whose textures
	// the module's device cannot take.
	Unbind_D3DX9_Runtime();
	return true;
}

bool Direct3D12_Is_Active(void)
{
	return Module != NULL;
}

HMODULE Direct3D12_Module(void)
{
	return Module;
}

IDirect3D9 * Direct3D12_Create(UINT sdk_version)
{
	return Create != NULL ? Create(sdk_version) : NULL;
}

void Direct3D12_Name_Shader(const void * shader, const char * name)
{
	if (NameShader != NULL) {
		NameShader(shader, name);
	}
}

void Direct3D12_Keep_D3D8_Declaration(IDirect3DVertexDeclaration9 * declaration, const DWORD * d3d8_tokens)
{
	if (KeepDeclaration != NULL) {
		KeepDeclaration(declaration, d3d8_tokens);
	}
}
