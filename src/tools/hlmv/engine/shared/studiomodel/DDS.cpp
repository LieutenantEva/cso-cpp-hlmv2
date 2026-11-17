#include <algorithm>
#include <cstring>

#include "DDS.h"

namespace studiomdl
{
namespace dds
{
namespace
{
constexpr uint32_t DDS_MAGIC = 0x20534444;			//'DDS '
constexpr uint32_t FOURCC_DX10 = 0x30315844;		//'DX10'

constexpr uint32_t DDSD_MIPMAPCOUNT = 0x20000;
constexpr uint32_t DDSD_REQUIRED = 0x1 | 0x2 | 0x4 | 0x1000;	//caps, height, width, pixel format
constexpr uint32_t DDSD_LINEARSIZE = 0x80000;

constexpr uint32_t DDPF_ALPHAPIXELS = 0x1;
constexpr uint32_t DDPF_FOURCC = 0x4;
constexpr uint32_t DDPF_RGB = 0x40;

constexpr uint32_t DDSCAPS_COMPLEX = 0x8;
constexpr uint32_t DDSCAPS_TEXTURE = 0x1000;
constexpr uint32_t DDSCAPS_MIPMAP = 0x400000;

constexpr uint32_t DDSCAPS2_CUBEMAP = 0x200;
constexpr uint32_t DDSCAPS2_VOLUME = 0x200000;

constexpr uint32_t DXGI_R8G8B8A8_UNORM = 28;
constexpr uint32_t DXGI_R8G8B8A8_UNORM_SRGB = 29;
constexpr uint32_t DXGI_BC7_UNORM = 98;
constexpr uint32_t DXGI_BC7_UNORM_SRGB = 99;
constexpr uint32_t DXGI_B8G8R8A8_UNORM = 87;
constexpr uint32_t DXGI_B8G8R8X8_UNORM = 88;
constexpr uint32_t DXGI_B8G8R8A8_UNORM_SRGB = 91;
constexpr uint32_t DXGI_B8G8R8X8_UNORM_SRGB = 93;

constexpr size_t HEADER_SIZE = 124;
constexpr int MAX_DIMENSION = 16384;

uint32_t Get32( const uint8_t* data, const size_t offset )
{
	return static_cast<uint32_t>( data[ offset ] ) | ( static_cast<uint32_t>( data[ offset + 1 ] ) << 8 ) |
		( static_cast<uint32_t>( data[ offset + 2 ] ) << 16 ) | ( static_cast<uint32_t>( data[ offset + 3 ] ) << 24 );
}

void Put32( std::vector<uint8_t>& out, const size_t offset, const uint32_t value )
{
	for( int i = 0; i < 4; ++i )
	{
		out[ offset + i ] = static_cast<uint8_t>( value >> ( 8 * i ) );
	}
}

std::string FourCCToString( const uint32_t fourCC )
{
	std::string result;

	for( int i = 0; i < 4; ++i )
	{
		const char c = static_cast<char>( ( fourCC >> ( 8 * i ) ) & 0xFF );
		result += ( c >= 32 && c < 127 ) ? c : '?';
	}

	return result;
}

/**
*	Position of the lowest set bit of a mask and its width, or false for masks that are not contiguous.
*/
bool GetMaskShift( const uint32_t mask, int& shift, int& bits )
{
	if( mask == 0 )
	{
		return false;
	}

	shift = 0;

	while( !( ( mask >> shift ) & 1 ) )
	{
		++shift;
	}

	bits = 0;

	while( shift + bits < 32 && ( ( mask >> ( shift + bits ) ) & 1 ) )
	{
		++bits;
	}

	return ( mask >> shift ) == ( ( bits == 32 ) ? 0xFFFFFFFFu : ( ( 1u << bits ) - 1 ) );
}

uint8_t ExtractChannel( const uint32_t pixel, const uint32_t mask, const int shift, const int bits )
{
	const uint32_t value = ( pixel & mask ) >> shift;

	if( bits == 8 )
	{
		return static_cast<uint8_t>( value );
	}

	if( bits > 8 )
	{
		return static_cast<uint8_t>( value >> ( bits - 8 ) );
	}

	//Scale to the full 8 bit range
	const uint32_t maxValue = ( 1u << bits ) - 1;

	return static_cast<uint8_t>( ( value * 255 + maxValue / 2 ) / maxValue );
}
}

size_t BC7LevelSize( const int width, const int height, const int level )
{
	const int w = std::max( 1, width >> level );
	const int h = std::max( 1, height >> level );

	return static_cast<size_t>( ( w + 3 ) / 4 ) * static_cast<size_t>( ( h + 3 ) / 4 ) * 16;
}

size_t BC7ChainSize( const int width, const int height, const int levels )
{
	size_t total = 0;

	for( int i = 0; i < levels; ++i )
	{
		total += BC7LevelSize( width, height, i );
	}

	return total;
}

int FullMipCount( const int width, const int height )
{
	int levels = 1;

	for( int size = std::max( width, height ); size > 1; size >>= 1 )
	{
		++levels;
	}

	return levels;
}

bool Parse( const uint8_t* file, const size_t size, Image& image, std::string& error )
{
	image = Image();

	if( !file || size < 4 + HEADER_SIZE || Get32( file, 0 ) != DDS_MAGIC )
	{
		error = "Not a DDS file";
		return false;
	}

	const uint8_t* const header = file + 4;

	if( Get32( header, 0 ) != HEADER_SIZE || Get32( header, 72 ) != 32 )
	{
		error = "Invalid DDS header";
		return false;
	}

	const uint32_t flags = Get32( header, 4 );
	const int height = static_cast<int>( Get32( header, 8 ) );
	const int width = static_cast<int>( Get32( header, 12 ) );
	int mipCount = ( flags & DDSD_MIPMAPCOUNT ) ? static_cast<int>( Get32( header, 24 ) ) : 1;

	const uint32_t pfFlags = Get32( header, 76 );
	const uint32_t fourCC = Get32( header, 80 );
	const uint32_t bitCount = Get32( header, 84 );
	const uint32_t rMask = Get32( header, 88 );
	const uint32_t gMask = Get32( header, 92 );
	const uint32_t bMask = Get32( header, 96 );
	const uint32_t aMask = Get32( header, 100 );

	const uint32_t caps2 = Get32( header, 108 );

	if( ( flags & DDSD_REQUIRED ) != DDSD_REQUIRED )
	{
		error = "Invalid DDS header flags";
		return false;
	}

	if( width <= 0 || height <= 0 || width > MAX_DIMENSION || height > MAX_DIMENSION )
	{
		error = "Unsupported image dimensions " + std::to_string( width ) + " x " + std::to_string( height );
		return false;
	}

	if( caps2 & ( DDSCAPS2_CUBEMAP | DDSCAPS2_VOLUME ) )
	{
		error = "Cube maps and volume textures are not supported";
		return false;
	}

	if( mipCount < 1 )
	{
		mipCount = 1;
	}

	mipCount = std::min( mipCount, FullMipCount( width, height ) );

	size_t dataOffset = 4 + HEADER_SIZE;

	enum class Kind { BC7, Uncompressed } kind;

	// Source channel layout of uncompressed formats: shifts and widths of r, g, b, a (a = 0 bits: opaque)
	uint32_t masks[ 4 ] = {};
	int bytesPerPixel = 4;

	if( ( pfFlags & DDPF_FOURCC ) && fourCC == FOURCC_DX10 )
	{
		if( size < dataOffset + 20 )
		{
			error = "Truncated DDS header";
			return false;
		}

		const uint32_t dxgi = Get32( file, dataOffset );
		const uint32_t dimension = Get32( file, dataOffset + 4 );
		const uint32_t miscFlag = Get32( file, dataOffset + 8 );
		const uint32_t arraySize = Get32( file, dataOffset + 12 );

		dataOffset += 20;

		if( dimension != 3 || ( miscFlag & 0x4 ) || arraySize > 1 )
		{
			error = "Only single 2D textures are supported";
			return false;
		}

		switch( dxgi )
		{
		case DXGI_BC7_UNORM:
		case DXGI_BC7_UNORM_SRGB:
			kind = Kind::BC7;
			break;

		case DXGI_R8G8B8A8_UNORM:
		case DXGI_R8G8B8A8_UNORM_SRGB:
			kind = Kind::Uncompressed;
			masks[ 0 ] = 0x000000FF; masks[ 1 ] = 0x0000FF00; masks[ 2 ] = 0x00FF0000; masks[ 3 ] = 0xFF000000;
			break;

		case DXGI_B8G8R8A8_UNORM:
		case DXGI_B8G8R8A8_UNORM_SRGB:
			kind = Kind::Uncompressed;
			masks[ 0 ] = 0x00FF0000; masks[ 1 ] = 0x0000FF00; masks[ 2 ] = 0x000000FF; masks[ 3 ] = 0xFF000000;
			break;

		case DXGI_B8G8R8X8_UNORM:
		case DXGI_B8G8R8X8_UNORM_SRGB:
			kind = Kind::Uncompressed;
			masks[ 0 ] = 0x00FF0000; masks[ 1 ] = 0x0000FF00; masks[ 2 ] = 0x000000FF; masks[ 3 ] = 0;
			break;

		default:
			error = "Unsupported DXGI format " + std::to_string( dxgi ) + ". Use BC7 (BC7_UNORM) or uncompressed 8 bit RGBA";
			return false;
		}
	}
	else if( pfFlags & DDPF_FOURCC )
	{
		error = "Unsupported compressed format \"" + FourCCToString( fourCC ) + "\". Use BC7 (BC7_UNORM) or uncompressed 8 bit RGBA";
		return false;
	}
	else if( pfFlags & DDPF_RGB )
	{
		if( ( bitCount != 24 && bitCount != 32 ) || !rMask || !gMask || !bMask )
		{
			error = "Unsupported uncompressed pixel format";
			return false;
		}

		kind = Kind::Uncompressed;
		bytesPerPixel = static_cast<int>( bitCount / 8 );
		masks[ 0 ] = rMask; masks[ 1 ] = gMask; masks[ 2 ] = bMask;
		masks[ 3 ] = ( pfFlags & DDPF_ALPHAPIXELS ) ? aMask : 0;
	}
	else
	{
		error = "Unsupported DDS pixel format";
		return false;
	}

	image.width = width;
	image.height = height;

	if( kind == Kind::BC7 )
	{
		const size_t needed = BC7ChainSize( width, height, mipCount );

		if( size < dataOffset + needed )
		{
			error = "The file is too small for its mip levels";
			return false;
		}

		image.isBC7 = true;
		image.mipLevels = mipCount;
		image.data.assign( file + dataOffset, file + dataOffset + needed );

		return true;
	}

	const size_t pixelCount = static_cast<size_t>( width ) * height;

	if( size < dataOffset + pixelCount * bytesPerPixel )
	{
		error = "The file is too small for its pixel data";
		return false;
	}

	int shifts[ 4 ] = {};
	int bits[ 4 ] = {};

	for( int c = 0; c < 4; ++c )
	{
		if( masks[ c ] && !GetMaskShift( masks[ c ], shifts[ c ], bits[ c ] ) )
		{
			error = "Unsupported channel masks";
			return false;
		}
	}

	image.isBC7 = false;
	image.mipLevels = 1;
	image.data.resize( pixelCount * 4 );

	for( size_t i = 0; i < pixelCount; ++i )
	{
		uint32_t pixel = 0;

		for( int b = 0; b < bytesPerPixel; ++b )
		{
			pixel |= static_cast<uint32_t>( file[ dataOffset + i * bytesPerPixel + b ] ) << ( 8 * b );
		}

		for( int c = 0; c < 3; ++c )
		{
			image.data[ i * 4 + c ] = ExtractChannel( pixel, masks[ c ], shifts[ c ], bits[ c ] );
		}

		image.data[ i * 4 + 3 ] = masks[ 3 ] ? ExtractChannel( pixel, masks[ 3 ], shifts[ 3 ], bits[ 3 ] ) : 255;
	}

	return true;
}

std::vector<uint8_t> WriteBC7( const int width, const int height, const int mipLevels, const uint8_t* data )
{
	const size_t dataSize = BC7ChainSize( width, height, mipLevels );

	std::vector<uint8_t> out( 4 + HEADER_SIZE + 20 + dataSize, 0 );

	Put32( out, 0, DDS_MAGIC );

	const size_t h = 4;

	Put32( out, h + 0, HEADER_SIZE );
	Put32( out, h + 4, DDSD_REQUIRED | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE );
	Put32( out, h + 8, static_cast<uint32_t>( height ) );
	Put32( out, h + 12, static_cast<uint32_t>( width ) );
	Put32( out, h + 16, static_cast<uint32_t>( BC7LevelSize( width, height, 0 ) ) );
	Put32( out, h + 24, static_cast<uint32_t>( mipLevels ) );

	Put32( out, h + 72, 32 );
	Put32( out, h + 76, DDPF_FOURCC );
	Put32( out, h + 80, FOURCC_DX10 );

	Put32( out, h + 104, DDSCAPS_TEXTURE | ( mipLevels > 1 ? ( DDSCAPS_COMPLEX | DDSCAPS_MIPMAP ) : 0 ) );

	const size_t dx10 = 4 + HEADER_SIZE;

	Put32( out, dx10, DXGI_BC7_UNORM );
	Put32( out, dx10 + 4, 3 );		//2D
	Put32( out, dx10 + 12, 1 );		//array size

	memcpy( out.data() + dx10 + 20, data, dataSize );

	return out;
}
}
}
