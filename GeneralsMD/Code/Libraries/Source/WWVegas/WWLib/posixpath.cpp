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

// The engine's paths, resolved against a POSIX file system.  posixpath.h says what and why; zhio.h's
// forwarders are here too, because each is a resolve followed by the C runtime call.

#include "posixpath.h"
#include "zhio.h"
#include "wwdebug.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

// One directory's entries, keyed by their ASCII-folded names, each key's spellings in byte order.
struct Listing
{
	struct timespec modified;
	std::map<std::string, std::vector<std::string> > by_folded;
};

// Every lookup takes this: Miles opens streams from its service thread through the file system, so
// lookups arrive from more than one thread.
std::mutex g_lock;
std::map<std::string, Listing> g_listings;	// by real directory; "" is the current directory
std::set<std::string> g_reported;			// ambiguities already logged, by directory and folded name

bool is_separator(char c)
{
	return c == '\\' || c == '/';
}

// ASCII only, as the engine's own nocase comparisons are.  A byte above 0x7F compares as itself.
std::string fold(const std::string & name)
{
	std::string folded(name);
	for (size_t i = 0; i < folded.size(); ++i) {
		if (folded[i] >= 'A' && folded[i] <= 'Z') folded[i] = (char)(folded[i] - 'A' + 'a');
	}
	return folded;
}

std::string join(const std::string & directory, const std::string & name)
{
	if (directory.empty()) return name;
	if (directory == "/") return "/" + name;
	return directory + "/" + name;
}

std::string parent_of(const std::string & real_path)
{
	const size_t slash = real_path.rfind('/');
	if (slash == std::string::npos) return "";
	if (slash == 0) return "/";
	return real_path.substr(0, slash);
}

bool modification_time(const std::string & directory, struct timespec & modified)
{
	struct stat status;
	if (stat(directory.empty() ? "." : directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
		return false;
	}
#if defined(__APPLE__)
	modified = status.st_mtimespec;
#else
	modified = status.st_mtim;
#endif
	return true;
}

// The listing of a directory, read again only when its modification time has moved.  Called with
// g_lock held.  NULL when the directory cannot be read.
const Listing * listing_of(const std::string & directory)
{
	struct timespec now;
	if (!modification_time(directory, now)) {
		return NULL;
	}
	std::map<std::string, Listing>::iterator cached = g_listings.find(directory);
	if (cached != g_listings.end() && cached->second.modified.tv_sec == now.tv_sec
		&& cached->second.modified.tv_nsec == now.tv_nsec) {
		return &cached->second;
	}

	DIR * handle = opendir(directory.empty() ? "." : directory.c_str());
	if (handle == NULL) {
		return NULL;
	}
	Listing fresh;
	fresh.modified = now;
	while (struct dirent * entry = readdir(handle)) {
		const std::string name = entry->d_name;
		if (name == "." || name == "..") continue;
		fresh.by_folded[fold(name)].push_back(name);
	}
	closedir(handle);
	for (std::map<std::string, std::vector<std::string> >::iterator it = fresh.by_folded.begin(); it != fresh.by_folded.end(); ++it) {
		std::sort(it->second.begin(), it->second.end());
	}
	Listing & stored = g_listings[directory];
	stored = fresh;
	return &stored;
}

// The spelling on disk of one component, matched without regard to case.  Empty when there is none.
std::string match_in(const std::string & directory, const std::string & component)
{
	std::lock_guard<std::mutex> guard(g_lock);
	const Listing * listing = listing_of(directory);
	if (listing == NULL) {
		return std::string();
	}
	const std::string folded = fold(component);
	std::map<std::string, std::vector<std::string> >::const_iterator found = listing->by_folded.find(folded);
	if (found == listing->by_folded.end()) {
		return std::string();
	}
	// D2: an exact spelling would have been found by the caller's own stat, so this is the case where
	// none exists; byte order decides, and it is said once.
	if (found->second.size() > 1 && g_reported.insert(directory + "/" + folded).second) {
		WWDEBUG_WARNING(("PosixPath: \"%s\" matches %u names in \"%s\" that differ only in case; using \"%s\"\n",
			component.c_str(), (unsigned)found->second.size(), directory.empty() ? "." : directory.c_str(),
			found->second[0].c_str()));
	}
	return found->second[0];
}

} // namespace

bool PosixPath_Resolve(const char * engine_path, PosixPathIntent intent, std::string & real_path)
{
	real_path.clear();
	if (engine_path == NULL || engine_path[0] == '\0') {
		return false;
	}
	// A UNC path, and a drive letter: Windows spellings no POSIX file system has.
	if (is_separator(engine_path[0]) && is_separator(engine_path[1])) {
		return false;
	}
	if (((engine_path[0] >= 'A' && engine_path[0] <= 'Z') || (engine_path[0] >= 'a' && engine_path[0] <= 'z'))
		&& engine_path[1] == ':') {
		return false;
	}

	std::vector<std::string> components;
	std::string component;
	for (const char * at = engine_path; ; ++at) {
		if (*at == '\0' || is_separator(*at)) {
			if (!component.empty()) components.push_back(component);
			component.clear();
			if (*at == '\0') break;
		}
		else {
			component += *at;
		}
	}

	std::string current = is_separator(engine_path[0]) ? "/" : "";
	bool missing = false;	// once one component is missing, none after it can exist
	for (size_t i = 0; i < components.size(); ++i) {
		const std::string & name = components[i];
		const bool last = i + 1 == components.size();
		if (name == ".") {
			continue;
		}
		if (missing || name == "..") {
			current = join(current, name);
			continue;
		}
		const std::string exact = join(current, name);
		struct stat status;
		if (lstat(exact.c_str(), &status) == 0) {
			current = exact;
			continue;
		}
		const std::string on_disk = match_in(current, name);
		if (!on_disk.empty()) {
			current = join(current, on_disk);
			continue;
		}
		if (intent == POSIX_PATH_EXISTING || (intent == POSIX_PATH_CREATE_LEAF && !last)) {
			return false;
		}
		missing = true;
		current = exact;
	}
	real_path = current.empty() ? "." : current;
	return true;
}

void PosixPath_Forget_Directory(const char * real_directory)
{
	std::lock_guard<std::mutex> guard(g_lock);
	g_listings.erase(real_directory != NULL && strcmp(real_directory, ".") != 0 ? real_directory : "");
}

void PosixPath_Forget_All()
{
	std::lock_guard<std::mutex> guard(g_lock);
	g_listings.clear();
	g_reported.clear();
}

// ---- the zh_* forwarders (zhio.h) ------------------------------------------------------------------

namespace {

bool resolve_or_fail(const char * path, PosixPathIntent intent, std::string & real)
{
	if (PosixPath_Resolve(path, intent, real)) {
		return true;
	}
	errno = ENOENT;
	return false;
}

void forget_parent(const std::string & real)
{
	PosixPath_Forget_Directory(parent_of(real).c_str());
}

} // namespace

FILE * zh_fopen(const char * path, const char * mode)
{
	const bool creates = mode != NULL && (mode[0] == 'w' || mode[0] == 'a');
	std::string real;
	if (!resolve_or_fail(path, creates ? POSIX_PATH_CREATE_LEAF : POSIX_PATH_EXISTING, real)) {
		return NULL;
	}
	FILE * file = fopen(real.c_str(), mode);
	if (file != NULL && creates) {
		forget_parent(real);
	}
	return file;
}

int zh_open(const char * path, int flags, int permissions)
{
	const bool creates = (flags & O_CREAT) != 0;
	std::string real;
	if (!resolve_or_fail(path, creates ? POSIX_PATH_CREATE_LEAF : POSIX_PATH_EXISTING, real)) {
		return -1;
	}
	const int handle = open(real.c_str(), flags, permissions);
	if (handle >= 0 && creates) {
		forget_parent(real);
	}
	return handle;
}

int zh_access(const char * path, int mode)
{
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_EXISTING, real)) {
		return -1;
	}
	return access(real.c_str(), mode);
}

int zh_remove(const char * path)
{
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_EXISTING, real)) {
		return -1;
	}
	const int result = remove(real.c_str());
	if (result == 0) {
		forget_parent(real);
	}
	return result;
}

int zh_rename(const char * from, const char * to)
{
	std::string real_from, real_to;
	if (!resolve_or_fail(from, POSIX_PATH_EXISTING, real_from) || !resolve_or_fail(to, POSIX_PATH_CREATE_LEAF, real_to)) {
		return -1;
	}
	const int result = rename(real_from.c_str(), real_to.c_str());
	if (result == 0) {
		forget_parent(real_from);
		forget_parent(real_to);
	}
	return result;
}

int zh_mkdir(const char * path)
{
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_CREATE_LEAF, real)) {
		return -1;
	}
	const int result = mkdir(real.c_str(), 0777);
	if (result == 0) {
		forget_parent(real);
	}
	return result;
}
