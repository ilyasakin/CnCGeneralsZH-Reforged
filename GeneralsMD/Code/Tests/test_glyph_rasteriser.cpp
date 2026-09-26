/*
 * D6a: GlyphRasteriserClass against what GDI would take from the same font file.
 *
 * GDI's advance for a TrueType glyph at a size its hdmx table covers is the hdmx value; its tmAscent
 * and tmDescent are the VDMX table's; with ANTIALIASED_QUALITY it draws bilevel where the gasp table
 * says no grey.  This test reads those three tables (and cmap) with its OWN parser, not FreeType's, and
 * holds the rasteriser, built on the vendored FreeType 2.14.3, to them for the faces and sizes the
 * game uses (the census in D6's task file).  The design's measurements were made with Homebrew's
 * FreeType; this is the same comparison on the vendored one.
 *
 * Also: the antialias switch changes pixels and never advances or heights; "Generals" (Arial with an
 * lfWidth) is narrower with the same heights; the pen sits where ExtTextOutW's top-aligned origin puts
 * it; a registered file is found before the substitution table.  And a regression golden - the advance,
 * ink box and coverage hash of fixed strings - keyed to the font files' versions, since those numbers
 * are this machine's fonts' and nothing else's.
 *
 * What it cannot see: GDI itself (no Windows machine; Wine's GDI is FreeType); Windows' own Arial
 * (7.x, not macOS's 5.01), whose tables may differ; GDI's grey shading against FreeType's; how GDI
 * rounds a width-scaled font.  macOS only for the table checks and the golden: Linux's Liberation
 * fonts have no hdmx and different pixels, and are measured at the Linux milestone.
 */

#include "test_harness.h"

#include "glyphrasteriser.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

const char *const FONT_DIR = "/System/Library/Fonts/Supplemental/";

// ---- The test's own reading of the font file ------------------------------------------------------

struct FontFile
{
	std::vector<unsigned char> data;
	unsigned numGlyphs;
	bool ok;

	explicit FontFile( const std::string &path ) : numGlyphs( 0 ), ok( false )
	{
		FILE *file = fopen( path.c_str(), "rb" );
		if (file == NULL)
			return;
		fseek( file, 0, SEEK_END );
		data.resize( (size_t)ftell( file ) );
		fseek( file, 0, SEEK_SET );
		ok = !data.empty() && fread( &data[0], 1, data.size(), file ) == data.size();
		fclose( file );
		const unsigned char *maxp = table( "maxp" );
		if (maxp != NULL)
			numGlyphs = u16( maxp + 4 );
	}
	static unsigned u16( const unsigned char *p ) { return ((unsigned)p[0] << 8) | p[1]; }
	static int s16( const unsigned char *p ) { return (short)u16( p ); }
	static unsigned u32( const unsigned char *p ) { return (u16( p ) << 16) | u16( p + 2 ); }

	const unsigned char *table( const char *tag, unsigned *length = NULL ) const
	{
		if (!ok || data.size() < 12)
			return NULL;
		const unsigned count = u16( &data[4] );
		for (unsigned i = 0; i < count; ++i)
		{
			const unsigned char *record = &data[12 + 16 * i];
			if (memcmp( record, tag, 4 ) == 0)
			{
				if (length != NULL)
					*length = u32( record + 12 );
				return &data[u32( record + 8 )];
			}
		}
		return NULL;
	}

	/// cmap format 4 (Windows Unicode BMP): the glyph for a character, 0 without one
	unsigned glyph( unsigned ch ) const
	{
		const unsigned char *cmap = table( "cmap" );
		if (cmap == NULL)
			return 0;
		const unsigned count = u16( cmap + 2 );
		for (unsigned i = 0; i < count; ++i)
		{
			const unsigned char *rec = cmap + 4 + 8 * i;
			if (u16( rec ) != 3 || u16( rec + 2 ) != 1)
				continue;
			const unsigned char *sub = cmap + u32( rec + 4 );
			if (u16( sub ) != 4)
				continue;
			const unsigned segs = u16( sub + 6 ) / 2;
			const unsigned char *ends = sub + 14, *starts = ends + 2 * segs + 2, *deltas = starts + 2 * segs,
				*offsets = deltas + 2 * segs;
			for (unsigned s = 0; s < segs; ++s)
			{
				if (ch > u16( ends + 2 * s ) || ch < u16( starts + 2 * s ))
					continue;
				const unsigned offset = u16( offsets + 2 * s );
				if (offset == 0)
					return (ch + u16( deltas + 2 * s )) & 0xFFFF;
				const unsigned g = u16( offsets + 2 * s + offset + 2 * (ch - u16( starts + 2 * s )) );
				return g == 0 ? 0 : (g + u16( deltas + 2 * s )) & 0xFFFF;
			}
		}
		return 0;
	}

	/// hdmx's width for a glyph at a ppem; -1 when the table has no record for that ppem
	int hdmx( unsigned ppem, unsigned glyphIndex ) const
	{
		const unsigned char *h = table( "hdmx" );
		if (h == NULL)
			return -1;
		const unsigned records = u16( h + 2 ), size = u32( h + 4 );
		for (unsigned r = 0; r < records; ++r)
			if (h[8 + r * size] == ppem)
				return h[8 + r * size + 2 + glyphIndex];
		return -1;
	}

	/// VDMX's yMax and -yMin for a ppem, from the group for a 1:1 device; false without an entry
	bool vdmx( unsigned ppem, int &ascent, int &descent ) const
	{
		const unsigned char *v = table( "VDMX" );
		if (v == NULL)
			return false;
		const unsigned ratios = u16( v + 4 );
		for (unsigned i = 0; i < ratios; ++i)
		{
			const unsigned char *r = v + 6 + 4 * i;
			if (!((r[1] == 0 && r[2] == 0 && r[3] == 0) || (r[1] == 1 && r[2] <= 1 && r[3] >= 1)))
				continue;
			const unsigned char *group = v + u16( v + 6 + 4 * ratios + 2 * i );
			for (unsigned e = 0; e < u16( group ); ++e)
				if (u16( group + 4 + 6 * e ) == ppem)
				{
					ascent = s16( group + 6 + 6 * e );
					descent = -s16( group + 8 + 6 * e );
					return true;
				}
			return false;
		}
		return false;
	}

	/// gasp: whether the file allows grey at this ppem (true without a table)
	bool gaspGray( unsigned ppem ) const
	{
		const unsigned char *g = table( "gasp" );
		if (g == NULL)
			return true;
		for (unsigned i = 0; i < u16( g + 2 ); ++i)
			if (ppem <= u16( g + 4 + 4 * i ))
				return (u16( g + 6 + 4 * i ) & 0x0002) != 0;
		return true;
	}

	/// name ID 5, the version string, from the Windows Unicode record
	std::string version( void ) const
	{
		const unsigned char *n = table( "name" );
		if (n == NULL)
			return "";
		const unsigned count = u16( n + 2 ), strings = u16( n + 4 );
		for (unsigned i = 0; i < count; ++i)
		{
			const unsigned char *rec = n + 6 + 12 * i;
			if (u16( rec ) == 3 && u16( rec + 6 ) == 5)
			{
				std::string text;
				const unsigned char *p = n + strings + u16( rec + 10 );
				for (unsigned k = 0; k + 1 < u16( rec + 8 ); k += 2)
					text += (char)p[k + 1];
				return text;
			}
		}
		return "";
	}
};

bool haveFont( const char *file )
{
	FontFile font( std::string( FONT_DIR ) + file );
	if (font.ok)
		return true;
#if defined(__APPLE__)
	printf( "  FAIL: %s%s is missing; every macOS since 10.15 has it\n", FONT_DIR, file );
	CHECK( !"a macOS system font is missing" );
#else
	printf( "  skip: %s%s is not here (macOS paths; Linux is measured at its milestone)\n", FONT_DIR, file );
#endif
	return false;
}

// The faces the game uses and what each is on disk: census in D6's task file
struct Face
{
	const char *name;
	bool bold;
	const char *file;
};
const Face FACES[] =
{
	{ "Arial", false, "Arial.ttf" },
	{ "Arial", true, "Arial Bold.ttf" },
	{ "Times New Roman", false, "Times New Roman.ttf" },
	{ "Times New Roman", true, "Times New Roman Bold.ttf" },
	{ "Courier New", false, "Courier New.ttf" },
};

// The census's point sizes, as FontCharsClass turns them into pixels: -MulDiv( pt, 96, 72 )
int pixelsFor( int points ) { return (points * 96 + 36) / 72; }
const int POINTS[] = { 8, 9, 10, 12, 14, 15, 16, 20, 22 };

}  // namespace

TEST(glyph_rasteriser_pixel_heights_are_mulDivs)
{
	CHECK_EQ( pixelsFor( 8 ), 11 );
	CHECK_EQ( pixelsFor( 10 ), 13 );
	CHECK_EQ( pixelsFor( 14 ), 19 );
	CHECK_EQ( pixelsFor( 15 ), 20 );
}

TEST(glyph_rasteriser_advances_are_the_files_hdmx)
{
	GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_AS_GASP_SAYS );
	int compared = 0, sizes = 0;
	for (size_t f = 0; f < sizeof( FACES ) / sizeof( FACES[0] ); ++f)
	{
		if (!haveFont( FACES[f].file ))
			continue;
		FontFile font( std::string( FONT_DIR ) + FACES[f].file );
		for (int ppem = 9; ppem <= 40; ++ppem)
		{
			if (font.hdmx( ppem, 0 ) < 0)
				continue;		// hdmx has no record for this size: GDI hints, and so does v35
			GlyphRasteriserClass r;
			CHECK( r.Create_Font( FACES[f].name, ppem, 0, FACES[f].bold, 2 * ppem ) );
			CHECK( strstr( r.Get_File_Path(), FACES[f].file ) != NULL );
			++sizes;
			int wrong = 0;
			for (unsigned ch = 32; ch < 256; ++ch)
			{
				const unsigned g = font.glyph( ch );
				if (g == 0 || (ch >= 127 && ch < 160))
					continue;
				++compared;
				if (r.Get_Advance( ch ) != font.hdmx( ppem, g ))
				{
					if (wrong++ < 3)
						printf( "  %s%s %dpx U+%04X: advance %d, hdmx %d\n", FACES[f].name, FACES[f].bold ? " bold" : "", ppem,
							ch, r.Get_Advance( ch ), font.hdmx( ppem, g ) );
				}
			}
			CHECK_EQ( wrong, 0 );
		}
	}
	printf( "  %d advances at %d face sizes, each against the file's hdmx\n", compared, sizes );
#if defined(__APPLE__)
	CHECK( sizes >= 4 * 14 );		// Arial, Arial Bold, TNR and TNR Bold: 11,12,13,15,16,17,19,21,24,27,29,32,33,37
#endif
}

TEST(glyph_rasteriser_heights_are_the_files_vdmx)
{
	int compared = 0;
	for (size_t f = 0; f < sizeof( FACES ) / sizeof( FACES[0] ); ++f)
	{
		if (!haveFont( FACES[f].file ))
			continue;
		FontFile font( std::string( FONT_DIR ) + FACES[f].file );
		for (size_t p = 0; p < sizeof( POINTS ) / sizeof( POINTS[0] ); ++p)
		{
			const int ppem = pixelsFor( POINTS[p] );
			int ascent = 0, descent = 0;
			if (!font.vdmx( ppem, ascent, descent ))
				continue;
			GlyphRasteriserClass r;
			CHECK( r.Create_Font( FACES[f].name, ppem, 0, FACES[f].bold, 2 * ppem ) );
			++compared;
			if (r.Get_Metrics().Ascent != ascent || r.Get_Metrics().Descent != descent)
				printf( "  %s %dpx: %d+%d, VDMX %d+%d\n", FACES[f].file, ppem, r.Get_Metrics().Ascent,
					r.Get_Metrics().Descent, ascent, descent );
			CHECK_EQ( r.Get_Metrics().Ascent, ascent );
			CHECK_EQ( r.Get_Metrics().Descent, descent );
			CHECK_EQ( r.Get_Metrics().Height, ascent + descent );
			CHECK_EQ( r.Get_Metrics().Overhang, 0 );
		}
	}
	printf( "  %d face sizes' heights against the file's VDMX\n", compared );
#if defined(__APPLE__)
	CHECK( compared >= 30 );
#endif
}

namespace {

struct Drawn
{
	int advance;			// the string's width: the sum of the characters' advances
	int left, top, right, bottom;	// ink box over the string laid out at those advances
	unsigned hash;		// every coverage byte of every character's box
	bool anyGrey;
};

Drawn drawString( GlyphRasteriserClass &r, const char *text, int x_origin = 0 )
{
	Drawn d = { 0, 1 << 30, 1 << 30, -1, -1, 2166136261u, false };
	const int box = r.Get_Box_Size();
	for (const char *p = text; *p; ++p)
	{
		const uint32_t ch = (unsigned char)*p;
		r.Draw_Char( ch, x_origin );
		const uint8_t *c = r.Get_Coverage();
		for (int y = 0; y < box; ++y)
			for (int x = 0; x < box; ++x)
			{
				const uint8_t v = c[y * box + x];
				d.hash = (d.hash ^ v) * 16777619u;
				if (v == 0)
					continue;
				d.anyGrey = d.anyGrey || v != 255;
				const int gx = d.advance + x;
				if (gx < d.left) d.left = gx;
				if (gx > d.right) d.right = gx;
				if (y < d.top) d.top = y;
				if (y > d.bottom) d.bottom = y;
			}
		d.advance += r.Get_Advance( ch );
	}
	return d;
}

}  // namespace

TEST(glyph_rasteriser_follows_gasp_and_the_switch_moves_only_pixels)
{
	if (!haveFont( "Arial.ttf" ))
		return;
	FontFile arial( std::string( FONT_DIR ) + "Arial.ttf" );
	const char *text = "Command & Conquer: Generals";
	const int sizes[] = { 11, 13, 16, 19, 27 };
	for (size_t i = 0; i < sizeof( sizes ) / sizeof( sizes[0] ); ++i)
	{
		const int ppem = sizes[i];
		GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_AS_GASP_SAYS );
		GlyphRasteriserClass gasp;
		CHECK( gasp.Create_Font( "Arial", ppem, 0, false, 2 * ppem ) );
		const Drawn a = drawString( gasp, text );
		GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_ALWAYS_GRAY );
		GlyphRasteriserClass grey;
		CHECK( grey.Create_Font( "Arial", ppem, 0, false, 2 * ppem ) );
		const Drawn b = drawString( grey, text );
		GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_AS_GASP_SAYS );

		const bool gray = arial.gaspGray( ppem );
		CHECK( gasp.Is_Bilevel() == !gray );
		CHECK( !grey.Is_Bilevel() );
		CHECK( a.anyGrey == gray );				// bilevel draws only 0 and 255
		CHECK( b.anyGrey );
		CHECK_EQ( a.advance, b.advance );		// layout never depends on the switch
		CHECK( memcmp( &gasp.Get_Metrics(), &grey.Get_Metrics(), sizeof( GlyphRasteriserClass::MetricsStruct ) ) == 0 );
		if (!gray)
			CHECK( a.hash != b.hash );			// ...and the switch does change the pixels where gasp says bilevel
		printf( "  Arial %dpx: gasp %s; %d px wide either way\n", ppem, gray ? "grey" : "bilevel", a.advance );
	}
}

TEST(glyph_rasteriser_generals_is_arial_squeezed_to_its_lfwidth)
{
	if (!haveFont( "Arial.ttf" ))
		return;
	// FontCharsClass: "Generals" at 15 and 20 points is Arial with lfWidth 0.40 x the pixel height
	const int points[] = { 15, 20 };
	for (size_t i = 0; i < 2; ++i)
	{
		const int ppem = pixelsFor( points[i] ), width = (int)(ppem * 0.40f);
		GlyphRasteriserClass plain, squeezed;
		CHECK( plain.Create_Font( "Arial", ppem, 0, false, 2 * ppem ) );
		CHECK( squeezed.Create_Font( "Arial", ppem, width, false, 2 * ppem ) );
		const Drawn p = drawString( plain, "GENERALS" ), s = drawString( squeezed, "GENERALS" );
		CHECK( s.advance < p.advance );
		CHECK( s.advance * 10 > p.advance * 8 );		// narrower, not a different font: ~0.9 of the width
		CHECK( memcmp( &plain.Get_Metrics(), &squeezed.Get_Metrics(), sizeof( GlyphRasteriserClass::MetricsStruct ) ) == 0 );
		printf( "  Generals %dpt (%dpx, lfWidth %d): \"GENERALS\" %d px, Arial's %d px\n", points[i], ppem, width,
			s.advance, p.advance );
	}
}

TEST(glyph_rasteriser_draws_on_the_baseline_from_the_origin)
{
	if (!haveFont( "Arial.ttf" ))
		return;
	GlyphRasteriserClass r;
	CHECK( r.Create_Font( "Arial", 13, 0, false, 26 ) );
	const Drawn h = drawString( r, "H", 1 );		// Store_GDI_Char draws 'W' at x 1; any origin is honoured
	CHECK_EQ( h.bottom, r.Get_Metrics().Ascent - 1 );	// 'H' stands on the baseline: its last row is just above it
	CHECK( h.left >= 1 );
	CHECK( h.top > 0 );
	// and the box starts clean for each character (ETO_OPAQUE)
	r.Draw_Char( ' ', 0 );
	int ink = 0;
	for (int i = 0; i < 26 * 26; ++i)
		ink += r.Get_Coverage()[i] != 0;
	CHECK_EQ( ink, 0 );
}

TEST(glyph_rasteriser_finds_a_registered_file_before_the_table)
{
	if (!haveFont( "Arial Narrow.ttf" ))
		return;
	const std::string narrow = std::string( FONT_DIR ) + "Arial Narrow.ttf";
	char path[1024];
	// "Arial Narrow" is not in the table: an unknown face takes Arial...
	CHECK( GlyphRasteriserClass::Find_Font_File( "Arial Narrow", false, path, sizeof( path ) ) );
	CHECK( strstr( path, "/Arial.ttf" ) != NULL );
	// ...until its file is registered, as Language.ini's LocalFontFile would be
	CHECK( GlyphRasteriserClass::Register_Font_File( narrow.c_str() ) );
	CHECK( GlyphRasteriserClass::Find_Font_File( "arial narrow", false, path, sizeof( path ) ) );
	CHECK_STR( path, narrow.c_str() );
	GlyphRasteriserClass::Unregister_Font_File( narrow.c_str() );
	CHECK( GlyphRasteriserClass::Find_Font_File( "Arial Narrow", false, path, sizeof( path ) ) );
	CHECK( strstr( path, "/Arial.ttf" ) != NULL );
	// and the table's own substitutions
	CHECK( GlyphRasteriserClass::Find_Font_File( "COURIER", false, path, sizeof( path ) ) );
	CHECK( strstr( path, "/Courier New.ttf" ) != NULL );
	CHECK( GlyphRasteriserClass::Find_Font_File( "Times New Roman", true, path, sizeof( path ) ) );
	CHECK( strstr( path, "/Times New Roman Bold.ttf" ) != NULL );
}

namespace {

/* The regression golden: this machine's font files through the vendored FreeType, at the census's
	 faces and sizes.  Keyed to each file's version string: a different file is a different font, and
	 says so rather than failing on every number. */
struct Golden
{
	const char *face;
	bool bold;
	int points;
	int width;				// lfWidth; 0 for the face's own
	const char *version;
	int advance, left, top, right, bottom;
	unsigned hash;
};

#include "glyph_rasteriser_golden.inc"

}  // namespace

TEST(glyph_rasteriser_matches_its_golden)
{
#if !defined(__APPLE__)
	printf( "  skip: the golden is macOS's font files\n" );
#else
	const char *text = "General, 1,234 $ supplies: Qwerty jig!";
	const bool print = getenv( "ZH_GLYPH_GOLDEN_PRINT" ) != NULL;
	GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_AS_GASP_SAYS );
	int checked = 0;
	for (size_t i = 0; i < sizeof( GOLDEN ) / sizeof( GOLDEN[0] ); ++i)
	{
		const Golden &g = GOLDEN[i];
		const int ppem = pixelsFor( g.points );
		GlyphRasteriserClass r;
		CHECK( r.Create_Font( g.face, ppem, g.width, g.bold, 2 * ppem ) );
		const std::string version = FontFile( r.Get_File_Path() ).version();
		const Drawn d = drawString( r, text );
		if (print)
			printf( "\t{ \"%s\", %s, %d, %d, \"%s\", %d, %d, %d, %d, %d, 0x%08xu },\n", g.face, g.bold ? "true" : "false",
				g.points, g.width, version.c_str(), d.advance, d.left, d.top, d.right, d.bottom, d.hash );
		if (version != g.version)
		{
			printf( "  FAIL: %s is %s, the golden was made from %s: regenerate it (ZH_GLYPH_GOLDEN_PRINT=1)\n",
				r.Get_File_Path(), version.c_str(), g.version );
			CHECK( version == g.version );
			continue;
		}
		++checked;
		CHECK_EQ( d.advance, g.advance );
		CHECK_EQ( d.left, g.left );
		CHECK_EQ( d.top, g.top );
		CHECK_EQ( d.right, g.right );
		CHECK_EQ( d.bottom, g.bottom );
		CHECK_EQ( d.hash, g.hash );
	}
	printf( "  %d golden entries\n", checked );
#endif
}
