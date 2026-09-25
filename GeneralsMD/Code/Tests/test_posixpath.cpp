/*
 * test_posixpath - the engine's paths resolved against a POSIX file system (C1, posixpath.h), and the
 * zh_* forwarders built on it (zhio.h).
 *
 * One suite, run against a fixture tree laid out in the shapes of a real Zero Hour install - the
 * places where the code's spelling and the disk's are known to differ: the cursors
 * (data\cursors\SCCPointer.ANI against Data/Cursors/sccpointer.ani), the Bink movies
 * (Data/english/.../EA_LOGO.bik against Data/English/.../EA_LOGO.BIK), gensecZH.big.
 *
 * Case matters only on a case-sensitive volume, and the default macOS volume is not one - neither is
 * the exFAT the Steam installs sit on.  So the suite runs twice where it can:
 *   - in $TMPDIR, whatever that volume is, and it says which;
 *   - on macOS, on a small case-sensitive APFS image it creates and attaches with hdiutil.  If that
 *     cannot be done it prints SKIPPED with the reason; on Linux the first run is already on a
 *     case-sensitive file system.
 * Checks that only mean something on a case-sensitive volume (the on-disk spelling of a result, two
 * names differing only in case) run only there, and say so when they do not.
 */
#include "test_harness.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "posixpath.h"
#include "zhio.h"

namespace {

// ---- the fixture, written with plain POSIX calls, independently of the code under test ----

void make_directory(const std::string & path)
{
	mkdir(path.c_str(), 0777);
}

void write_file(const std::string & path, const std::string & contents)
{
	FILE * file = fopen(path.c_str(), "wb");
	if (file != NULL) {
		fwrite(contents.data(), 1, contents.size(), file);
		fclose(file);
	}
}

std::string read_file(const std::string & path)
{
	std::string contents;
	FILE * file = fopen(path.c_str(), "rb");
	if (file == NULL) return contents;
	char buffer[256];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) contents.append(buffer, got);
	fclose(file);
	return contents;
}

std::string lower(std::string s)
{
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
	}
	return s;
}

void build_fixture(const std::string & root)
{
	make_directory(root);
	make_directory(root + "/Data");
	make_directory(root + "/Data/INI");
	make_directory(root + "/Data/Cursors");
	make_directory(root + "/Data/English");
	make_directory(root + "/Data/English/Movies");
	make_directory(root + "/Data/Scripts");
	make_directory(root + "/Save");
	write_file(root + "/Data/INI/INIZH.big", "big");
	write_file(root + "/Data/Cursors/sccpointer.ani", "cursor");
	write_file(root + "/Data/English/Movies/EA_LOGO.BIK", "movie");
	write_file(root + "/Data/Scripts/SkirmishScripts.scb", "scripts");
	write_file(root + "/gensecZH.big", "gensec");
}

// Whether names in this directory differ by case: create one spelling, look for the other.
bool is_case_sensitive(const std::string & directory)
{
	const std::string probe = directory + "/case_probe";
	write_file(probe, "x");
	struct stat status;
	const bool sensitive = stat((directory + "/CASE_PROBE").c_str(), &status) != 0;
	remove(probe.c_str());
	return sensitive;
}

// Resolves for reading and returns the contents, "" when it did not resolve.
std::string resolved_contents(const std::string & engine_path)
{
	std::string real;
	if (!PosixPath_Resolve(engine_path.c_str(), POSIX_PATH_EXISTING, real)) return std::string();
	return read_file(real);
}

std::string resolved(const std::string & engine_path, PosixPathIntent intent)
{
	std::string real;
	if (!PosixPath_Resolve(engine_path.c_str(), intent, real)) return "(unresolved)";
	return real;
}

unsigned run_suite(const std::string & root, const char * where)
{
	build_fixture(root);
	const bool sensitive = is_case_sensitive(root);
	printf("  %s: %s, a %s volume\n", where, root.c_str(), sensitive ? "case-SENSITIVE" : "case-insensitive");
	PosixPath_Forget_All();

	char previous[4096];
	if (getcwd(previous, sizeof(previous)) == NULL || chdir(root.c_str()) != 0) {
		printf("  could not enter %s\n", root.c_str());
		CHECK(false);
		return 0;
	}
	unsigned sensitive_only = 0;

	// The code's spelling against the disk's, relative to the install root.
	CHECK_STR(resolved_contents("Data\\INI\\INIZH.big").c_str(), "big");
	CHECK_STR(resolved_contents("data\\cursors\\SCCPointer.ANI").c_str(), "cursor");
	CHECK_STR(resolved_contents("Data/english/Movies/EA_LOGO.bik").c_str(), "movie");
	CHECK_STR(resolved_contents("genseczh.big").c_str(), "gensec");
	CHECK_STR(resolved_contents("Data\\Scripts/SkirmishScripts.scb").c_str(), "scripts");
	CHECK_STR(resolved_contents("Data\\.\\INI\\..\\Scripts\\SKIRMISHSCRIPTS.SCB").c_str(), "scripts");
	CHECK_STR(resolved_contents("Data\\\\Scripts\\\\SkirmishScripts.scb").c_str(), "scripts");	// doubled separators
	if (sensitive) {
		// What the operating system is actually handed.
		CHECK_STR(resolved("data\\cursors\\SCCPointer.ANI", POSIX_PATH_EXISTING).c_str(), "Data/Cursors/sccpointer.ani");
		CHECK_STR(resolved("Data/english/Movies/EA_LOGO.bik", POSIX_PATH_EXISTING).c_str(), "Data/English/Movies/EA_LOGO.BIK");
		CHECK_STR(resolved("genseczh.big", POSIX_PATH_EXISTING).c_str(), "gensecZH.big");
		sensitive_only += 3;
	}

	// Absolute paths: the user-data directory is one, with the engine's joins after it, and some code
	// lowercases the whole thing.
	CHECK_STR(resolved_contents(root + "\\DATA\\SCRIPTS\\skirmishscripts.SCB").c_str(), "scripts");
	CHECK_STR(resolved_contents(lower(root) + "/data/scripts/skirmishscripts.scb").c_str(), "scripts");

	// What does not exist, and what no POSIX system can have.
	std::string real;
	CHECK(!PosixPath_Resolve("Data\\INI\\Nope.ini", POSIX_PATH_EXISTING, real));
	CHECK(!PosixPath_Resolve("", POSIX_PATH_EXISTING, real));
	CHECK(!PosixPath_Resolve("C:\\Games\\Data\\INI\\INIZH.big", POSIX_PATH_EXISTING, real));
	CHECK(!PosixPath_Resolve("\\\\server\\share\\INIZH.big", POSIX_PATH_EXISTING, real));

	// Creating: existing directories keep the disk's spelling, a new last component the engine's.
	if (sensitive) {
		CHECK_STR(resolved("SAVE\\NewGame.sav", POSIX_PATH_CREATE_LEAF).c_str(), "Save/NewGame.sav");
		CHECK_STR(resolved("save\\Sub\\Deeper\\x.txt", POSIX_PATH_CREATE_PATH).c_str(), "Save/Sub/Deeper/x.txt");
		sensitive_only += 2;
	}
	CHECK(!PosixPath_Resolve("NoDir\\x.txt", POSIX_PATH_CREATE_LEAF, real));
	CHECK(PosixPath_Resolve("NoDir\\Sub\\x.txt", POSIX_PATH_CREATE_PATH, real));

	// D2: two names differing only in case.  Only a case-sensitive volume can hold both.
	if (sensitive) {
		make_directory(root + "/Amb");
		write_file(root + "/Amb/Foo.txt", "upper");
		write_file(root + "/Amb/foo.txt", "lower");
		CHECK_STR(resolved_contents("amb\\FOO.TXT").c_str(), "upper");	// no exact spelling: byte order, 'F' < 'f'
		CHECK_STR(resolved_contents("Amb\\foo.txt").c_str(), "lower");	// the exact spelling wins
		CHECK_STR(resolved_contents("Amb\\Foo.txt").c_str(), "upper");
		sensitive_only += 3;
	}

	// A file that appears after its directory was listed, created outside the resolver: the
	// directory's time moves, and the next lookup reads it again.
	CHECK(!PosixPath_Resolve("data\\LATE\\file.txt", POSIX_PATH_EXISTING, real));
	make_directory(root + "/Data/late");
	write_file(root + "/Data/late/FILE.TXT", "late");
	CHECK_STR(resolved_contents("data\\LATE\\file.txt").c_str(), "late");

	// The forwarders.
	FILE * file = zh_fopen("DATA\\SCRIPTS\\New.txt", "w");
	CHECK(file != NULL);
	if (file != NULL) {
		fputs("written", file);
		fclose(file);
	}
	CHECK_STR(read_file(root + "/Data/Scripts/New.txt").c_str(), "written");
	file = zh_fopen("data\\scripts\\NEW.TXT", "r");
	CHECK(file != NULL);
	if (file != NULL) fclose(file);
	CHECK(zh_fopen("Data\\Scripts\\Absent.txt", "r") == NULL && errno == ENOENT);
	CHECK(zh_fopen("Absent\\New.txt", "w") == NULL);	// a missing directory is not created, as on Windows
	CHECK_EQ(zh_access("data\\scripts\\new.txt", F_OK), 0);
	CHECK_EQ(zh_rename("data\\scripts\\new.txt", "DATA\\scripts\\Renamed.txt"), 0);
	CHECK_STR(read_file(root + "/Data/Scripts/Renamed.txt").c_str(), "written");
	CHECK(zh_access("Data\\Scripts\\New.txt", F_OK) != 0);
	CHECK_EQ(zh_remove("data\\SCRIPTS\\renamed.TXT"), 0);
	CHECK(zh_access("Data\\Scripts\\Renamed.txt", F_OK) != 0);
	CHECK_EQ(zh_mkdir("SAVE\\Replays"), 0);
	struct stat status;
	CHECK(stat((root + "/Save/Replays").c_str(), &status) == 0 && S_ISDIR(status.st_mode));
	const int handle = zh_open("save\\replays\\Last.rep", O_CREAT | O_WRONLY | O_TRUNC, 0666);
	CHECK(handle >= 0);
	if (handle >= 0) {
		CHECK_EQ((int)write(handle, "rep", 3), 3);
		close(handle);
	}
	CHECK_STR(read_file(root + "/Save/Replays/Last.rep").c_str(), "rep");

	if (chdir(previous) != 0) CHECK(false);
	if (!sensitive) {
		printf("  %s: the %u checks that need a case-sensitive volume did not run here\n", where, 8u);
	}
	return sensitive_only;
}

std::string temp_root(const char * name)
{
	const char * temp = getenv("TMPDIR");
	std::string base = (temp != NULL && *temp) ? temp : "/tmp";
	if (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
	char unique[64];
	snprintf(unique, sizeof(unique), "/%s_%d", name, (int)getpid());
	return base + unique;
}

void remove_tree(const std::string & root)
{
	const std::string command = "rm -rf '" + root + "'";
	if (system(command.c_str()) != 0) printf("  could not remove %s\n", root.c_str());
}

} // namespace

TEST(engine_paths_resolve_on_this_volume)
{
	const std::string root = temp_root("test_posixpath");
	run_suite(root, "default volume");
	remove_tree(root);
}

TEST(engine_paths_resolve_on_a_case_sensitive_volume)
{
#if defined(__APPLE__)
	// A 16 MB sparse image, formatted case-sensitive APFS, attached without appearing in Finder.
	const std::string image_root = temp_root("test_posixpath_image");
	const std::string image = image_root + ".sparseimage";
	const std::string mount = image_root + "_mount";
	mkdir(mount.c_str(), 0777);
	const std::string create = "hdiutil create -quiet -size 16m -type SPARSE -fs 'Case-sensitive APFS' -volname ZHCS '" + image_root + "' 2>&1";
	const std::string attach = "hdiutil attach -quiet -nobrowse -mountpoint '" + mount + "' '" + image + "' 2>&1";
	if (system(create.c_str()) != 0 || system(attach.c_str()) != 0) {
		printf("  SKIPPED the case-sensitive run: hdiutil could not create or attach an image in %s\n", mount.c_str());
		remove(image.c_str());
		rmdir(mount.c_str());
		return;
	}
	const unsigned sensitive_checks = run_suite(mount + "/root", "case-sensitive APFS image");
	CHECK(sensitive_checks > 0);	// the image really is case-sensitive, or this run proved nothing
	const std::string detach = "hdiutil detach -quiet '" + mount + "' 2>&1";
	if (system(detach.c_str()) != 0) printf("  could not detach %s\n", mount.c_str());
	remove(image.c_str());
	rmdir(mount.c_str());
#else
	printf("  not needed off macOS: the default-volume run is already case-sensitive here\n");
#endif
}
