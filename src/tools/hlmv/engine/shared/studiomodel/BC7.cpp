#include <algorithm>
#include <cstring>

#include "BC7.h"

namespace studiomdl
{
namespace bc7
{
namespace
{
//Partition and anchor tables. Derived by probing a reference BC7 decoder with crafted blocks (see the BC7 spec for the layout).
static const uint8_t PARTITION2[64][16] =
{
	{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1 },
	{ 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
	{ 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1 },
	{ 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1 },
	{ 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1 },
	{ 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0 },
	{ 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
	{ 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1 },
	{ 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0 },
	{ 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0 },
	{ 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0 },
	{ 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0 },
	{ 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1 },
	{ 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0 },
	{ 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0 },
	{ 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0 },
	{ 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0 },
	{ 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1 },
	{ 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1 },
	{ 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0 },
	{ 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0 },
	{ 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0 },
	{ 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0 },
	{ 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 },
	{ 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1 },
	{ 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0 },
	{ 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0 },
	{ 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1 },
	{ 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0 },
	{ 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0 },
	{ 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1 },
	{ 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1 },
	{ 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1 },
	{ 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0 },
	{ 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1 },
};

static const uint8_t PARTITION3[64][16] =
{
	{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2 },
	{ 0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1 },
	{ 0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1 },
	{ 0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2 },
	{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2 },
	{ 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2 },
	{ 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2 },
	{ 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2 },
	{ 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0 },
	{ 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2 },
	{ 0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1 },
	{ 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2 },
	{ 0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1 },
	{ 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2 },
	{ 0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0 },
	{ 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2 },
	{ 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1 },
	{ 0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2 },
	{ 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1 },
	{ 0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2 },
	{ 0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2 },
	{ 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0 },
	{ 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0 },
	{ 0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0 },
	{ 0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1 },
	{ 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1 },
	{ 0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1 },
	{ 0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1 },
	{ 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1 },
	{ 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1 },
	{ 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2 },
	{ 0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1 },
	{ 0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2 },
	{ 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2 },
	{ 0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2 },
	{ 0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2 },
	{ 0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2 },
	{ 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1 },
	{ 0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2 },
	{ 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0 },
};

//Anchor pixel of subset 1 for each 2 subset partition (subset 0 always has pixel 0).
static const uint8_t ANCHOR2[64] =
{
	15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2, 15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15
};

//Anchor pixels of subsets 1 and 2 for each 3 subset partition.
static const uint8_t ANCHOR3[64][2] =
{
	{ 3, 15 },
	{ 3, 8 },
	{ 15, 8 },
	{ 15, 3 },
	{ 8, 15 },
	{ 3, 15 },
	{ 15, 3 },
	{ 15, 8 },
	{ 8, 15 },
	{ 8, 15 },
	{ 6, 15 },
	{ 6, 15 },
	{ 6, 15 },
	{ 5, 15 },
	{ 3, 15 },
	{ 3, 8 },
	{ 3, 15 },
	{ 3, 8 },
	{ 8, 15 },
	{ 15, 3 },
	{ 3, 15 },
	{ 3, 8 },
	{ 6, 15 },
	{ 10, 8 },
	{ 5, 3 },
	{ 8, 15 },
	{ 8, 6 },
	{ 6, 10 },
	{ 8, 15 },
	{ 5, 15 },
	{ 15, 10 },
	{ 15, 8 },
	{ 8, 15 },
	{ 15, 3 },
	{ 3, 15 },
	{ 5, 10 },
	{ 6, 10 },
	{ 10, 8 },
	{ 8, 9 },
	{ 15, 10 },
	{ 15, 6 },
	{ 3, 15 },
	{ 15, 8 },
	{ 5, 15 },
	{ 15, 3 },
	{ 15, 6 },
	{ 15, 6 },
	{ 15, 8 },
	{ 3, 15 },
	{ 15, 3 },
	{ 5, 15 },
	{ 5, 15 },
	{ 5, 15 },
	{ 8, 15 },
	{ 5, 15 },
	{ 10, 15 },
	{ 5, 15 },
	{ 10, 15 },
	{ 8, 15 },
	{ 13, 15 },
	{ 15, 3 },
	{ 12, 15 },
	{ 3, 15 },
	{ 3, 8 },
};

struct ModeInfo
{
	int numSubsets;
	int partitionBits;
	int rotationBits;
	int indexSelectionBits;
	int colorBits;
	int alphaBits;
	int endpointPBits;		//One per endpoint
	int sharedPBits;		//One per subset
	int indexBits;
	int index2Bits;
};

static const ModeInfo MODES[8] =
{
	{ 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
	{ 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
	{ 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
	{ 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
	{ 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
	{ 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
	{ 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
	{ 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
};

static const int WEIGHTS2[4] = { 0, 21, 43, 64 };
static const int WEIGHTS3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const int WEIGHTS4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

int GetWeight( const int indexBits, const int index )
{
	switch( indexBits )
	{
	case 2: return WEIGHTS2[ index ];
	case 3: return WEIGHTS3[ index ];
	default: return WEIGHTS4[ index ];
	}
}

class BitReader
{
public:
	explicit BitReader( const uint8_t* block )
	{
		for( int i = 0; i < 8; ++i )
		{
			m_Lo |= static_cast<uint64_t>( block[ i ] ) << ( 8 * i );
			m_Hi |= static_cast<uint64_t>( block[ 8 + i ] ) << ( 8 * i );
		}
	}

	unsigned Read( const int count )
	{
		unsigned value = 0;

		for( int i = 0; i < count; ++i )
		{
			const unsigned bit = m_Position < 64 ? ( m_Lo >> m_Position ) & 1 : ( m_Hi >> ( m_Position - 64 ) ) & 1;
			value |= bit << i;
			++m_Position;
		}

		return value;
	}

private:
	uint64_t m_Lo = 0;
	uint64_t m_Hi = 0;
	unsigned m_Position = 0;
};

inline int Expand( const int value, const int bits )
{
	return bits >= 8 ? value : ( ( value << ( 8 - bits ) ) | ( value >> ( 2 * bits - 8 ) ) );
}

inline int Interpolate( const int e0, const int e1, const int weight )
{
	return ( e0 * ( 64 - weight ) + e1 * weight + 32 ) >> 6;
}
}

bool DecodeBlock( const uint8_t* block, uint8_t* outRGBA )
{
	int mode = 0;

	while( mode < 8 && !( block[ 0 ] & ( 1 << mode ) ) )
	{
		++mode;
	}

	if( mode >= 8 )
	{
		memset( outRGBA, 0, 16 * 4 );
		return false;
	}

	const ModeInfo& info = MODES[ mode ];

	BitReader bits( block );

	bits.Read( mode + 1 );

	const int partition = static_cast<int>( bits.Read( info.partitionBits ) );
	const int rotation = static_cast<int>( bits.Read( info.rotationBits ) );
	const int indexSelection = static_cast<int>( bits.Read( info.indexSelectionBits ) );

	const int numEndpoints = info.numSubsets * 2;

	int endpoints[ 6 ][ 4 ] = {};

	for( int channel = 0; channel < 3; ++channel )
	{
		for( int e = 0; e < numEndpoints; ++e )
		{
			endpoints[ e ][ channel ] = static_cast<int>( bits.Read( info.colorBits ) );
		}
	}

	for( int e = 0; e < numEndpoints; ++e )
	{
		endpoints[ e ][ 3 ] = info.alphaBits > 0 ? static_cast<int>( bits.Read( info.alphaBits ) ) : 255;
	}

	int pbits[ 6 ] = {};

	if( info.endpointPBits )
	{
		for( int e = 0; e < numEndpoints; ++e )
		{
			pbits[ e ] = static_cast<int>( bits.Read( 1 ) );
		}
	}
	else if( info.sharedPBits )
	{
		for( int s = 0; s < info.numSubsets; ++s )
		{
			pbits[ s * 2 ] = pbits[ s * 2 + 1 ] = static_cast<int>( bits.Read( 1 ) );
		}
	}

	const bool hasPBits = info.endpointPBits || info.sharedPBits;

	for( int e = 0; e < numEndpoints; ++e )
	{
		for( int channel = 0; channel < 3; ++channel )
		{
			int value = endpoints[ e ][ channel ];
			int count = info.colorBits;

			if( hasPBits )
			{
				value = ( value << 1 ) | pbits[ e ];
				++count;
			}

			endpoints[ e ][ channel ] = Expand( value, count );
		}

		if( info.alphaBits > 0 )
		{
			int value = endpoints[ e ][ 3 ];
			int count = info.alphaBits;

			if( hasPBits )
			{
				value = ( value << 1 ) | pbits[ e ];
				++count;
			}

			endpoints[ e ][ 3 ] = Expand( value, count );
		}
	}

	auto subsetOf = [ & ]( const int pixel ) -> int
	{
		switch( info.numSubsets )
		{
		case 2: return PARTITION2[ partition ][ pixel ];
		case 3: return PARTITION3[ partition ][ pixel ];
		default: return 0;
		}
	};

	auto isAnchor = [ & ]( const int pixel ) -> bool
	{
		if( pixel == 0 )
		{
			return true;
		}

		switch( info.numSubsets )
		{
		case 2: return pixel == ANCHOR2[ partition ];
		case 3: return pixel == ANCHOR3[ partition ][ 0 ] || pixel == ANCHOR3[ partition ][ 1 ];
		default: return false;
		}
	};

	int indices[ 16 ] = {};
	int indices2[ 16 ] = {};

	for( int i = 0; i < 16; ++i )
	{
		indices[ i ] = static_cast<int>( bits.Read( isAnchor( i ) ? info.indexBits - 1 : info.indexBits ) );
	}

	if( info.index2Bits > 0 )
	{
		//Only single subset modes have a secondary index set, so only pixel 0 is an anchor
		for( int i = 0; i < 16; ++i )
		{
			indices2[ i ] = static_cast<int>( bits.Read( i == 0 ? info.index2Bits - 1 : info.index2Bits ) );
		}
	}

	for( int i = 0; i < 16; ++i )
	{
		const int subset = subsetOf( i );
		const int* e0 = endpoints[ subset * 2 ];
		const int* e1 = endpoints[ subset * 2 + 1 ];

		int colorWeight;
		int alphaWeight;

		if( info.index2Bits == 0 )
		{
			colorWeight = alphaWeight = GetWeight( info.indexBits, indices[ i ] );
		}
		else if( indexSelection == 0 )
		{
			colorWeight = GetWeight( info.indexBits, indices[ i ] );
			alphaWeight = GetWeight( info.index2Bits, indices2[ i ] );
		}
		else
		{
			colorWeight = GetWeight( info.index2Bits, indices2[ i ] );
			alphaWeight = GetWeight( info.indexBits, indices[ i ] );
		}

		int pixel[ 4 ];

		for( int channel = 0; channel < 3; ++channel )
		{
			pixel[ channel ] = Interpolate( e0[ channel ], e1[ channel ], colorWeight );
		}

		pixel[ 3 ] = Interpolate( e0[ 3 ], e1[ 3 ], alphaWeight );

		if( rotation != 0 )
		{
			std::swap( pixel[ 3 ], pixel[ rotation - 1 ] );
		}

		for( int channel = 0; channel < 4; ++channel )
		{
			outRGBA[ i * 4 + channel ] = static_cast<uint8_t>( pixel[ channel ] );
		}
	}

	return true;
}

int DecodeImage( const uint8_t* data, const size_t size, const int width, const int height, std::vector<uint8_t>& outRGBA )
{
	if( !data || width <= 0 || height <= 0 || size < BlocksSize( width, height ) )
	{
		return -1;
	}

	outRGBA.assign( static_cast<size_t>( width ) * height * 4, 0 );

	const int blocksX = ( width + 3 ) / 4;
	const int blocksY = ( height + 3 ) / 4;

	int invalidBlocks = 0;

	uint8_t pixels[ 16 * 4 ];

	for( int by = 0; by < blocksY; ++by )
	{
		for( int bx = 0; bx < blocksX; ++bx )
		{
			if( !DecodeBlock( data + ( static_cast<size_t>( by ) * blocksX + bx ) * 16, pixels ) )
			{
				++invalidBlocks;
			}

			for( int py = 0; py < 4; ++py )
			{
				const int y = by * 4 + py;

				if( y >= height )
				{
					break;
				}

				for( int px = 0; px < 4; ++px )
				{
					const int x = bx * 4 + px;

					if( x >= width )
					{
						break;
					}

					memcpy( &outRGBA[ ( static_cast<size_t>( y ) * width + x ) * 4 ], &pixels[ ( py * 4 + px ) * 4 ], 4 );
				}
			}
		}
	}

	return invalidBlocks;
}
}
}
