#ifndef GAME_STUDIOMODEL_CSTUDIOMODEL_H
#define GAME_STUDIOMODEL_CSTUDIOMODEL_H

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "shared/Const.h"

#include "utility/mathlib.h"
#include "utility/Color.h"

#include "graphics/OpenGL.h"

#include "studio.h"

namespace studiomdl
{
namespace v2026
{
class StudioModelData;
struct Model;
}

/**
*	@brief Base class for all studio model exceptions
*/
class StudioModelException : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

/**
*	@brief Indicates that the studio model file does not exist
*/
class StudioModelNotFound : public StudioModelException
{
public:
	using StudioModelException::StudioModelException;
};

/**
*	@brief Indicates that a studio model file has the wrong format (i.e. not a studio model)
*/
class StudioModelInvalidFormat : public StudioModelException
{
public:
	using StudioModelException::StudioModelException;
};

/**
*	@brief Indicates that a studio model file has the wrong version (e.g. trying to load a Source model)
*/
class StudioModelVersionDiffers : public StudioModelException
{
public:
	StudioModelVersionDiffers(const std::string& message, int version)
		: StudioModelException(message)
		, _version(version)
	{
	}

	/**
	*	@brief Gets the version that the model has
	*/
	int GetVersion() const { return _version; }

private:
	const int _version;
};

/**
*	@brief Indicates that a studio model file is not a main header (e.g. trying to load texture header as a main header)
*/
class StudioModelIsNotMainHeader : public StudioModelException
{
public:
	using StudioModelException::StudioModelException;
};

struct StudioDataDeleter
{
	void operator()(studiohdr_t* pointer) const
	{
		delete[] pointer;
	}

	void operator()(studioseqhdr_t* pointer) const
	{
		delete[] pointer;
	}
};

template<typename T>
using studio_ptr = std::unique_ptr<T, StudioDataDeleter>;

class CStudioModel;

/**
*	Loads a studio model
*	@param pszFilename Name of the model to load. This is the entire path, including the extension
*	@exception StudioModelNotFound If a file could not be found
*	@exception StudioModelInvalidFormat If a file has an invalid format
*	@exception StudioModelVersionDiffers If a file has the wrong studio version
*/
std::unique_ptr<CStudioModel> LoadStudioModel(const char* const pszFilename);

/**
*	Saves a studio model.
*	@param pszFilename Name of the file to save the model to. This is the entire path, including the extension.
*	@param model Model to save.
* *	@param correctSequenceGroupFileNames Whether the sequence group filenames embedded in the main file should be corrected
*	@exception StudioModelException If an error occurs or if the given data is invalid
*/
void SaveStudioModel( const char* const pszFilename, CStudioModel& model, bool correctSequenceGroupFileNames );

/**
*	Container representing a studiomodel and its data.
*/
class CStudioModel final
{
private:
	typedef std::vector<const mstudiomesh_t*> MeshList_t;
	typedef std::vector<MeshList_t> TextureMeshMap_t;

protected:
	friend std::unique_ptr<CStudioModel> LoadStudioModel(const char* const pszFilename);

public:
	static const size_t MAX_SEQGROUPS = 32;
	static const size_t MAX_TEXTURES = MAXSTUDIOSKINS;

public:
	CStudioModel(std::string&& fileName, studio_ptr<studiohdr_t>&& pStudioHdr, studio_ptr<studiohdr_t>&& pTextureHdr,
		std::vector<studio_ptr<studioseqhdr_t>>&& sequenceHeaders, std::vector<GLuint>&& textures,
		std::unique_ptr<v2026::StudioModelData>&& v2026Data = nullptr);
	~CStudioModel();

	const std::string& GetFileName() const { return m_FileName; }

	void SetFileName(std::string&& fileName)
	{
		m_FileName = std::move(fileName);
	}

	studiohdr_t* GetStudioHeader() const { return m_pStudioHdr.get(); }

	bool HasSeparateTextureHeader() const { return !!m_pTextureHdr; }

	studiohdr_t* GetTextureHeader() const
	{
		if (m_pTextureHdr)
		{
			return m_pTextureHdr.get();
		}

		return m_pStudioHdr.get();
	}

	studioseqhdr_t*	GetSeqGroupHeader( const size_t i ) const { return m_SequenceHeaders[ i ].get(); }

	mstudioanim_t*	GetAnim( mstudioseqdesc_t* pseqdesc ) const;

	mstudiomodel_t* GetModelByBodyPart( const int iBody, const int iBodyPart ) const;

	/**
	*	@brief Whether this is a version 2026 model. Its models, meshes and textures are described to the rest of the program by
	*		version 10 style records, but the actual data is in GetV2026Data(). See StudioV2026.h.
	*/
	bool IsV2026() const { return m_pV2026Data != nullptr; }

	v2026::StudioModelData* GetV2026Data() const { return m_pV2026Data.get(); }

	/**
	*	@brief Finds the version 2026 model that a model record returned by GetModelByBodyPart describes. Null if not a v2026 model.
	*/
	const v2026::Model* GetV2026Model( const mstudiomodel_t* pModel ) const;

	/**
	*	@brief Gets the texture coordinates (0..1, 3 per triangle) of the sub mesh that a mesh record describes.
	*	@return false if this is not a v2026 model or the record is not valid.
	*/
	bool GetV2026SubMeshUVs( const mstudiomesh_t* pMesh, std::vector<glm::vec2>& uvs ) const;

	/**
	*	@brief Saves a version 2026 texture as a 24 bit BMP.
	*	@return false if this is not a v2026 model, the index is invalid or the file could not be written.
	*/
	bool ExportV2026TextureBMP( const int iIndex, const char* pszFileName ) const;

	/**
	*	@brief Saves a version 2026 texture as a BC7 DDS file, with the mip levels stored in the model. This is the native format, nothing is lost.
	*	@return false if this is not a v2026 model, the index is invalid or the file could not be written.
	*/
	bool ExportV2026TextureDDS( const int iIndex, const char* pszFileName ) const;

	/**
	*	@brief Saves a version 2026 texture. The format follows the extension of the file name: .bmp is saved as a 24 bit BMP,
	*		anything else as a BC7 DDS file (see ExportV2026TextureDDS).
	*/
	bool ExportV2026Texture( const int iIndex, const char* pszFileName ) const;

	/**
	*	@brief Replaces a version 2026 texture with the contents of an image file. The extension decides how it is read:
	*		.dds (see ImportV2026TextureDDS) or .bmp (8, 24 and 32 bit, compressed with a simple built in encoder).
	*	@return An empty string on success, otherwise a message that says why the file couldn't be used.
	*/
	std::string ImportV2026Texture( const int iIndex, const char* pszFileName );

	/**
	*	@brief Replaces a version 2026 texture with the contents of a DDS file.
	*	BC7 files are used as they are, including their mip levels. Uncompressed 8 bit RGBA files are compressed by a simple built in encoder.
	*	@return An empty string on success, otherwise a message that says why the file couldn't be used.
	*/
	std::string ImportV2026TextureDDS( const int iIndex, const char* pszFileName );

	/**
	*	@brief Replaces a version 2026 texture with an RGBA image (compressed by a simple built in encoder).
	*	@return An empty string on success, otherwise a message that says why the image couldn't be used.
	*/
	std::string ImportV2026TextureRGBA( const int iIndex, const int iWidth, const int iHeight, const uint8_t* pRGBA );

	int GetBodyValueForGroup(int compositeValue, int group) const;

	bool			CalculateBodygroup( const int iGroup, const int iValue, int& iInOutBodygroup ) const;

	GLuint			GetTextureId( const int iIndex ) const;

	void			ReplaceTexture( mstudiotexture_t* ptexture, byte *data, byte *pal, GLuint textureId );

	/**
	*	Reuploads a texture. Useful for making changes made to the texture's pixel, palette or flag data show up in the model itself.
	*	@param ptexture Texture to reupload. Must be a texture that is part of this model.
	*/
	void ReuploadTexture( mstudiotexture_t* ptexture );

	std::vector<mstudiobone_t*> GetRootBones()
	{
		std::vector<mstudiobone_t*> bones;

		for (int i = 0; i < m_pStudioHdr->numbones; ++i)
		{
			auto bone = m_pStudioHdr->GetBone(i);

			if (bone->parent == -1)
			{
				bones.emplace_back(bone);
			}
		}

		return bones;
	}

private:
	std::string m_FileName;

	studio_ptr<studiohdr_t> m_pStudioHdr;
	studio_ptr<studiohdr_t> m_pTextureHdr;

	std::vector<studio_ptr<studioseqhdr_t>> m_SequenceHeaders;

	std::vector<GLuint> m_Textures;

	std::unique_ptr<v2026::StudioModelData> m_pV2026Data;

private:
	void OnV2026TextureReplaced( const int iIndex );

	CStudioModel( const CStudioModel& ) = delete;
	CStudioModel& operator=( const CStudioModel& ) = delete;
};

void ScaleMeshes( CStudioModel* pStudioModel, const float flScale );
void ScaleBones( CStudioModel* pStudioModel, const float flScale );

/**
*	Returns the string representation for a studio control value.
*	@param iControl Value containing a STUDIO_* control bit.
*	@return String representation for the control, or nullptr.
*/
const char* ControlToString( const int iControl );

/**
*	Returns the description for a studio control value.
*	@param iControl Value containing a STUDIO_* control bit.
*	@return String description for the control, or nullptr.
*/
const char* ControlToStringDescription( const int iControl );

/**
*	Converts a string to a control value.
*	@param pszString String representation of a control value.
*	@return Control value, or -1 if the string does not represent a valid control.
*/
int StringToControl( const char* const pszString );
}

#endif //GAME_STUDIOMODEL_CSTUDIOMODEL_H
