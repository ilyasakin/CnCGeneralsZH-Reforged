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

/*
** The device's frame on SDL3 GPU (decision 7, phase A3a): the SDL_GPUDevice, C2's window claimed for it,
** the offscreen back buffer and depth-stencil the engine draws into, and Present, which takes the back
** buffer to the window through D3D9's gamma ramp.  Only a device with a window has one: -headless makes
** no SDL_GPUDevice at all (tasks/A-posix-d3d9-device.md, "A3 design").
**
** Nothing is issued as the engine asks for it.  Clears and draws are recorded into a batch, with a copy
** of every byte a draw reads that can still change (the staging stream) and the uploads of the GPU
** copies it needs; Flush runs the batch as one copy pass and then render passes that replay the records
** in order, a clear being the load operation of the pass after it (A3c; tasks/A-posix-d3d9-device.md,
** "The frame").  Only a clear of the whole target is taken here; a clear of part of it is a clear draw.
**
** Main thread only, as every SDL GPU call is.
*/

#pragma once

#ifndef SDLGPUFRAME_H
#define SDLGPUFRAME_H

#include "Platform/RenderTypes.h"

#include <stdint.h>
#include <string>
#include <vector>

struct SDL_GPUBuffer;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUShader;
struct SDL_GPUTexture;
struct SDL_Window;

/// One draw as the flush replays it, everything by value.  The device's resolve fills it.
struct SdlRecordedDraw
{
	enum { MAXIMUM_SAMPLERS = 8 };
	SDL_GPUGraphicsPipeline * Pipeline;
	SDL_GPUBuffer * VertexBuffer;		///< a GPU copy, or null for the batch's staging stream
	uint32_t VertexOffset;				///< bytes into it
	SDL_GPUBuffer * IndexBuffer;		///< as VertexBuffer, when IndexSize is not 0
	uint32_t IndexOffset;
	uint32_t IndexSize;					///< 0 (not indexed), 2 or 4
	uint32_t Count;						///< vertices, or indices
	uint32_t First;						///< the first vertex, or the first index
	int32_t BaseVertex;
	uint32_t VertexConstants;			///< offsets into the batch's constant bytes (Constants)
	uint32_t VertexConstantsSize;
	uint32_t PixelConstants;
	uint32_t PixelConstantsSize;
	uint32_t SamplerCount;
	SDL_GPUTexture * Textures[MAXIMUM_SAMPLERS];
	SDL_GPUSampler * Samplers[MAXIMUM_SAMPLERS];
	float Viewport[6];					///< x, y, width, height, min depth, max depth: as the GPU takes it
	int32_t Scissor[4];					///< x, y, width, height
	uint32_t StencilReference;
	uint32_t BlendFactor;				///< D3DCOLOR
};

class SdlGpuFrame
{
public:
	/// The device, with the window claimed when there is one.  A null window makes an offscreen frame,
	/// which is what the tests use: Present then needs a target of the caller's (Present_To).  Null, with
	/// the reason, when SDL has no GPU device to give.
	static SdlGpuFrame * Create(RenderWindow window, unsigned int width, unsigned int height, std::string & error);
	~SdlGpuFrame();

	/// A new back buffer and depth-stencil of this size (Reset), after running what is recorded.
	bool Resize(unsigned int width, unsigned int height);

	/// D3DCLEAR_TARGET, _ZBUFFER and _STENCIL over the whole back buffer, recorded for the next pass.  A
	/// later clear of the same thing replaces an earlier one, as the second would overwrite the first.
	void Clear_Back_Buffer(bool colour, bool depth, bool stencil, uint32_t argb, float z, uint32_t stencil_value);

	/// Runs what is recorded into the back buffer.  False when the GPU refused the work.
	bool Flush();

	// ---- The batch (A3c).  Offsets are into this batch only: a flush starts the next one empty.

	/// Room for `size` bytes in the staging stream, which the flush uploads into one GPU buffer that the
	/// records bind as vertices and as indices; 16-byte aligned.  The pointer is good until the next call.
	uint8_t * Stage(uint32_t size, uint32_t & offset);
	/// Room for bytes going to a GPU copy, and the uploads that take them there in the copy pass.  A
	/// buffer or level-0 upload cycles its target, so draws already submitted keep what they read.
	uint8_t * Upload_Space(uint32_t size, uint32_t & offset);
	void Queue_Buffer_Upload(SDL_GPUBuffer * buffer, uint32_t upload_offset, uint32_t size);
	void Queue_Texture_Upload(SDL_GPUTexture * texture, unsigned int level, unsigned int width, unsigned int height,
		uint32_t upload_offset);
	/// A constant block's bytes, for stage 0 (vertex) or 1 (pixel); the same bytes as that stage's block
	/// before share its offset, so the flush pushes only a change.
	uint32_t Constants(unsigned int stage, const void * bytes, uint32_t size);
	void Record_Draw(const SdlRecordedDraw & draw);
	/// A GPU object nothing will record again, released once the batch that may use it has gone.
	void Release_After_Batch(SDL_GPUTexture * texture, SDL_GPUBuffer * buffer);
	/// Counts flushes: what a GPU copy compares to know whether this batch has used it.
	uint64_t Batch() const { return BatchNumber; }
	/// Whether the batch has grown enough that the next draw should flush first.
	bool Batch_Is_Full() const;
	/// The depth-stencil's SDL_GPUTextureFormat.  Every pass has it attached.
	unsigned int Depth_Format() const { return DepthFormat; }

	/// Flush, then the back buffer to the window through the gamma ramp (null: none, a straight blit).
	bool Present(const uint16_t (*ramp)[256]);

	/// Present into a texture of the caller's, of Target_Format(), instead of the window: the test's
	/// window.  The back buffer's size must be the target's.
	bool Present_To(SDL_GPUTexture * target, unsigned int width, unsigned int height, const uint16_t (*ramp)[256]);

	/// The texture's pixels, B8G8R8A8 rows top first, width * 4 bytes each.  Flushes first.
	bool Read_Back(SDL_GPUTexture * texture, unsigned int width, unsigned int height, std::vector<uint8_t> & bgra);

	/// Writes B8G8R8A8 pixels into the back buffer (the gamma test's input picture).
	bool Upload_Back_Buffer(const std::vector<uint8_t> & bgra);

	SDL_GPUDevice * Device() const { return GpuDevice; }
	SDL_GPUTexture * Back_Buffer() const { return BackBuffer; }
	unsigned int Width() const { return BackWidth; }
	unsigned int Height() const { return BackHeight; }
	/// The back buffer's format, and so what a Present_To target has to be.
	static unsigned int Target_Format();

private:
	SdlGpuFrame();
	bool Create_Targets(unsigned int width, unsigned int height);
	void Release_Targets();
	bool Record_Batch(struct SDL_GPUCommandBuffer * commands);
	bool Record_Passes(struct SDL_GPUCommandBuffer * commands, SDL_GPUBuffer * stream);
	void End_Batch();
	bool Present_Into(struct SDL_GPUCommandBuffer * commands, SDL_GPUTexture * target, unsigned int width,
		unsigned int height, unsigned int format, const uint16_t (*ramp)[256]);
	SDL_GPUGraphicsPipeline * Gamma_Pipeline(unsigned int format);
	bool Upload_Ramp(struct SDL_GPUCommandBuffer * commands, const uint16_t (*ramp)[256]);

	SDL_GPUDevice * GpuDevice;
	SDL_Window * Window;				///< C2's, claimed; null for an offscreen frame
	SDL_GPUTexture * BackBuffer;
	SDL_GPUTexture * DepthStencil;
	unsigned int DepthFormat;			///< an SDL_GPUTextureFormat: D24S8 where there is one, else D32S8
	unsigned int BackWidth;
	unsigned int BackHeight;

	// The batch.  A command is a clear or a draw, in the order the engine asked for them.
	struct Command
	{
		bool IsDraw;
		uint32_t Draw;					///< into Draws
		bool Colour, Depth, Stencil;	///< a clear's
		uint32_t Argb;
		float Z;
		uint32_t StencilValue;
	};
	struct Upload
	{
		SDL_GPUBuffer * Buffer;			///< or
		SDL_GPUTexture * Texture;
		unsigned int Level, Width, Height;
		uint32_t Offset, Size;
	};
	std::vector<Command> Commands;
	std::vector<SdlRecordedDraw> Draws;
	std::vector<uint8_t> StreamBytes;
	std::vector<uint8_t> UploadBytes;
	std::vector<uint8_t> ConstantBytes;
	uint32_t LastConstants[2], LastConstantsSize[2];
	std::vector<Upload> Uploads;
	std::vector<SDL_GPUTexture *> DeadTextures;
	std::vector<SDL_GPUBuffer *> DeadBuffers;
	uint64_t BatchNumber;
	SDL_GPUBuffer * StreamBuffer;		///< the staging stream on the GPU, grown as needed
	uint32_t StreamBufferSize;
	struct SDL_GPUTransferBuffer * Transfer;
	uint32_t TransferSize;

	// The gamma pass, made the first time a ramp is not the identity.
	SDL_GPUShader * GammaVertex;
	SDL_GPUShader * GammaPixel;
	SDL_GPUGraphicsPipeline * GammaPipeline;
	unsigned int GammaPipelineFormat;
	SDL_GPUTexture * RampTexture;		///< 256 x 1, the three ramps in R, G and B
	SDL_GPUSampler * PointSampler;
	std::vector<uint16_t> UploadedRamp;	///< what RampTexture holds, to upload only a change
};

/// Whether the three ramps are D3D9's identity, value i as i * 257, which Present takes as a blit.
bool Sdl_Gamma_Is_Identity(const uint16_t (*ramp)[256]);

#endif // SDLGPUFRAME_H
