/*
 * fs_oracle - prints what the engine's local file system would see of a folder, for comparing the
 * Windows code with its POSIX replacement (C1).  One source, two builds:
 *
 *   - Windows (mingw-w64, run under Wine): Win32LocalFileSystem::getFileListInDirectory copied
 *     verbatim but for AsciiString, and _read on files opened _O_TEXT.
 *   - POSIX: PosixPath_List_Like_Win32 and zh_read_text from WWLib.
 *
 * Both print the list as the engine holds it - a set ordered without regard to case, as
 * FilenameList is - and, with "text", how each file reads in text mode, for chunk sizes from one
 * byte up.  The two outputs are compared with diff.  C1's task file records the runs.
 *
 *   fs_oracle list <root> <originalDirectory> <searchName> <0|1 subdirectories>
 *   fs_oracle text <root> <originalDirectory> <searchName> <0|1 subdirectories>
 *
 * <root> becomes the current directory first, as the install root is the game's.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define compare_no_case _stricmp
#else
#include <strings.h>
#include <unistd.h>
#include "posixpath.h"
#include "zhio.h"
#define compare_no_case strcasecmp
#endif

namespace {

struct LessNoCase
{
	bool operator()(const std::string & a, const std::string & b) const { return compare_no_case(a.c_str(), b.c_str()) < 0; }
};

typedef std::set<std::string, LessNoCase> FilenameList;

#if defined(_WIN32)

// Win32LocalFileSystem::getFileListInDirectory, with std::string for AsciiString.
void getFileListInDirectory(const std::string & currentDirectory, const std::string & originalDirectory,
	const std::string & searchName, FilenameList & filenameList, bool searchSubdirectories)
{
	HANDLE fileHandle = NULL;
	WIN32_FIND_DATAA findData;

	std::string asciisearch = originalDirectory + currentDirectory + searchName;
	bool done = false;

	fileHandle = FindFirstFileA(asciisearch.c_str(), &findData);
	done = (fileHandle == INVALID_HANDLE_VALUE);

	while (!done) {
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				(strcmp(findData.cFileName, ".") && strcmp(findData.cFileName, ".."))) {
			std::string newFilename = originalDirectory + currentDirectory + findData.cFileName;
			if (filenameList.find(newFilename) == filenameList.end()) {
				filenameList.insert(newFilename);
			}
		}
		done = (FindNextFileA(fileHandle, &findData) == 0);
	}
	FindClose(fileHandle);

	if (searchSubdirectories) {
		std::string subdirsearch = originalDirectory + currentDirectory + "*.";
		fileHandle = FindFirstFileA(subdirsearch.c_str(), &findData);
		done = fileHandle == INVALID_HANDLE_VALUE;

		while (!done) {
			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
					(strcmp(findData.cFileName, ".") && strcmp(findData.cFileName, ".."))) {
				std::string tempsearchstr = currentDirectory + findData.cFileName + '\\';
				getFileListInDirectory(tempsearchstr, originalDirectory, searchName, filenameList, searchSubdirectories);
			}
			done = (FindNextFileA(fileHandle, &findData) == 0);
		}
		FindClose(fileHandle);
	}
}

bool enter(const char * root) { return SetCurrentDirectoryA(root) != 0; }
int open_text(const char * path) { return _open(path, _O_RDONLY | _O_TEXT); }
int read_text(int handle, char * buffer, unsigned bytes) { return _read(handle, buffer, bytes); }
long position(int handle) { return _lseek(handle, 0, SEEK_CUR); }
void close_text(int handle) { _close(handle); }

#else

void getFileListInDirectory(const std::string & currentDirectory, const std::string & originalDirectory,
	const std::string & searchName, FilenameList & filenameList, bool searchSubdirectories)
{
	std::vector<std::string> found;
	PosixPath_List_Like_Win32(currentDirectory, originalDirectory, searchName, searchSubdirectories, found);
	for (size_t i = 0; i < found.size(); ++i) {
		if (filenameList.find(found[i]) == filenameList.end()) {
			filenameList.insert(found[i]);
		}
	}
}

bool enter(const char * root) { return chdir(root) == 0; }
int open_text(const char * path) { return zh_open(path, O_RDONLY, 0); }
int read_text(int handle, char * buffer, unsigned bytes) { return zh_read_text(handle, buffer, bytes); }
long position(int handle) { return (long)lseek(handle, 0, SEEK_CUR); }
void close_text(int handle) { close(handle); }

#endif

unsigned long long fnv1a(const std::string & bytes)
{
	unsigned long long hash = 1469598103934665603ULL;
	for (size_t i = 0; i < bytes.size(); ++i) {
		hash ^= (unsigned char)bytes[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

// How one file reads in text mode, a chunk size per line: what came back, and where the file
// position ended.  Chunk sizes 1 to 8, and 4096; the small ones put a '\r' at the end of a read often.
void print_text_reads(const std::string & path)
{
	static const unsigned chunks[] = { 1, 2, 3, 4, 5, 6, 7, 8, 4096 };
	for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); ++c) {
		const int handle = open_text(path.c_str());
		if (handle < 0) {
			printf("  %u: could not open\n", chunks[c]);
			continue;
		}
		std::string text;
		char buffer[4096];
		int got;
		while ((got = read_text(handle, buffer, chunks[c])) > 0) text.append(buffer, got);
		printf("  %u: %lu bytes fnv %016llx, ends at %ld\n", chunks[c], (unsigned long)text.size(), fnv1a(text), position(handle));
		close_text(handle);
	}
}

} // namespace

int main(int argc, char ** argv)
{
	if (argc != 6 || (strcmp(argv[1], "list") != 0 && strcmp(argv[1], "text") != 0)) {
		fprintf(stderr, "usage: fs_oracle list|text <root> <originalDirectory> <searchName> <0|1 subdirectories>\n");
		return 2;
	}
#if defined(_WIN32)
	_setmode(_fileno(stdout), _O_BINARY);	// "\n" alone, as the POSIX build prints
#endif
	if (!enter(argv[2])) {
		fprintf(stderr, "could not enter %s\n", argv[2]);
		return 1;
	}
	FilenameList list;
	getFileListInDirectory("", argv[3], argv[4], list, atoi(argv[5]) != 0);
	const bool text = strcmp(argv[1], "text") == 0;
	for (FilenameList::const_iterator it = list.begin(); it != list.end(); ++it) {
		printf("%s\n", it->c_str());
		if (text) print_text_reads(*it);
	}
	return 0;
}
