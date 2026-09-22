/*
 * The width of WideChar, and what depends on it.
 *
 * B1 changes `typedef wchar_t WideChar` to `typedef char16_t WideChar` because wchar_t is two
 * bytes under MSVC and four under clang, and the difference is not a warning - it is the byte
 * count that reaches the .csf string files, the map chunks, the save games, the LAN packets and,
 * worst of all, the CRC that replays and network games compare.  A four-byte WideChar does not
 * fail to build.  It desyncs, and it reports the desync as a desync rather than as a bug.
 *
 * These tests are written before that typedef moves, on purpose: a test written afterwards can
 * only describe what the new code does, and this one has to hold both before and after.  On
 * Windows, where wchar_t is already two bytes, it passes today.  On a Mac build it is red until
 * B1 lands - that is the signal it exists to give, not a fault in the test.
 *
 * See docs/mac-port/tasks/B1-widechar-char16.md and docs/mac-port/B1-widechar-survey.md.
 */

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "Lib/WideCharFns.h"

namespace
{

/* "OYUN SEÇENEKLERİ" - the GUI:GameOptions string, lifted from Data/Turkish/Generals.str, which
	 is 3,853 lines of exactly this problem.  Built out of explicit code points rather than written
	 as a literal, for three reasons: the test source then does not have to change when L"..."
	 becomes u"...", it does not depend on how any compiler reads a UTF-8 source file (CMakeLists
	 passes no /utf-8, so MSVC reads this tree in the system ANSI codepage), and the values are
	 visible to a reader who has to check the constant below by hand.

	 U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE is the one that matters: its high byte is 0x01,
	 so a width or byte-order mistake changes the answer instead of hiding in a zero. */
const unsigned TURKISH_GAME_OPTIONS[] =
{
	0x004F, 0x0059, 0x0055, 0x004E, 0x0020, 0x0053, 0x0045, 0x00C7,		// O Y U N _ S E Ç
	0x0045, 0x004E, 0x0045, 0x004B, 0x004C, 0x0045, 0x0052, 0x0130		// E N E K L E R İ
};
const Int TURKISH_GAME_OPTIONS_LEN = 16;

/* The CRC of that string, taken by XferCRC over two-byte code units.

	 Derived twice, independently, and not observed from a run of this test: once by hand from
	 XferCRC::addCRC and XferCRC::xferImplementation, and once by compiling those two functions on
	 their own over the 32 bytes below.  Both give 0x25265B36.  At four bytes per code unit the
	 same string gives 0xD22E0198, so this constant discriminates - which is the entire point of
	 committing it.

	 It is a plain number on both of the platforms this project targets, because getCRC() applies
	 htonl and x64 and arm64 are both little-endian.  A big-endian port would have to revisit
	 this, and a great deal else. */
const UnsignedInt TURKISH_GAME_OPTIONS_CRC = 0x25265B36u;

UnicodeString turkishGameOptions( void )
{
	WideChar buffer[ TURKISH_GAME_OPTIONS_LEN + 1 ];
	for (Int i = 0; i < TURKISH_GAME_OPTIONS_LEN; ++i)
		buffer[i] = (WideChar)TURKISH_GAME_OPTIONS[i];
	buffer[ TURKISH_GAME_OPTIONS_LEN ] = 0;
	return UnicodeString( buffer );
}

UnsignedInt crcOfUnicodeString( UnicodeString text )
{
	XferCRC xfer;
	xfer.open( "test_widechar_width" );
	// Note which function this reaches: XferCRC does not override xferUnicodeString, so this is
	// Xfer::xferUnicodeString - the sizeof(WideChar) at Xfer.cpp:209.  That line is on the replay
	// and network checksum path, not merely next to it.
	xfer.xferUnicodeString( &text );
	xfer.close();
	return xfer.getCRC();
}

long fileSizeOf( const char *path )
{
	FILE *fp = fopen( path, "rb" );
	if (fp == NULL)
		return -1;
	fseek( fp, 0, SEEK_END );
	const long size = ftell( fp );
	fclose( fp );
	return size;
}

}  // namespace

// The one assertion the whole task exists to make true.  B1 puts a static_assert next to the
// typedef as well; this is the runtime half, and it is here so that a failure names the reason
// rather than stopping the build with a line number.
TEST(widechar_is_two_bytes_because_every_file_and_wire_format_says_so)
{
	CHECK_EQ( (Int)sizeof(WideChar), 2 );
}

// Non-ASCII text survives the string class, which is the precondition for everything below.
TEST(unicode_string_carries_turkish_code_points_unchanged)
{
	CHECK( bootOnce() );

	const UnicodeString text = turkishGameOptions();
	CHECK_EQ( text.getLength(), TURKISH_GAME_OPTIONS_LEN );
	for (Int i = 0; i < TURKISH_GAME_OPTIONS_LEN; ++i)
		CHECK_EQ( (UnsignedInt)text.getCharAt( i ), (UnsignedInt)TURKISH_GAME_OPTIONS[i] );

	// ...and the engine's own strlen agrees with the class about where it ends
	CHECK_EQ( (Int)WideCharLen( text.str() ), TURKISH_GAME_OPTIONS_LEN );
}

// The save game round trip.  XferSave writes an UnsignedByte length and then sizeof(WideChar)*len
// bytes; XferLoad reads the same arithmetic back.  The two agree with each other at any width, so
// the round trip alone would pass on a four-byte build - the file size is what pins it.
TEST(xfer_save_and_load_round_trip_a_turkish_string_at_two_bytes_a_character)
{
	CHECK( bootOnce() );

	const char *path = "test_widechar_width_tmp.bin";
	UnicodeString written = turkishGameOptions();

	XferSave save;
	save.open( AsciiString( path ) );
	save.xferUnicodeString( &written );
	save.close();

	// one length byte, then two bytes a character and not four
	CHECK_EQ( fileSizeOf( path ), (long)(1 + 2 * TURKISH_GAME_OPTIONS_LEN) );

	UnicodeString read;
	XferLoad load;
	load.open( AsciiString( path ) );
	load.xferUnicodeString( &read );
	load.close();

	CHECK_EQ( read.getLength(), written.getLength() );
	CHECK( read == written );
	CHECK_EQ( (UnsignedInt)read.getCharAt( 7 ), 0x00C7u );		// Ç
	CHECK_EQ( (UnsignedInt)read.getCharAt( 15 ), 0x0130u );		// İ

	remove( path );
}

// The one that would have caught a silent desync.  A Windows build and a Mac build have to agree
// on this number or they disagree about every unit with a name in it.
TEST(xfer_crc_of_a_turkish_string_matches_the_committed_constant)
{
	CHECK( bootOnce() );

	CHECK_EQ( crcOfUnicodeString( turkishGameOptions() ), TURKISH_GAME_OPTIONS_CRC );

	// the same string twice is the same fingerprint...
	CHECK_EQ( crcOfUnicodeString( turkishGameOptions() ),
	          crcOfUnicodeString( turkishGameOptions() ) );

	// ...and folding the Turkish letters away to their ASCII lookalikes is a different one, so
	// the CRC really is reading the high byte of every code unit
	{
		WideChar folded[ TURKISH_GAME_OPTIONS_LEN + 1 ];
		for (Int i = 0; i < TURKISH_GAME_OPTIONS_LEN; ++i)
			folded[i] = (WideChar)TURKISH_GAME_OPTIONS[i];
		folded[ 7] = (WideChar)'C';		// Ç -> C
		folded[15] = (WideChar)'I';		// İ -> I
		folded[ TURKISH_GAME_OPTIONS_LEN ] = 0;
		CHECK_NE( crcOfUnicodeString( UnicodeString( folded ) ), TURKISH_GAME_OPTIONS_CRC );
	}

	// an empty string xfers no bytes at all, so it must not disturb a fresh CRC
	CHECK_EQ( crcOfUnicodeString( UnicodeString::TheEmptyString ), 0u );
}
