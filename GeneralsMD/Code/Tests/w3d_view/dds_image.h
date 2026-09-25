/*
 * A DDS as the game's art ships it: DXT1, DXT3 or DXT5 (BC1, BC2, BC3), 2D, with a mip chain.
 * Measured across TexturesZH.big and Textures.big: 3,964 DXT1, 3,138 DXT5, 9 DXT3, nothing else.
 * The blocks are handed to the GPU as they are; nothing is decoded.
 */
#pragma once

#include <string>
#include <vector>

enum DdsFormat { DDS_BC1, DDS_BC2, DDS_BC3 };

struct DdsLevel
{
	unsigned width;
	unsigned height;
	size_t offset;	// into DdsImage::bytes
	size_t size;
};

struct DdsImage
{
	DdsFormat format;
	std::vector<DdsLevel> levels;
	std::vector<unsigned char> bytes;	// the whole file; levels point into it
};

// False with a reason for anything that is not one of the three formats above.
bool parseDds(std::vector<unsigned char> file, DdsImage *image, std::string *why);
