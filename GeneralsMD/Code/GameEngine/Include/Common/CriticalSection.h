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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// CriticalSection.h ///////////////////////////////////////////////////////
// Utility class to use critical sections in areas of code.
// Author: JohnM And MattC, August 13, 2002

#pragma once

#ifndef __CRITICALSECTION_H__
#define __CRITICALSECTION_H__

#include "Common/PerfTimer.h"

#include <mutex>

#ifdef PERF_TIMERS
extern PerfGather TheCritSecPerfGather;
#endif

/*
	This was a raw Win32 CRITICAL_SECTION.  It is a std::recursive_mutex now (B11), and the
	"recursive" is not conservatism - it is required, by one caller, unconditionally:

	    UnicodeString::set( const UnicodeString & )    UnicodeString.cpp:155
	        takes TheUnicodeStringCriticalSection      UnicodeString.cpp:157
	        calls releaseBuffer()                      UnicodeString.cpp:162
	            takes TheUnicodeStringCriticalSection  UnicodeString.cpp:130   <- again, same lock

	The scoped lock taken at :157 is still in scope when :162 runs, and the branch it sits in is
	`if (&stringSrc != this)` - which is the ordinary case, not an edge one.  A Win32
	CRITICAL_SECTION is recursive, so this has always been well-defined; a std::mutex is not, and
	re-locking one you already hold is undefined behaviour rather than a deadlock you would
	notice in a debugger.  So std::mutex is not available to us here, and a future reader who
	thinks the "recursive" looks unnecessary should re-read those four lines before removing it.

	The other three locks this class backs do not recurse - checked, not assumed:

	  TheMemoryPoolCriticalSection  GameMemory.cpp 1644, 1734, 1793, 1814.  The functions reached
	                                under it - createBlob, freeBlob, init, sysAllocateDoNotZero,
	                                sysFree - take no lock of their own.
	  TheDmaCriticalSection         GameMemory.cpp 2182, 2298.  Nests into the pool lock, which is
	                                a different object.
	  TheDebugLogCriticalSection    Debug.cpp 451.  doLogOutput is a leaf: fprintf and
	                                OutputDebugString, no allocation and no way back into DebugLog.

	They share one class, so they get the recursive one too.  The cost of that over a plain mutex
	is an owner check on an uncontended acquire, which is also exactly what CRITICAL_SECTION was
	doing before - this is the mapping that keeps behaviour identical, not a concession.

	Lock ordering, since three of the four nest: Unicode -> Dma -> Pool, in that direction only.
	UnicodeString::releaseBuffer calls TheDynamicMemoryAllocator->freeBytes under the Unicode
	lock, and DynamicMemoryAllocator::allocateBytesDoNotZeroImplementation calls into the pool
	under the Dma lock.  Nothing in GameMemory.cpp takes the Unicode lock, so there is no cycle.
	Keep it that way.

	No spin count is lost in the move.  InitializeCriticalSectionAndSpinCount,
	SetCriticalSectionSpinCount and TryEnterCriticalSection appear nowhere in this tree, so every
	one of these locks was already a plain InitializeCriticalSection with the system default.
*/
class CriticalSection
{
	std::recursive_mutex m_mutex;

	public:
		CriticalSection()
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
		}

		virtual ~CriticalSection()
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
		}

	public:	// Use these when entering/exiting a critical section.
		void enter( void )
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
			m_mutex.lock();
		}

		void exit( void )
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
			m_mutex.unlock();
		}
};

class ScopedCriticalSection
{
	private:
		CriticalSection *m_cs;

	public:
		ScopedCriticalSection( CriticalSection *cs ) : m_cs(cs)
		{
			if (m_cs)
				m_cs->enter();
		}

		virtual ~ScopedCriticalSection( )
		{
			if (m_cs)
				m_cs->exit();
		}
};

// These should be NULL on creation then non-NULL in WinMain or equivalent.
// This allows us to be silently non-threadsafe for WB and other single-threaded apps.
//
// TheAsciiStringCriticalSection used to sit at the top of this list, a FastCriticalSectionClass
// from WWVegas' mutex.h rather than one of these.  It is gone, and so is the #include of mutex.h
// that only it needed: its three uses in AsciiString.h (:378, :389, :450) are all commented out,
// nothing else in the tree named it, and WinMain never assigned it.  mutex.h reaches <intrin.h>
// and _interlockedbittestandset, so a dead extern was keeping MSVC intrinsics in the header that
// the allocator and both string classes compile against.
extern CriticalSection *TheUnicodeStringCriticalSection;
extern CriticalSection *TheDmaCriticalSection;
extern CriticalSection *TheMemoryPoolCriticalSection;
extern CriticalSection *TheDebugLogCriticalSection;

#endif /* __CRITICALSECTION_H__ */
