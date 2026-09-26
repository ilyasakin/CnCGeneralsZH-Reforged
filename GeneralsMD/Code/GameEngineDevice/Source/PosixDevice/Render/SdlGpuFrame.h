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
** A3a clears and presents; draws land in A3c.  A clear is recorded, not issued, and becomes the load
** operation of the next pass over the back buffer, as the design's recorded frame does for everything.
** Only a clear of the whole target is taken here; a clear of part of it is a clear draw, which is A3c's.
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

struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUShader;
struct SDL_GPUTexture;
struct SDL_Window;

class SdlGpuFrame
{
public:
	/// The device, with the window claimed when there is one.  A null window makes an offscreen frame,
	/// which is what the tests use: Present then needs a target of the caller's (Present_To).  Null, with
	/// the reason, when SDL has no GPU device to give.
	static SdlGpuFrame * Create(RenderWindow window, unsigned int width, unsigned int height, std::string & error);
	~SdlGpuFrame();

	/// A new back buffer and depth-stencil of this size (Reset).  Anything recorded is dropped.
	bool Resize(unsigned int width, unsigned int height);

	/// D3DCLEAR_TARGET, _ZBUFFER and _STENCIL over the whole back buffer, recorded for the next pass.  A
	/// later clear of the same thing replaces an earlier one, as the second would overwrite the first.
	void Clear_Back_Buffer(bool colour, bool depth, bool stencil, uint32_t argb, float z, uint32_t stencil_value);

	/// Runs what is recorded into the back buffer.  False when the GPU refused the work.
	bool Flush();

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
	bool Record_Clear_Pass(struct SDL_GPUCommandBuffer * commands);
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

	// The clear waiting for the next pass.
	bool ClearColour;
	bool ClearDepth;
	bool ClearStencil;
	uint32_t ClearArgb;
	float ClearZ;
	uint32_t ClearStencilValue;

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
