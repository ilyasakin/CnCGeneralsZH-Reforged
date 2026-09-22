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

// WideCharFns.h
// The engine's own string functions over WideChar.
// Added for the macOS port, B1.  See docs/mac-port/B1-widechar-survey.md.

#pragma once

#ifndef _WIDECHARFNS_H_
#define _WIDECHARFNS_H_

#include "Lib/BaseType.h"
#include <stdarg.h>
#include <stdio.h>

/* The engine carries text in WideChar.  The C library has no string functions for it: the wcs*
	 family is wchar_t, which MSVC makes 2 bytes and clang makes 4, so calling wcslen on engine
	 text is a width bug that only shows up when the compiler changes.  These are the engine's
	 own, and they are correct at either width - which is why they can be written and reviewed
	 before the typedef moves rather than after.

	 They are deliberately NOT named wcs*, and deliberately not macros or overloads over those
	 names.  Common/Audio/urllaunch.cpp and simpleplayer.cpp call the real CRT wcsstr, wcscpy,
	 wcsncpy, wcslen, wcspbrk and swprintf on real LPWSTRs handed to them by the Windows shell and
	 by Windows Media, and they must keep doing exactly that.  A macro over wcslen would break
	 those two files from three directories away, which is the worst way to find out.

	 On Windows every function below that can forward to the CRT does, through a cast, so the
	 Windows build keeps the behaviour it has today.  char16_t and wchar_t are layout-compatible
	 there, so the cast is honest.  Off Windows these are the implementation.

	 Two exceptions, both marked at their declaration: WideCharICmp and WideCharNICmp fold ASCII
	 only, on every platform, and do not forward to _wcsicmp. */

//-----------------------------------------------------------------------------------------------
// Length, copy, concatenate
//-----------------------------------------------------------------------------------------------

size_t    WideCharLen  ( const WideChar *s );
WideChar *WideCharCpy  ( WideChar *dst, const WideChar *src );
WideChar *WideCharCat  ( WideChar *dst, const WideChar *src );

/** wcsncpy's semantics exactly, pad-with-zeros and all: at most n code units are copied, and if
	  src is shorter than n the remainder of dst is filled with zeros.  Twenty-three of the
	  twenty-four call sites this replaces are the fixed-buffer idiom
	  `WideCharNCpy(dst, src, N); dst[N] = 0;` on a wire or UI buffer, and they rely on it. */
WideChar *WideCharNCpy ( WideChar *dst, const WideChar *src, size_t n );

//-----------------------------------------------------------------------------------------------
// Compare
//-----------------------------------------------------------------------------------------------

Int WideCharCmp  ( const WideChar *a, const WideChar *b );
Int WideCharNCmp ( const WideChar *a, const WideChar *b, size_t n );

/** Case-insensitive compare, folding **ASCII A-Z only**, on every platform.

	  This is not _wcsicmp.  _wcsicmp folds by the current C locale on MSVC, which means the same
	  two strings can compare differently on two machines with two locales, and the engine reaches
	  this from UnicodeString::compareNoCase - which sorts the lobby's game list and matches UI
	  labels.  A port that quietly kept a locale-sensitive compare would be trading one silent
	  cross-machine disagreement for another.  Pinned here so it is the same answer everywhere.

	  Nothing calls these yet.  When UnicodeString::compareNoCase is moved onto them, that IS a
	  behaviour change on Windows for strings holding non-ASCII letters, and it needs its own row
	  in docs/mac-port/WINDOWS-DEBT.md at the time. */
Int WideCharICmp  ( const WideChar *a, const WideChar *b );
Int WideCharNICmp ( const WideChar *a, const WideChar *b, size_t n );

//-----------------------------------------------------------------------------------------------
// Search
//-----------------------------------------------------------------------------------------------

const WideChar *WideCharChr  ( const WideChar *s, WideChar c );
const WideChar *WideCharRChr ( const WideChar *s, WideChar c );
const WideChar *WideCharStr  ( const WideChar *haystack, const WideChar *needle );

/* size_t, not Int: UnicodeString::nextToken does pointer arithmetic with the result. */
size_t WideCharSpn  ( const WideChar *s, const WideChar *accept );
size_t WideCharCSpn ( const WideChar *s, const WideChar *reject );

//-----------------------------------------------------------------------------------------------
// Classification
//-----------------------------------------------------------------------------------------------

/* These take and return Int rather than WideChar because the CRT's isw* family is wint_t-based
	 and the call sites already rely on that - ControlBar.cpp casts the result of towupper back to
	 a WideChar by hand. */
Bool WideCharIsSpace ( Int c );
Bool WideCharIsDigit ( Int c );
Bool WideCharIsAlpha ( Int c );
Bool WideCharIsAlNum ( Int c );
Bool WideCharIsAscii ( Int c );
Int  WideCharToUpper ( Int c );

//-----------------------------------------------------------------------------------------------
// Formatting - the one funnel
//-----------------------------------------------------------------------------------------------

/* _vsnwprintf has no char16_t form on any platform and never will.  Every wide format in the
	 engine goes through here: UnicodeString::format_va (both overloads) and InGameUI's three
	 message() variants.  Do not add a second one.

	 **The contract is MSVC's `_vsnwprintf`, on both platforms, on purpose.**  At most `outCount`
	 code units are written, the terminator counted among them.  On success the return is the
	 number written not counting the terminator, and `out` is terminated.  **On truncation the
	 return is negative.**  All five call sites branch on that sign - two throw
	 ERROR_OUT_OF_MEMORY and three log and truncate - so a bare POSIX `vswprintf`, which returns
	 the would-be length instead, would silently turn a truncated chat line into a success on the
	 Mac and nobody would notice until a long player name reached the UI.

	 One deliberate difference from MSVC, in the single case where MSVC is already wrong: an
	 output of exactly `outCount` code units.  MSVC writes them, does NOT terminate, and returns
	 `outCount`; UnicodeString::format_va then calls set() on an unterminated buffer and reads
	 past its end.  Off Windows that case reports truncation instead, which is what the call
	 sites are written for.  It costs one character of the longest possible message and removes a
	 buffer overread.

	 Known limitation, and the reason docs/mac-port/B1-widechar-survey.md section 3.4 exists: a
	 `%ls` or `%ws` inside `format` still means "this argument is a wchar_t*" to the C library.
	 Forty-two wide formats in the engine carry one and pass a WideChar*.  Those arguments live
	 in the va_list and no function here can reach them; they are a separate sweep. */
Int WideCharFormatV ( WideChar *out, size_t outCount, const WideChar *format, va_list args );

/** The variadic spelling of WideCharFormatV, with the same contract. */
Int WideCharFormat ( WideChar *out, size_t outCount, const WideChar *format, ... );

/** swscanf over WideChar, which has no char16_t form either.  Two call sites, both parsing the
	  direct-connect IP address out of a combo box (NetworkDirectConnect.cpp). */
Int WideCharScan ( const WideChar *in, const WideChar *format, ... );

/** Write a WideChar string to a FILE*, with no terminator and no newline, exactly as
	  `fwprintf(f, L"%ws", s)` does today.  Returns the number of code units written, or a negative
	  value on error.

	  Used by the replay header (Recorder.cpp), which writes each string with this and then a
	  `fputwc(0, ...)`, and reads it back one `fgetwc` at a time.  The bytes that reach the disk
	  are whatever the C library's wide-to-multibyte conversion produces for the stream, and they
	  must not change: a replay written by one build has to be readable by the other.  On Windows
	  this is byte-for-byte the call it replaces.

	  See docs/mac-port/WINDOWS-DEBT.md: mixing fwrite and fwprintf on one FILE* is undefined, and
	  Recorder.cpp does it heavily.  MSVC tolerates it.  A POSIX C library sets the stream's
	  orientation on first use and then fails every call of the other kind, so the replay writer
	  needs a byte-oriented rewrite before it runs on a Mac.  That is C1/M2 work, not B1's, and
	  this funnel is where it will be done when it is. */
Int WideCharFileWrite ( FILE *f, const WideChar *s );

#endif // _WIDECHARFNS_H_
