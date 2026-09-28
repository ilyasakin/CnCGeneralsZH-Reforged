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

// zh_d3d12.dll's COM adapter: the interfaces D3D12Adapter.h declares, and the resources'.

#include "D3D12Adapter.h"

#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <unordered_map>

namespace zhd3d12 {

//-------------------------------------------------------------------------------------------------
// Logging, and the methods posixd3d9 does not have.
//-------------------------------------------------------------------------------------------------

void Log(const char * format, ...)
{
	char line[512];
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	fputs(line, stderr);
	fflush(stderr);
	OutputDebugStringA(line);
}

HRESULT Unsupported(bool & said, const char * what)
{
	if (!said) {
		said = true;
		Log("zh_d3d12: %s is not implemented by the -d3d12 device; the call fails\n", what);
	}
	return D3DERR_INVALIDCALL;
}

//-------------------------------------------------------------------------------------------------
// The wrappers' map.
//-------------------------------------------------------------------------------------------------

static std::mutex & wrapper_lock()
{
	static std::mutex lock;
	return lock;
}

static std::unordered_map<zhposix::D3D9PosixUnknown *, WrapperCore *> & wrappers()
{
	static std::unordered_map<zhposix::D3D9PosixUnknown *, WrapperCore *> map;
	return map;
}

bool WrapperCore::Try_Add_Ref()
{
	LONG seen = References;
	while (seen != 0) {
		const LONG was = InterlockedCompareExchange(&References, seen + 1, seen);
		if (was == seen)
			return true;
		seen = was;
	}
	return false;
}

void WrapperCore::Forget(zhposix::D3D9PosixUnknown * object)
{
	std::lock_guard<std::mutex> hold(wrapper_lock());
	auto found = wrappers().find(object);
	if (found != wrappers().end() && found->second == this)
		wrappers().erase(found);
}

WrapperCore * Find_Wrapper(zhposix::D3D9PosixUnknown * object)
{
	std::lock_guard<std::mutex> hold(wrapper_lock());
	auto found = wrappers().find(object);
	if (found != wrappers().end() && found->second->Try_Add_Ref())
		return found->second;
	return NULL;
}

void Remember_Wrapper(zhposix::D3D9PosixUnknown * object, WrapperCore * wrapper)
{
	std::lock_guard<std::mutex> hold(wrapper_lock());
	wrappers()[object] = wrapper;
}

//-------------------------------------------------------------------------------------------------
// The resources.  Resource_Wrapper has IDirect3DResource9's methods, Texture_Wrapper IDirect3DBaseTexture9's.
//-------------------------------------------------------------------------------------------------

template <class Com, class Shim, D3DRESOURCETYPE Type>
class Resource_Wrapper : public Wrapper<Com, Shim>
{
public:
	Resource_Wrapper(Shim * object, DeviceAdapter * device) : Wrapper<Com, Shim>(object, device) {}

	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 ** device) override
	{
		if (device == NULL)
			return D3DERR_INVALIDCALL;
		this->Device->AddRef();
		*device = this->Device;
		return D3D_OK;
	}
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DResource9::SetPrivateData"); }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DResource9::GetPrivateData"); }
	HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DResource9::FreePrivateData"); }
	DWORD STDMETHODCALLTYPE SetPriority(DWORD priority) override { return this->Object->SetPriority(priority); }
	DWORD STDMETHODCALLTYPE GetPriority() override { return this->Object->GetPriority(); }
	void STDMETHODCALLTYPE PreLoad() override {}
	D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return Type; }
};

template <class Com, class Shim, D3DRESOURCETYPE Type>
class Texture_Wrapper : public Resource_Wrapper<Com, Shim, Type>
{
public:
	Texture_Wrapper(Shim * object, DeviceAdapter * device) : Resource_Wrapper<Com, Shim, Type>(object, device) {}

	DWORD STDMETHODCALLTYPE SetLOD(DWORD lod) override { return this->Object->SetLOD(lod); }
	DWORD STDMETHODCALLTYPE GetLOD() override { return this->Object->GetLOD(); }
	DWORD STDMETHODCALLTYPE GetLevelCount() override { return this->Object->GetLevelCount(); }
	HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DBaseTexture9::SetAutoGenFilterType"); }
	D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_LINEAR; }
	void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
};

class SurfaceAdapter : public Resource_Wrapper<IDirect3DSurface9, zhposix::IDirect3DSurface9, D3DRTYPE_SURFACE>
{
public:
	SurfaceAdapter(zhposix::IDirect3DSurface9 * object, DeviceAdapter * device) : Resource_Wrapper(object, device) {}

	HRESULT STDMETHODCALLTYPE GetContainer(REFIID iid, void ** container) override
	{
		if (container == NULL)
			return D3DERR_INVALIDCALL;
		*container = NULL;
		void * found = NULL;
		const HRESULT result = Object->GetContainer(*Same_Layout<zhposix::D3D9PosixGuid>(&iid), &found);
		if (FAILED(result) || found == NULL)
			return result;
		// The device answers only for the two it holds surfaces in (PosixSurface9::GetContainer).
		if (IsEqualGUID(iid, __uuidof(IDirect3DTexture9)))
			*container = Adopt_Texture(static_cast<zhposix::IDirect3DTexture9 *>(
				static_cast<zhposix::IDirect3DBaseTexture9 *>(found)), Device);
		else
			*container = Adopt_Cube_Texture(static_cast<zhposix::IDirect3DCubeTexture9 *>(
				static_cast<zhposix::IDirect3DBaseTexture9 *>(found)), Device);
		return D3D_OK;
	}
	HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC * desc) override
		{ return Object->GetDesc(Same_Layout<zhposix::D3DSURFACE_DESC>(desc)); }
	HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT * locked, const RECT * rect, DWORD flags) override
		{ return Object->LockRect(Same_Layout<zhposix::D3DLOCKED_RECT>(locked), rect, flags); }
	HRESULT STDMETHODCALLTYPE UnlockRect() override { return Object->UnlockRect(); }
	HRESULT STDMETHODCALLTYPE GetDC(HDC *) override { ZH_D3D12_UNSUPPORTED("IDirect3DSurface9::GetDC"); }
	HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { ZH_D3D12_UNSUPPORTED("IDirect3DSurface9::ReleaseDC"); }
};

class VolumeAdapter : public Wrapper<IDirect3DVolume9, zhposix::IDirect3DVolume9>
{
public:
	VolumeAdapter(zhposix::IDirect3DVolume9 * object, DeviceAdapter * device) : Wrapper(object, device) {}

	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 ** device) override
	{
		if (device == NULL)
			return D3DERR_INVALIDCALL;
		Device->AddRef();
		*device = Device;
		return D3D_OK;
	}
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DVolume9::SetPrivateData"); }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DVolume9::GetPrivateData"); }
	HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DVolume9::FreePrivateData"); }
	HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void **) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DVolume9::GetContainer"); }
	HRESULT STDMETHODCALLTYPE GetDesc(D3DVOLUME_DESC * desc) override
		{ return Object->GetDesc(Same_Layout<zhposix::D3DVOLUME_DESC>(desc)); }
	HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX * locked, const D3DBOX * box, DWORD flags) override
		{ return Object->LockBox(Same_Layout<zhposix::D3DLOCKED_BOX>(locked), Same_Layout<zhposix::D3DBOX>(box), flags); }
	HRESULT STDMETHODCALLTYPE UnlockBox() override { return Object->UnlockBox(); }
};

class TextureAdapter : public Texture_Wrapper<IDirect3DTexture9, zhposix::IDirect3DTexture9, D3DRTYPE_TEXTURE>
{
public:
	TextureAdapter(zhposix::IDirect3DTexture9 * object, DeviceAdapter * device) : Texture_Wrapper(object, device) {}

	HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC * desc) override
		{ return Object->GetLevelDesc(level, Same_Layout<zhposix::D3DSURFACE_DESC>(desc)); }
	HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9 ** surface) override
	{
		if (surface == NULL)
			return D3DERR_INVALIDCALL;
		zhposix::IDirect3DSurface9 * found = NULL;
		const HRESULT result = Object->GetSurfaceLevel(level, &found);
		*surface = Adopt<SurfaceAdapter>(found, Device);
		return result;
	}
	HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT * locked, const RECT * rect, DWORD flags) override
		{ return Object->LockRect(level, Same_Layout<zhposix::D3DLOCKED_RECT>(locked), rect, flags); }
	HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override { return Object->UnlockRect(level); }
	HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT * dirty) override { return Object->AddDirtyRect(dirty); }
};

class CubeTextureAdapter : public Texture_Wrapper<IDirect3DCubeTexture9, zhposix::IDirect3DCubeTexture9, D3DRTYPE_CUBETEXTURE>
{
public:
	CubeTextureAdapter(zhposix::IDirect3DCubeTexture9 * object, DeviceAdapter * device) : Texture_Wrapper(object, device) {}

	HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC * desc) override
		{ return Object->GetLevelDesc(level, Same_Layout<zhposix::D3DSURFACE_DESC>(desc)); }
	HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 ** surface) override
	{
		if (surface == NULL)
			return D3DERR_INVALIDCALL;
		zhposix::IDirect3DSurface9 * found = NULL;
		const HRESULT result = Object->GetCubeMapSurface((zhposix::D3DCUBEMAP_FACES)face, level, &found);
		*surface = Adopt<SurfaceAdapter>(found, Device);
		return result;
	}
	HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT * locked, const RECT * rect, DWORD flags) override
		{ return Object->LockRect((zhposix::D3DCUBEMAP_FACES)face, level, Same_Layout<zhposix::D3DLOCKED_RECT>(locked), rect, flags); }
	HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face, UINT level) override
		{ return Object->UnlockRect((zhposix::D3DCUBEMAP_FACES)face, level); }
	HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES face, const RECT * dirty) override
		{ return Object->AddDirtyRect((zhposix::D3DCUBEMAP_FACES)face, dirty); }
};

class VolumeTextureAdapter : public Texture_Wrapper<IDirect3DVolumeTexture9, zhposix::IDirect3DVolumeTexture9, D3DRTYPE_VOLUMETEXTURE>
{
public:
	VolumeTextureAdapter(zhposix::IDirect3DVolumeTexture9 * object, DeviceAdapter * device) : Texture_Wrapper(object, device) {}

	HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DVOLUME_DESC * desc) override
		{ return Object->GetLevelDesc(level, Same_Layout<zhposix::D3DVOLUME_DESC>(desc)); }
	HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT level, IDirect3DVolume9 ** volume) override
	{
		if (volume == NULL)
			return D3DERR_INVALIDCALL;
		zhposix::IDirect3DVolume9 * found = NULL;
		const HRESULT result = Object->GetVolumeLevel(level, &found);
		*volume = Adopt<VolumeAdapter>(found, Device);
		return result;
	}
	HRESULT STDMETHODCALLTYPE LockBox(UINT level, D3DLOCKED_BOX * locked, const D3DBOX * box, DWORD flags) override
		{ return Object->LockBox(level, Same_Layout<zhposix::D3DLOCKED_BOX>(locked), Same_Layout<zhposix::D3DBOX>(box), flags); }
	HRESULT STDMETHODCALLTYPE UnlockBox(UINT level) override { return Object->UnlockBox(level); }
	HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX *) override { return D3D_OK; }
};

template <class Com, class Shim, D3DRESOURCETYPE Type, class Desc, class ShimDesc>
class Buffer_Wrapper : public Resource_Wrapper<Com, Shim, Type>
{
public:
	Buffer_Wrapper(Shim * object, DeviceAdapter * device) : Resource_Wrapper<Com, Shim, Type>(object, device) {}

	HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void ** data, DWORD flags) override
		{ return this->Object->Lock(offset, size, data, flags); }
	HRESULT STDMETHODCALLTYPE Unlock() override { return this->Object->Unlock(); }
	HRESULT STDMETHODCALLTYPE GetDesc(Desc * desc) override { return this->Object->GetDesc(Same_Layout<ShimDesc>(desc)); }
};

typedef Buffer_Wrapper<IDirect3DVertexBuffer9, zhposix::IDirect3DVertexBuffer9, D3DRTYPE_VERTEXBUFFER,
	D3DVERTEXBUFFER_DESC, zhposix::D3DVERTEXBUFFER_DESC> VertexBufferAdapter;
typedef Buffer_Wrapper<IDirect3DIndexBuffer9, zhposix::IDirect3DIndexBuffer9, D3DRTYPE_INDEXBUFFER,
	D3DINDEXBUFFER_DESC, zhposix::D3DINDEXBUFFER_DESC> IndexBufferAdapter;

// The engine names its shaders by the pointer it holds, a vertex or a pixel shader it cannot tell apart
// (W3DShaderManager registers both through one call), so each shader wrapper is listed by that pointer.
static std::mutex & shader_lock()
{
	static std::mutex lock;
	return lock;
}

static std::unordered_map<const void *, const void *> & shaders()
{
	static std::unordered_map<const void *, const void *> map;
	return map;
}

const void * Shim_Of_Shader(const void * shader)
{
	std::lock_guard<std::mutex> hold(shader_lock());
	auto found = shaders().find(shader);
	return found != shaders().end() ? found->second : NULL;
}

// Shaders and declarations: handles, whose only method is GetDevice and one reading back what made them.
template <class Com, class Shim>
class Handle_Wrapper : public Wrapper<Com, Shim>
{
public:
	Handle_Wrapper(Shim * object, DeviceAdapter * device) : Wrapper<Com, Shim>(object, device)
	{
		std::lock_guard<std::mutex> hold(shader_lock());
		shaders()[static_cast<Com *>(this)] = object;
	}
	~Handle_Wrapper() override
	{
		std::lock_guard<std::mutex> hold(shader_lock());
		shaders().erase(static_cast<Com *>(this));
	}

	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 ** device) override
	{
		if (device == NULL)
			return D3DERR_INVALIDCALL;
		this->Device->AddRef();
		*device = this->Device;
		return D3D_OK;
	}
};

class VertexShaderAdapter : public Handle_Wrapper<IDirect3DVertexShader9, zhposix::IDirect3DVertexShader9>
{
public:
	VertexShaderAdapter(zhposix::IDirect3DVertexShader9 * object, DeviceAdapter * device) : Handle_Wrapper(object, device) {}
	HRESULT STDMETHODCALLTYPE GetFunction(void *, UINT *) override { ZH_D3D12_UNSUPPORTED("IDirect3DVertexShader9::GetFunction"); }
};

class PixelShaderAdapter : public Handle_Wrapper<IDirect3DPixelShader9, zhposix::IDirect3DPixelShader9>
{
public:
	PixelShaderAdapter(zhposix::IDirect3DPixelShader9 * object, DeviceAdapter * device) : Handle_Wrapper(object, device) {}
	HRESULT STDMETHODCALLTYPE GetFunction(void *, UINT *) override { ZH_D3D12_UNSUPPORTED("IDirect3DPixelShader9::GetFunction"); }
};

class VertexDeclarationAdapter : public Handle_Wrapper<IDirect3DVertexDeclaration9, zhposix::IDirect3DVertexDeclaration9>
{
public:
	VertexDeclarationAdapter(zhposix::IDirect3DVertexDeclaration9 * object, DeviceAdapter * device) : Handle_Wrapper(object, device) {}
	HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9 *, UINT *) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DVertexDeclaration9::GetDeclaration"); }
};

class SwapChainAdapter : public Wrapper<IDirect3DSwapChain9, zhposix::IDirect3DSwapChain9>
{
public:
	SwapChainAdapter(zhposix::IDirect3DSwapChain9 * object, DeviceAdapter * device) : Wrapper(object, device) {}

	HRESULT STDMETHODCALLTYPE Present(const RECT * source, const RECT * dest, HWND override_window, const RGNDATA * dirty_region,
		DWORD flags) override
		{ return Object->Present(source, dest, override_window, dirty_region, flags); }
	HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9 * dest) override { return Object->GetFrontBufferData(Shim_Of(dest)); }
	HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 ** surface) override
	{
		if (surface == NULL)
			return D3DERR_INVALIDCALL;
		zhposix::IDirect3DSurface9 * found = NULL;
		const HRESULT result = Object->GetBackBuffer(index, (zhposix::D3DBACKBUFFER_TYPE)type, &found);
		*surface = Adopt<SurfaceAdapter>(found, Device);
		return result;
	}
	HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS *) override { ZH_D3D12_UNSUPPORTED("IDirect3DSwapChain9::GetRasterStatus"); }
	HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE * mode) override
		{ return Object->GetDisplayMode(Same_Layout<zhposix::D3DDISPLAYMODE>(mode)); }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 ** device) override
	{
		if (device == NULL)
			return D3DERR_INVALIDCALL;
		Device->AddRef();
		*device = Device;
		return D3D_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS *) override
		{ ZH_D3D12_UNSUPPORTED("IDirect3DSwapChain9::GetPresentParameters"); }
};

//-------------------------------------------------------------------------------------------------
// From the engine's pointers to the device's objects, and back.
//-------------------------------------------------------------------------------------------------

zhposix::IDirect3DSurface9 * Shim_Of(IDirect3DSurface9 * surface)
	{ return surface != NULL ? static_cast<SurfaceAdapter *>(surface)->Object : NULL; }
zhposix::IDirect3DVertexBuffer9 * Shim_Of(IDirect3DVertexBuffer9 * buffer)
	{ return buffer != NULL ? static_cast<VertexBufferAdapter *>(buffer)->Object : NULL; }
zhposix::IDirect3DIndexBuffer9 * Shim_Of(IDirect3DIndexBuffer9 * buffer)
	{ return buffer != NULL ? static_cast<IndexBufferAdapter *>(buffer)->Object : NULL; }
zhposix::IDirect3DVertexDeclaration9 * Shim_Of(IDirect3DVertexDeclaration9 * declaration)
	{ return declaration != NULL ? static_cast<VertexDeclarationAdapter *>(declaration)->Object : NULL; }
zhposix::IDirect3DVertexShader9 * Shim_Of(IDirect3DVertexShader9 * shader)
	{ return shader != NULL ? static_cast<VertexShaderAdapter *>(shader)->Object : NULL; }
zhposix::IDirect3DPixelShader9 * Shim_Of(IDirect3DPixelShader9 * shader)
	{ return shader != NULL ? static_cast<PixelShaderAdapter *>(shader)->Object : NULL; }
zhposix::IDirect3DDevice9 * Shim_Of(IDirect3DDevice9 * device)
	{ return device != NULL ? static_cast<DeviceAdapter *>(device)->Device : NULL; }

zhposix::IDirect3DBaseTexture9 * Shim_Of(IDirect3DBaseTexture9 * texture)
{
	if (texture == NULL)
		return NULL;
	// GetType is each wrapper's own constant, so this reads no device state.
	switch (texture->GetType()) {
	case D3DRTYPE_TEXTURE: return static_cast<TextureAdapter *>(static_cast<IDirect3DTexture9 *>(texture))->Object;
	case D3DRTYPE_CUBETEXTURE: return static_cast<CubeTextureAdapter *>(static_cast<IDirect3DCubeTexture9 *>(texture))->Object;
	case D3DRTYPE_VOLUMETEXTURE: return static_cast<VolumeTextureAdapter *>(static_cast<IDirect3DVolumeTexture9 *>(texture))->Object;
	default: return NULL;
	}
}

IDirect3DTexture9 * Adopt_Texture(zhposix::IDirect3DTexture9 * texture, DeviceAdapter * device)
	{ return Adopt<TextureAdapter>(texture, device); }
IDirect3DCubeTexture9 * Adopt_Cube_Texture(zhposix::IDirect3DCubeTexture9 * texture, DeviceAdapter * device)
	{ return Adopt<CubeTextureAdapter>(texture, device); }
IDirect3DVolumeTexture9 * Adopt_Volume_Texture(zhposix::IDirect3DVolumeTexture9 * texture, DeviceAdapter * device)
	{ return Adopt<VolumeTextureAdapter>(texture, device); }

IDirect3DBaseTexture9 * Adopt_Base_Texture(zhposix::IDirect3DBaseTexture9 * texture, DeviceAdapter * device)
{
	if (texture == NULL)
		return NULL;
	switch (texture->GetType()) {
	case zhposix::D3DRTYPE_TEXTURE: return Adopt_Texture(static_cast<zhposix::IDirect3DTexture9 *>(texture), device);
	case zhposix::D3DRTYPE_CUBETEXTURE: return Adopt_Cube_Texture(static_cast<zhposix::IDirect3DCubeTexture9 *>(texture), device);
	case zhposix::D3DRTYPE_VOLUMETEXTURE: return Adopt_Volume_Texture(static_cast<zhposix::IDirect3DVolumeTexture9 *>(texture), device);
	default:
		texture->Release();
		return NULL;
	}
}

//-------------------------------------------------------------------------------------------------
// The device.
//-------------------------------------------------------------------------------------------------

DeviceAdapter::DeviceAdapter(zhposix::IDirect3DDevice9 * device, Direct3DAdapter * owner, const D3DDEVICE_CREATION_PARAMETERS & creation) :
	Device(device),
	References(1),
	Owner(owner),
	Creation(creation)
{
	Owner->AddRef();
}

DeviceAdapter::~DeviceAdapter()
{
	Device->Release();
	Owner->Release();
}

HRESULT DeviceAdapter::QueryInterface(REFIID iid, void ** out)
{
	if (out == NULL)
		return E_POINTER;
	if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, __uuidof(IDirect3DDevice9))) {
		AddRef();
		*out = static_cast<IDirect3DDevice9 *>(this);
		return S_OK;
	}
	*out = NULL;
	return E_NOINTERFACE;
}

ULONG DeviceAdapter::AddRef() { return (ULONG)InterlockedIncrement(&References); }

ULONG DeviceAdapter::Release()
{
	const LONG left = InterlockedDecrement(&References);
	if (left == 0)
		delete this;
	return (ULONG)left;
}

HRESULT DeviceAdapter::TestCooperativeLevel() { return Device->TestCooperativeLevel(); }
UINT DeviceAdapter::GetAvailableTextureMem() { return Device->GetAvailableTextureMem(); }
HRESULT DeviceAdapter::EvictManagedResources() { return Device->EvictManagedResources(); }

HRESULT DeviceAdapter::GetDirect3D(IDirect3D9 ** direct3d)
{
	if (direct3d == NULL)
		return D3DERR_INVALIDCALL;
	Owner->AddRef();
	*direct3d = Owner;
	return D3D_OK;
}

HRESULT DeviceAdapter::GetDeviceCaps(D3DCAPS9 * caps) { return Device->GetDeviceCaps(Same_Layout<zhposix::D3DCAPS9>(caps)); }
HRESULT DeviceAdapter::GetDisplayMode(UINT swap_chain, D3DDISPLAYMODE * mode)
	{ return Device->GetDisplayMode(swap_chain, Same_Layout<zhposix::D3DDISPLAYMODE>(mode)); }

HRESULT DeviceAdapter::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS * parameters)
{
	if (parameters == NULL)
		return D3DERR_INVALIDCALL;
	*parameters = Creation;
	return D3D_OK;
}

HRESULT DeviceAdapter::SetCursorProperties(UINT hotspot_x, UINT hotspot_y, IDirect3DSurface9 * bitmap)
	{ return Device->SetCursorProperties(hotspot_x, hotspot_y, Shim_Of(bitmap)); }
void DeviceAdapter::SetCursorPosition(int x, int y, DWORD flags) { Device->SetCursorPosition(x, y, flags); }
BOOL DeviceAdapter::ShowCursor(BOOL show) { return Device->ShowCursor(show); }

HRESULT DeviceAdapter::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS * parameters, IDirect3DSwapChain9 ** swap_chain)
{
	if (swap_chain == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DSwapChain9 * made = NULL;
	const HRESULT result = Device->CreateAdditionalSwapChain(Same_Layout<zhposix::D3DPRESENT_PARAMETERS>(parameters), &made);
	*swap_chain = Adopt<SwapChainAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::GetSwapChain(UINT, IDirect3DSwapChain9 **) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetSwapChain"); }
UINT DeviceAdapter::GetNumberOfSwapChains() { return 1; }
HRESULT DeviceAdapter::Reset(D3DPRESENT_PARAMETERS * parameters)
	{ return Device->Reset(Same_Layout<zhposix::D3DPRESENT_PARAMETERS>(parameters)); }
HRESULT DeviceAdapter::Present(const RECT * source, const RECT * dest, HWND override_window, const RGNDATA * dirty_region)
	{ return Device->Present(source, dest, override_window, dirty_region); }

HRESULT DeviceAdapter::GetBackBuffer(UINT swap_chain, UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 ** surface)
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DSurface9 * found = NULL;
	const HRESULT result = Device->GetBackBuffer(swap_chain, index, (zhposix::D3DBACKBUFFER_TYPE)type, &found);
	*surface = Adopt<SurfaceAdapter>(found, this);
	return result;
}

HRESULT DeviceAdapter::GetRasterStatus(UINT, D3DRASTER_STATUS *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetRasterStatus"); }
HRESULT DeviceAdapter::SetDialogBoxMode(BOOL) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetDialogBoxMode"); }
void DeviceAdapter::SetGammaRamp(UINT swap_chain, DWORD flags, const D3DGAMMARAMP * ramp)
	{ Device->SetGammaRamp(swap_chain, flags, Same_Layout<zhposix::D3DGAMMARAMP>(ramp)); }

void DeviceAdapter::GetGammaRamp(UINT, D3DGAMMARAMP * ramp)
{
	// The identity ramp, which is what SetGammaRamp starts from.
	if (ramp == NULL)
		return;
	for (int index = 0; index < 256; ++index)
		ramp->red[index] = ramp->green[index] = ramp->blue[index] = (WORD)(index * 257);
}

HRESULT DeviceAdapter::CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
	IDirect3DTexture9 ** texture, HANDLE * shared)
{
	if (texture == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DTexture9 * made = NULL;
	const HRESULT result = Device->CreateTexture(width, height, levels, usage, (zhposix::D3DFORMAT)format, (zhposix::D3DPOOL)pool,
		&made, shared);
	*texture = Adopt_Texture(made, this);
	return result;
}

HRESULT DeviceAdapter::CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
	D3DPOOL pool, IDirect3DVolumeTexture9 ** texture, HANDLE * shared)
{
	if (texture == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DVolumeTexture9 * made = NULL;
	const HRESULT result = Device->CreateVolumeTexture(width, height, depth, levels, usage, (zhposix::D3DFORMAT)format,
		(zhposix::D3DPOOL)pool, &made, shared);
	*texture = Adopt_Volume_Texture(made, this);
	return result;
}

HRESULT DeviceAdapter::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
	IDirect3DCubeTexture9 ** texture, HANDLE * shared)
{
	if (texture == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DCubeTexture9 * made = NULL;
	const HRESULT result = Device->CreateCubeTexture(edge, levels, usage, (zhposix::D3DFORMAT)format, (zhposix::D3DPOOL)pool,
		&made, shared);
	*texture = Adopt_Cube_Texture(made, this);
	return result;
}

HRESULT DeviceAdapter::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 ** buffer,
	HANDLE * shared)
{
	if (buffer == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DVertexBuffer9 * made = NULL;
	const HRESULT result = Device->CreateVertexBuffer(length, usage, fvf, (zhposix::D3DPOOL)pool, &made, shared);
	*buffer = Adopt<VertexBufferAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 ** buffer,
	HANDLE * shared)
{
	if (buffer == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DIndexBuffer9 * made = NULL;
	const HRESULT result = Device->CreateIndexBuffer(length, usage, (zhposix::D3DFORMAT)format, (zhposix::D3DPOOL)pool, &made, shared);
	*buffer = Adopt<IndexBufferAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample,
	DWORD quality, BOOL lockable, IDirect3DSurface9 ** surface, HANDLE * shared)
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DSurface9 * made = NULL;
	const HRESULT result = Device->CreateRenderTarget(width, height, (zhposix::D3DFORMAT)format,
		(zhposix::D3DMULTISAMPLE_TYPE)multisample, quality, lockable, &made, shared);
	*surface = Adopt<SurfaceAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample,
	DWORD quality, BOOL discard, IDirect3DSurface9 ** surface, HANDLE * shared)
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DSurface9 * made = NULL;
	const HRESULT result = Device->CreateDepthStencilSurface(width, height, (zhposix::D3DFORMAT)format,
		(zhposix::D3DMULTISAMPLE_TYPE)multisample, quality, discard, &made, shared);
	*surface = Adopt<SurfaceAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::UpdateSurface(IDirect3DSurface9 * source, const RECT * source_rect, IDirect3DSurface9 * dest,
	const POINT * dest_point)
	{ return Device->UpdateSurface(Shim_Of(source), source_rect, Shim_Of(dest), dest_point); }
HRESULT DeviceAdapter::UpdateTexture(IDirect3DBaseTexture9 * source, IDirect3DBaseTexture9 * dest)
	{ return Device->UpdateTexture(Shim_Of(source), Shim_Of(dest)); }
HRESULT DeviceAdapter::GetRenderTargetData(IDirect3DSurface9 * render_target, IDirect3DSurface9 * dest)
	{ return Device->GetRenderTargetData(Shim_Of(render_target), Shim_Of(dest)); }
HRESULT DeviceAdapter::GetFrontBufferData(UINT swap_chain, IDirect3DSurface9 * dest)
	{ return Device->GetFrontBufferData(swap_chain, Shim_Of(dest)); }
HRESULT DeviceAdapter::StretchRect(IDirect3DSurface9 * source, const RECT * source_rect, IDirect3DSurface9 * dest,
	const RECT * dest_rect, D3DTEXTUREFILTERTYPE filter)
	{ return Device->StretchRect(Shim_Of(source), source_rect, Shim_Of(dest), dest_rect, (zhposix::D3DTEXTUREFILTERTYPE)filter); }
HRESULT DeviceAdapter::ColorFill(IDirect3DSurface9 *, const RECT *, D3DCOLOR) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::ColorFill"); }

HRESULT DeviceAdapter::CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
	IDirect3DSurface9 ** surface, HANDLE * shared)
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DSurface9 * made = NULL;
	const HRESULT result = Device->CreateOffscreenPlainSurface(width, height, (zhposix::D3DFORMAT)format, (zhposix::D3DPOOL)pool,
		&made, shared);
	*surface = Adopt<SurfaceAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::SetRenderTarget(DWORD index, IDirect3DSurface9 * surface) { return Device->SetRenderTarget(index, Shim_Of(surface)); }

HRESULT DeviceAdapter::GetRenderTarget(DWORD index, IDirect3DSurface9 ** surface)
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DSurface9 * found = NULL;
	const HRESULT result = Device->GetRenderTarget(index, &found);
	*surface = Adopt<SurfaceAdapter>(found, this);
	return result;
}

HRESULT DeviceAdapter::SetDepthStencilSurface(IDirect3DSurface9 * surface) { return Device->SetDepthStencilSurface(Shim_Of(surface)); }

HRESULT DeviceAdapter::GetDepthStencilSurface(IDirect3DSurface9 ** surface)
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DSurface9 * found = NULL;
	const HRESULT result = Device->GetDepthStencilSurface(&found);
	*surface = Adopt<SurfaceAdapter>(found, this);
	return result;
}

HRESULT DeviceAdapter::BeginScene() { return Device->BeginScene(); }
HRESULT DeviceAdapter::EndScene() { return Device->EndScene(); }
HRESULT DeviceAdapter::Clear(DWORD count, const D3DRECT * rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
	{ return Device->Clear(count, Same_Layout<zhposix::D3DRECT>(rects), flags, color, z, stencil); }
HRESULT DeviceAdapter::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX * matrix)
	{ return Device->SetTransform((zhposix::D3DTRANSFORMSTATETYPE)state, Same_Layout<zhposix::D3DMATRIX>(matrix)); }
HRESULT DeviceAdapter::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX * matrix)
	{ return Device->GetTransform((zhposix::D3DTRANSFORMSTATETYPE)state, Same_Layout<zhposix::D3DMATRIX>(matrix)); }
HRESULT DeviceAdapter::MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::MultiplyTransform"); }
HRESULT DeviceAdapter::SetViewport(const D3DVIEWPORT9 * viewport) { return Device->SetViewport(Same_Layout<zhposix::D3DVIEWPORT9>(viewport)); }
HRESULT DeviceAdapter::GetViewport(D3DVIEWPORT9 * viewport) { return Device->GetViewport(Same_Layout<zhposix::D3DVIEWPORT9>(viewport)); }
HRESULT DeviceAdapter::SetMaterial(const D3DMATERIAL9 * material) { return Device->SetMaterial(Same_Layout<zhposix::D3DMATERIAL9>(material)); }
HRESULT DeviceAdapter::GetMaterial(D3DMATERIAL9 *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetMaterial"); }
HRESULT DeviceAdapter::SetLight(DWORD index, const D3DLIGHT9 * light) { return Device->SetLight(index, Same_Layout<zhposix::D3DLIGHT9>(light)); }
HRESULT DeviceAdapter::GetLight(DWORD, D3DLIGHT9 *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetLight"); }
HRESULT DeviceAdapter::LightEnable(DWORD index, BOOL enable) { return Device->LightEnable(index, enable); }
HRESULT DeviceAdapter::GetLightEnable(DWORD, BOOL *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetLightEnable"); }
HRESULT DeviceAdapter::SetClipPlane(DWORD index, const float * plane) { return Device->SetClipPlane(index, plane); }
HRESULT DeviceAdapter::GetClipPlane(DWORD, float *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetClipPlane"); }
HRESULT DeviceAdapter::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
	{ return Device->SetRenderState((zhposix::D3DRENDERSTATETYPE)state, value); }
HRESULT DeviceAdapter::GetRenderState(D3DRENDERSTATETYPE state, DWORD * value)
	{ return Device->GetRenderState((zhposix::D3DRENDERSTATETYPE)state, value); }
HRESULT DeviceAdapter::CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9 **) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::CreateStateBlock"); }
HRESULT DeviceAdapter::BeginStateBlock() { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::BeginStateBlock"); }
HRESULT DeviceAdapter::EndStateBlock(IDirect3DStateBlock9 **) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::EndStateBlock"); }
HRESULT DeviceAdapter::SetClipStatus(const D3DCLIPSTATUS9 *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetClipStatus"); }
HRESULT DeviceAdapter::GetClipStatus(D3DCLIPSTATUS9 *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetClipStatus"); }

HRESULT DeviceAdapter::GetTexture(DWORD stage, IDirect3DBaseTexture9 ** texture)
{
	if (texture == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DBaseTexture9 * found = NULL;
	const HRESULT result = Device->GetTexture(stage, &found);
	*texture = Adopt_Base_Texture(found, this);
	return result;
}

HRESULT DeviceAdapter::SetTexture(DWORD stage, IDirect3DBaseTexture9 * texture) { return Device->SetTexture(stage, Shim_Of(texture)); }
HRESULT DeviceAdapter::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD * value)
	{ return Device->GetTextureStageState(stage, (zhposix::D3DTEXTURESTAGESTATETYPE)type, value); }
HRESULT DeviceAdapter::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
	{ return Device->SetTextureStageState(stage, (zhposix::D3DTEXTURESTAGESTATETYPE)type, value); }
HRESULT DeviceAdapter::GetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetSamplerState"); }
HRESULT DeviceAdapter::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
	{ return Device->SetSamplerState(sampler, (zhposix::D3DSAMPLERSTATETYPE)type, value); }
HRESULT DeviceAdapter::ValidateDevice(DWORD * passes) { return Device->ValidateDevice(passes); }
HRESULT DeviceAdapter::SetPaletteEntries(UINT, const PALETTEENTRY *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetPaletteEntries"); }
HRESULT DeviceAdapter::GetPaletteEntries(UINT, PALETTEENTRY *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetPaletteEntries"); }
HRESULT DeviceAdapter::SetCurrentTexturePalette(UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetCurrentTexturePalette"); }
HRESULT DeviceAdapter::GetCurrentTexturePalette(UINT *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetCurrentTexturePalette"); }
HRESULT DeviceAdapter::SetScissorRect(const RECT *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetScissorRect"); }
HRESULT DeviceAdapter::GetScissorRect(RECT *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetScissorRect"); }
HRESULT DeviceAdapter::SetSoftwareVertexProcessing(BOOL software) { return Device->SetSoftwareVertexProcessing(software); }
BOOL DeviceAdapter::GetSoftwareVertexProcessing() { return FALSE; }
HRESULT DeviceAdapter::SetNPatchMode(float) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetNPatchMode"); }
float DeviceAdapter::GetNPatchMode() { return 0.0f; }
HRESULT DeviceAdapter::DrawPrimitive(D3DPRIMITIVETYPE type, UINT start_vertex, UINT primitive_count)
	{ return Device->DrawPrimitive((zhposix::D3DPRIMITIVETYPE)type, start_vertex, primitive_count); }
HRESULT DeviceAdapter::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_vertex, UINT vertex_count,
	UINT start_index, UINT primitive_count)
	{ return Device->DrawIndexedPrimitive((zhposix::D3DPRIMITIVETYPE)type, base_vertex, min_vertex, vertex_count, start_index, primitive_count); }
HRESULT DeviceAdapter::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitive_count, const void * vertices, UINT stride)
	{ return Device->DrawPrimitiveUP((zhposix::D3DPRIMITIVETYPE)type, primitive_count, vertices, stride); }
HRESULT DeviceAdapter::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT)
	{ ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::DrawIndexedPrimitiveUP"); }
HRESULT DeviceAdapter::ProcessVertices(UINT source_start, UINT dest_index, UINT vertex_count, IDirect3DVertexBuffer9 * dest,
	IDirect3DVertexDeclaration9 * declaration, DWORD flags)
	{ return Device->ProcessVertices(source_start, dest_index, vertex_count, Shim_Of(dest), Shim_Of(declaration), flags); }

HRESULT DeviceAdapter::CreateVertexDeclaration(const D3DVERTEXELEMENT9 * elements, IDirect3DVertexDeclaration9 ** declaration)
{
	if (declaration == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DVertexDeclaration9 * made = NULL;
	const HRESULT result = Device->CreateVertexDeclaration(Same_Layout<zhposix::D3DVERTEXELEMENT9>(elements), &made);
	*declaration = Adopt<VertexDeclarationAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::SetVertexDeclaration(IDirect3DVertexDeclaration9 * declaration)
	{ return Device->SetVertexDeclaration(Shim_Of(declaration)); }
HRESULT DeviceAdapter::GetVertexDeclaration(IDirect3DVertexDeclaration9 **) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetVertexDeclaration"); }
HRESULT DeviceAdapter::SetFVF(DWORD fvf) { return Device->SetFVF(fvf); }
HRESULT DeviceAdapter::GetFVF(DWORD *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetFVF"); }

HRESULT DeviceAdapter::CreateVertexShader(const DWORD * function, IDirect3DVertexShader9 ** shader)
{
	if (shader == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DVertexShader9 * made = NULL;
	const HRESULT result = Device->CreateVertexShader(function, &made);
	*shader = Adopt<VertexShaderAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::SetVertexShader(IDirect3DVertexShader9 * shader) { return Device->SetVertexShader(Shim_Of(shader)); }

HRESULT DeviceAdapter::GetVertexShader(IDirect3DVertexShader9 ** shader)
{
	if (shader == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DVertexShader9 * found = NULL;
	const HRESULT result = Device->GetVertexShader(&found);
	*shader = Adopt<VertexShaderAdapter>(found, this);
	return result;
}

HRESULT DeviceAdapter::SetVertexShaderConstantF(UINT start, const float * data, UINT count)
	{ return Device->SetVertexShaderConstantF(start, data, count); }
HRESULT DeviceAdapter::GetVertexShaderConstantF(UINT, float *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetVertexShaderConstantF"); }
HRESULT DeviceAdapter::SetVertexShaderConstantI(UINT, const int *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetVertexShaderConstantI"); }
HRESULT DeviceAdapter::GetVertexShaderConstantI(UINT, int *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetVertexShaderConstantI"); }
HRESULT DeviceAdapter::SetVertexShaderConstantB(UINT, const BOOL *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetVertexShaderConstantB"); }
HRESULT DeviceAdapter::GetVertexShaderConstantB(UINT, BOOL *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetVertexShaderConstantB"); }
HRESULT DeviceAdapter::SetStreamSource(UINT stream, IDirect3DVertexBuffer9 * buffer, UINT offset, UINT stride)
	{ return Device->SetStreamSource(stream, Shim_Of(buffer), offset, stride); }
HRESULT DeviceAdapter::GetStreamSource(UINT, IDirect3DVertexBuffer9 **, UINT *, UINT *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetStreamSource"); }
HRESULT DeviceAdapter::SetStreamSourceFreq(UINT, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetStreamSourceFreq"); }
HRESULT DeviceAdapter::GetStreamSourceFreq(UINT, UINT *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetStreamSourceFreq"); }
HRESULT DeviceAdapter::SetIndices(IDirect3DIndexBuffer9 * buffer) { return Device->SetIndices(Shim_Of(buffer)); }

HRESULT DeviceAdapter::GetIndices(IDirect3DIndexBuffer9 ** buffer)
{
	if (buffer == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DIndexBuffer9 * found = NULL;
	const HRESULT result = Device->GetIndices(&found);
	*buffer = Adopt<IndexBufferAdapter>(found, this);
	return result;
}

HRESULT DeviceAdapter::CreatePixelShader(const DWORD * function, IDirect3DPixelShader9 ** shader)
{
	if (shader == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DPixelShader9 * made = NULL;
	const HRESULT result = Device->CreatePixelShader(function, &made);
	*shader = Adopt<PixelShaderAdapter>(made, this);
	return result;
}

HRESULT DeviceAdapter::SetPixelShader(IDirect3DPixelShader9 * shader) { return Device->SetPixelShader(Shim_Of(shader)); }

HRESULT DeviceAdapter::GetPixelShader(IDirect3DPixelShader9 ** shader)
{
	if (shader == NULL)
		return D3DERR_INVALIDCALL;
	zhposix::IDirect3DPixelShader9 * found = NULL;
	const HRESULT result = Device->GetPixelShader(&found);
	*shader = Adopt<PixelShaderAdapter>(found, this);
	return result;
}

HRESULT DeviceAdapter::SetPixelShaderConstantF(UINT start, const float * data, UINT count)
	{ return Device->SetPixelShaderConstantF(start, data, count); }
HRESULT DeviceAdapter::GetPixelShaderConstantF(UINT, float *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetPixelShaderConstantF"); }
HRESULT DeviceAdapter::SetPixelShaderConstantI(UINT, const int *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetPixelShaderConstantI"); }
HRESULT DeviceAdapter::GetPixelShaderConstantI(UINT, int *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetPixelShaderConstantI"); }
HRESULT DeviceAdapter::SetPixelShaderConstantB(UINT, const BOOL *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::SetPixelShaderConstantB"); }
HRESULT DeviceAdapter::GetPixelShaderConstantB(UINT, BOOL *, UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::GetPixelShaderConstantB"); }
HRESULT DeviceAdapter::DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::DrawRectPatch"); }
HRESULT DeviceAdapter::DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::DrawTriPatch"); }
HRESULT DeviceAdapter::DeletePatch(UINT) { ZH_D3D12_UNSUPPORTED("IDirect3DDevice9::DeletePatch"); }
HRESULT DeviceAdapter::CreateQuery(D3DQUERYTYPE, IDirect3DQuery9 ** query)
{
	// A null query asks whether the type is supported; the answer is no for every type.
	if (query != NULL)
		*query = NULL;
	return D3DERR_NOTAVAILABLE;
}

//-------------------------------------------------------------------------------------------------
// Direct3DCreate9's object.
//-------------------------------------------------------------------------------------------------

Direct3DAdapter::Direct3DAdapter(zhposix::IDirect3D9 * direct3d) : References(1), Direct3D(direct3d) {}

Direct3DAdapter::~Direct3DAdapter() { Direct3D->Release(); }

HRESULT Direct3DAdapter::QueryInterface(REFIID iid, void ** out)
{
	if (out == NULL)
		return E_POINTER;
	if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, __uuidof(IDirect3D9))) {
		AddRef();
		*out = static_cast<IDirect3D9 *>(this);
		return S_OK;
	}
	*out = NULL;
	return E_NOINTERFACE;
}

ULONG Direct3DAdapter::AddRef() { return (ULONG)InterlockedIncrement(&References); }

ULONG Direct3DAdapter::Release()
{
	const LONG left = InterlockedDecrement(&References);
	if (left == 0)
		delete this;
	return (ULONG)left;
}

HRESULT Direct3DAdapter::RegisterSoftwareDevice(void *) { ZH_D3D12_UNSUPPORTED("IDirect3D9::RegisterSoftwareDevice"); }
UINT Direct3DAdapter::GetAdapterCount() { return Direct3D->GetAdapterCount(); }
HRESULT Direct3DAdapter::GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 * identifier)
	{ return Direct3D->GetAdapterIdentifier(adapter, flags, Same_Layout<zhposix::D3DADAPTER_IDENTIFIER9>(identifier)); }
UINT Direct3DAdapter::GetAdapterModeCount(UINT adapter, D3DFORMAT format)
	{ return Direct3D->GetAdapterModeCount(adapter, (zhposix::D3DFORMAT)format); }
HRESULT Direct3DAdapter::EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE * display_mode)
	{ return Direct3D->EnumAdapterModes(adapter, (zhposix::D3DFORMAT)format, mode, Same_Layout<zhposix::D3DDISPLAYMODE>(display_mode)); }
HRESULT Direct3DAdapter::GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE * mode)
	{ return Direct3D->GetAdapterDisplayMode(adapter, Same_Layout<zhposix::D3DDISPLAYMODE>(mode)); }
HRESULT Direct3DAdapter::CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT display_format, D3DFORMAT back_buffer_format,
	BOOL windowed)
	{ return Direct3D->CheckDeviceType(adapter, (zhposix::D3DDEVTYPE)type, (zhposix::D3DFORMAT)display_format,
		(zhposix::D3DFORMAT)back_buffer_format, windowed); }
HRESULT Direct3DAdapter::CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, DWORD usage,
	D3DRESOURCETYPE resource_type, D3DFORMAT check_format)
	{ return Direct3D->CheckDeviceFormat(adapter, (zhposix::D3DDEVTYPE)type, (zhposix::D3DFORMAT)adapter_format, usage,
		(zhposix::D3DRESOURCETYPE)resource_type, (zhposix::D3DFORMAT)check_format); }
HRESULT Direct3DAdapter::CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT surface_format, BOOL windowed,
	D3DMULTISAMPLE_TYPE multisample, DWORD * quality_levels)
	{ return Direct3D->CheckDeviceMultiSampleType(adapter, (zhposix::D3DDEVTYPE)type, (zhposix::D3DFORMAT)surface_format, windowed,
		(zhposix::D3DMULTISAMPLE_TYPE)multisample, quality_levels); }
HRESULT Direct3DAdapter::CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
	D3DFORMAT render_target_format, D3DFORMAT depth_stencil_format)
	{ return Direct3D->CheckDepthStencilMatch(adapter, (zhposix::D3DDEVTYPE)type, (zhposix::D3DFORMAT)adapter_format,
		(zhposix::D3DFORMAT)render_target_format, (zhposix::D3DFORMAT)depth_stencil_format); }
HRESULT Direct3DAdapter::CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT)
	{ ZH_D3D12_UNSUPPORTED("IDirect3D9::CheckDeviceFormatConversion"); }
HRESULT Direct3DAdapter::GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 * caps)
	{ return Direct3D->GetDeviceCaps(adapter, (zhposix::D3DDEVTYPE)type, Same_Layout<zhposix::D3DCAPS9>(caps)); }
HMONITOR Direct3DAdapter::GetAdapterMonitor(UINT)
{
	const POINT origin = { 0, 0 };
	return MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
}

HRESULT Direct3DAdapter::CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focus_window, DWORD behaviour_flags,
	D3DPRESENT_PARAMETERS * parameters, IDirect3DDevice9 ** device)
{
	if (device == NULL)
		return D3DERR_INVALIDCALL;
	*device = NULL;
	zhposix::IDirect3DDevice9 * made = NULL;
	const HRESULT result = Direct3D->CreateDevice(adapter, (zhposix::D3DDEVTYPE)type, focus_window, behaviour_flags,
		Same_Layout<zhposix::D3DPRESENT_PARAMETERS>(parameters), &made);
	if (FAILED(result) || made == NULL)
		return FAILED(result) ? result : D3DERR_NOTAVAILABLE;
	D3DDEVICE_CREATION_PARAMETERS creation;
	creation.AdapterOrdinal = adapter;
	creation.DeviceType = type;
	creation.hFocusWindow = focus_window;
	creation.BehaviorFlags = behaviour_flags;
	*device = new DeviceAdapter(made, this, creation);
	return result;
}

}  // namespace zhd3d12
