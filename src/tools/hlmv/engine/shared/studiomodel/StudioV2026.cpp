#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include "utility/mathlib.h"

#include "BC7.h"
#include "DDS.h"
#include "StudioV2026.h"

namespace studiomdl
{
namespace v2026
{
namespace
{
constexpr size_t BODYPART_SIZE = sizeof( mstudiobodyparts_t );	//76, same as version 10
constexpr size_t SRC_MODEL_SIZE = 76;
constexpr size_t SRC_MESH_SIZE = 32;
constexpr size_t SRC_SUBMESH_SIZE = 16;
constexpr size_t SRC_TEXTURE_SIZE = 96;

constexpr int TEXTURE_FORMAT_BC7 = 8;

constexpr int MAX_TEXTURE_SIZE = 16384;
constexpr int MAX_COUNT = 1 << 20;

static_assert( sizeof( mstudiobodyparts_t ) == 76, "Unexpected bodypart size" );
static_assert( sizeof( mstudiomodel_t ) == 112, "Unexpected model size" );
static_assert( sizeof( mstudiomesh_t ) == 20, "Unexpected mesh size" );
static_assert( sizeof( mstudiotexture_t ) == 80, "Unexpected texture size" );

[[noreturn]] void Fail( const std::string& message )
{
	throw StudioModelV2026Error( message );
}

/**
*	Checks that [offset, offset + count * elementSize) lies inside the file.
*/
void CheckRange( const size_t fileSize, const int64_t offset, const int64_t count, const size_t elementSize, const char* what )
{
	if( offset < 0 || count < 0 || count > MAX_COUNT * 64LL )
	{
		Fail( std::string( what ) + ": invalid offset or count" );
	}

	const uint64_t end = static_cast<uint64_t>( offset ) + static_cast<uint64_t>( count ) * elementSize;

	if( end > fileSize )
	{
		Fail( std::string( what ) + " lies outside the file" );
	}
}

/**
*	The tables shared with version 10 are accessed through pointers, so they have to be aligned like the compiler writes them.
*/
void RequireAligned( const int64_t offset, const int alignment, const std::string& what )
{
	if( offset % alignment != 0 )
	{
		Fail( what + " is not aligned to " + std::to_string( alignment ) + " bytes" );
	}
}

template<typename T>
T Read( const byte* file, const size_t offset )
{
	T value;
	memcpy( &value, file + offset, sizeof( T ) );
	return value;
}

std::string ReadName( const byte* file, const size_t offset, const size_t maxLength )
{
	const char* name = reinterpret_cast<const char*>( file + offset );

	size_t length = 0;

	while( length < maxLength && name[ length ] )
	{
		++length;
	}

	return std::string( name, length );
}

void WriteName( byte* destination, const std::string& name, const size_t bufferSize )
{
	const size_t length = std::min( name.size(), bufferSize - 1 );
	memcpy( destination, name.data(), length );
}

glm::mat3x4 InvertAffine( const glm::mat3x4& matrix )
{
	glm::mat4 m( 1.0f );

	for( int row = 0; row < 3; ++row )
	{
		for( int col = 0; col < 4; ++col )
		{
			m[ col ][ row ] = matrix[ row ][ col ];
		}
	}

	const glm::mat4 inverse = glm::inverse( m );

	glm::mat3x4 result;

	for( int row = 0; row < 3; ++row )
	{
		for( int col = 0; col < 4; ++col )
		{
			result[ row ][ col ] = inverse[ col ][ row ];
		}
	}

	return result;
}

std::vector<glm::mat3x4> ComputeInverseBindPose( const byte* file, const size_t fileSize, const studiohdr_t& hdr )
{
	const auto bind = ComputeBindPose( file, fileSize, hdr );

	std::vector<glm::mat3x4> inverse( bind.size() );

	for( size_t i = 0; i < bind.size(); ++i )
	{
		inverse[ i ] = InvertAffine( bind[ i ] );
	}

	return inverse;
}

/**
*	Walks every run length encoded animation channel of one sequence blend set the way the renderer does,
*	and checks that it never leaves the file.
*/
void ValidateAnimation( const byte* file, const size_t size, const int64_t base, const int numBones, const int numBlends, const int numFrames, const std::string& where )
{
	CheckRange( size, base, static_cast<int64_t>( numBones ) * numBlends, 12, ( where + " animation headers" ).c_str() );
	RequireAligned( base, 2, where + " animation data" );

	for( int64_t bone = 0; bone < static_cast<int64_t>( numBones ) * numBlends; ++bone )
	{
		const size_t headerOffset = static_cast<size_t>( base + bone * 12 );

		for( int channel = 0; channel < 6; ++channel )
		{
			const uint16_t offset = Read<uint16_t>( file, headerOffset + channel * sizeof( uint16_t ) );

			if( offset == 0 )
			{
				continue;
			}

			size_t position = headerOffset + offset;
			int covered = 0;

			while( covered < numFrames )
			{
				//The renderer can read up to 2 values past the end of a run
				if( position + 2 > size )
				{
					Fail( where + ": animation data runs past the end of the file" );
				}

				const unsigned valid = file[ position ];
				const unsigned total = file[ position + 1 ];

				if( total == 0 || valid > total || position + 2 + ( valid + 2 ) * sizeof( int16_t ) > size )
				{
					Fail( where + ": malformed animation data" );
				}

				covered += static_cast<int>( total );
				position += 2 + valid * sizeof( int16_t );
			}
		}
	}
}

/**
*	The skeleton, controller, hitbox, attachment and sequence tables are shared with version 10 and used by code that trusts them
*	(array indices, pointers). Check everything that is used that way.
*/
void ValidateSharedTables( const byte* file, const size_t size, const studiohdr_t& hdr )
{
	const int numBones = hdr.numbones;

	if( hdr.numbonecontrollers < 0 || hdr.numbonecontrollers > MAXSTUDIOCONTROLLERS )
	{
		Fail( "Invalid number of bone controllers: " + std::to_string( hdr.numbonecontrollers ) );
	}

	if( hdr.numbonecontrollers > 0 )
	{
		CheckRange( size, hdr.bonecontrollerindex, hdr.numbonecontrollers, sizeof( mstudiobonecontroller_t ), "Bone controller table" );
		RequireAligned( hdr.bonecontrollerindex, 4, "Bone controller table" );

		for( int i = 0; i < hdr.numbonecontrollers; ++i )
		{
			const auto controller = Read<mstudiobonecontroller_t>( file, hdr.bonecontrollerindex + static_cast<size_t>( i ) * sizeof( mstudiobonecontroller_t ) );

			if( controller.index < 0 || controller.bone < -1 || controller.bone >= numBones )
			{
				Fail( "Bone controller " + std::to_string( i ) + " has an invalid bone or index" );
			}
		}
	}

	if( hdr.numhitboxes > 0 )
	{
		CheckRange( size, hdr.hitboxindex, hdr.numhitboxes, sizeof( mstudiobbox_t ), "Hitbox table" );
		RequireAligned( hdr.hitboxindex, 4, "Hitbox table" );

		for( int i = 0; i < hdr.numhitboxes; ++i )
		{
			const auto box = Read<mstudiobbox_t>( file, hdr.hitboxindex + static_cast<size_t>( i ) * sizeof( mstudiobbox_t ) );

			if( box.bone < 0 || box.bone >= numBones )
			{
				Fail( "Hitbox " + std::to_string( i ) + " has an invalid bone" );
			}
		}
	}
	else if( hdr.numhitboxes < 0 )
	{
		Fail( "Invalid number of hitboxes" );
	}

	if( hdr.numattachments > 0 )
	{
		CheckRange( size, hdr.attachmentindex, hdr.numattachments, sizeof( mstudioattachment_t ), "Attachment table" );
		RequireAligned( hdr.attachmentindex, 4, "Attachment table" );

		for( int i = 0; i < hdr.numattachments; ++i )
		{
			const auto attachment = Read<mstudioattachment_t>( file, hdr.attachmentindex + static_cast<size_t>( i ) * sizeof( mstudioattachment_t ) );

			if( attachment.bone < 0 || attachment.bone >= numBones )
			{
				Fail( "Attachment " + std::to_string( i ) + " has an invalid bone" );
			}
		}
	}
	else if( hdr.numattachments < 0 )
	{
		Fail( "Invalid number of attachments" );
	}

	if( hdr.numseqgroups < 1 || hdr.numseqgroups > 4096 )
	{
		Fail( "Invalid number of sequence groups: " + std::to_string( hdr.numseqgroups ) );
	}

	CheckRange( size, hdr.seqgroupindex, hdr.numseqgroups, sizeof( mstudioseqgroup_t ), "Sequence group table" );
	RequireAligned( hdr.seqgroupindex, 4, "Sequence group table" );

	const auto group0 = Read<mstudioseqgroup_t>( file, hdr.seqgroupindex );

	if( hdr.numseq < 0 )
	{
		Fail( "Invalid number of sequences" );
	}

	if( hdr.numseq > 0 )
	{
		CheckRange( size, hdr.seqindex, hdr.numseq, sizeof( mstudioseqdesc_t ), "Sequence table" );
		RequireAligned( hdr.seqindex, 4, "Sequence table" );
	}

	for( int i = 0; i < hdr.numseq; ++i )
	{
		const auto seq = Read<mstudioseqdesc_t>( file, hdr.seqindex + static_cast<size_t>( i ) * sizeof( mstudioseqdesc_t ) );

		const std::string where = "Sequence " + std::to_string( i );

		if( seq.seqgroup < 0 || seq.seqgroup >= hdr.numseqgroups )
		{
			Fail( where + " has an invalid sequence group" );
		}

		if( seq.numframes < 1 || seq.numblends < 1 || seq.numblends > 16 )
		{
			Fail( where + " has an invalid frame or blend count" );
		}

		if( ( seq.motiontype & ( STUDIO_X | STUDIO_Y | STUDIO_Z ) ) && ( seq.motionbone < 0 || seq.motionbone >= numBones ) )
		{
			Fail( where + " has an invalid motion bone" );
		}

		if( seq.numevents > 0 )
		{
			CheckRange( size, seq.eventindex, seq.numevents, sizeof( mstudioevent_t ), ( where + " events" ).c_str() );
			RequireAligned( seq.eventindex, 4, where + " events" );
		}
		else if( seq.numevents < 0 )
		{
			Fail( where + " has an invalid event count" );
		}

		if( seq.numpivots > 0 )
		{
			CheckRange( size, seq.pivotindex, seq.numpivots, sizeof( mstudiopivot_t ), ( where + " pivots" ).c_str() );
			RequireAligned( seq.pivotindex, 4, where + " pivots" );
		}
		else if( seq.numpivots < 0 )
		{
			Fail( where + " has an invalid pivot count" );
		}

		//Other groups keep their animations in separate files that are not part of this one
		if( seq.seqgroup == 0 )
		{
			//GetAnim() adds the sequence group's legacy data offset
			ValidateAnimation( file, size, static_cast<int64_t>( group0.unused2 ) + seq.animindex, numBones, seq.numblends, seq.numframes, where );
		}
	}
}

void ParseBodyParts( const byte* file, const size_t size, const studiohdr_t& hdr, StudioModelData& data )
{
	if( hdr.numbodyparts < 0 || hdr.numbodyparts > MAXSTUDIOBODYPARTS * 4 )
	{
		Fail( "Invalid number of body parts" );
	}

	CheckRange( size, hdr.bodypartindex, hdr.numbodyparts, BODYPART_SIZE, "Body part table" );

	for( int bp = 0; bp < hdr.numbodyparts; ++bp )
	{
		const size_t recordOffset = hdr.bodypartindex + static_cast<size_t>( bp ) * BODYPART_SIZE;

		BodyPart bodyPart;
		bodyPart.name = ReadName( file, recordOffset, 64 );

		const int numModels = Read<int>( file, recordOffset + 64 );
		bodyPart.base = Read<int>( file, recordOffset + 68 );
		const int modelIndex = Read<int>( file, recordOffset + 72 );

		//The body value is split with these (value / base % nummodels)
		if( numModels < 1 || numModels > MAXSTUDIOMODELS * 4 || bodyPart.base < 1 )
		{
			Fail( "Body part " + std::to_string( bp ) + " has an invalid number of models or base" );
		}

		CheckRange( size, modelIndex, numModels, SRC_MODEL_SIZE, "Model table" );

		for( int m = 0; m < numModels; ++m )
		{
			const size_t modelOffset = modelIndex + static_cast<size_t>( m ) * SRC_MODEL_SIZE;

			Model model;
			model.name = ReadName( file, modelOffset, 64 );
			model.boundingRadius = Read<float>( file, modelOffset + 64 );

			const int numMeshes = Read<int>( file, modelOffset + 68 );
			const int meshIndex = Read<int>( file, modelOffset + 72 );

			if( numMeshes < 0 || numMeshes > MAXSTUDIOMESHES * 4 )
			{
				Fail( "Model '" + model.name + "' has an invalid number of meshes" );
			}

			CheckRange( size, meshIndex, numMeshes, SRC_MESH_SIZE, "Mesh table" );

			for( int k = 0; k < numMeshes; ++k )
			{
				const size_t meshOffset = meshIndex + static_cast<size_t>( k ) * SRC_MESH_SIZE;

				const uint32_t numVerts = Read<uint32_t>( file, meshOffset );
				const uint32_t vertexOffset = Read<uint32_t>( file, meshOffset + 4 );
				const uint32_t numIndices = Read<uint32_t>( file, meshOffset + 8 );
				const uint32_t indexOffset = Read<uint32_t>( file, meshOffset + 12 );
				const uint32_t numSubMeshes = Read<uint32_t>( file, meshOffset + 16 );
				const uint32_t subMeshOffset = Read<uint32_t>( file, meshOffset + 20 );

				const std::string where = "Model '" + model.name + "' mesh " + std::to_string( k );

				CheckRange( size, vertexOffset, numVerts, sizeof( Vertex ), ( where + " vertices" ).c_str() );
				CheckRange( size, indexOffset, numIndices, sizeof( uint32_t ), ( where + " indices" ).c_str() );
				CheckRange( size, subMeshOffset, numSubMeshes, SRC_SUBMESH_SIZE, ( where + " sub meshes" ).c_str() );

				Mesh mesh;
				mesh.vertexFileOffset = vertexOffset;

				mesh.vertices.resize( numVerts );

				if( numVerts )
				{
					memcpy( mesh.vertices.data(), file + vertexOffset, static_cast<size_t>( numVerts ) * sizeof( Vertex ) );
				}

				mesh.indices.resize( numIndices );

				if( numIndices )
				{
					memcpy( mesh.indices.data(), file + indexOffset, static_cast<size_t>( numIndices ) * sizeof( uint32_t ) );
				}

				for( const auto index : mesh.indices )
				{
					if( index >= numVerts )
					{
						Fail( where + ": index " + std::to_string( index ) + " is out of range (" + std::to_string( numVerts ) + " vertices)" );
					}
				}

				for( const auto& vertex : mesh.vertices )
				{
					const unsigned sum = vertex.weights[ 0 ] + vertex.weights[ 1 ] + vertex.weights[ 2 ] + vertex.weights[ 3 ];

					for( int w = 0; w < 4; ++w )
					{
						//Unused slots are ignored unless nothing is weighted, in which case slot 0 is used
						if( vertex.weights[ w ] || ( w == 0 && sum == 0 ) )
						{
							if( vertex.bones[ w ] >= hdr.numbones )
							{
								Fail( where + ": vertex uses bone " + std::to_string( vertex.bones[ w ] ) + " but the model has " + std::to_string( hdr.numbones ) + " bones" );
							}
						}
					}
				}

				for( uint32_t s = 0; s < numSubMeshes; ++s )
				{
					const size_t subOffset = subMeshOffset + static_cast<size_t>( s ) * SRC_SUBMESH_SIZE;

					SubMesh sub;
					sub.skinref = Read<uint32_t>( file, subOffset );
					sub.firstIndex = Read<uint32_t>( file, subOffset + 4 );
					sub.indexCount = Read<uint32_t>( file, subOffset + 8 );

					if( sub.indexCount % 3 != 0 || static_cast<uint64_t>( sub.firstIndex ) + sub.indexCount > numIndices )
					{
						Fail( where + ": sub mesh " + std::to_string( s ) + " has an invalid index range" );
					}

					if( sub.skinref >= static_cast<uint32_t>( std::max( hdr.numskinref, 0 ) ) )
					{
						Fail( where + ": sub mesh " + std::to_string( s ) + " has an invalid skin reference " + std::to_string( sub.skinref ) );
					}

					mesh.subMeshes.push_back( sub );
				}

				model.meshes.push_back( std::move( mesh ) );
			}

			bodyPart.models.push_back( std::move( model ) );
		}

		data.bodyParts.push_back( std::move( bodyPart ) );
	}
}

void ParseTextures( const byte* file, const size_t size, const studiohdr_t& hdr, StudioModelData& data )
{
	if( hdr.numtextures < 0 || hdr.numtextures > MAXSTUDIOSKINS )
	{
		Fail( "Invalid number of textures" );
	}

	CheckRange( size, hdr.textureindex, hdr.numtextures, SRC_TEXTURE_SIZE, "Texture table" );

	for( int i = 0; i < hdr.numtextures; ++i )
	{
		const size_t recordOffset = hdr.textureindex + static_cast<size_t>( i ) * SRC_TEXTURE_SIZE;

		Texture texture;
		texture.name = ReadName( file, recordOffset, 64 );
		texture.flags = Read<int>( file, recordOffset + 64 );
		texture.format = Read<int>( file, recordOffset + 68 );
		texture.width = Read<int>( file, recordOffset + 72 );
		texture.height = Read<int>( file, recordOffset + 76 );
		texture.mipLevels = Read<int>( file, recordOffset + 80 );
		const int dataOffset = Read<int>( file, recordOffset + 84 );
		const int dataSize = Read<int>( file, recordOffset + 88 );

		const std::string where = "Texture '" + texture.name + "'";

		if( texture.width <= 0 || texture.height <= 0 || texture.width > MAX_TEXTURE_SIZE || texture.height > MAX_TEXTURE_SIZE )
		{
			Fail( where + " has invalid dimensions " + std::to_string( texture.width ) + "x" + std::to_string( texture.height ) );
		}

		const size_t topMipSize = bc7::BlocksSize( texture.width, texture.height );

		CheckRange( size, dataOffset, static_cast<int64_t>( topMipSize ), 1, ( where + " pixel data" ).c_str() );

		//The file says how many bytes belong to the texture. Use that if it makes sense, so the whole mip chain is kept.
		size_t reservedSize = topMipSize;

		if( dataSize > 0 && static_cast<size_t>( dataSize ) >= topMipSize && static_cast<uint64_t>( dataOffset ) + dataSize <= size )
		{
			reservedSize = static_cast<size_t>( dataSize );
		}

		texture.fileOffset = static_cast<uint32_t>( dataOffset );
		texture.fileSize = static_cast<uint32_t>( reservedSize );
		texture.compressed.assign( file + dataOffset, file + dataOffset + reservedSize );

		//Only count the mip levels that are fully present
		texture.mipLevels = std::min( std::max( texture.mipLevels, 1 ), dds::FullMipCount( texture.width, texture.height ) );

		while( texture.mipLevels > 1 && dds::BC7ChainSize( texture.width, texture.height, texture.mipLevels ) > reservedSize )
		{
			--texture.mipLevels;
		}

		const int invalidBlocks = bc7::DecodeImage( file + dataOffset, topMipSize, texture.width, texture.height, texture.rgba );

		if( invalidBlocks < 0 )
		{
			Fail( where + " could not be decoded" );
		}

		if( texture.format != TEXTURE_FORMAT_BC7 )
		{
			//Only format 8 (BC7) has been seen. Anything else that is not a valid BC7 stream is refused rather than shown as garbage.
			if( invalidBlocks > 0 )
			{
				Fail( where + " uses unsupported texture format " + std::to_string( texture.format ) );
			}

			data.warnings.push_back( where + " has unknown texture format " + std::to_string( texture.format ) + ", decoded as BC7" );
		}
		else if( invalidBlocks > 0 )
		{
			data.warnings.push_back( where + " contains " + std::to_string( invalidBlocks ) + " invalid BC7 blocks" );
		}

		data.textures.push_back( std::move( texture ) );
	}

	//Can the pixel data be rebuilt? It has to be one gapless block.
	std::vector<const Texture*> byOffset;

	for( const auto& texture : data.textures )
	{
		byOffset.push_back( &texture );
	}

	std::sort( byOffset.begin(), byOffset.end(), []( const Texture* lhs, const Texture* rhs ) { return lhs->fileOffset < rhs->fileOffset; } );

	data.textureDataContiguous = !byOffset.empty();

	for( size_t i = 1; i < byOffset.size(); ++i )
	{
		if( byOffset[ i ]->fileOffset != byOffset[ i - 1 ]->fileOffset + byOffset[ i - 1 ]->fileSize )
		{
			data.textureDataContiguous = false;
		}
	}

	if( !byOffset.empty() )
	{
		data.textureDataStart = byOffset.front()->fileOffset;
		data.textureDataEnd = static_cast<size_t>( byOffset.back()->fileOffset ) + byOffset.back()->fileSize;
	}
}
}

const Model* StudioModelData::FindModel( const byte* base, const mstudiomodel_t* record ) const
{
	if( !base || !record )
	{
		return nullptr;
	}

	const byte* const address = reinterpret_cast<const byte*>( record );

	if( address < base + shadowModelsOffset )
	{
		return nullptr;
	}

	const size_t relative = static_cast<size_t>( address - ( base + shadowModelsOffset ) );

	if( relative % sizeof( mstudiomodel_t ) != 0 )
	{
		return nullptr;
	}

	const size_t index = relative / sizeof( mstudiomodel_t );

	if( index >= modelRefs.size() )
	{
		return nullptr;
	}

	const auto& ref = modelRefs[ index ];

	return &bodyParts[ ref.bodyPart ].models[ ref.model ];
}

const SubMeshRef* StudioModelData::FindSubMesh( const mstudiomesh_t* record ) const
{
	if( !record || record->triindex < 0 || static_cast<size_t>( record->triindex ) >= subMeshRefs.size() )
	{
		return nullptr;
	}

	return &subMeshRefs[ record->triindex ];
}

const SubMesh& StudioModelData::GetSubMesh( const SubMeshRef& ref ) const
{
	return bodyParts[ ref.bodyPart ].models[ ref.model ].meshes[ ref.mesh ].subMeshes[ ref.subMesh ];
}

const Mesh& StudioModelData::GetMesh( const SubMeshRef& ref ) const
{
	return bodyParts[ ref.bodyPart ].models[ ref.model ].meshes[ ref.mesh ];
}

std::unique_ptr<StudioModelData> Parse( const byte* file, const size_t size )
{
	if( !file || size < sizeof( studiohdr_t ) )
	{
		Fail( "File is too small to be a studio model" );
	}

	studiohdr_t hdr;
	memcpy( &hdr, file, sizeof( hdr ) );

	if( strncmp( reinterpret_cast<const char*>( &hdr.id ), STUDIOMDL_HDR_ID, 4 ) != 0 )
	{
		Fail( "Not a studio model" );
	}

	if( hdr.version != STUDIO_VERSION_V2026 )
	{
		Fail( "Not a version 2026 model" );
	}

	if( hdr.numbones <= 0 || hdr.numbones > MAXSTUDIOBONES )
	{
		Fail( "Invalid number of bones: " + std::to_string( hdr.numbones ) );
	}

	if( hdr.numtextures <= 0 )
	{
		Fail( "The model has no textures" );
	}

	auto data = std::make_unique<StudioModelData>();

	data->originalSize = size;
	data->originalBodyPartIndex = hdr.bodypartindex;
	data->originalTextureIndex = hdr.textureindex;
	data->originalTextureDataIndex = hdr.texturedataindex;
	data->originalSkinIndex = hdr.skinindex;

	data->inverseBindPose = ComputeInverseBindPose( file, size, hdr );

	ValidateSharedTables( file, size, hdr );

	ParseTextures( file, size, hdr, *data );

	if( hdr.numskinref < 0 || hdr.numskinfamilies < 0 || static_cast<int64_t>( hdr.numskinref ) * hdr.numskinfamilies > MAX_COUNT )
	{
		Fail( "Invalid skin table size" );
	}

	const int numSkins = hdr.numskinref * hdr.numskinfamilies;

	CheckRange( size, hdr.skinindex, numSkins, sizeof( short ), "Skin table" );
	RequireAligned( hdr.skinindex, 2, "Skin table" );

	for( int i = 0; i < numSkins; ++i )
	{
		const short texture = Read<short>( file, hdr.skinindex + static_cast<size_t>( i ) * sizeof( short ) );

		if( texture < 0 || texture >= hdr.numtextures )
		{
			Fail( "Skin table entry " + std::to_string( i ) + " refers to texture " + std::to_string( texture ) );
		}
	}

	ParseBodyParts( file, size, hdr, *data );

	return data;
}

std::unique_ptr<byte[]> BuildShadowBuffer( const byte* file, const size_t size, StudioModelData& data, size_t& outSize )
{
	studiohdr_t hdr;
	memcpy( &hdr, file, sizeof( hdr ) );

	size_t numModels = 0;
	size_t numMeshRecords = 0;

	data.modelRefs.clear();
	data.subMeshRefs.clear();

	for( size_t bp = 0; bp < data.bodyParts.size(); ++bp )
	{
		for( size_t m = 0; m < data.bodyParts[ bp ].models.size(); ++m )
		{
			++numModels;
			data.modelRefs.push_back( { static_cast<int>( bp ), static_cast<int>( m ) } );

			const auto& model = data.bodyParts[ bp ].models[ m ];

			for( size_t k = 0; k < model.meshes.size(); ++k )
			{
				for( size_t s = 0; s < model.meshes[ k ].subMeshes.size(); ++s )
				{
					++numMeshRecords;
					data.subMeshRefs.push_back( { static_cast<int>( bp ), static_cast<int>( m ), static_cast<int>( k ), static_cast<int>( s ) } );
				}
			}
		}
	}

	const size_t numTextures = data.textures.size();
	const size_t numSkins = static_cast<size_t>( hdr.numskinref ) * hdr.numskinfamilies;

	auto align4 = []( const size_t value ) { return ( value + 3 ) & ~static_cast<size_t>( 3 ); };

	size_t offset = align4( size );

	const size_t bodyPartOffset = offset;
	offset += data.bodyParts.size() * BODYPART_SIZE;

	const size_t modelOffset = offset;
	offset += numModels * sizeof( mstudiomodel_t );

	const size_t meshOffset = offset;
	offset += numMeshRecords * sizeof( mstudiomesh_t );

	const size_t textureOffset = offset;
	offset += numTextures * sizeof( mstudiotexture_t );

	const size_t skinOffset = offset;
	offset += align4( numSkins * sizeof( short ) );

	outSize = offset;

	auto buffer = std::make_unique<byte[]>( outSize );

	memcpy( buffer.get(), file, size );

	data.shadowModelsOffset = modelOffset;

	size_t nextModel = 0;
	size_t nextMesh = 0;

	for( size_t bp = 0; bp < data.bodyParts.size(); ++bp )
	{
		const auto& bodyPart = data.bodyParts[ bp ];

		mstudiobodyparts_t record;
		memset( &record, 0, sizeof( record ) );

		WriteName( reinterpret_cast<byte*>( record.name ), bodyPart.name, sizeof( record.name ) );
		record.nummodels = static_cast<int>( bodyPart.models.size() );
		record.base = bodyPart.base;
		record.modelindex = static_cast<int>( modelOffset + nextModel * sizeof( mstudiomodel_t ) );

		memcpy( buffer.get() + bodyPartOffset + bp * BODYPART_SIZE, &record, sizeof( record ) );

		for( const auto& model : bodyPart.models )
		{
			mstudiomodel_t shadow;
			memset( &shadow, 0, sizeof( shadow ) );

			WriteName( reinterpret_cast<byte*>( shadow.name ), model.name, sizeof( shadow.name ) );
			shadow.boundingradius = model.boundingRadius;

			int subMeshes = 0;

			for( const auto& mesh : model.meshes )
			{
				subMeshes += static_cast<int>( mesh.subMeshes.size() );
			}

			shadow.nummesh = subMeshes;
			shadow.meshindex = static_cast<int>( meshOffset + nextMesh * sizeof( mstudiomesh_t ) );

			//numverts, numnorms and all vertex/normal indices stay 0 on purpose: code written for version 10 that loops over
			//them must not touch the file buffer.

			memcpy( buffer.get() + modelOffset + nextModel * sizeof( mstudiomodel_t ), &shadow, sizeof( shadow ) );

			for( const auto& mesh : model.meshes )
			{
				for( const auto& sub : mesh.subMeshes )
				{
					mstudiomesh_t meshRecord;
					memset( &meshRecord, 0, sizeof( meshRecord ) );

					meshRecord.numtris = static_cast<int>( sub.indexCount / 3 );
					meshRecord.triindex = static_cast<int>( nextMesh );		//Index into subMeshRefs, not a file offset
					meshRecord.skinref = static_cast<int>( sub.skinref );

					memcpy( buffer.get() + meshOffset + nextMesh * sizeof( mstudiomesh_t ), &meshRecord, sizeof( meshRecord ) );

					++nextMesh;
				}
			}

			++nextModel;
		}
	}

	for( size_t i = 0; i < numTextures; ++i )
	{
		const auto& texture = data.textures[ i ];

		mstudiotexture_t record;
		memset( &record, 0, sizeof( record ) );

		WriteName( reinterpret_cast<byte*>( record.name ), texture.name, sizeof( record.name ) );
		record.flags = texture.flags;
		record.width = texture.width;
		record.height = texture.height;
		record.index = 0;		//There is no 8 bit pixel data

		memcpy( buffer.get() + textureOffset + i * sizeof( mstudiotexture_t ), &record, sizeof( record ) );
	}

	if( numSkins )
	{
		memcpy( buffer.get() + skinOffset, file + hdr.skinindex, numSkins * sizeof( short ) );
	}

	auto* const newHdr = reinterpret_cast<studiohdr_t*>( buffer.get() );

	newHdr->bodypartindex = static_cast<int>( bodyPartOffset );
	newHdr->textureindex = static_cast<int>( textureOffset );
	newHdr->texturedataindex = static_cast<int>( skinOffset );
	newHdr->skinindex = static_cast<int>( skinOffset );
	newHdr->length = static_cast<int>( outSize );

	return buffer;
}

std::vector<glm::mat3x4> ComputeBindPose( const byte* file, const size_t fileSize, const studiohdr_t& hdr )
{
	std::vector<glm::mat3x4> bind( hdr.numbones );

	CheckRange( fileSize, hdr.boneindex, hdr.numbones, sizeof( mstudiobone_t ), "Bone table" );
	RequireAligned( hdr.boneindex, 4, "Bone table" );

	for( int i = 0; i < hdr.numbones; ++i )
	{
		const auto bone = Read<mstudiobone_t>( file, hdr.boneindex + static_cast<size_t>( i ) * sizeof( mstudiobone_t ) );

		//The renderer uses these to index a fixed size array
		for( int c = 0; c < 6; ++c )
		{
			if( bone.bonecontroller[ c ] < -1 || bone.bonecontroller[ c ] >= MAXSTUDIOCONTROLLERS )
			{
				Fail( "Bone " + std::to_string( i ) + " has an invalid bone controller index " + std::to_string( bone.bonecontroller[ c ] ) );
			}
		}

		glm::vec4 q;
		AngleQuaternion( glm::vec3( bone.value[ 3 ], bone.value[ 4 ], bone.value[ 5 ] ), q );

		glm::mat3x4 local;
		QuaternionMatrix( q, local );

		local[ 0 ][ 3 ] = bone.value[ 0 ];
		local[ 1 ][ 3 ] = bone.value[ 1 ];
		local[ 2 ][ 3 ] = bone.value[ 2 ];

		if( bone.parent < -1 || bone.parent >= i )
		{
			Fail( "Bone " + std::to_string( i ) + " has an invalid parent " + std::to_string( bone.parent ) );
		}

		if( bone.parent >= 0 )
		{
			R_ConcatTransforms( bind[ bone.parent ], local, bind[ i ] );
		}
		else
		{
			bind[ i ] = local;
		}
	}

	return bind;
}

size_t Texture::ChainSize() const
{
	return dds::BC7ChainSize( width, height, mipLevels );
}

void StudioModelData::ReplaceTexture( const int index, const int width, const int height, const int mipLevels, std::vector<uint8_t>&& chain )
{
	if( index < 0 || static_cast<size_t>( index ) >= textures.size() )
	{
		Fail( "Invalid texture index" );
	}

	if( width <= 0 || height <= 0 || width > MAX_TEXTURE_SIZE || height > MAX_TEXTURE_SIZE )
	{
		Fail( "Unsupported texture dimensions " + std::to_string( width ) + "x" + std::to_string( height ) );
	}

	if( mipLevels < 1 || mipLevels > dds::FullMipCount( width, height ) )
	{
		Fail( "Invalid number of mip levels" );
	}

	if( chain.size() != dds::BC7ChainSize( width, height, mipLevels ) )
	{
		Fail( "Texture data has the wrong size" );
	}

	std::vector<uint8_t> rgba;

	const int invalidBlocks = bc7::DecodeImage( chain.data(), chain.size(), width, height, rgba );

	if( invalidBlocks != 0 )
	{
		Fail( "Texture data is not valid BC7" );
	}

	auto& texture = textures[ index ];

	texture.format = TEXTURE_FORMAT_BC7;
	texture.width = width;
	texture.height = height;
	texture.mipLevels = mipLevels;
	texture.rgba = std::move( rgba );
	texture.compressed = std::move( chain );
	texture.modified = true;
}

std::vector<byte> BuildSaveBuffer( const byte* memory, const size_t memorySize, const StudioModelData& data )
{
	if( !memory || memorySize < sizeof( studiohdr_t ) || memorySize < data.originalSize )
	{
		Fail( "Invalid model buffer" );
	}

	studiohdr_t memoryHdr;
	memcpy( &memoryHdr, memory, sizeof( memoryHdr ) );

	//Everything that belongs to the file is in the beginning of the buffer. The records appended after it are only descriptions.
	std::vector<byte> out( memory, memory + data.originalSize );

	auto* const hdr = reinterpret_cast<studiohdr_t*>( out.data() );

	hdr->bodypartindex = data.originalBodyPartIndex;
	hdr->textureindex = data.originalTextureIndex;
	hdr->texturedataindex = data.originalTextureDataIndex;
	hdr->skinindex = data.originalSkinIndex;

	const size_t numTextures = data.textures.size();

	CheckRange( out.size(), data.originalTextureIndex, static_cast<int64_t>( numTextures ), SRC_TEXTURE_SIZE, "Texture table" );
	CheckRange( memorySize, memoryHdr.textureindex, static_cast<int64_t>( numTextures ), sizeof( mstudiotexture_t ), "Texture description table" );

	//Skin table
	const int64_t numSkins = static_cast<int64_t>( memoryHdr.numskinref ) * memoryHdr.numskinfamilies;

	if( numSkins > 0 )
	{
		CheckRange( out.size(), data.originalSkinIndex, numSkins, sizeof( short ), "Skin table" );
		CheckRange( memorySize, memoryHdr.skinindex, numSkins, sizeof( short ), "Skin description table" );

		memcpy( out.data() + data.originalSkinIndex, memory + memoryHdr.skinindex, static_cast<size_t>( numSkins ) * sizeof( short ) );
	}

	//Texture flags can be edited
	for( size_t i = 0; i < numTextures; ++i )
	{
		const int flags = Read<int>( memory, memoryHdr.textureindex + i * sizeof( mstudiotexture_t ) + 64 );

		memcpy( out.data() + data.originalTextureIndex + i * SRC_TEXTURE_SIZE + 64, &flags, sizeof( flags ) );
	}

	//Vertices are in model space. If the default pose of the bones or the size of the meshes changed they have to follow.
	{
		const auto bind = ComputeBindPose( memory, memorySize, memoryHdr );

		bool bake = data.meshScale != 1.0f;

		if( bind.size() != data.inverseBindPose.size() )
		{
			Fail( "The number of bones changed" );
		}

		for( size_t b = 0; b < bind.size() && !bake; ++b )
		{
			const glm::mat3x4 original = InvertAffine( data.inverseBindPose[ b ] );

			for( int row = 0; row < 3 && !bake; ++row )
			{
				for( int col = 0; col < 4; ++col )
				{
					if( std::fabs( bind[ b ][ row ][ col ] - original[ row ][ col ] ) > 1e-4f )
					{
						bake = true;
						break;
					}
				}
			}
		}

		if( bake )
		{
			std::vector<glm::mat3x4> skin( bind.size() );

			ComputeSkinMatrices( bind.data(), data.inverseBindPose.data(), bind.size(), skin.data(), data.meshScale );

			std::vector<glm::vec3> positions;
			std::vector<glm::vec3> normals;

			for( const auto& bodyPart : data.bodyParts )
			{
				for( const auto& model : bodyPart.models )
				{
					for( const auto& mesh : model.meshes )
					{
						positions.resize( mesh.vertices.size() );
						normals.resize( mesh.vertices.size() );

						SkinMesh( mesh, skin.data(), positions.data(), normals.data() );

						for( size_t v = 0; v < mesh.vertices.size(); ++v )
						{
							byte* const destination = out.data() + mesh.vertexFileOffset + v * sizeof( Vertex );

							memcpy( destination, &positions[ v ], sizeof( glm::vec3 ) );
							memcpy( destination + sizeof( glm::vec3 ), &normals[ v ], sizeof( glm::vec3 ) );
						}
					}
				}
			}
		}
	}

	//Replaced textures
	bool anyModified = false;

	for( const auto& texture : data.textures )
	{
		anyModified = anyModified || texture.modified;
	}

	auto writeRecord = [ & ]( const size_t index, const Texture& texture, const size_t offset )
	{
		byte* const record = out.data() + data.originalTextureIndex + index * SRC_TEXTURE_SIZE;

		const int values[ 7 ] = { texture.format, texture.width, texture.height, texture.mipLevels,
			static_cast<int>( offset ), static_cast<int>( texture.compressed.size() ), 0 };

		//format, width, height, mip levels, data offset, data size. The last field is padding and is left as it is.
		memcpy( record + 68, values, 6 * sizeof( int ) );
	};

	if( anyModified )
	{
		bool sameSizes = true;

		for( const auto& texture : data.textures )
		{
			sameSizes = sameSizes && texture.compressed.size() == texture.fileSize;
		}

		if( sameSizes )
		{
			//Everything stays where it is
			for( size_t i = 0; i < numTextures; ++i )
			{
				const auto& texture = data.textures[ i ];

				if( texture.modified )
				{
					memcpy( out.data() + texture.fileOffset, texture.compressed.data(), texture.compressed.size() );
					writeRecord( i, texture, texture.fileOffset );
				}
			}
		}
		else
		{
			if( !data.textureDataContiguous )
			{
				Fail( "The texture data of this model is not stored in one block, so replaced textures of a different size can't be saved" );
			}

			for( const auto& bodyPart : data.bodyParts )
			{
				for( const auto& model : bodyPart.models )
				{
					for( const auto& mesh : model.meshes )
					{
						const size_t begin = mesh.vertexFileOffset;
						const size_t end = begin + mesh.vertices.size() * sizeof( Vertex );

						if( begin < data.textureDataEnd && end > data.textureDataStart )
						{
							Fail( "Mesh data lies inside the texture data, can't rebuild it" );
						}
					}
				}
			}

			const std::vector<byte> tail( out.begin() + data.textureDataEnd, out.end() );

			out.resize( data.textureDataStart );

			for( size_t i = 0; i < numTextures; ++i )
			{
				const auto& texture = data.textures[ i ];
				const size_t offset = out.size();

				out.insert( out.end(), texture.compressed.begin(), texture.compressed.end() );

				if( texture.modified )
				{
					writeRecord( i, texture, offset );
				}
				else
				{
					//Only the position changes
					const int values[ 2 ] = { static_cast<int>( offset ), static_cast<int>( texture.compressed.size() ) };
					memcpy( out.data() + data.originalTextureIndex + i * SRC_TEXTURE_SIZE + 84, values, sizeof( values ) );
				}
			}

			out.insert( out.end(), tail.begin(), tail.end() );
		}
	}

	reinterpret_cast<studiohdr_t*>( out.data() )->length = static_cast<int>( out.size() );

	return out;
}

void ComputeSkinMatrices( const glm::mat3x4* boneToWorld, const glm::mat3x4* inverseBind, const size_t numBones, glm::mat3x4* out, const float localScale )
{
	for( size_t i = 0; i < numBones; ++i )
	{
		if( localScale == 1.0f )
		{
			R_ConcatTransforms( boneToWorld[ i ], inverseBind[ i ], out[ i ] );
		}
		else
		{
			//diag( s ) * inverse: scales every row, including the translation column
			const glm::mat3x4 scaled( inverseBind[ i ][ 0 ] * localScale, inverseBind[ i ][ 1 ] * localScale, inverseBind[ i ][ 2 ] * localScale );

			R_ConcatTransforms( boneToWorld[ i ], scaled, out[ i ] );
		}
	}
}

uint8_t GetDominantBone( const Vertex& vertex )
{
	int best = 0;

	for( int i = 1; i < 4; ++i )
	{
		if( vertex.weights[ i ] > vertex.weights[ best ] )
		{
			best = i;
		}
	}

	return vertex.bones[ best ];
}

namespace
{
inline glm::vec3 Transform( const glm::mat3x4& m, const glm::vec3& v )
{
	return glm::vec3(
		v.x * m[ 0 ][ 0 ] + v.y * m[ 0 ][ 1 ] + v.z * m[ 0 ][ 2 ] + m[ 0 ][ 3 ],
		v.x * m[ 1 ][ 0 ] + v.y * m[ 1 ][ 1 ] + v.z * m[ 1 ][ 2 ] + m[ 1 ][ 3 ],
		v.x * m[ 2 ][ 0 ] + v.y * m[ 2 ][ 1 ] + v.z * m[ 2 ][ 2 ] + m[ 2 ][ 3 ] );
}

inline glm::vec3 Rotate( const glm::mat3x4& m, const glm::vec3& v )
{
	return glm::vec3(
		v.x * m[ 0 ][ 0 ] + v.y * m[ 0 ][ 1 ] + v.z * m[ 0 ][ 2 ],
		v.x * m[ 1 ][ 0 ] + v.y * m[ 1 ][ 1 ] + v.z * m[ 1 ][ 2 ],
		v.x * m[ 2 ][ 0 ] + v.y * m[ 2 ][ 1 ] + v.z * m[ 2 ][ 2 ] );
}
}

void SkinMesh( const Mesh& mesh, const glm::mat3x4* skin, glm::vec3* outPositions, glm::vec3* outNormals )
{
	for( size_t i = 0; i < mesh.vertices.size(); ++i )
	{
		const Vertex& vertex = mesh.vertices[ i ];

		const unsigned sum = vertex.weights[ 0 ] + vertex.weights[ 1 ] + vertex.weights[ 2 ] + vertex.weights[ 3 ];

		glm::vec3 position( 0 );
		glm::vec3 normal( 0 );

		if( sum == 0 )
		{
			position = Transform( skin[ vertex.bones[ 0 ] ], vertex.position );
			normal = Rotate( skin[ vertex.bones[ 0 ] ], vertex.normal );
		}
		else
		{
			for( int w = 0; w < 4; ++w )
			{
				if( !vertex.weights[ w ] )
				{
					continue;
				}

				const float weight = static_cast<float>( vertex.weights[ w ] ) / static_cast<float>( sum );
				const glm::mat3x4& matrix = skin[ vertex.bones[ w ] ];

				position += weight * Transform( matrix, vertex.position );
				normal += weight * Rotate( matrix, vertex.normal );
			}
		}

		const float length = std::sqrt( normal.x * normal.x + normal.y * normal.y + normal.z * normal.z );

		if( length > 1e-6f )
		{
			normal /= length;
		}

		outPositions[ i ] = position;
		outNormals[ i ] = normal;
	}
}
}
}
