#ifndef GAME_STUDIOMODEL_BC7_H
#define GAME_STUDIOMODEL_BC7_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace studiomdl
{
namespace bc7
{
/**
*	@brief Decodes a single 4x4 BC7 block (16 bytes) to 16 RGBA8 pixels (row major, 4 bytes per pixel).
*	@return false if the block uses the reserved mode (the output is then fully transparent black).
*/
bool DecodeBlock( const uint8_t* block, uint8_t* outRGBA );

/**
*	@brief Decodes the top mip level of a BC7 image to RGBA8.
*	@param data Compressed blocks, row major, 16 bytes per 4x4 block.
*	@param size Number of bytes available in @p data. Must be at least BlocksSize( width, height ).
*	@param width Image width in pixels (does not have to be a multiple of 4).
*	@param height Image height in pixels.
*	@param[out] outRGBA Receives width * height * 4 bytes.
*	@return Number of blocks that used the reserved mode, or -1 if the arguments are invalid.
*/
int DecodeImage( const uint8_t* data, size_t size, int width, int height, std::vector<uint8_t>& outRGBA );

/**
*	@brief Number of bytes needed for the top mip level of a width x height BC7 image.
*/
inline size_t BlocksSize( const int width, const int height )
{
	return static_cast<size_t>( ( width + 3 ) / 4 ) * static_cast<size_t>( ( height + 3 ) / 4 ) * 16;
}
}
}

#endif //GAME_STUDIOMODEL_BC7_H
