/*
 * Link-time stand-ins for test_posixlocalfilesystem, which is built from the real GameMemory,
 * MemoryInit, LocalFile, File, RAMFile and PosixDevice sources rather than from gameengine, since
 * gameengine does not link on macOS yet.  Each name here is one those sources refer to and the test
 * never reaches; when gameengine links, the test links it instead and this file goes.
 *
 *  - DebugInit, DebugLog: Debug.cpp does not compile off Windows yet.  DebugLog prints, so a
 *    DEBUG_LOG from the memory manager is not lost.
 *  - INI: SubsystemInterfaceList::initSubsystem loads INI files; nothing here initialises a
 *    subsystem through it.
 *  - TheFileSystem, FileSystem::openFile: RAMFile::open(const char*) opens through the whole file
 *    system; the test opens files through PosixLocalFileSystem directly.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "Common/Debug.h"
#include "Common/FileSystem.h"
#include "Common/INI.h"

extern "C" void DebugInit(int)
{
}

extern "C" void DebugLog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs("  [DebugLog] ", stderr);
	vfprintf(stderr, format, args);
	va_end(args);
}

static void unreachable(const char *what)
{
	fprintf(stderr, "test_posixlocalfilesystem: %s was reached, and this test has no stand-in for it\n", what);
	abort();
}

INI::INI() { unreachable("INI::INI"); }
INI::~INI() {}
void INI::loadDirectory(AsciiString, Bool, INILoadType, Xfer *) { unreachable("INI::loadDirectory"); }
void INI::load(AsciiString, INILoadType, Xfer *) { unreachable("INI::load"); }

FileSystem *TheFileSystem = NULL;
File *FileSystem::openFile(const Char *, Int) { unreachable("FileSystem::openFile"); return NULL; }
