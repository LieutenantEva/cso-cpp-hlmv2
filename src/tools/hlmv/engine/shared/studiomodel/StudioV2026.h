#ifndef GAME_STUDIOMODEL_STUDIOV2026_H
#define GAME_STUDIOMODEL_STUDIOV2026_H

/**
*	Support for the "IDST version 2026" studio model format used by newer Counter-Strike Online clients.
*
*	Header, bones, bone controllers, hitboxes, attachments, sequences, events and animation data are laid out exactly like
*	version 10, with 12 extra bytes after the 244 byte header. Everything else differs:
*
*	- Models are 76 bytes: char name[64]; float boundingradius; int nummesh; int meshindex.
*	- A mesh header (32 bytes) is: u32 numverts, vertexoffset, numindices, indexoffset, numsubmeshes, submeshoffset, 2 reserved.
*	- Vertices are 48 bytes, in model space: float pos[3], normal[3], uv[2]; u8 bones[4]; u8 weights[4]; 8 reserved bytes.
*	  A vertex can be influenced by up to 4 bones, weights add up to 255.
*	- Indices are u32 triangle lists. A submesh (16 bytes: skinref, firstindex, indexcount, padding) selects a range of them.
*	- Texture records are 96 bytes: name[64], flags, format (8 = BC7), width, height, miplevels, dataoffset, datasize, pad.
*	  Pixel data is a BC7 compressed mip chain, top mip first. UVs are 0..1 floats, v = 0 at the top row.
*	- Triangles use the same winding as version 10.
*
*	The loaded file is kept as is. Parse() reads the v2026 specific data into native structures and BuildShadowBuffer() appends
*	version 10 shaped records (models, meshes, textures, skin table) that describe the data, so code that only lists or counts
*	things keeps working. Meshes and pixels are never read from those records.
*/

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat3x4.hpp>

#include "shared/Const.h"

#include "studio.h"

namespace studiomdl
{
namespace v2026
{
/**
*	Thrown when a version 2026 file is malformed or uses something that is not understood.
*/
class StudioModelV2026Error : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

#pragma pack( push, 1 )
/**
*	On disk vertex layout.
*/
struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	uint8_t bones[ 4 ];
	uint8_t weights[ 4 ];
	uint8_t reserved[ 8 ];
};
#pragma pack( pop )

static_assert( sizeof( Vertex ) == 48, "Vertex must match the on disk layout" );

struct SubMesh
{
	uint32_t skinref = 0;
	uint32_t firstIndex = 0;
	uint32_t indexCount = 0;
};

struct Mesh
{
	/**
	*	Offset of the vertices in the file, used when saving.
	*/
	uint32_t vertexFileOffset = 0;

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<SubMesh> subMeshes;
};

struct Model
{
	std::string name;
	float boundingRadius = 0;
	std::vector<Mesh> meshes;
};

struct BodyPart
{
	std::string name;
	int base = 1;
	std::vector<Model> models;
};

struct Texture
{
	std::string name;
	int flags = 0;
	int format = 0;
	int width = 0;
	int height = 0;
	int mipLevels = 0;

	/**
	*	Decoded top mip, RGBA8, width * height * 4 bytes. Alpha is the alpha stored in the file.
	*/
	std::vector<uint8_t> rgba;

	/**
	*	The BC7 data as stored, top mip first. At least the whole mip chain (see ChainSize), possibly followed by padding.
	*/
	std::vector<uint8_t> compressed;

	/**
	*	Where the data is in the file, and how many bytes the file reserves for it.
	*/
	uint32_t fileOffset = 0;
	uint32_t fileSize = 0;

	/**
	*	true if the texture was replaced after loading.
	*/
	bool modified = false;

	/**
	*	@brief Number of bytes of the mip chain that are actually used.
	*/
	size_t ChainSize() const;
};

struct ModelRef
{
	int bodyPart;
	int model;
};

struct SubMeshRef
{
	int bodyPart;
	int model;
	int mesh;
	int subMesh;
};

class StudioModelData
{
public:
	std::vector<BodyPart> bodyParts;
	std::vector<Texture> textures;

	/**
	*	Inverse of each bone's default pose (world -> bone). Same row layout as the renderer's bone matrices.
	*/
	std::vector<glm::mat3x4> inverseBindPose;

	/**
	*	Non fatal problems found while parsing.
	*/
	std::vector<std::string> warnings;

	/**
	*	Models in the order of the appended version 10 model records.
	*/
	std::vector<ModelRef> modelRefs;

	/**
	*	Sub meshes in the order of the appended version 10 mesh records. The record's triindex holds the index into this list.
	*/
	std::vector<SubMeshRef> subMeshRefs;

	size_t shadowModelsOffset = 0;

	/**
	*	Size of the file as it was loaded and the header fields that were redirected to the appended records.
	*/
	size_t originalSize = 0;
	int originalBodyPartIndex = 0;
	int originalTextureIndex = 0;
	int originalTextureDataIndex = 0;
	int originalSkinIndex = 0;

	/**
	*	true if the pixel data of all textures forms one block in the file, so it can be rebuilt when textures are replaced.
	*/
	bool textureDataContiguous = false;
	size_t textureDataStart = 0;
	size_t textureDataEnd = 0;

	/**
	*	@brief Finds the v2026 model that an appended version 10 model record describes.
	*	@param base Start of the buffer returned by BuildShadowBuffer.
	*	@return The model, or nullptr if the record is not one of the appended ones.
	*/
	const Model* FindModel( const byte* base, const mstudiomodel_t* record ) const;

	/**
	*	@brief Finds the sub mesh that an appended version 10 mesh record describes. Null if it is not one.
	*/
	const SubMeshRef* FindSubMesh( const mstudiomesh_t* record ) const;

	const SubMesh& GetSubMesh( const SubMeshRef& ref ) const;
	const Mesh& GetMesh( const SubMeshRef& ref ) const;

	/**
	*	Scale applied to the bone local coordinates of all vertices, see ScaleMeshes in CStudioModel.
	*	Version 10 stores vertices relative to their bone and scales them there, so this is applied before the bone transform
	*	instead of to the model space positions stored in the file.
	*/
	float meshScale = 1.0f;

	void ScaleMeshes( const float scale ) { meshScale *= scale; }

	/**
	*	@brief Replaces the pixels of a texture.
	*	@param chain BC7 mip chain, top level first. Must be exactly dds::BC7ChainSize( width, height, mipLevels ) bytes.
	*	@throws StudioModelV2026Error
	*/
	void ReplaceTexture( int index, int width, int height, int mipLevels, std::vector<uint8_t>&& chain );
};

/**
*	@brief Reads the version 2026 specific data from a complete model file.
*	@param file Start of the file. Must be suitably aligned for studiohdr_t (e.g. from new[]).
*	@throws StudioModelV2026Error
*/
std::unique_ptr<StudioModelData> Parse( const byte* file, size_t size );

/**
*	@brief Creates a copy of the file with version 10 style description records appended, and redirects
*		the header's bodypartindex, textureindex, texturedataindex, skinindex and length to them.
*		The header's version stays 2026. Fills in the lookup tables of @p data.
*	@param[out] outSize Size of the returned buffer.
*/
std::unique_ptr<byte[]> BuildShadowBuffer( const byte* file, size_t size, StudioModelData& data, size_t& outSize );

/**
*	@brief Computes the default pose of every bone (bone to world).
*	@throws StudioModelV2026Error
*/
std::vector<glm::mat3x4> ComputeBindPose( const byte* file, size_t size, const studiohdr_t& hdr );

/**
*	@brief Creates the file to save.
*	@param memory The model buffer as returned by BuildShadowBuffer, including any edits made to it.
*	@param memorySize Size of memory.
*
*	Starts with the original file and applies what can be edited: everything in the version 10 compatible part of the buffer
*	(header, bones, sequences, hitboxes, ...), texture flags, replaced textures, and the bone and mesh scale. Vertices are stored in model
*	space, so when bones or meshes were scaled the vertices are recalculated for the new default pose.
*	@throws StudioModelV2026Error
*/
std::vector<byte> BuildSaveBuffer( const byte* memory, size_t memorySize, const StudioModelData& data );

/**
*	@brief Computes out[ i ] = boneToWorld[ i ] * diag( localScale ) * inverseBind[ i ]. Pass these to SkinMesh.
*	@param localScale Scales the vertices in bone space (see StudioModelData::meshScale).
*/
void ComputeSkinMatrices( const glm::mat3x4* boneToWorld, const glm::mat3x4* inverseBind, size_t numBones, glm::mat3x4* out, float localScale = 1.0f );

/**
*	@brief Skins all vertices of a mesh with up to 4 blended bones.
*	@param skin Matrices from ComputeSkinMatrices. Must have at least as many entries as the model has bones.
*	@param[out] outPositions Receives vertices.size() positions.
*	@param[out] outNormals Receives vertices.size() normalized normals.
*/
void SkinMesh( const Mesh& mesh, const glm::mat3x4* skin, glm::vec3* outPositions, glm::vec3* outNormals );

/**
*	@brief Returns the bone with the highest weight.
*/
uint8_t GetDominantBone( const Vertex& vertex );
}
}

#endif //GAME_STUDIOMODEL_STUDIOV2026_H
