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

// FILE: PosixRenderHooks.cpp /////////////////////////////////////////////////////////////////////
// Desc:   The renderer's globals and free functions gameengine calls, off Windows, until the D track
//         brings a renderer.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* Each of these is defined on Windows in W3DDevice or WW3D2 and named by gameengine through an extern
	 or a header.  Each was checked at every call site before it was given a body here (B6); what the
	 body does, and why nothing the simulation computes depends on it, is beside it. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Shadow.h"
#include "cpudetect.h"
#include "benchmark.h"

// ---- ScriptActions -------------------------------------------------------------------------------

/* W3DWater.cpp's body, as it is there: the script action "show/hide the sky box" is a GlobalData
	 flag, which the renderer reads.  Nothing renderer-specific happens here, so off Windows it does the
	 same thing rather than nothing, and a script that sets it leaves it set on every platform. */
void doSkyBoxSet(Bool startDraw)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox = startDraw;
}

/* BaseHeightMap.cpp's body asks the terrain render object to draw extra terrain past the map edge,
	 and does nothing when there is no terrain render object.  Off Windows there never is one yet, so
	 nothing is exactly what it does. */
void oversizeTheTerrain(Int)
{
}

// ---- Shadows and decals ----------------------------------------------------------------------------

/* W3DProjectedShadow.cpp's singleton.  NULL until a renderer creates one.  Its users: ParticleSys and
	 W3DModelDraw test it; RadiusDecal and InGameUI::addSignalMark did not, and test it now (B6). */
ProjectedShadowManager *TheProjectedShadowManager = NULL;

// ---- DX8Wrapper's two globals ------------------------------------------------------------------------

/* -preserveFPU sets it (CommandLine.cpp) and only DX8Wrapper reads it, to pass D3DCREATE_FPU_PRESERVE.
	 Kept so the switch still parses; there is no device to pass it to. */
int DX8Wrapper_PreserveFPU = 0;

/* Debug.cpp shows an assertion as a dialog only while windowed, and GameEngine's teardown clears it so
	 teardown asserts go to the log.  dx8wrapper.cpp starts it true; so does this. */
bool DX8Wrapper_IsWindowed = true;

// ---- GameLOD ---------------------------------------------------------------------------------------

/* The CPU speed reported for a processor that cpudetect cannot time.  Decision 2 (B19, option (c)): an
	 unmeasurable CPU is treated as fast when the default detail preset is chosen, and the choice is made
	 here, not claimed by cpudetect, which keeps reporting the 0 it honestly measured.  3049 is the
	 fastest processor GameLODPresets.ini names (BenchProfile = P4 3049), so this meets every BenchProfile
	 and LODPreset the shipped file has and is far above its ReallyLowMHz (600).
	 Why it matters: on a first launch the CPU type is XX, GameLOD runs the benchmark, and the benchmark
	 library's fixed indices beat every profile, so GameLOD adopts a profile's speed.  On every later
	 launch it takes this function's speed instead, and at 0 isReallyLowMHz() turned the shell map off -
	 the first launch and the later ones disagreed.  With this they agree. */
enum { UNMEASURED_CPU_REPORTED_MHZ = 3049 };

/* W3DShaderManager::testMinimumRequirements, less what needs a Direct3D device.
	 - videoChipType: DC_UNKNOWN.  Windows asks the D3D adapter; here there is no device to ask until
	   the D track, and GameLOD presumes DC_TNT2 for an unknown chip.
	 - cpuType: the same tests as Windows.  cpudetect answers them from CPUID, which only x86 under MSVC
	   has, so everywhere this file is built it is XX - which is what sends a first launch to the
	   benchmark, as on Windows for a CPU it does not know.
	 - cpuFreq: cpudetect's speed, or UNMEASURED_CPU_REPORTED_MHZ when it could not time the CPU.
	 - numRAM and the benchmark: as on Windows. */
Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, Int *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex)
{
	if (videoChipType)
		*videoChipType = DC_UNKNOWN;

	if (cpuType)
	{
		*cpuType = XX;	//unknown

		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_AMD &&
				CPUDetectClass::Get_AMD_Processor() >= CPUDetectClass::AMD_PROCESSOR_ATHLON_025)
				*cpuType = K7;
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM_III_MODEL_7)
				*cpuType = P3;
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM4)
				*cpuType = P4;
	}

	if (cpuFreq)
	{
		const Int measured = CPUDetectClass::Get_Processor_Speed();
#if defined(CPUDETECT_UNMEASURED_PROCESSOR_MHZ)
		const Bool unmeasured = measured == CPUDETECT_UNMEASURED_PROCESSOR_MHZ;
#else
		const Bool unmeasured = measured <= 0;
#endif
		*cpuFreq = unmeasured ? (Int)UNMEASURED_CPU_REPORTED_MHZ : measured;
	}

	if (numRAM)
		*numRAM = CPUDetectClass::Get_Total_Physical_Memory();

	if (intBenchIndex && floatBenchIndex && memBenchIndex)
	{
		RunBenchmark(0, NULL, floatBenchIndex, intBenchIndex, memBenchIndex);
	}

	return TRUE;
}

// ---- The message of the day ------------------------------------------------------------------------

/* W3DMOTD.cpp's window callback, bound by name through FunctionLexicon, so it has to exist for the
	 table to link.  The window it serves shows a message downloaded from EA's servers, which are gone:
	 it never opens.  Dead service; it handles nothing. */
WindowMsgHandledType MOTDSystem( GameWindow *, UnsignedInt, WindowMsgData, WindowMsgData )
{
	return MSG_IGNORED;
}
