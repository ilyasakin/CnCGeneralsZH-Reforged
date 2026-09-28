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

// zh_d3d12.dll's COM adapter (X1, -d3d12): the SDK's Direct3D 9 interfaces, each forwarding to the SDL3 GPU
// device's own object of the same name (D3D12Bridge.h).
//
//   - One wrapper per device object, found again through a map from the device object to it, so a pointer
//     the engine gets twice (GetRenderTarget, GetSurfaceLevel) is the same pointer, as it is from d3d9.dll.
//   - A wrapper holds one reference on its device object and gives it up when the engine's last reference
//     to the wrapper goes.  A Get call that finds a live wrapper drops the extra reference the device
//     handed it, so the device's counts are what they would be without the adapter.
//   - AddRef and Release take no lock; the map's lock is taken where a wrapper is made, found again or
//     dies, and a wrapper found already dying is replaced rather than revived.
//   - The methods posixd3d9 does not have (state blocks, palettes, patches, queries and the other calls
//     the engine never makes off Windows) fail with D3DERR_INVALIDCALL and say so once each, by name.

#ifndef D3D12DEVICE_D3D12ADAPTER_H
#define D3D12DEVICE_D3D12ADAPTER_H

#include "D3D12Bridge.h"

namespace zhd3d12 {

/// One line to stderr and to the debugger, both of which a -d3d12 run can be watched through.
void Log(const char * format, ...);

/// The failure every missing method returns, said once per method.
HRESULT Unsupported(bool & said, const char * what);
#define ZH_D3D12_UNSUPPORTED(what) do { static bool said = false; return zhd3d12::Unsupported(said, what); } while (0)

class DeviceAdapter;

class WrapperCore
{
public:
	/// A reference for the map's finder, unless the count already reached zero (the wrapper is dying).
	bool Try_Add_Ref();

protected:
	WrapperCore() : References(1) {}
	virtual ~WrapperCore() {}
	/// Takes this wrapper's entry out of the map, if it is still the one there.
	void Forget(zhposix::D3D9PosixUnknown * object);

	volatile LONG References;
};

template <class Com, class Shim>
class Wrapper : public Com, public WrapperCore
{
public:
	typedef Com ComType;
	typedef Shim ShimType;

	Wrapper(Shim * object, DeviceAdapter * device) : Object(object), Device(device) {}

	Shim * const Object;
	DeviceAdapter * const Device;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void ** out) override
	{
		if (out == NULL)
			return E_POINTER;
		if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, __uuidof(Com))) {
			AddRef();
			*out = static_cast<Com *>(this);
			return S_OK;
		}
		*out = NULL;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&References); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const LONG left = InterlockedDecrement(&References);
		if (left == 0) {
			Forget(Object);
			Object->Release();
			delete this;
		}
		return (ULONG)left;
	}
};

/// The wrapper for a device object the engine is about to be handed, with the reference the device gave
/// the caller: made, or found again (and that reference dropped, since the wrapper holds one).
WrapperCore * Find_Wrapper(zhposix::D3D9PosixUnknown * object);
void Remember_Wrapper(zhposix::D3D9PosixUnknown * object, WrapperCore * wrapper);

template <class W>
typename W::ComType * Adopt(typename W::ShimType * object, DeviceAdapter * device)
{
	if (object == NULL)
		return NULL;
	if (WrapperCore * found = Find_Wrapper(object)) {
		object->Release();
		return static_cast<W *>(found);
	}
	W * made = new W(object, device);
	Remember_Wrapper(object, made);
	return made;
}

// The device's object behind each of the engine's pointers.  Every pointer the engine passes in is one the
// adapter handed out, so these are casts, not lookups.
zhposix::IDirect3DSurface9 * Shim_Of(IDirect3DSurface9 * surface);
zhposix::IDirect3DBaseTexture9 * Shim_Of(IDirect3DBaseTexture9 * texture);
zhposix::IDirect3DVertexBuffer9 * Shim_Of(IDirect3DVertexBuffer9 * buffer);
zhposix::IDirect3DIndexBuffer9 * Shim_Of(IDirect3DIndexBuffer9 * buffer);
zhposix::IDirect3DVertexDeclaration9 * Shim_Of(IDirect3DVertexDeclaration9 * declaration);
zhposix::IDirect3DVertexShader9 * Shim_Of(IDirect3DVertexShader9 * shader);
zhposix::IDirect3DPixelShader9 * Shim_Of(IDirect3DPixelShader9 * shader);
zhposix::IDirect3DDevice9 * Shim_Of(IDirect3DDevice9 * device);

/// The device's interface pointer behind a shader the engine holds, of either kind, which is what the device
/// names shaders by (PosixDevice_Name_Shader); NULL for a pointer the adapter did not hand out.
const void * Shim_Of_Shader(const void * shader);

// Wrapping a base texture the device hands back, by what it is.
IDirect3DBaseTexture9 * Adopt_Base_Texture(zhposix::IDirect3DBaseTexture9 * texture, DeviceAdapter * device);

IDirect3DTexture9 * Adopt_Texture(zhposix::IDirect3DTexture9 * texture, DeviceAdapter * device);
IDirect3DCubeTexture9 * Adopt_Cube_Texture(zhposix::IDirect3DCubeTexture9 * texture, DeviceAdapter * device);
IDirect3DVolumeTexture9 * Adopt_Volume_Texture(zhposix::IDirect3DVolumeTexture9 * texture, DeviceAdapter * device);

class Direct3DAdapter;

class DeviceAdapter : public IDirect3DDevice9
{
public:
	DeviceAdapter(zhposix::IDirect3DDevice9 * device, Direct3DAdapter * owner, const D3DDEVICE_CREATION_PARAMETERS & creation);

	zhposix::IDirect3DDevice9 * const Device;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void ** out) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
	UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
	HRESULT STDMETHODCALLTYPE EvictManagedResources() override;
	HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9 ** direct3d) override;
	HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 * caps) override;
	HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT swap_chain, D3DDISPLAYMODE * mode) override;
	HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS * parameters) override;
	HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT hotspot_x, UINT hotspot_y, IDirect3DSurface9 * bitmap) override;
	void STDMETHODCALLTYPE SetCursorPosition(int x, int y, DWORD flags) override;
	BOOL STDMETHODCALLTYPE ShowCursor(BOOL show) override;
	HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS * parameters, IDirect3DSwapChain9 ** swap_chain) override;
	HRESULT STDMETHODCALLTYPE GetSwapChain(UINT index, IDirect3DSwapChain9 ** swap_chain) override;
	UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override;
	HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS * parameters) override;
	HRESULT STDMETHODCALLTYPE Present(const RECT * source, const RECT * dest, HWND override_window, const RGNDATA * dirty_region) override;
	HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT swap_chain, UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 ** surface) override;
	HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT swap_chain, D3DRASTER_STATUS * status) override;
	HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL enable) override;
	void STDMETHODCALLTYPE SetGammaRamp(UINT swap_chain, DWORD flags, const D3DGAMMARAMP * ramp) override;
	void STDMETHODCALLTYPE GetGammaRamp(UINT swap_chain, D3DGAMMARAMP * ramp) override;
	HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
		IDirect3DTexture9 ** texture, HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
		D3DPOOL pool, IDirect3DVolumeTexture9 ** texture, HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
		IDirect3DCubeTexture9 ** texture, HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 ** buffer,
		HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 ** buffer,
		HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample,
		DWORD quality, BOOL lockable, IDirect3DSurface9 ** surface, HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample,
		DWORD quality, BOOL discard, IDirect3DSurface9 ** surface, HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 * source, const RECT * source_rect, IDirect3DSurface9 * dest,
		const POINT * dest_point) override;
	HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9 * source, IDirect3DBaseTexture9 * dest) override;
	HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9 * render_target, IDirect3DSurface9 * dest) override;
	HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT swap_chain, IDirect3DSurface9 * dest) override;
	HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 * source, const RECT * source_rect, IDirect3DSurface9 * dest,
		const RECT * dest_rect, D3DTEXTUREFILTERTYPE filter) override;
	HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 * surface, const RECT * rect, D3DCOLOR color) override;
	HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
		IDirect3DSurface9 ** surface, HANDLE * shared) override;
	HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index, IDirect3DSurface9 * surface) override;
	HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD index, IDirect3DSurface9 ** surface) override;
	HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9 * surface) override;
	HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9 ** surface) override;
	HRESULT STDMETHODCALLTYPE BeginScene() override;
	HRESULT STDMETHODCALLTYPE EndScene() override;
	HRESULT STDMETHODCALLTYPE Clear(DWORD count, const D3DRECT * rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
	HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX * matrix) override;
	HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX * matrix) override;
	HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX * matrix) override;
	HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 * viewport) override;
	HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9 * viewport) override;
	HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 * material) override;
	HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9 * material) override;
	HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT9 * light) override;
	HRESULT STDMETHODCALLTYPE GetLight(DWORD index, D3DLIGHT9 * light) override;
	HRESULT STDMETHODCALLTYPE LightEnable(DWORD index, BOOL enable) override;
	HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD index, BOOL * enable) override;
	HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, const float * plane) override;
	HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD index, float * plane) override;
	HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override;
	HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, DWORD * value) override;
	HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 ** block) override;
	HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
	HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9 ** block) override;
	HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 * status) override;
	HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9 * status) override;
	HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9 ** texture) override;
	HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9 * texture) override;
	HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD * value) override;
	HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override;
	HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD * value) override;
	HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override;
	HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD * passes) override;
	HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette, const PALETTEENTRY * entries) override;
	HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT palette, PALETTEENTRY * entries) override;
	HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT palette) override;
	HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT * palette) override;
	HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT * rect) override;
	HRESULT STDMETHODCALLTYPE GetScissorRect(RECT * rect) override;
	HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL software) override;
	BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override;
	HRESULT STDMETHODCALLTYPE SetNPatchMode(float segments) override;
	float STDMETHODCALLTYPE GetNPatchMode() override;
	HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT start_vertex, UINT primitive_count) override;
	HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_vertex, UINT vertex_count,
		UINT start_index, UINT primitive_count) override;
	HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitive_count, const void * vertices, UINT stride) override;
	HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min_vertex, UINT vertex_count, UINT primitive_count,
		const void * indices, D3DFORMAT index_format, const void * vertices, UINT stride) override;
	HRESULT STDMETHODCALLTYPE ProcessVertices(UINT source_start, UINT dest_index, UINT vertex_count, IDirect3DVertexBuffer9 * dest,
		IDirect3DVertexDeclaration9 * declaration, DWORD flags) override;
	HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 * elements, IDirect3DVertexDeclaration9 ** declaration) override;
	HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9 * declaration) override;
	HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9 ** declaration) override;
	HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override;
	HRESULT STDMETHODCALLTYPE GetFVF(DWORD * fvf) override;
	HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD * function, IDirect3DVertexShader9 ** shader) override;
	HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9 * shader) override;
	HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9 ** shader) override;
	HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start, const float * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start, float * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT start, const int * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT start, int * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT start, const BOOL * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT start, BOOL * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream, IDirect3DVertexBuffer9 * buffer, UINT offset, UINT stride) override;
	HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream, IDirect3DVertexBuffer9 ** buffer, UINT * offset, UINT * stride) override;
	HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT stream, UINT setting) override;
	HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT stream, UINT * setting) override;
	HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9 * buffer) override;
	HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9 ** buffer) override;
	HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD * function, IDirect3DPixelShader9 ** shader) override;
	HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9 * shader) override;
	HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9 ** shader) override;
	HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start, const float * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT start, float * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT start, const int * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT start, int * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT start, const BOOL * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT start, BOOL * data, UINT count) override;
	HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float * segments, const D3DRECTPATCH_INFO * info) override;
	HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float * segments, const D3DTRIPATCH_INFO * info) override;
	HRESULT STDMETHODCALLTYPE DeletePatch(UINT handle) override;
	HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 ** query) override;

private:
	~DeviceAdapter();

	volatile LONG References;
	Direct3DAdapter * const Owner;
	D3DDEVICE_CREATION_PARAMETERS Creation;
};

class Direct3DAdapter : public IDirect3D9
{
public:
	explicit Direct3DAdapter(zhposix::IDirect3D9 * direct3d);

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void ** out) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void * initialize) override;
	UINT STDMETHODCALLTYPE GetAdapterCount() override;
	HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 * identifier) override;
	UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override;
	HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE * display_mode) override;
	HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE * mode) override;
	HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT display_format, D3DFORMAT back_buffer_format,
		BOOL windowed) override;
	HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, DWORD usage,
		D3DRESOURCETYPE resource_type, D3DFORMAT check_format) override;
	HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT surface_format, BOOL windowed,
		D3DMULTISAMPLE_TYPE multisample, DWORD * quality_levels) override;
	HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
		D3DFORMAT render_target_format, D3DFORMAT depth_stencil_format) override;
	HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT adapter, D3DDEVTYPE type, D3DFORMAT source_format,
		D3DFORMAT target_format) override;
	HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 * caps) override;
	HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override;
	HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focus_window, DWORD behaviour_flags,
		D3DPRESENT_PARAMETERS * parameters, IDirect3DDevice9 ** device) override;

private:
	~Direct3DAdapter();

	volatile LONG References;
	zhposix::IDirect3D9 * const Direct3D;
};

}  // namespace zhd3d12

#endif // D3D12DEVICE_D3D12ADAPTER_H
