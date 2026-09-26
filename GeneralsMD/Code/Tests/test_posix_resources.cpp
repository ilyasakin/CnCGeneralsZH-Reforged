/*
 * Decision 7's A2: the D3D9-shaped device's resources in memory (PosixDevice/Render/PosixResources9).
 * Formats' block layouts, mip chains, locks and their D3D9 rules, COM reference counting between a
 * texture and its surfaces, cube and volume textures, and buffers.  Run under ASan where it can be,
 * since a reference count that is off shows up as a leak or a use after free, not as a wrong value.
 */
#include "test_harness.h"

#include "PosixResources9.h"

#include <string.h>

namespace {

PosixFormatLayout layout_of(D3DFORMAT format)
{
	PosixFormatLayout layout = { 0, 0, 0 };
	posixFormatLayout(format, &layout);
	return layout;
}

} // namespace

TEST(posix_formats_have_d3d9_block_layouts)
{
	CHECK_EQ(layout_of(D3DFMT_A8R8G8B8).bytesPerBlock, 4u);
	CHECK_EQ(layout_of(D3DFMT_R8G8B8).bytesPerBlock, 3u);
	CHECK_EQ(layout_of(D3DFMT_R5G6B5).bytesPerBlock, 2u);
	CHECK_EQ(layout_of(D3DFMT_L8).bytesPerBlock, 1u);
	CHECK_EQ(layout_of(D3DFMT_DXT1).blockWidth, 4u);
	CHECK_EQ(layout_of(D3DFMT_DXT1).blockHeight, 4u);
	CHECK_EQ(layout_of(D3DFMT_DXT1).bytesPerBlock, 8u);
	CHECK_EQ(layout_of(D3DFMT_DXT3).bytesPerBlock, 16u);
	CHECK_EQ(layout_of(D3DFMT_DXT5).bytesPerBlock, 16u);
	CHECK_EQ(layout_of(D3DFMT_YUY2).blockWidth, 2u);
	CHECK_EQ(layout_of(D3DFMT_YUY2).bytesPerBlock, 4u);
	CHECK_EQ(layout_of(D3DFMT_D24S8).bytesPerBlock, 4u);
	CHECK_EQ(layout_of(D3DFMT_A32B32G32R32F).bytesPerBlock, 16u);
	CHECK(!posixFormatLayout(D3DFMT_UNKNOWN, NULL));
	CHECK(!posixFormatLayout(D3DFMT_VERTEXDATA, NULL));
}

TEST(posix_texture_full_chain_and_level_sizes)
{
	CHECK_EQ(posixFullChainLength(256, 64, 1), 9u);
	CHECK_EQ(posixFullChainLength(1, 1, 1), 1u);
	CHECK_EQ(posixFullChainLength(3, 1, 1), 2u);

	PosixTexture9 *texture = PosixTexture9::create(256, 64, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED);
	CHECK(texture != NULL);
	if (texture == NULL) return;
	CHECK_EQ(texture->GetLevelCount(), 9u);
	D3DSURFACE_DESC desc;
	CHECK_EQ(texture->GetLevelDesc(2, &desc), D3D_OK);
	CHECK_EQ(desc.Width, 64u);
	CHECK_EQ(desc.Height, 16u);
	CHECK_EQ(desc.Format, D3DFMT_DXT1);
	CHECK_EQ(desc.Pool, D3DPOOL_MANAGED);
	CHECK_EQ(texture->GetLevelDesc(8, &desc), D3D_OK);
	CHECK_EQ(desc.Width, 1u);
	CHECK_EQ(desc.Height, 1u);
	CHECK_EQ(texture->GetLevelDesc(9, &desc), D3DERR_INVALIDCALL);
	// DXT pitch is one row of 4x4 blocks: 256 / 4 * 8, and a 1x1 level is still one whole block.
	CHECK_EQ(texture->level(0).rowPitch(), 512u);
	CHECK_EQ(texture->level(8).rowPitch(), 8u);
	CHECK_EQ(texture->level(8).size(), (size_t)8);
	CHECK_EQ(texture->Release(), 0u);

	PosixTexture9 *two = PosixTexture9::create(64, 64, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK_EQ(two->GetLevelCount(), 2u);
	two->Release();
	PosixTexture9 *autogen = PosixTexture9::create(64, 64, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
	CHECK_EQ(autogen->GetLevelCount(), 1u);
	autogen->Release();
	CHECK(PosixTexture9::create(64, 64, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED) == NULL);
	CHECK(PosixTexture9::create(0, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED) == NULL);
}

TEST(posix_texture_locks_write_and_read_back)
{
	PosixTexture9 *texture = PosixTexture9::create(8, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	D3DLOCKED_RECT locked;
	CHECK_EQ(texture->LockRect(0, &locked, NULL, 0), D3D_OK);
	CHECK_EQ(locked.Pitch, 32);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 8; ++x)
			((uint32_t *)((uint8_t *)locked.pBits + y * locked.Pitch))[x] = 0xff000000u | (uint32_t)(y * 8 + x);
	// locked twice, and unlocked twice: both are errors
	D3DLOCKED_RECT again;
	CHECK_EQ(texture->LockRect(0, &again, NULL, 0), D3DERR_INVALIDCALL);
	CHECK_EQ(texture->UnlockRect(0), D3D_OK);
	CHECK_EQ(texture->UnlockRect(0), D3DERR_INVALIDCALL);
	CHECK_EQ(texture->level(0).version(), 1u);

	// a sub-rectangle, read-only: the right address, and no write counted
	RenderRect rect = { 2, 1, 5, 3 };
	CHECK_EQ(texture->LockRect(0, &locked, &rect, D3DLOCK_READONLY), D3D_OK);
	CHECK_EQ(((uint32_t *)locked.pBits)[0], 0xff000000u | 10u);
	CHECK_EQ(((uint32_t *)((uint8_t *)locked.pBits + locked.Pitch))[2], 0xff000000u | 20u);
	CHECK_EQ(texture->UnlockRect(0), D3D_OK);
	CHECK_EQ(texture->level(0).version(), 1u);

	RenderRect outside = { 0, 0, 9, 4 };
	CHECK_EQ(texture->LockRect(0, &locked, &outside, 0), D3DERR_INVALIDCALL);
	CHECK(locked.pBits == NULL);
	RenderRect empty = { 3, 1, 3, 2 };
	CHECK_EQ(texture->LockRect(0, &locked, &empty, 0), D3DERR_INVALIDCALL);
	texture->Release();
}

TEST(posix_block_format_locks_are_on_block_boundaries)
{
	PosixTexture9 *texture = PosixTexture9::create(16, 16, 1, 0, D3DFMT_DXT5, D3DPOOL_MANAGED);
	D3DLOCKED_RECT locked;
	CHECK_EQ(texture->LockRect(0, &locked, NULL, 0), D3D_OK);
	uint8_t *base = (uint8_t *)locked.pBits;
	CHECK_EQ(locked.Pitch, 64);
	texture->UnlockRect(0);

	RenderRect aligned = { 4, 8, 12, 16 };
	CHECK_EQ(texture->LockRect(0, &locked, &aligned, 0), D3D_OK);
	CHECK(locked.pBits == base + 2 * 64 + 1 * 16);
	texture->UnlockRect(0);
	RenderRect misaligned = { 1, 0, 4, 4 };
	CHECK_EQ(texture->LockRect(0, &locked, &misaligned, 0), D3DERR_INVALIDCALL);
	texture->Release();

	// a 6x6 DXT1 image: blocks cover 8x8, and a rectangle may end at the image's edge
	PosixTexture9 *odd = PosixTexture9::create(6, 6, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED);
	CHECK_EQ(odd->level(0).size(), (size_t)(2 * 2 * 8));
	RenderRect edge = { 4, 4, 6, 6 };
	CHECK_EQ(odd->LockRect(0, &locked, &edge, 0), D3D_OK);
	odd->UnlockRect(0);
	RenderRect short_of_edge = { 0, 0, 5, 4 };
	CHECK_EQ(odd->LockRect(0, &locked, &short_of_edge, 0), D3DERR_INVALIDCALL);
	odd->Release();
}

TEST(posix_texture_surfaces_count_on_the_texture)
{
	PosixTexture9 *texture = PosixTexture9::create(32, 32, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK_EQ(texture->references(), 1u);
	IDirect3DSurface9 *surface = NULL;
	CHECK_EQ(texture->GetSurfaceLevel(1, &surface), D3D_OK);
	CHECK(surface != NULL);
	CHECK_EQ(texture->references(), 2u);
	IDirect3DSurface9 *same = NULL;
	texture->GetSurfaceLevel(1, &same);
	CHECK(same == surface);
	CHECK_EQ(texture->references(), 3u);
	same->Release();

	D3DSURFACE_DESC desc;
	CHECK_EQ(surface->GetDesc(&desc), D3D_OK);
	CHECK_EQ(desc.Width, 16u);
	CHECK_EQ(desc.Type, D3DRTYPE_SURFACE);

	void *container = NULL;
	CHECK_EQ(surface->GetContainer(IID_IDirect3DTexture9, &container), D3D_OK);
	CHECK(container == (IDirect3DTexture9 *)texture);
	CHECK_EQ(texture->references(), 3u);
	((IDirect3DTexture9 *)container)->Release();
	CHECK_EQ(surface->GetContainer(IID_IDirect3DCubeTexture9, &container), POSIX_D3D_NOINTERFACE);
	CHECK(container == NULL);

	// the surface keeps its texture alive: drop the texture's own reference, write through the surface
	CHECK_EQ(texture->Release(), 1u);
	D3DLOCKED_RECT locked;
	CHECK_EQ(surface->LockRect(&locked, NULL, 0), D3D_OK);
	memset(locked.pBits, 0xab, 16 * 4);
	CHECK_EQ(surface->UnlockRect(), D3D_OK);
	CHECK_EQ(surface->Release(), 0u);		// the texture goes with it (ASan says if it does not)
}

TEST(posix_standalone_surfaces_count_themselves)
{
	PosixSurface9 *surface = PosixSurface9::createStandalone(100, 100, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET,
		D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0);
	CHECK(surface != NULL);
	D3DSURFACE_DESC desc;
	surface->GetDesc(&desc);
	CHECK_EQ(desc.Usage, (RenderUInt32)D3DUSAGE_RENDERTARGET);
	CHECK_EQ(desc.Pool, D3DPOOL_DEFAULT);
	void *container = (void *)1;
	CHECK_EQ(surface->GetContainer(IID_IDirect3DTexture9, &container), POSIX_D3D_NOINTERFACE);
	CHECK(container == NULL);
	CHECK_EQ(surface->AddRef(), 2u);
	CHECK_EQ(surface->Release(), 1u);
	CHECK_EQ(surface->Release(), 0u);
	CHECK(PosixSurface9::createStandalone(16, 16, D3DFMT_UNKNOWN, 0, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0) == NULL);
}

TEST(posix_cube_textures_have_six_separate_faces)
{
	PosixCubeTexture9 *cube = PosixCubeTexture9::create(16, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK(cube != NULL);
	CHECK_EQ(cube->GetLevelCount(), 5u);
	D3DLOCKED_RECT locked;
	for (int face = 0; face < 6; ++face)
	{
		CHECK_EQ(cube->LockRect((D3DCUBEMAP_FACES)face, 0, &locked, NULL, 0), D3D_OK);
		((uint32_t *)locked.pBits)[0] = 0x100u + (uint32_t)face;
		cube->UnlockRect((D3DCUBEMAP_FACES)face, 0);
	}
	for (int face = 0; face < 6; ++face)
		CHECK_EQ(((const uint32_t *)cube->image(face, 0)->bytes())[0], 0x100u + (uint32_t)face);
	CHECK_EQ(cube->LockRect((D3DCUBEMAP_FACES)6, 0, &locked, NULL, 0), D3DERR_INVALIDCALL);

	IDirect3DSurface9 *surface = NULL;
	CHECK_EQ(cube->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Y, 2, &surface), D3D_OK);
	void *container = NULL;
	CHECK_EQ(surface->GetContainer(IID_IDirect3DCubeTexture9, &container), D3D_OK);
	CHECK(container == (IDirect3DCubeTexture9 *)cube);
	((IDirect3DCubeTexture9 *)container)->Release();
	CHECK_EQ(surface->GetContainer(IID_IDirect3DTexture9, &container), POSIX_D3D_NOINTERFACE);
	surface->Release();
	CHECK_EQ(cube->Release(), 0u);
}

TEST(posix_volume_textures_lock_boxes)
{
	PosixVolumeTexture9 *volume = PosixVolumeTexture9::create(32, 16, 8, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK(volume != NULL);
	CHECK_EQ(volume->GetLevelCount(), 6u);
	D3DVOLUME_DESC desc;
	CHECK_EQ(volume->GetLevelDesc(1, &desc), D3D_OK);
	CHECK_EQ(desc.Width, 16u);
	CHECK_EQ(desc.Height, 8u);
	CHECK_EQ(desc.Depth, 4u);
	D3DLOCKED_BOX locked;
	D3DBOX box = { 1, 2, 3, 4, 5, 6 };		// Left, Top, Right, Bottom, Front, Back
	CHECK_EQ(volume->LockBox(0, &locked, &box, 0), D3D_OK);
	CHECK_EQ(locked.RowPitch, 128);
	CHECK_EQ(locked.SlicePitch, 128 * 16);
	CHECK(locked.pBits == volume->level(0).bytes() + 5 * 128 * 16 + 2 * 128 + 1 * 4);
	CHECK_EQ(volume->UnlockBox(0), D3D_OK);
	IDirect3DVolume9 *level = NULL;
	CHECK_EQ(volume->GetVolumeLevel(5, &level), D3D_OK);
	CHECK_EQ(level->GetDesc(&desc), D3D_OK);
	CHECK_EQ(desc.Width, 1u);
	level->Release();
	CHECK_EQ(volume->Release(), 0u);
}

TEST(posix_buffers_lock_ranges_as_d3d9_does)
{
	PosixVertexBuffer9 *vertices = PosixVertexBuffer9::create(1024, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, D3DFVF_XYZ, D3DPOOL_DEFAULT);
	CHECK(vertices != NULL);
	void *data = NULL;
	CHECK_EQ(vertices->Lock(0, 0, &data, D3DLOCK_DISCARD), D3D_OK);		// 0, 0: the whole buffer
	CHECK(data == vertices->storage().bytes());
	void *more = NULL;
	CHECK_EQ(vertices->Lock(1000, 24, &more, D3DLOCK_NOOVERWRITE), D3D_OK);	// a second lock is allowed
	CHECK(more == vertices->storage().bytes() + 1000);
	CHECK_EQ(vertices->Lock(1000, 25, &more, 0), D3DERR_INVALIDCALL);
	CHECK(more == NULL);
	CHECK_EQ(vertices->Unlock(), D3D_OK);
	CHECK_EQ(vertices->Unlock(), D3D_OK);
	CHECK_EQ(vertices->Unlock(), D3DERR_INVALIDCALL);
	D3DVERTEXBUFFER_DESC vdesc;
	vertices->GetDesc(&vdesc);
	CHECK_EQ(vdesc.Size, 1024u);
	CHECK_EQ(vdesc.FVF, (RenderUInt32)D3DFVF_XYZ);
	CHECK_EQ(vdesc.Format, D3DFMT_VERTEXDATA);
	CHECK_EQ(vertices->Release(), 0u);

	PosixIndexBuffer9 *indices = PosixIndexBuffer9::create(600, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED);
	D3DINDEXBUFFER_DESC idesc;
	indices->GetDesc(&idesc);
	CHECK_EQ(idesc.Format, D3DFMT_INDEX16);
	CHECK_EQ(idesc.Size, 600u);
	indices->Release();
	CHECK(PosixIndexBuffer9::create(600, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED) == NULL);
	CHECK(PosixVertexBuffer9::create(0, 0, 0, D3DPOOL_MANAGED) == NULL);
}
