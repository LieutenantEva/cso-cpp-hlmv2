#include <algorithm>
#include <cmath>
#include <cstring>

#include "BC7Encoder.h"

namespace studiomdl
{
namespace bc7
{
namespace
{
const int WEIGHTS4[ 16 ] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

struct Candidate
{
	int e0[ 4 ];		//Reconstructed 8 bit endpoints
	int e1[ 4 ];
	int q0[ 4 ];		//7 bit values
	int q1[ 4 ];
	int p0;
	int p1;
	int indices[ 16 ];
	int64_t error;
};

/**
*	Picks the p-bit and 7 bit values that bring a float endpoint closest to a value the format can store.
*/
void QuantizeEndpoint( const double* e, int* q, int& pbit, int* reconstructed )
{
	double bestError = 1e30;

	for( int p = 0; p < 2; ++p )
	{
		int candidate[ 4 ];
		double error = 0;

		for( int c = 0; c < 4; ++c )
		{
			const int value = std::min( 127, std::max( 0, static_cast<int>( std::floor( ( e[ c ] - p ) / 2.0 + 0.5 ) ) ) );
			candidate[ c ] = value;

			const double diff = value * 2 + p - e[ c ];
			error += diff * diff;
		}

		if( error < bestError )
		{
			bestError = error;
			pbit = p;

			for( int c = 0; c < 4; ++c )
			{
				q[ c ] = candidate[ c ];
				reconstructed[ c ] = candidate[ c ] * 2 + p;
			}
		}
	}
}

/**
*	Assigns every pixel the index that is closest to it with the exact interpolation the decoder uses.
*/
int64_t AssignIndices( const uint8_t* px, const int* e0, const int* e1, int* indices )
{
	int palette[ 16 ][ 4 ];

	for( int i = 0; i < 16; ++i )
	{
		for( int c = 0; c < 4; ++c )
		{
			palette[ i ][ c ] = ( e0[ c ] * ( 64 - WEIGHTS4[ i ] ) + e1[ c ] * WEIGHTS4[ i ] + 32 ) >> 6;
		}
	}

	int64_t total = 0;

	for( int p = 0; p < 16; ++p )
	{
		int64_t best = INT64_MAX;

		for( int i = 0; i < 16; ++i )
		{
			int64_t error = 0;

			for( int c = 0; c < 4; ++c )
			{
				const int diff = palette[ i ][ c ] - px[ p * 4 + c ];
				error += diff * diff;
			}

			if( error < best )
			{
				best = error;
				indices[ p ] = i;
			}
		}

		total += best;
	}

	return total;
}

void Evaluate( const uint8_t* px, const double* f0, const double* f1, Candidate& out )
{
	QuantizeEndpoint( f0, out.q0, out.p0, out.e0 );
	QuantizeEndpoint( f1, out.q1, out.p1, out.e1 );

	out.error = AssignIndices( px, out.e0, out.e1, out.indices );
}

/**
*	Least squares fit of both endpoints for fixed indices. Returns false if the indices don't allow a fit
*	(for example when all pixels use the same index).
*/
bool RefineEndpoints( const uint8_t* px, const int* indices, double* f0, double* f1 )
{
	double saa = 0, sab = 0, sbb = 0;

	for( int p = 0; p < 16; ++p )
	{
		const double b = WEIGHTS4[ indices[ p ] ] / 64.0;
		const double a = 1.0 - b;

		saa += a * a;
		sab += a * b;
		sbb += b * b;
	}

	const double det = saa * sbb - sab * sab;

	if( det < 1e-6 )
	{
		return false;
	}

	for( int c = 0; c < 4; ++c )
	{
		double sap = 0, sbp = 0;

		for( int p = 0; p < 16; ++p )
		{
			const double b = WEIGHTS4[ indices[ p ] ] / 64.0;
			const double a = 1.0 - b;

			sap += a * px[ p * 4 + c ];
			sbp += b * px[ p * 4 + c ];
		}

		f0[ c ] = std::min( 255.0, std::max( 0.0, ( sbb * sap - sab * sbp ) / det ) );
		f1[ c ] = std::min( 255.0, std::max( 0.0, ( saa * sbp - sab * sap ) / det ) );
	}

	return true;
}

void Fit( const uint8_t* px, double* f0, double* f1, Candidate& best )
{
	Candidate current;

	Evaluate( px, f0, f1, current );
	best = current;

	double g0[ 4 ], g1[ 4 ];
	memcpy( g0, f0, sizeof( g0 ) );
	memcpy( g1, f1, sizeof( g1 ) );

	for( int iteration = 0; iteration < 4; ++iteration )
	{
		if( !RefineEndpoints( px, current.indices, g0, g1 ) )
		{
			break;
		}

		Evaluate( px, g0, g1, current );

		if( current.error < best.error )
		{
			best = current;
		}
	}
}

class BitWriter
{
public:
	void Write( const uint32_t value, const int count )
	{
		for( int i = 0; i < count; ++i )
		{
			if( ( value >> i ) & 1 )
			{
				m_Bytes[ m_Position / 8 ] |= static_cast<uint8_t>( 1 << ( m_Position % 8 ) );
			}

			++m_Position;
		}
	}

	uint8_t m_Bytes[ 16 ] = {};

private:
	int m_Position = 0;
};
}

void EncodeBlock( const uint8_t* rgba, uint8_t* out )
{
	// Principal axis of the pixels in RGBA space
	double mean[ 4 ] = {};

	for( int p = 0; p < 16; ++p )
	{
		for( int c = 0; c < 4; ++c )
		{
			mean[ c ] += rgba[ p * 4 + c ];
		}
	}

	for( int c = 0; c < 4; ++c )
	{
		mean[ c ] /= 16.0;
	}

	double covariance[ 4 ][ 4 ] = {};

	for( int p = 0; p < 16; ++p )
	{
		double d[ 4 ];

		for( int c = 0; c < 4; ++c )
		{
			d[ c ] = rgba[ p * 4 + c ] - mean[ c ];
		}

		for( int i = 0; i < 4; ++i )
		{
			for( int j = 0; j < 4; ++j )
			{
				covariance[ i ][ j ] += d[ i ] * d[ j ];
			}
		}
	}

	int minimum[ 4 ] = { 255, 255, 255, 255 };
	int maximum[ 4 ] = { 0, 0, 0, 0 };

	for( int p = 0; p < 16; ++p )
	{
		for( int c = 0; c < 4; ++c )
		{
			minimum[ c ] = std::min<int>( minimum[ c ], rgba[ p * 4 + c ] );
			maximum[ c ] = std::max<int>( maximum[ c ], rgba[ p * 4 + c ] );
		}
	}

	//Start from the strongest column of the covariance matrix. It always has a component along the principal axis, unlike
	//e.g. the per channel range, which is orthogonal to it when channels are anti-correlated (red goes up, blue goes down).
	int strongest = 0;

	for( int c = 1; c < 4; ++c )
	{
		if( covariance[ c ][ c ] > covariance[ strongest ][ strongest ] )
		{
			strongest = c;
		}
	}

	double axis[ 4 ];

	for( int c = 0; c < 4; ++c )
	{
		axis[ c ] = covariance[ c ][ strongest ];
	}

	for( int iteration = 0; iteration < 8; ++iteration )
	{
		double next[ 4 ] = {};

		for( int i = 0; i < 4; ++i )
		{
			for( int j = 0; j < 4; ++j )
			{
				next[ i ] += covariance[ i ][ j ] * axis[ j ];
			}
		}

		const double length = std::sqrt( next[ 0 ] * next[ 0 ] + next[ 1 ] * next[ 1 ] + next[ 2 ] * next[ 2 ] + next[ 3 ] * next[ 3 ] );

		if( length < 1e-9 )
		{
			break;
		}

		for( int c = 0; c < 4; ++c )
		{
			axis[ c ] = next[ c ] / length;
		}
	}

	double axisLength = std::sqrt( axis[ 0 ] * axis[ 0 ] + axis[ 1 ] * axis[ 1 ] + axis[ 2 ] * axis[ 2 ] + axis[ 3 ] * axis[ 3 ] );

	if( axisLength < 1e-9 )
	{
		axisLength = 1;
	}

	double tMin = 1e30, tMax = -1e30;

	for( int p = 0; p < 16; ++p )
	{
		double t = 0;

		for( int c = 0; c < 4; ++c )
		{
			t += ( rgba[ p * 4 + c ] - mean[ c ] ) * axis[ c ] / axisLength;
		}

		tMin = std::min( tMin, t );
		tMax = std::max( tMax, t );
	}

	double pca0[ 4 ], pca1[ 4 ], box0[ 4 ], box1[ 4 ];

	for( int c = 0; c < 4; ++c )
	{
		pca0[ c ] = std::min( 255.0, std::max( 0.0, mean[ c ] + axis[ c ] / axisLength * tMin ) );
		pca1[ c ] = std::min( 255.0, std::max( 0.0, mean[ c ] + axis[ c ] / axisLength * tMax ) );
		box0[ c ] = minimum[ c ];
		box1[ c ] = maximum[ c ];
	}

	Candidate best, other;

	Fit( rgba, pca0, pca1, best );
	Fit( rgba, box0, box1, other );

	if( other.error < best.error )
	{
		best = other;
	}

	//The first pixel's index only has 3 bits, so its top bit has to be 0. Swapping the endpoints and mirroring the indices
	//gives the same result (the weights are symmetric).
	if( best.indices[ 0 ] >= 8 )
	{
		for( int c = 0; c < 4; ++c )
		{
			std::swap( best.q0[ c ], best.q1[ c ] );
		}

		std::swap( best.p0, best.p1 );

		for( int p = 0; p < 16; ++p )
		{
			best.indices[ p ] = 15 - best.indices[ p ];
		}
	}

	BitWriter bits;

	bits.Write( 1 << 6, 7 );	//Mode 6

	for( int c = 0; c < 4; ++c )
	{
		bits.Write( best.q0[ c ], 7 );
		bits.Write( best.q1[ c ], 7 );
	}

	bits.Write( best.p0, 1 );
	bits.Write( best.p1, 1 );

	for( int p = 0; p < 16; ++p )
	{
		bits.Write( best.indices[ p ], p == 0 ? 3 : 4 );
	}

	memcpy( out, bits.m_Bytes, 16 );
}

std::vector<uint8_t> EncodeLevel( const uint8_t* rgba, const int width, const int height )
{
	const int blocksX = ( width + 3 ) / 4;
	const int blocksY = ( height + 3 ) / 4;

	std::vector<uint8_t> out( static_cast<size_t>( blocksX ) * blocksY * 16 );

	uint8_t block[ 16 * 4 ];

	for( int by = 0; by < blocksY; ++by )
	{
		for( int bx = 0; bx < blocksX; ++bx )
		{
			for( int py = 0; py < 4; ++py )
			{
				const int y = std::min( height - 1, by * 4 + py );

				for( int px = 0; px < 4; ++px )
				{
					const int x = std::min( width - 1, bx * 4 + px );

					memcpy( &block[ ( py * 4 + px ) * 4 ], &rgba[ ( static_cast<size_t>( y ) * width + x ) * 4 ], 4 );
				}
			}

			EncodeBlock( block, &out[ ( static_cast<size_t>( by ) * blocksX + bx ) * 16 ] );
		}
	}

	return out;
}

std::vector<uint8_t> Downsample( const uint8_t* rgba, const int width, const int height, int& outWidth, int& outHeight )
{
	outWidth = std::max( 1, width / 2 );
	outHeight = std::max( 1, height / 2 );

	std::vector<uint8_t> out( static_cast<size_t>( outWidth ) * outHeight * 4 );

	for( int y = 0; y < outHeight; ++y )
	{
		const int y0 = std::min( height - 1, y * 2 );
		const int y1 = std::min( height - 1, y * 2 + 1 );

		for( int x = 0; x < outWidth; ++x )
		{
			const int x0 = std::min( width - 1, x * 2 );
			const int x1 = std::min( width - 1, x * 2 + 1 );

			for( int c = 0; c < 4; ++c )
			{
				const int sum = rgba[ ( static_cast<size_t>( y0 ) * width + x0 ) * 4 + c ] + rgba[ ( static_cast<size_t>( y0 ) * width + x1 ) * 4 + c ] +
					rgba[ ( static_cast<size_t>( y1 ) * width + x0 ) * 4 + c ] + rgba[ ( static_cast<size_t>( y1 ) * width + x1 ) * 4 + c ];

				out[ ( static_cast<size_t>( y ) * outWidth + x ) * 4 + c ] = static_cast<uint8_t>( ( sum + 2 ) / 4 );
			}
		}
	}

	return out;
}

std::vector<uint8_t> EncodeMipChain( const uint8_t* rgba, const int width, const int height, const int mipLevels )
{
	std::vector<uint8_t> result;

	std::vector<uint8_t> current( rgba, rgba + static_cast<size_t>( width ) * height * 4 );
	int w = width;
	int h = height;

	for( int level = 0; level < mipLevels; ++level )
	{
		const auto encoded = EncodeLevel( current.data(), w, h );

		result.insert( result.end(), encoded.begin(), encoded.end() );

		if( level + 1 < mipLevels )
		{
			int nextWidth, nextHeight;
			current = Downsample( current.data(), w, h, nextWidth, nextHeight );
			w = nextWidth;
			h = nextHeight;
		}
	}

	return result;
}
}
}
