/*
 * test_wwdownload's stand-in for the engine's DebugLog (GameEngine/Source/Common/System/Debug.cpp).
 *
 * A Debug build of wwdownload compiles its DEBUG_LOG calls (FTP.cpp), and the test links wwdownload without
 * the engine, so the call needs a definition here.  A Release wwdownload compiles them out, and so does this
 * file: its object is empty there.
 */
#if defined(_DEBUG)

#include <stdarg.h>
#include <stdio.h>

extern "C" void DebugLog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs("  [DebugLog] ", stderr);
	vfprintf(stderr, format, args);
	va_end(args);
}

#endif
