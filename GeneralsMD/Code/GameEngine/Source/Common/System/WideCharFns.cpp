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

// WideCharFns.cpp
// The engine's own string functions over WideChar.  See Lib/WideCharFns.h for why these exist
// and why they are not called wcs*.
// Added for the macOS port, B1.  See docs/mac-port/B1-widechar-survey.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/WideCharFns.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifndef _WIN32
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>			// where macOS declares newlocale/uselocale
#endif
#endif

/* Two shapes of implementation below, and the split is not the obvious one.

	 The functions that take a WideChar POINTER care about the width, so on Windows they forward to
	 the CRT through a cast - char16_t and wchar_t are layout-compatible there and the Windows build
	 keeps the code it has today - and off Windows they are written out.  The written-out versions
	 are correct at either width, which is what lets this file be reviewed before the typedef moves.

	 The functions that take a character VALUE do not care about the width at all: iswspace and
	 friends take a wint_t, which is at least 32 bits everywhere, so there was never a portability
	 problem with them.  They forward to the CRT on both platforms.  They are in this file only so
	 that Language.h's Game* macros have one place to point at. */

#ifdef _WIN32
#define AS_CRT(p)  (reinterpret_cast<const wchar_t *>(p))
#define AS_CRTW(p) (reinterpret_cast<wchar_t *>(p))
#endif

//-----------------------------------------------------------------------------------------------
// Length, copy, concatenate
//-----------------------------------------------------------------------------------------------

size_t WideCharLen( const WideChar *s )
{
#ifdef _WIN32
	return ::wcslen( AS_CRT( s ) );
#else
	const WideChar *p = s;
	while (*p)
		++p;
	return (size_t)(p - s);
#endif
}

WideChar *WideCharCpy( WideChar *dst, const WideChar *src )
{
#ifdef _WIN32
	::wcscpy( AS_CRTW( dst ), AS_CRT( src ) );
	return dst;
#else
	WideChar *out = dst;
	while ((*out++ = *src++) != 0)
		;
	return dst;
#endif
}

WideChar *WideCharCat( WideChar *dst, const WideChar *src )
{
#ifdef _WIN32
	::wcscat( AS_CRTW( dst ), AS_CRT( src ) );
	return dst;
#else
	WideChar *out = dst;
	while (*out)
		++out;
	while ((*out++ = *src++) != 0)
		;
	return dst;
#endif
}

WideChar *WideCharNCpy( WideChar *dst, const WideChar *src, size_t n )
{
#ifdef _WIN32
	::wcsncpy( AS_CRTW( dst ), AS_CRT( src ), n );
	return dst;
#else
	// wcsncpy's semantics, pad-with-zeros included - see the header for why that matters.
	size_t i = 0;
	while (i < n && src[i] != 0)
	{
		dst[i] = src[i];
		++i;
	}
	while (i < n)
		dst[i++] = 0;
	return dst;
#endif
}

//-----------------------------------------------------------------------------------------------
// Compare
//-----------------------------------------------------------------------------------------------

/* Three-way rather than a subtraction: at a 4-byte WideChar the difference of two code units does
	 not fit in an Int, and these have to be correct at either width. */
static inline Int compareUnits( WideChar a, WideChar b )
{
	if (a < b) return -1;
	if (a > b) return 1;
	return 0;
}

Int WideCharCmp( const WideChar *a, const WideChar *b )
{
#ifdef _WIN32
	return ::wcscmp( AS_CRT( a ), AS_CRT( b ) );
#else
	while (*a && *a == *b)
	{
		++a;
		++b;
	}
	return compareUnits( *a, *b );
#endif
}

Int WideCharNCmp( const WideChar *a, const WideChar *b, size_t n )
{
#ifdef _WIN32
	return ::wcsncmp( AS_CRT( a ), AS_CRT( b ), n );
#else
	while (n > 0 && *a && *a == *b)
	{
		++a;
		++b;
		--n;
	}
	if (n == 0)
		return 0;
	return compareUnits( *a, *b );
#endif
}

/* ASCII only, on every platform, and NOT _wcsicmp.  The header says why. */
static inline WideChar foldAscii( WideChar c )
{
	return (c >= 'A' && c <= 'Z') ? (WideChar)(c + ('a' - 'A')) : c;
}

Int WideCharICmp( const WideChar *a, const WideChar *b )
{
	for (;;)
	{
		const WideChar ca = foldAscii( *a++ );
		const WideChar cb = foldAscii( *b++ );
		if (ca != cb)
			return compareUnits( ca, cb );
		if (ca == 0)
			return 0;
	}
}

Int WideCharNICmp( const WideChar *a, const WideChar *b, size_t n )
{
	while (n-- > 0)
	{
		const WideChar ca = foldAscii( *a++ );
		const WideChar cb = foldAscii( *b++ );
		if (ca != cb)
			return compareUnits( ca, cb );
		if (ca == 0)
			return 0;
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------
// Search
//-----------------------------------------------------------------------------------------------

const WideChar *WideCharChr( const WideChar *s, WideChar c )
{
#ifdef _WIN32
	return reinterpret_cast<const WideChar *>( ::wcschr( AS_CRT( s ), (wchar_t)c ) );
#else
	for (;; ++s)
	{
		if (*s == c)
			return s;			// finding the terminator is wcschr's documented behaviour, not an accident
		if (*s == 0)
			return NULL;
	}
#endif
}

const WideChar *WideCharRChr( const WideChar *s, WideChar c )
{
#ifdef _WIN32
	return reinterpret_cast<const WideChar *>( ::wcsrchr( AS_CRT( s ), (wchar_t)c ) );
#else
	const WideChar *found = NULL;
	for (;; ++s)
	{
		if (*s == c)
			found = s;
		if (*s == 0)
			return found;
	}
#endif
}

const WideChar *WideCharStr( const WideChar *haystack, const WideChar *needle )
{
#ifdef _WIN32
	return reinterpret_cast<const WideChar *>( ::wcsstr( AS_CRT( haystack ), AS_CRT( needle ) ) );
#else
	if (*needle == 0)
		return haystack;

	for (; *haystack; ++haystack)
	{
		const WideChar *h = haystack;
		const WideChar *n = needle;
		while (*n && *h == *n)
		{
			++h;
			++n;
		}
		if (*n == 0)
			return haystack;
	}
	return NULL;
#endif
}

size_t WideCharSpn( const WideChar *s, const WideChar *accept )
{
#ifdef _WIN32
	return ::wcsspn( AS_CRT( s ), AS_CRT( accept ) );
#else
	// *p is never the terminator inside the loop, so WideCharChr's match-the-terminator behaviour
	// cannot fire here.
	const WideChar *p = s;
	while (*p && WideCharChr( accept, *p ) != NULL)
		++p;
	return (size_t)(p - s);
#endif
}

size_t WideCharCSpn( const WideChar *s, const WideChar *reject )
{
#ifdef _WIN32
	return ::wcscspn( AS_CRT( s ), AS_CRT( reject ) );
#else
	const WideChar *p = s;
	while (*p && WideCharChr( reject, *p ) == NULL)
		++p;
	return (size_t)(p - s);
#endif
}

//-----------------------------------------------------------------------------------------------
// Classification - width-independent, forwarded on both platforms
//-----------------------------------------------------------------------------------------------

Bool WideCharIsSpace( Int c ) { return ::iswspace( (wint_t)c ) != 0; }
Bool WideCharIsDigit( Int c ) { return ::iswdigit( (wint_t)c ) != 0; }
Bool WideCharIsAlpha( Int c ) { return ::iswalpha( (wint_t)c ) != 0; }
Bool WideCharIsAlNum( Int c ) { return ::iswalnum( (wint_t)c ) != 0; }
Int  WideCharToUpper( Int c ) { return (Int)::towupper( (wint_t)c ); }

/* Spelled out rather than forwarded to iswascii, which is a BSD/Win32 extension and is not in
	 the standard headers everywhere.  This is its definition. */
Bool WideCharIsAscii( Int c ) { return c >= 0 && c < 128; }

//-----------------------------------------------------------------------------------------------
// Formatting
//-----------------------------------------------------------------------------------------------

#ifndef _WIN32

/* Off Windows the funnel widens to wchar_t, calls the C library, and narrows back.  The two
	 scratch buffers are the whole cost of the thing, and they are why this is one function and not
	 five call sites. */
static const size_t WIDE_SCRATCH = 2048;		// UnicodeString::MAX_FORMAT_BUF_LEN

/* ...and this is the part that is not obvious, and that was measured rather than reasoned about.

	 A process starts in the "C" locale.  In that locale macOS's vswprintf, vswscanf and fwprintf
	 **fail outright, returning -1 with errno EILSEQ, on any wide character outside ASCII** - they
	 have to be able to convert between the wide and multibyte forms and the C locale's multibyte
	 form is ASCII.  MSVC's _vsnwprintf does no such conversion and passes anything through.

	 So a funnel that just called vswprintf would have every UnicodeString::format of a Turkish,
	 German or French string return negative, which UnicodeString::format_va turns into a thrown
	 ERROR_OUT_OF_MEMORY.  Every localisation but English would abort on the first formatted
	 message, on the Mac only, for a reason nothing in the log would name.

	 uselocale() is POSIX-2008, sets LC_CTYPE for the calling thread only, and leaves the global
	 locale and every other thread alone - which matters, because the engine formats text from
	 more than one thread and this must not become a global setting the rest of the game can see.
	 The locale object is created once and never freed: it lives as long as the process, and
	 freeing it at exit would only introduce a teardown order to get wrong. */

static locale_t utf8CtypeLocale( void )
{
	// C++11 magic statics: initialised once, thread-safely, on first use.
	static locale_t loc = []() -> locale_t
	{
		static const char *const names[] = { "en_US.UTF-8", "UTF-8", "C.UTF-8" };
		for (size_t i = 0; i < sizeof( names ) / sizeof( names[0] ); ++i)
		{
			locale_t l = ::newlocale( LC_CTYPE_MASK, names[i], (locale_t)0 );
			if (l != (locale_t)0)
				return l;
		}
		return (locale_t)0;
	}();
	return loc;
}

/** LC_CTYPE is UTF-8 for this thread for the lifetime of the object, and exactly as it was after
	  it.  A scope guard rather than a pair of calls because WideCharFormatV has an early return. */
class ScopedUtf8Ctype
{
public:
	ScopedUtf8Ctype() : m_previous( (locale_t)0 )
	{
		const locale_t utf8 = utf8CtypeLocale();
		if (utf8 != (locale_t)0)
			m_previous = ::uselocale( utf8 );
	}
	~ScopedUtf8Ctype()
	{
		if (m_previous != (locale_t)0)
			::uselocale( m_previous );
	}
private:
	locale_t m_previous;
	ScopedUtf8Ctype( const ScopedUtf8Ctype & );
	ScopedUtf8Ctype &operator=( const ScopedUtf8Ctype & );
};

static size_t widenToWchar( const WideChar *in, wchar_t *out, size_t outCount )
{
	size_t i = 0;
	while (in[i] != 0 && i + 1 < outCount)
	{
		out[i] = (wchar_t)in[i];
		++i;
	}
	out[i] = 0;
	return i;
}

static size_t narrowToWideChar( const wchar_t *in, WideChar *out, size_t outCount )
{
	size_t i = 0;
	while (in[i] != 0 && i + 1 < outCount)
	{
		// Anything outside the BMP cannot be carried by a 16-bit code unit.  B1 changes a width,
		// not a text model (see the task's "Do not" list), and nothing in the engine's format
		// strings or string tables is outside the BMP - so substitute rather than invent a
		// surrogate pair, and make it visible if it ever happens.
		const unsigned long cp = (unsigned long)in[i];
		out[i] = (WideChar)(cp > 0xFFFFul ? 0xFFFDul : cp);
		++i;
	}
	out[i] = 0;
	return i;
}

#endif // !_WIN32

Int WideCharFormatV( WideChar *out, size_t outCount, const WideChar *format, va_list args )
{
	if (out == NULL || outCount == 0 || format == NULL)
		return -1;

#ifdef _WIN32

	// Byte for byte the call this replaces.
	return (Int)::_vsnwprintf( AS_CRTW( out ), outCount, AS_CRT( format ), args );

#else

	const ScopedUtf8Ctype utf8;		// see above - without this, any non-ASCII fails with EILSEQ

	wchar_t wideFormat[ WIDE_SCRATCH ];
	widenToWchar( format, wideFormat, WIDE_SCRATCH );

	/* outCount, not outCount+1.  Both functions count the terminator against the buffer, so this
		 is the same capacity MSVC is given - and it puts the truncation threshold one character
		 earlier than MSVC's, which is the deliberate difference the header describes. */
	wchar_t stackBuf[ WIDE_SCRATCH ];
	wchar_t *wideBuf = stackBuf;
	wchar_t *heapBuf = NULL;
	if (outCount > WIDE_SCRATCH)
	{
		heapBuf = (wchar_t *)malloc( outCount * sizeof( wchar_t ) );
		if (heapBuf == NULL)
			return -1;
		wideBuf = heapBuf;
	}

	const int written = ::vswprintf( wideBuf, outCount, wideFormat, args );

	Int result;
	if (written < 0)
	{
		// C99 returns negative for truncation as well as for an encoding error, which is the sign
		// MSVC gives for truncation.  Both callers' branches want exactly that, so pass it through.
		result = -1;
	}
	else
	{
		narrowToWideChar( wideBuf, out, outCount );
		result = (Int)written;
	}

	if (heapBuf != NULL)
		free( heapBuf );
	return result;

#endif
}

Int WideCharFormat( WideChar *out, size_t outCount, const WideChar *format, ... )
{
	va_list args;
	va_start( args, format );
	const Int result = WideCharFormatV( out, outCount, format, args );
	va_end( args );
	return result;
}

Int WideCharScan( const WideChar *in, const WideChar *format, ... )
{
	if (in == NULL || format == NULL)
		return -1;

	va_list args;
	va_start( args, format );

#ifdef _WIN32

	const Int result = (Int)::vswscanf( AS_CRT( in ), AS_CRT( format ), args );

#else

	const ScopedUtf8Ctype utf8;		// see WideCharFormatV

	wchar_t wideIn[ WIDE_SCRATCH ];
	wchar_t wideFormat[ WIDE_SCRATCH ];
	widenToWchar( in, wideIn, WIDE_SCRATCH );
	widenToWchar( format, wideFormat, WIDE_SCRATCH );

	const Int result = (Int)::vswscanf( wideIn, wideFormat, args );

#endif

	va_end( args );
	return result;
}

//-----------------------------------------------------------------------------------------------
// WideChar text in a narrow printf
//-----------------------------------------------------------------------------------------------

size_t WideCharToUtf8( const WideChar *s, char *out, size_t outBytes )
{
	if (out == NULL || outBytes == 0)
		return 0;

	size_t written = 0;
	if (s == NULL)
	{
		out[0] = 0;
		return 0;
	}

	while (*s != 0)
	{
		/* Read one code point.  At a 2-byte WideChar a character outside the BMP arrives as a
			 surrogate pair and has to be put back together, or the output is CESU-8 rather than UTF-8
			 and a text editor shows two replacement characters.  At a 4-byte WideChar there are no
			 surrogates and the first branch never fires.  Both widths have to work: this file is
			 written to be correct before and after the typedef moves. */
		unsigned long cp = (unsigned long)*s++;

		if (cp >= 0xD800ul && cp <= 0xDBFFul)				// high surrogate
		{
			const unsigned long low = (unsigned long)*s;
			if (low >= 0xDC00ul && low <= 0xDFFFul)
			{
				cp = 0x10000ul + ((cp - 0xD800ul) << 10) + (low - 0xDC00ul);
				++s;
			}
			else
			{
				cp = 0xFFFDul;										// a high surrogate with nothing after it
			}
		}
		else if (cp >= 0xDC00ul && cp <= 0xDFFFul)	// a low surrogate on its own
		{
			cp = 0xFFFDul;
		}

		// how many bytes this character needs, and stop on a whole character rather than half of one
		size_t need;
		if (cp < 0x80ul)					need = 1;
		else if (cp < 0x800ul)		need = 2;
		else if (cp < 0x10000ul)	need = 3;
		else											need = 4;

		if (written + need + 1 > outBytes)
			break;

		switch (need)
		{
			case 1:
				out[written++] = (char)cp;
				break;
			case 2:
				out[written++] = (char)(0xC0ul | (cp >> 6));
				out[written++] = (char)(0x80ul | (cp & 0x3Ful));
				break;
			case 3:
				out[written++] = (char)(0xE0ul | (cp >> 12));
				out[written++] = (char)(0x80ul | ((cp >> 6) & 0x3Ful));
				out[written++] = (char)(0x80ul | (cp & 0x3Ful));
				break;
			default:
				out[written++] = (char)(0xF0ul | (cp >> 18));
				out[written++] = (char)(0x80ul | ((cp >> 12) & 0x3Ful));
				out[written++] = (char)(0x80ul | ((cp >> 6) & 0x3Ful));
				out[written++] = (char)(0x80ul | (cp & 0x3Ful));
				break;
		}
	}

	out[written] = 0;
	return written;
}

size_t WideCharFromUtf8( const char *in, WideChar *out, size_t outUnits )
{
	if (out == NULL || outUnits == 0)
		return 0;

	size_t written = 0;
	const unsigned char *p = (const unsigned char *)in;
	while (p != NULL && *p != 0)
	{
		// Read one code point, or U+FFFD for one bad byte.
		unsigned long cp = 0xFFFDul;
		size_t length = 1;
		const unsigned char lead = p[0];
		if (lead < 0x80)
		{
			cp = lead;
		}
		else
		{
			size_t follow = 0;
			unsigned long value = 0, least = 0;
			if ((lead & 0xE0) == 0xC0)			{ follow = 1; value = lead & 0x1F; least = 0x80ul; }
			else if ((lead & 0xF0) == 0xE0)	{ follow = 2; value = lead & 0x0F; least = 0x800ul; }
			else if ((lead & 0xF8) == 0xF0)	{ follow = 3; value = lead & 0x07; least = 0x10000ul; }
			size_t k = 1;
			for (; follow != 0 && k <= follow && (p[k] & 0xC0) == 0x80; ++k)
				value = (value << 6) | (p[k] & 0x3F);
			if (follow != 0 && k == follow + 1 && value >= least && value <= 0x10FFFFul
				&& !(value >= 0xD800ul && value <= 0xDFFFul))
			{
				cp = value;
				length = follow + 1;
			}
		}

		// Write it, and stop on a whole character rather than half of one.
		const size_t need = (sizeof(WideChar) == 2 && cp >= 0x10000ul) ? 2 : 1;
		if (written + need >= outUnits)
			break;
		if (need == 2)
		{
			out[written++] = (WideChar)(0xD800ul + ((cp - 0x10000ul) >> 10));
			out[written++] = (WideChar)(0xDC00ul + ((cp - 0x10000ul) & 0x3FFul));
		}
		else
		{
			out[written++] = (WideChar)cp;
		}
		p += length;
	}
	out[written] = 0;
	return written;
}

Int WideCharFileWrite( FILE *f, const WideChar *s )
{
	if (f == NULL || s == NULL)
		return -1;

#ifdef _WIN32

	// Byte for byte the call this replaces: fwprintf(f, L"%ws", s).
	return (Int)::fwprintf( f, L"%ls", AS_CRT( s ) );

#else

	// See the header: this is the Mac build's placeholder, not its answer.  Recorder.cpp mixes
	// fwrite and fwprintf on one FILE*, which a POSIX C library will not do, so the replay writer
	// needs a byte-oriented rewrite before any of this runs - C1/M2 work.  Until then this at
	// least keeps the WideChar-to-wchar_t conversion in one place instead of five.
	const ScopedUtf8Ctype utf8;		// see WideCharFormatV

	wchar_t wide[ WIDE_SCRATCH ];
	widenToWchar( s, wide, WIDE_SCRATCH );
	return (Int)::fwprintf( f, L"%ls", wide );

#endif
}
