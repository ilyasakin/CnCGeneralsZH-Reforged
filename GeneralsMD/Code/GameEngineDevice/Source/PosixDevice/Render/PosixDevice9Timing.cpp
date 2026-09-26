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

// PERF1's timing aid.  ZH_GPU_TIMING="delay,count" measures the `count` frames that start `delay` seconds
// after the first Present - a stretch after loading whatever the resolution - and prints, as soon as the
// last of them is in (a run ended by a timeout still reports), the distribution of each over them:
//   - frame: the wall time from one Present to the next, everything the game did in between;
//   - draws: the draws recorded in it;
//   - device draw: the CPU time inside Gpu_Draw, recording those draws (state, programs, copies, staging);
//   - device flush: mid-frame flushes (a full batch, a read-back) recording and submitting the batch;
//   - device present: Present recording and submitting the frame's last batch, and the gamma pass,
//     including the swapchain wait;
//   - swapchain wait: SDL_WaitAndAcquireGPUSwapchainTexture's, which is the display's pacing (a hidden
//     window still waits for vsync), and work: the frame without it - what the frame costs;
//   - GPU (ZH_GPU_TIMING_SYNC=1 only): every submit waits for its fence, and the waits add up.  That
//     serialises CPU and GPU, so a SYNC run's frame times are not the game's: it measures the GPU.
// What is left of the frame after the device's parts is the engine's own CPU time (and, unsynced, any
// wait for the GPU inside SDL's swapchain acquire).

#include "PosixDevice9.h"
#include "SdlGpuFrame.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

namespace {

struct TimingState
{
	bool Asked;
	bool Sync;
	double Delay;
	unsigned int Count;
	Uint64 FirstPresent;
	Uint64 LastPresent;
	bool Reported;
	std::vector<double> Frame, Work, DrawMs, FlushMs, PresentMs, AcquireMs, OffscreenMs, FenceMs, Draws, Flushes;
	bool Offscreen;
};

TimingState &timing_state()
{
	static TimingState state;
	static bool made = false;
	if (!made) {
		made = true;
		const char *asked = getenv("ZH_GPU_TIMING");
		state.Asked = asked != NULL && sscanf(asked, "%lf,%u", &state.Delay, &state.Count) == 2 && state.Count > 0;
		const char *sync = getenv("ZH_GPU_TIMING_SYNC");
		state.Sync = state.Asked && sync != NULL && sync[0] == '1';
		state.FirstPresent = 0;
		state.LastPresent = 0;
		state.Reported = false;
		state.Offscreen = false;
	}
	return state;
}

void report(const char *what, std::vector<double> values, const char *unit)
{
	if (values.empty()) {
		return;
	}
	std::sort(values.begin(), values.end());
	double sum = 0.0;
	for (size_t i = 0; i < values.size(); ++i) sum += values[i];
	const size_t n = values.size();
	fprintf(stderr, "PosixDevice9 timing: %-15s mean %8.3f  p50 %8.3f  p95 %8.3f  p99 %8.3f  worst %8.3f %s\n", what, sum / n,
		values[n / 2], values[std::min(n - 1, (size_t)(n * 0.95))], values[std::min(n - 1, (size_t)(n * 0.99))], values[n - 1],
		unit);
}

}  // namespace

bool PosixDevice9::Timing_Is_Asked()
{
	return timing_state().Asked;
}

void PosixDevice9::Timing_Frame_Start()
{
	if (Gpu != NULL) {
		Gpu->Serialize_Submits(timing_state().Sync);
	}
}

void PosixDevice9::Timing_Present(double present_ms, unsigned int draws)
{
	TimingState &state = timing_state();
	const Uint64 now = SDL_GetTicksNS();
	double flush_ms = 0.0, fence_ms = 0.0, acquire_ms = 0.0, offscreen_ms = 0.0;
	unsigned int flushes = 0;
	Gpu->Take_Timing(flush_ms, fence_ms, flushes, acquire_ms, offscreen_ms);
	state.Offscreen = Gpu->Offscreen_Presents();
	if (state.FirstPresent == 0) {
		state.FirstPresent = now;
	}
	const bool measuring = (double)(now - state.FirstPresent) / 1.0e9 >= state.Delay && state.Frame.size() < state.Count;
	if (measuring && state.LastPresent != 0) {
		const double frame_ms = (double)(now - state.LastPresent) / 1.0e6;
		state.Frame.push_back(frame_ms);
		state.Work.push_back(frame_ms - acquire_ms - offscreen_ms);
		state.AcquireMs.push_back(acquire_ms);
		state.OffscreenMs.push_back(offscreen_ms);
		state.Draws.push_back((double)draws);
		state.DrawMs.push_back(TimingDrawMs);
		state.FlushMs.push_back(flush_ms);
		state.Flushes.push_back((double)flushes);
		state.PresentMs.push_back(present_ms);
		state.FenceMs.push_back(fence_ms);
	}
	state.LastPresent = now;
	TimingDrawMs = 0.0;
	if (!state.Reported && state.Frame.size() == state.Count) {
		Timing_Report();
	}
}

void PosixDevice9::Timing_Report()
{
	TimingState &state = timing_state();
	if (!state.Asked || state.Reported) {
		return;
	}
	state.Reported = true;
	fprintf(stderr, "PosixDevice9 timing: %zu frames from %.0f s after the first Present%s%s\n", state.Frame.size(),
		state.Delay, state.Frame.size() < state.Count ? " (the run ended before the window filled)" : "",
		state.Sync ? ", SERIALISED (every submit waits for its fence: GPU time, not frame rate)" : "");
	report("frame", state.Frame, "ms");
	report("work (no vsync)", state.Work, "ms");
	if (state.Offscreen) {
		// -offscreen: there is no swapchain; its wait is the GPU's two frames in flight and the pacer.
		fprintf(stderr, "PosixDevice9 timing: OFFSCREEN (no swapchain: no vsync, no drawable; see ZH_OFFSCREEN_HZ)\n");
		report("offscreen wait", state.OffscreenMs, "ms");
	} else {
		report("swapchain wait", state.AcquireMs, "ms");
	}
	report("draws", state.Draws, "");
	report("device draw", state.DrawMs, "ms");
	report("device flush", state.FlushMs, "ms");
	report("flushes", state.Flushes, "");
	report("device present", state.PresentMs, "ms");
	if (state.Sync) {
		report("GPU (fences)", state.FenceMs, "ms");
	}
}
