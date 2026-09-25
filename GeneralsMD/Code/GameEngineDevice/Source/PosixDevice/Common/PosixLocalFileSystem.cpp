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

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "PosixDevice/Common/PosixLocalFile.h"

#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "posixpath.h"
#include "zhio.h"

PosixLocalFileSystem::PosixLocalFileSystem() : LocalFileSystem()
{
}

PosixLocalFileSystem::~PosixLocalFileSystem()
{
}

File * PosixLocalFileSystem::openFile(const Char *filename, Int access /* = 0 */)
{
	// sanity check
	if (filename == NULL || filename[0] == '\0') {
		return NULL;
	}

	PosixLocalFile *file = newInstance( PosixLocalFile );

	if (access & File::WRITE) {
		// Win32LocalFileSystem's walk: every token before the file is a directory to create, and the
		// file is the first token with a '.' when no '.' follows it.  nextToken drops a leading
		// separator, which on Windows only loses nothing ("C:" comes first); here it would turn an
		// absolute path relative, so the root is put back.  A last component with no '.' ran that
		// walk on Windows until AsciiString's length ceiling threw (defect 12 in docs/mac-port's
		// README); here it stops when the name runs out.
		AsciiString string;
		string = filename;
		AsciiString token;
		AsciiString dirName;
		string.nextToken(&token, "\\/");
		if (filename[0] == '/' || filename[0] == '\\') {
			dirName = "/";
		}
		dirName.concat(token);
		while (token.isNotEmpty() && ((token.find('.') == NULL) || (string.find('.') != NULL))) {
			createDirectory(dirName);
			string.nextToken(&token, "\\/");
			dirName.concat('\\');
			dirName.concat(token);
		}
	}

	if (file->open(filename, access) == FALSE) {
		file->close();
		file->deleteInstance();
		file = NULL;
	} else {
		file->deleteOnClose();
	}

	return file;
}

void PosixLocalFileSystem::update()
{
}

void PosixLocalFileSystem::init()
{
}

void PosixLocalFileSystem::reset()
{
}

Bool PosixLocalFileSystem::doesFileExist(const Char *filename) const
{
	return zh_access(filename, F_OK) == 0;
}

void PosixLocalFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList & filenameList, Bool searchSubdirectories) const
{
	std::vector<std::string> found;
	PosixPath_List_Like_Win32(currentDirectory.str(), originalDirectory.str(), searchName.str(), searchSubdirectories != FALSE, found);
	for (size_t i = 0; i < found.size(); ++i) {
		AsciiString newFilename = found[i].c_str();
		if (filenameList.find(newFilename) == filenameList.end()) {
			filenameList.insert(newFilename);
		}
	}
}

// What FindFirstFile reports for one name: the last write time as a FILETIME (100 ns units since
// 1601), and the size, 0 for a directory.
Bool PosixLocalFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	std::string real;
	struct stat status;
	if (!PosixPath_Resolve(filename.str(), POSIX_PATH_EXISTING, real) || stat(real.c_str(), &status) != 0) {
		return FALSE;
	}

#if defined(__APPLE__)
	const long long nanoseconds = status.st_mtimespec.tv_nsec;
#else
	const long long nanoseconds = status.st_mtim.tv_nsec;
#endif
	const unsigned long long seconds_1601_to_1970 = 11644473600ULL;
	const unsigned long long filetime = ((unsigned long long)status.st_mtime + seconds_1601_to_1970) * 10000000ULL + nanoseconds / 100;
	const unsigned long long size = S_ISDIR(status.st_mode) ? 0 : (unsigned long long)status.st_size;

	fileInfo->timestampHigh = (Int)(filetime >> 32);
	fileInfo->timestampLow = (Int)(filetime & 0xFFFFFFFFULL);
	fileInfo->sizeHigh = (Int)(size >> 32);
	fileInfo->sizeLow = (Int)(size & 0xFFFFFFFFULL);

	return TRUE;
}

// One level, as CreateDirectory makes it: false when it exists already or its parent does not.
// Windows' _MAX_DIR limit is not imposed; the operating system's own applies.
Bool PosixLocalFileSystem::createDirectory(AsciiString directory)
{
	if (directory.getLength() > 0) {
		return zh_mkdir(directory.str()) == 0;
	}
	return FALSE;
}
