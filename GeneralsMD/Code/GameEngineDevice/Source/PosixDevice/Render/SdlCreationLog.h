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

// PERF1's creation log, to find what stalls a frame: ZH_GPU_CREATION_LOG=1 prints, as they happen, what
// the device makes on first use - a program compiled, a pipeline created, a texture's GPU copy made and
// its levels converted and queued - each with the time since SDL started and how long it took, and every
// frame longer than 50 ms, so a long frame can be set beside what was made in it.  Off unless asked: a
// site then costs one cached check.

#pragma once

#ifndef SDLCREATIONLOG_H
#define SDLCREATIONLOG_H

/// Whether ZH_GPU_CREATION_LOG asks for the log.
bool Sdl_Creation_Log_Asked();
/// Milliseconds since SDL started.
double Sdl_Now_Ms();
/// One line: what was made, when it started (Sdl_Now_Ms), how long it took, and what it was.
void Sdl_Creation_Log(const char *what, double started_ms, double took_ms, const char *detail);

#endif // SDLCREATIONLOG_H
