#ifndef GAME_STUDIOMODEL_BC7ENCODER_H
#define GAME_STUDIOMODEL_BC7ENCODER_H

/**
*	A small BC7 encoder. Only mode 6 is used (one subset, 7 bit RGBA endpoints with p-bits, 4 bit indices), which gives
*	good results for smooth content and acceptable results elsewhere.
*	For the best quality import textures that were compressed with a dedicated tool, as DDS files.
*/

#include <cstddef>
#include <cstdint>
#include <vector>

namespace studiomdl
{
namespace bc7
{
/**
*	@brief Encodes a 4x4 block of RGBA8 pixels (row major, 4 bytes per pixel) to 16 bytes.
*/
void EncodeBlock( const uint8_t* rgba, uint8_t* out );

/**
*	@brief Encodes one mip level. Dimensions that are not a multiple of 4 are padded by repeating the edge pixels.
*	@return Compressed blocks, row major.
*/
std::vector<uint8_t> EncodeLevel( const uint8_t* rgba, int width, int height );

/**
*	@brief Box filters an RGBA8 image to half size (rounded down, minimum 1).
*/
std::vector<uint8_t> Downsample( const uint8_t* rgba, int width, int height, int& outWidth, int& outHeight );

/**
*	@brief Encodes mipLevels levels, each one box filtered from the previous one. Top level first.
*/
std::vector<uint8_t> EncodeMipChain( const uint8_t* rgba, int width, int height, int mipLevels );
}
}

#endif //GAME_STUDIOMODEL_BC7ENCODER_H
