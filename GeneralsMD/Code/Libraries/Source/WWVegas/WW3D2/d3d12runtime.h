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

// -d3d12 (X1): the Direct3D 9 the engine draws with comes from zh_d3d12.dll - the SDL3 GPU device macOS and
// Linux draw with, on Direct3D 12, behind a COM adapter - instead of d3d9.dll, and D3DX's entry points come
// from the same module instead of d3dx9_43.dll.  Windows only; off Windows that device is the renderer.
//
// W3DDisplay::init activates it before WW3D::Init, because DX8Wrapper::Init makes the Direct3D 9 interface
// and binds D3DX from whatever this says then.  Activation is one load of the module; if it fails the run
// keeps the default renderer and W3DDisplay logs why, and nothing here shows a dialog.

#ifndef D3D12RUNTIME_H
#define D3D12RUNTIME_H

#if defined(_WIN32)

#include <d3d9.h>

/// Loads zh_d3d12.dll from the executable's folder and finds its entry points.  True if it is there and
/// complete, and from then on for the rest of the run; otherwise false, with why in 'why'.  D3DX is unbound,
/// so the next Bind_D3DX9_Runtime binds from the module rather than keeping d3dx9_43.dll's.
bool Direct3D12_Activate(char * why, size_t why_size);

/// Whether this run draws through zh_d3d12.dll.
bool Direct3D12_Is_Active(void);

/// The module, for the D3DX binder; NULL unless active.
HMODULE Direct3D12_Module(void);

/// Direct3DCreate9, from the module; NULL unless active.
IDirect3D9 * Direct3D12_Create(UINT sdk_version);

/// The two things the module's device reads that d3d9.dll never did (Platform/EngineShaderName.h, which is
/// how the POSIX device gets them): the name the engine registered a shader under, and the D3D8 declaration
/// a vertex declaration was decoded from.  Nothing unless active.
void Direct3D12_Name_Shader(const void * shader, const char * name);
void Direct3D12_Keep_D3D8_Declaration(IDirect3DVertexDeclaration9 * declaration, const DWORD * d3d8_tokens);

/// Where the module keeps the programs it compiles: the user data folder (d3d12shaders.cache), beside the
/// d3d12shaders.shipped next to the executable.  Before the device makes its first program.
void Direct3D12_Set_Shader_Cache_Directory(const char * directory);

#endif // _WIN32

#endif // D3D12RUNTIME_H
