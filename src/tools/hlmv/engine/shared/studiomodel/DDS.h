#ifndef GAME_STUDIOMODEL_DDS_H
#define GAME_STUDIOMODEL_DDS_H

/**
*	Minimal DirectDraw Surface reader/writer for 2D textures.
*
*	Reads BC7 (DXGI_FORMAT_BC7_UNORM and _SRGB, DX10 header) and uncompressed 24/32 bit RGB(A) files (legacy and DX10 headers).
*	Writes BC7 with a DX10 header. Cube maps, volumes and texture arrays are not supported.
*/

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace studiomdl
{
namespace dds
{
struct Image
{
	int width = 0;
	int height = 0;

	/**
	*	Number of mip levels contained in data (BC7 only, always 1 for uncompressed images).
	*/
	int mipLevels = 0;

	/**
	*	true: data is the BC7 mip chain, top level first.
	*	false: data is the top level as RGBA8, width * height * 4 bytes.
	*/
	bool isBC7 = false;

	std::vector<uint8_t> data;
};

/**
*	@brief Size in bytes of one BC7 mip level.
*/
size_t BC7LevelSize( int width, int height, int level );

/**
*	@brief Size in bytes of the first @p levels levels of a BC7 mip chain.
*/
size_t BC7ChainSize( int width, int height, int levels );

/**
*	@brief Number of levels in a full mip chain down to 1x1.
*/
int FullMipCount( int width, int height );

/**
*	@brief Parses a DDS file.
*	@param[out] error Describes why the file was rejected.
*	@return true on success.
*/
bool Parse( const uint8_t* file, size_t size, Image& image, std::string& error );

/**
*	@brief Creates a BC7 DDS file.
*	@param data The mip chain, top level first. Must contain at least BC7ChainSize( width, height, mipLevels ) bytes.
*/
std::vector<uint8_t> WriteBC7( int width, int height, int mipLevels, const uint8_t* data );
}
}

#endif //GAME_STUDIOMODEL_DDS_H
