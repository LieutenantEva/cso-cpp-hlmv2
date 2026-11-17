#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "Palette.h"

#include "BMPFile.h"

#include "utility/IOUtils.h"

namespace graphics
{
namespace bmpfile
{
bool SaveBMPFile( const char* const pszFilename, const int iWidth, const int iHeight, const uint8_t* pPixels, const uint8_t* pPalette )
{
	if( !pszFilename || !( *pszFilename ) )
		return false;

	if( iWidth <= 0 || iHeight <= 0 )
		return false;

	if( !pPixels || !( pPalette ) )
		return false;

	FILE* pFile = utf8_fopen( pszFilename, "wb" );

	if( !pFile )
		return false;

	Header header;
	InfoHeader infoHeader;

	memset( &header, 0, sizeof( header ) );
	memset( &infoHeader, 0, sizeof( infoHeader ) );

	const int32_t iDiskWidth = ( iWidth + 3 ) & ~3;

	const int32_t iPixelsBytes = iDiskWidth * iHeight;

	const int32_t iPaletteBytes = PALETTE_ENTRIES * sizeof( RGBQuad );

	//This might not be correct. TODO: verify
	header.bfType		= BMP_TYPE_ID;

	header.bfSize		= sizeof( Header ) + sizeof( InfoHeader ) + iPixelsBytes + iPaletteBytes;
	header.bfReserved1	= 0;
	header.bfReserved2	= 0;
	header.bfOffBits	= sizeof( Header ) + sizeof( InfoHeader ) + iPaletteBytes;

	//Write the header.
	if( fwrite( &header, sizeof( header ), 1, pFile ) != 1 )
	{
		fclose( pFile );
		return false;
	}

	infoHeader.biSize			= sizeof( infoHeader );
	infoHeader.biWidth			= iDiskWidth;
	infoHeader.biHeight			= iHeight;
	infoHeader.biPlanes			= 1;
	infoHeader.biBitCount		= 8;						//8 bit encoding.
	infoHeader.biCompression	= COMPRESSION_RGB;			//Uncompressed.
	infoHeader.biSizeImage		= 0;
	infoHeader.biXPelsPerMeter	= 0;
	infoHeader.biYPelsPerMeter	= 0;
	infoHeader.biClrUsed		= PALETTE_ENTRIES;			//Store all colors.
	infoHeader.biClrImportant	= 0;

	//Write the info header.
	if( fwrite( &infoHeader, sizeof( infoHeader ), 1, pFile ) != 1 )
	{
		fclose( pFile );
		return false;
	}

	RGBQuad palette[ PALETTE_ENTRIES ];

	for( size_t uiIndex = 0; uiIndex < PALETTE_ENTRIES; ++uiIndex, pPalette += 3 )
	{
		palette[ uiIndex ].rgbRed		= pPalette[ 0 ];
		palette[ uiIndex ].rgbGreen		= pPalette[ 1 ];
		palette[ uiIndex ].rgbBlue		= pPalette[ 2 ];
		palette[ uiIndex ].rgbReserved	= 0;
	}

	//Write the palette
	if( fwrite( palette, sizeof( palette ), 1, pFile ) != 1 )
	{
		fclose( pFile );
		return false;
	}

	std::unique_ptr<uint8_t[]> pixels = std::make_unique<uint8_t[]>( iPixelsBytes );

	//Flip the image vertically.
	const uint8_t* pSrcData = pPixels + ( iHeight - 1 ) * iWidth;

	for( int iIndex = 0; iIndex < iHeight; ++iIndex, pSrcData -= iWidth )
	{
		memcpy( pixels.get() + ( iDiskWidth * iIndex ), pSrcData, iWidth );
	}

	if( fwrite( pixels.get(), iPixelsBytes, 1, pFile ) != 1 )
	{
		fclose( pFile );
		return false;
	}

	fclose( pFile );

	return true;
}
bool SaveBMPFileRGBA( const char* const pszFilename, const int iWidth, const int iHeight, const uint8_t* pRGBA )
{
	if( !pszFilename || !( *pszFilename ) )
		return false;

	if( iWidth <= 0 || iHeight <= 0 || !pRGBA )
		return false;

	const size_t uiRowBytes = ( static_cast<size_t>( iWidth ) * 3 + 3 ) & ~static_cast<size_t>( 3 );
	const size_t uiPixelBytes = uiRowBytes * iHeight;
	const size_t uiHeadersSize = 14 + 40;

	if( uiPixelBytes + uiHeadersSize > 0xFFFFFFFFu )
		return false;

	//The headers are written field by field so the result doesn't depend on structure padding.
	std::vector<uint8_t> file( uiHeadersSize + uiPixelBytes, 0 );

	auto put16 = [ & ]( const size_t offset, const uint32_t value )
	{
		file[ offset ] = static_cast<uint8_t>( value );
		file[ offset + 1 ] = static_cast<uint8_t>( value >> 8 );
	};

	auto put32 = [ & ]( const size_t offset, const uint32_t value )
	{
		put16( offset, value );
		put16( offset + 2, value >> 16 );
	};

	put16( 0, BMP_TYPE_ID );
	put32( 2, static_cast<uint32_t>( file.size() ) );
	put32( 10, static_cast<uint32_t>( uiHeadersSize ) );

	put32( 14, 40 );						//biSize
	put32( 18, static_cast<uint32_t>( iWidth ) );
	put32( 22, static_cast<uint32_t>( iHeight ) );	//Positive: rows are stored bottom to top
	put16( 26, 1 );							//biPlanes
	put16( 28, 24 );						//biBitCount
	put32( 30, COMPRESSION_RGB );
	put32( 34, static_cast<uint32_t>( uiPixelBytes ) );

	for( int y = 0; y < iHeight; ++y )
	{
		const uint8_t* pSource = pRGBA + static_cast<size_t>( iHeight - 1 - y ) * iWidth * 4;
		uint8_t* pDestination = &file[ uiHeadersSize + static_cast<size_t>( y ) * uiRowBytes ];

		for( int x = 0; x < iWidth; ++x, pSource += 4, pDestination += 3 )
		{
			pDestination[ 0 ] = pSource[ 2 ];
			pDestination[ 1 ] = pSource[ 1 ];
			pDestination[ 2 ] = pSource[ 0 ];
		}
	}

	FILE* pFile = utf8_fopen( pszFilename, "wb" );

	if( !pFile )
		return false;

	const bool bSuccess = fwrite( file.data(), file.size(), 1, pFile ) == 1;

	fclose( pFile );

	return bSuccess;
}
bool LoadBMPFileRGBA( const char* const pszFilename, int& iWidth, int& iHeight, std::vector<uint8_t>& rgba, std::string* pError )
{
	auto fail = [ & ]( const char* pszMessage )
	{
		if( pError )
			*pError = pszMessage;

		return false;
	};

	iWidth = iHeight = 0;
	rgba.clear();

	if( !pszFilename || !( *pszFilename ) )
		return fail( "No file name" );

	FILE* pFile = utf8_fopen( pszFilename, "rb" );

	if( !pFile )
		return fail( "Could not open the file" );

	std::vector<uint8_t> file;

	{
		uint8_t buffer[ 65536 ];
		size_t uiRead;

		constexpr size_t MAX_FILE_SIZE = 512 * 1024 * 1024;

		while( ( uiRead = fread( buffer, 1, sizeof( buffer ), pFile ) ) > 0 )
		{
			file.insert( file.end(), buffer, buffer + uiRead );

			if( file.size() > MAX_FILE_SIZE )
			{
				fclose( pFile );
				return fail( "The file is too large" );
			}
		}
	}

	fclose( pFile );

	auto get16 = [ & ]( const size_t offset ) -> uint32_t
	{
		return static_cast<uint32_t>( file[ offset ] ) | ( static_cast<uint32_t>( file[ offset + 1 ] ) << 8 );
	};

	auto get32 = [ & ]( const size_t offset ) -> uint32_t
	{
		return get16( offset ) | ( get16( offset + 2 ) << 16 );
	};

	if( file.size() < 14 + 40 || get16( 0 ) != BMP_TYPE_ID )
		return fail( "Not a BMP file" );

	const uint32_t uiDataOffset = get32( 10 );
	const uint32_t uiHeaderSize = get32( 14 );

	if( uiHeaderSize < 40 || uiHeaderSize > 124 || file.size() < 14 + static_cast<size_t>( uiHeaderSize ) )
		return fail( "Unsupported BMP header" );

	const int32_t iRawWidth = static_cast<int32_t>( get32( 18 ) );
	const int32_t iRawHeight = static_cast<int32_t>( get32( 22 ) );
	const uint32_t uiBitCount = get16( 28 );
	const uint32_t uiCompression = get32( 30 );
	uint32_t uiColorsUsed = get32( 46 );

	const bool bTopDown = iRawHeight < 0;

	const int64_t width = iRawWidth;
	const int64_t height = bTopDown ? -static_cast<int64_t>( iRawHeight ) : iRawHeight;

	if( width <= 0 || height <= 0 || width > 16384 || height > 16384 )
		return fail( "Unsupported image dimensions" );

	if( uiBitCount != 1 && uiBitCount != 4 && uiBitCount != 8 && uiBitCount != 24 && uiBitCount != 32 )
		return fail( "Unsupported bits per pixel (use 8, 24 or 32 bit)" );

	uint32_t masks[ 4 ] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0 };		//BGRA byte order as a little endian value

	if( uiCompression == 3 && uiBitCount == 32 )
	{
		//BI_BITFIELDS: three masks follow the header (inside it for version 4/5 headers)
		if( file.size() < 14 + 40 + 12 )
			return fail( "Truncated BMP header" );

		masks[ 0 ] = get32( 14 + 40 );
		masks[ 1 ] = get32( 14 + 40 + 4 );
		masks[ 2 ] = get32( 14 + 40 + 8 );
	}
	else if( uiCompression != 0 )
	{
		return fail( "Compressed BMP files are not supported" );
	}

	if( uiHeaderSize >= 108 && uiBitCount == 32 )
	{
		//Version 4 and 5 headers have an alpha mask
		masks[ 3 ] = get32( 14 + 54 );

		if( uiCompression == 0 )
		{
			masks[ 0 ] = get32( 14 + 40 );
			masks[ 1 ] = get32( 14 + 44 );
			masks[ 2 ] = get32( 14 + 48 );
		}
	}

	std::vector<uint8_t> palette;

	if( uiBitCount <= 8 )
	{
		if( uiColorsUsed == 0 || uiColorsUsed > ( 1u << uiBitCount ) )
			uiColorsUsed = 1u << uiBitCount;

		const size_t uiPaletteOffset = 14 + static_cast<size_t>( uiHeaderSize );

		if( file.size() < uiPaletteOffset + static_cast<size_t>( uiColorsUsed ) * 4 )
			return fail( "Truncated palette" );

		palette.assign( file.begin() + uiPaletteOffset, file.begin() + uiPaletteOffset + static_cast<size_t>( uiColorsUsed ) * 4 );
	}

	const size_t uiRowBytes = ( ( static_cast<size_t>( width ) * uiBitCount + 31 ) / 32 ) * 4;

	if( uiDataOffset < 14 + uiHeaderSize || static_cast<uint64_t>( uiDataOffset ) + static_cast<uint64_t>( uiRowBytes ) * static_cast<uint64_t>( height ) > file.size() )
		return fail( "The file is too small for its pixel data" );

	auto channel = [ & ]( const uint32_t pixel, const uint32_t mask ) -> uint8_t
	{
		if( !mask )
			return 0;

		int shift = 0;

		while( !( ( mask >> shift ) & 1 ) )
			++shift;

		const uint32_t maxValue = mask >> shift;

		return static_cast<uint8_t>( ( ( ( pixel & mask ) >> shift ) * 255 + maxValue / 2 ) / maxValue );
	};

	iWidth = static_cast<int>( width );
	iHeight = static_cast<int>( height );

	rgba.resize( static_cast<size_t>( width ) * height * 4 );

	for( int64_t y = 0; y < height; ++y )
	{
		//Rows are stored bottom to top unless the height is negative
		const int64_t sourceRow = bTopDown ? y : height - 1 - y;
		const uint8_t* pRow = file.data() + uiDataOffset + static_cast<size_t>( sourceRow ) * uiRowBytes;
		uint8_t* pDestination = &rgba[ static_cast<size_t>( y ) * width * 4 ];

		for( int64_t x = 0; x < width; ++x, pDestination += 4 )
		{
			if( uiBitCount <= 8 )
			{
				const size_t uiBit = static_cast<size_t>( x ) * uiBitCount;
				const uint32_t uiShift = 8 - uiBitCount - ( uiBit % 8 );
				const uint32_t uiIndex = ( pRow[ uiBit / 8 ] >> uiShift ) & ( ( 1u << uiBitCount ) - 1 );

				//Indices past the end of the palette are black
				if( uiIndex * 4 + 2 < palette.size() )
				{
					pDestination[ 0 ] = palette[ uiIndex * 4 + 2 ];
					pDestination[ 1 ] = palette[ uiIndex * 4 + 1 ];
					pDestination[ 2 ] = palette[ uiIndex * 4 ];
				}
				else
				{
					pDestination[ 0 ] = pDestination[ 1 ] = pDestination[ 2 ] = 0;
				}

				pDestination[ 3 ] = 255;
			}
			else if( uiBitCount == 24 )
			{
				const uint8_t* pPixel = pRow + x * 3;

				pDestination[ 0 ] = pPixel[ 2 ];
				pDestination[ 1 ] = pPixel[ 1 ];
				pDestination[ 2 ] = pPixel[ 0 ];
				pDestination[ 3 ] = 255;
			}
			else
			{
				const uint8_t* pPixel = pRow + x * 4;
				const uint32_t uiPixel = static_cast<uint32_t>( pPixel[ 0 ] ) | ( static_cast<uint32_t>( pPixel[ 1 ] ) << 8 ) |
					( static_cast<uint32_t>( pPixel[ 2 ] ) << 16 ) | ( static_cast<uint32_t>( pPixel[ 3 ] ) << 24 );

				pDestination[ 0 ] = channel( uiPixel, masks[ 0 ] );
				pDestination[ 1 ] = channel( uiPixel, masks[ 1 ] );
				pDestination[ 2 ] = channel( uiPixel, masks[ 2 ] );
				pDestination[ 3 ] = masks[ 3 ] ? channel( uiPixel, masks[ 3 ] ) : 255;
			}
		}
	}

	return true;
}
}
}