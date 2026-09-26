/*
 * WWDownload's registry functions and FormatURLFromRegistry off Windows (WWDownloadRegistryPosix.cpp),
 * over this user's Registry.ini: what OptionsMenu, ScoreScreen, PopupPlayerInfo,
 * SkirmishGameOptionsMenu and MainMenuUtils call.  The file is the one registry.cpp's own readers use,
 * so a value set here is the value the engine's AsciiString getters read, and the tests check that
 * from both sides.
 *
 * The user data directory is ZH_USER_DATA_DIR, set by CMake to a folder in the build tree; the test
 * empties it first.  Built with test_registryfile's engine sources (CMakeLists.txt).
 */

#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "WWDownload/Registry.h"
#include "WWDownload/urlBuilder.h"

// The engine's own getters, from registry.cpp.  Declared here rather than through Common/Registry.h,
// whose AsciiString overloads would make every call below with a string literal ambiguous.
Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val);
Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val);

namespace {

std::string registry_path()
{
	const char *dir = getenv("ZH_USER_DATA_DIR");
	return std::string(dir != NULL ? dir : "") + "/Registry.ini";
}

std::string read_registry()
{
	std::string contents;
	FILE *file = fopen(registry_path().c_str(), "rb");
	if (file != NULL) {
		char chunk[512];
		size_t count;
		while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0)
			contents.append(chunk, count);
		fclose(file);
	}
	return contents;
}

void start_empty()
{
	static bool booted = false;
	if (!booted) {
		initMemoryManager();
		booted = true;
	}
	const char *dir = getenv("ZH_USER_DATA_DIR");
	if (dir != NULL && *dir) {
		const std::string command = std::string("mkdir -p '") + dir + "'";
		if (system(command.c_str()) != 0)
			printf("  could not make %s\n", dir);
	}
	remove(registry_path().c_str());
}

}  // namespace

TEST(wwdownload_registry_needs_a_user_data_directory_to_write_to)
{
	const char *dir = getenv("ZH_USER_DATA_DIR");
	CHECK(dir != NULL && *dir != 0);	// CMake sets it; without it every write below would fail
}

TEST(wwdownload_registry_strings_round_trip_through_registry_ini)
{
	start_empty();
	std::string value = "untouched";
	CHECK(!GetStringFromRegistry(std::string(""), std::string("Proxy"), value));
	CHECK(value == "untouched");	// a missing key leaves the caller's value alone, as on Windows

	CHECK(SetStringInRegistry("", "Proxy", "proxy.example:8080"));
	CHECK(GetStringFromRegistry(std::string(""), std::string("Proxy"), value));
	CHECK(value == "proxy.example:8080");
	CHECK(read_registry().find("Proxy = proxy.example:8080") != std::string::npos);

	// and the engine's own reader sees the same value in the same file
	AsciiString engineValue;
	CHECK(GetStringFromRegistry(AsciiString(""), AsciiString("Proxy"), engineValue));
	CHECK(strcmp(engineValue.str(), "proxy.example:8080") == 0);

	// a second write replaces it rather than adding a second line
	CHECK(SetStringInRegistry("", "Proxy", "other.example:3128"));
	CHECK(GetStringFromRegistry(std::string(""), std::string("Proxy"), value));
	CHECK(value == "other.example:3128");
	CHECK(read_registry().find("proxy.example:8080") == std::string::npos);
}

TEST(wwdownload_registry_numbers_are_decimal_text_both_sides_read)
{
	start_empty();
	CHECK(SetUnsignedIntInRegistry("", "Version", 65540u));
	unsigned int value = 0;
	CHECK(GetUnsignedIntFromRegistry(std::string(""), std::string("Version"), value));
	CHECK_EQ((int)value, 65540);
	CHECK(read_registry().find("Version = 65540") != std::string::npos);

	UnsignedInt engineValue = 0;
	CHECK(GetUnsignedIntFromRegistry(AsciiString(""), AsciiString("Version"), engineValue));
	CHECK_EQ((int)engineValue, 65540);

	// the whole range, which is what ScoreScreen's counters can reach
	CHECK(SetUnsignedIntInRegistry("", "dc", 4294967295u));
	CHECK(GetUnsignedIntFromRegistry(std::string(""), std::string("dc"), value));
	CHECK(value == 4294967295u);

	// text that is not a number reads as missing, as registry.cpp's reader has it
	CHECK(SetStringInRegistry("", "se", "twelve"));
	value = 7;
	CHECK(!GetUnsignedIntFromRegistry(std::string(""), std::string("se"), value));
	CHECK_EQ((int)value, 7);
}

TEST(wwdownload_registry_paths_below_the_game_key_are_part_of_the_key)
{
	start_empty();
	CHECK(SetStringInRegistry("\\ergc", "", "ABCD-EFGH"));
	CHECK(read_registry().find("ergc\\ = ABCD-EFGH") != std::string::npos);
	std::string value;
	CHECK(GetStringFromRegistry(std::string("\\ergc"), std::string(""), value));
	CHECK(value == "ABCD-EFGH");
	// and it is not the same value as the one at the top of the key
	CHECK(!GetStringFromRegistry(std::string(""), std::string(""), value));
}

TEST(wwdownload_registry_clears_a_value_with_an_empty_string)
{
	/* Windows stores an empty string, which the proxy box's readers take as "no proxy".  Registry.ini
		 writes "Proxy =", which reads as missing, and those readers take missing as "no proxy" too. */
	start_empty();
	CHECK(SetStringInRegistry("", "Proxy", "proxy.example:8080"));
	CHECK(SetStringInRegistry("", "Proxy", ""));
	std::string value;
	CHECK(!GetStringFromRegistry(std::string(""), std::string("Proxy"), value));
	CHECK(read_registry().find("proxy.example") == std::string::npos);
}

TEST(format_url_from_registry_is_urlbuilders_on_an_empty_registry_and_follows_it)
{
	start_empty();
	std::string game, map, config, motd;
	FormatURLFromRegistry(game, map, config, motd);
	CHECK(game == "http://servserv.generals.ea.com/servserv/GeneralsZH/english-0.txt");
	CHECK(map == "http://servserv.generals.ea.com/servserv/GeneralsZH/maps-0.txt");
	CHECK(config == "http://servserv.generals.ea.com/servserv/GeneralsZH/config.txt");
	CHECK(motd == "http://servserv.generals.ea.com/servserv/GeneralsZH/MOTD-english.txt");

	CHECK(SetStringInRegistry("", "Language", "turkish"));
	CHECK(SetUnsignedIntInRegistry("", "Version", 3));
	CHECK(SetUnsignedIntInRegistry("", "MapPackVersion", 2));
	CHECK(SetStringInRegistry("", "BaseURL", "http://example.invalid/zh/"));
	FormatURLFromRegistry(game, map, config, motd);
	CHECK(game == "http://example.invalid/zh/turkish-3.txt");
	CHECK(map == "http://example.invalid/zh/maps-2.txt");
	CHECK(config == "http://example.invalid/zh/config.txt");
	CHECK(motd == "http://example.invalid/zh/MOTD-turkish.txt");
}
