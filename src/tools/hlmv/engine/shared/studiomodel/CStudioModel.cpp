#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "shared/Platform.h"
#include "shared/Logging.h"

#include "utility/IOUtils.h"
#include "utility/StringUtils.h"

#include "cvar/CCVar.h"

#include "graphics/GraphicsUtils.h"
#include "graphics/Palette.h"

#include "CStudioModel.h"
#include "StudioV2026.h"

#include "graphics/BMPFile.h"

#include "BC7Encoder.h"
#include "DDS.h"

namespace studiomdl
{
namespace
{
//Note: multiple libraries can include this file and define this cvar. The first library to register theirs wins.
//All others will point to that one. - Solokiller
static cvar::CCVar r_filtertextures("r_filtertextures", cvar::CCVarArgsBuilder().FloatValue(1).HelpInfo("Whether to filter textures or not"));

static cvar::CCVar r_powerof2textures("r_powerof2textures",
	cvar::CCVarArgsBuilder()
	.Flags(cvar::Flag::ARCHIVE)
	.FloatValue(1)
	.MinValue(0)
	.MaxValue(1)
	.HelpInfo("Whether to resize textures to power of 2 dimensions"));

void UploadRGBATexture(const int iWidth, const int iHeight, const byte* pData, GLuint textureId, const bool bFilterTextures)
{
	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pData);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, bFilterTextures ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bFilterTextures ? GL_LINEAR : GL_NEAREST);
}

//Dol differs only in texture storage
//Instead of pixels followed by RGB palette, it has a 32 byte texture name (name of file without extension), followed by an RGBA palette and pixels
void ConvertDolToMdl(byte* pBuffer, const mstudiotexture_t& texture)
{
	const int RGBA_PALETTE_CHANNELS = 4;

	auto palette = std::make_unique<byte[]>(PALETTE_SIZE);

	//Starts off with 32 byte texture name
	auto pSourcePalette = pBuffer + texture.index + 32;

	//Discard alpha value
	//TODO: convert alpha value somehow? is it even used?
	for (int i = 0; i < PALETTE_ENTRIES; ++i)
	{
		for (int j = 0; j < PALETTE_CHANNELS; ++j)
		{
			palette[i * PALETTE_CHANNELS + j] = pSourcePalette[i * RGBA_PALETTE_CHANNELS + j];
		}
	}

	const auto size = texture.width * texture.height;

	auto pixels = std::make_unique<byte[]>(size);

	auto pSourcePixels = pSourcePalette + PALETTE_ENTRIES * RGBA_PALETTE_CHANNELS;

	for (int i = 0; i < size; ++i)
	{
		auto pixel = pSourcePixels[i];

		auto masked = pixel & 0x1F;

		//Adjust the index to map to the correct palette entry
		if (masked >= 8)
		{
			if (masked >= 16)
			{
				if (masked < 24)
				{
					pixel -= 8;
				}
			}
			else
			{
				pixel += 8;
			}
		}

		pixels[i] = pixel;
	}

	//Now write the correct data to the model buffer
	auto pDestPixels = pBuffer + texture.index;

	for (int i = 0; i < size; ++i)
	{
		pDestPixels[i] = pixels[i];
	}

	auto pDestPalette = pBuffer + texture.index + texture.width * texture.height;

	memcpy(pDestPalette, palette.get(), PALETTE_SIZE);

	//Some data will be left dangling after the palette and before the next texture/end of file. Nothing will reference it though
	//in the SL version this will not be a problem since the file isn't loaded in one chunk
}

void UploadTexture(const mstudiotexture_t* ptexture, const byte* data, byte* pal, GLuint name, const bool bFilterTextures, const bool bPowerOf2)
{
	// unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight;
	int		i, j;
	int		row1[MAX_TEXTURE_DIMS], row2[MAX_TEXTURE_DIMS], col1[MAX_TEXTURE_DIMS], col2[MAX_TEXTURE_DIMS];
	const byte* pix1, * pix2, * pix3, * pix4;

	// convert texture to power of 2
	int outwidth;
	int outheight;

	if (bPowerOf2)
	{
		if (!graphics::CalculateImageDimensions(ptexture->width, ptexture->height, outwidth, outheight))
			return;
	}
	else
	{
		outwidth = ptexture->width;
		outheight = ptexture->height;
	}

	const size_t uiSize = outwidth * outheight * 4;

	//Needs at least one pixel (satisfies code analysis)
	if (uiSize < 4)
		return;

	auto tex = std::make_unique<byte[]>(uiSize);

	/*
	int k = 0;
	for (i = 0; i < ptexture->height; i++)
	{
	for (j = 0; j < ptexture->width; j++)
	{

	in[k++] = pal[data[i * ptexture->width + j] * 3 + 0];
	in[k++] = pal[data[i * ptexture->width + j] * 3 + 1];
	in[k++] = pal[data[i * ptexture->width + j] * 3 + 2];
	in[k++] = 0xff;;
	}
	}

	gluScaleImage (GL_RGBA, ptexture->width, ptexture->height, GL_UNSIGNED_BYTE, in, outwidth, outheight, GL_UNSIGNED_BYTE, out);
	free (in);
	*/

	for (i = 0; i < outwidth; i++)
	{
		col1[i] = (int) ((i + 0.25) * (ptexture->width / (float) outwidth));
		col2[i] = (int) ((i + 0.75) * (ptexture->width / (float) outwidth));
	}

	for (i = 0; i < outheight; i++)
	{
		row1[i] = (int) ((i + 0.25) * (ptexture->height / (float) outheight)) * ptexture->width;
		row2[i] = (int) ((i + 0.75) * (ptexture->height / (float) outheight)) * ptexture->width;
	}

	const byte* const pAlpha = &pal[PALETTE_ALPHA_INDEX];

	//This modifies the model's data. Sets the mask color to black. This is also done by Jed's model viewer. (export texture has black)
	if (ptexture->flags & STUDIO_NF_MASKED)
	{
		pal[255 * 3 + 0] = pal[255 * 3 + 1] = pal[255 * 3 + 2] = 0;
	}

	auto out = tex.get();

	// scale down and convert to 32bit RGB
	for (i = 0; i < outheight; i++)
	{
		for (j = 0; j < outwidth; j++, out += 4)
		{
			pix1 = &pal[data[row1[i] + col1[j]] * 3];
			pix2 = &pal[data[row1[i] + col2[j]] * 3];
			pix3 = &pal[data[row2[i] + col1[j]] * 3];
			pix4 = &pal[data[row2[i] + col2[j]] * 3];

			out[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			out[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			out[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;

			if (ptexture->flags & STUDIO_NF_MASKED && pix1 == pAlpha && pix2 == pAlpha && pix3 == pAlpha && pix4 == pAlpha)
			{
				//Set alpha to 0 to enable transparent pixel.
				out[3] = 0x00;
			}
			else
			{
				out[3] = 0xFF;
			}
		}
	}

	UploadRGBATexture(outwidth, outheight, tex.get(), name, bFilterTextures);
}

//Bilinear resize, only used to make non power of 2 version 2026 textures power of 2 when r_powerof2textures is set.
std::vector<byte> ResampleRGBA(const std::vector<byte>& in, const int inWidth, const int inHeight, const int outWidth, const int outHeight)
{
	std::vector<byte> out(static_cast<size_t>(outWidth) * outHeight * 4);

	for (int y = 0; y < outHeight; ++y)
	{
		const float fy = (y + 0.5f) * inHeight / outHeight - 0.5f;
		const int y0 = std::max(0, static_cast<int>(std::floor(fy)));
		const int y1 = std::min(inHeight - 1, y0 + 1);
		const float ty = std::max(0.f, fy - y0);

		for (int x = 0; x < outWidth; ++x)
		{
			const float fx = (x + 0.5f) * inWidth / outWidth - 0.5f;
			const int x0 = std::max(0, static_cast<int>(std::floor(fx)));
			const int x1 = std::min(inWidth - 1, x0 + 1);
			const float tx = std::max(0.f, fx - x0);

			for (int c = 0; c < 4; ++c)
			{
				const float top = in[(static_cast<size_t>(y0) * inWidth + x0) * 4 + c] * (1 - tx) + in[(static_cast<size_t>(y0) * inWidth + x1) * 4 + c] * tx;
				const float bottom = in[(static_cast<size_t>(y1) * inWidth + x0) * 4 + c] * (1 - tx) + in[(static_cast<size_t>(y1) * inWidth + x1) * 4 + c] * tx;

				out[(static_cast<size_t>(y) * outWidth + x) * 4 + c] = static_cast<byte>(top * (1 - ty) + bottom * ty + 0.5f);
			}
		}
	}

	return out;
}

/**
*	Uploads a version 2026 texture. The alpha channel only means something for masked textures (it is what the alpha test uses),
*	everywhere else it is forced opaque so it can't interfere with blending.
*/
void UploadV2026Texture(const v2026::Texture& texture, const int flags, const GLuint name, const bool bFilterTextures, const bool bPowerOf2)
{
	std::vector<byte> rgba = texture.rgba;
	int width = texture.width;
	int height = texture.height;

	if (!(flags & STUDIO_NF_MASKED))
	{
		for (size_t i = 3; i < rgba.size(); i += 4)
		{
			rgba[i] = 0xFF;
		}
	}

	int outWidth, outHeight;

	if (bPowerOf2 && graphics::CalculateImageDimensions(width, height, outWidth, outHeight) && (outWidth != width || outHeight != height))
	{
		rgba = ResampleRGBA(rgba, width, height, outWidth, outHeight);
		width = outWidth;
		height = outHeight;
	}

	UploadRGBATexture(width, height, rgba.data(), name, bFilterTextures);
}

void UploadV2026Textures(const v2026::StudioModelData& data, const studiohdr_t& hdr, std::vector<GLuint>& textures, const bool bFilterTextures, const bool bPowerOf2)
{
	for (size_t i = 0; i < data.textures.size(); ++i)
	{
		GLuint name;

		glBindTexture(GL_TEXTURE_2D, 0);
		glGenTextures(1, &name);

		UploadV2026Texture(data.textures[i], hdr.GetTexture(static_cast<int>(i))->flags, name, bFilterTextures, bPowerOf2);

		textures.emplace_back(name);
	}
}

size_t UploadTextures(studiohdr_t& textureHdr, std::vector<GLuint>& textures, const bool bFilterTextures, const bool bPowerOf2, const bool bIsDol)
{
	size_t uiNumTextures = 0;

	if (textureHdr.textureindex > 0 && textureHdr.numtextures <= CStudioModel::MAX_TEXTURES)
	{
		mstudiotexture_t* ptexture = textureHdr.GetTextures();

		byte* pIn = reinterpret_cast<byte*>(&textureHdr);

		const int n = textureHdr.numtextures;

		for (int i = 0; i < n; ++i)
		{
			GLuint name;

			glBindTexture(GL_TEXTURE_2D, 0);
			glGenTextures(1, &name);

			if (bIsDol)
			{
				ConvertDolToMdl(pIn, ptexture[i]);
			}

			UploadTexture(&ptexture[i], pIn + ptexture[i].index, pIn + ptexture[i].width * ptexture[i].height + ptexture[i].index, name, bFilterTextures, bPowerOf2);

			textures.emplace_back(name);
		}

		uiNumTextures = n;
	}

	return uiNumTextures;
}
}

CStudioModel::CStudioModel(std::string&& fileName, studio_ptr<studiohdr_t>&& pStudioHdr, studio_ptr<studiohdr_t>&& pTextureHdr,
	std::vector<studio_ptr<studioseqhdr_t>>&& sequenceHeaders, std::vector<GLuint>&& textures,
	std::unique_ptr<v2026::StudioModelData>&& v2026Data)
	: m_FileName(std::move(fileName))
	, m_pStudioHdr(std::move(pStudioHdr))
	, m_pTextureHdr(std::move(pTextureHdr))
	, m_SequenceHeaders(std::move(sequenceHeaders))
	, m_Textures(std::move(textures))
	, m_pV2026Data(std::move(v2026Data))
{
	assert(m_pStudioHdr);
}

CStudioModel::~CStudioModel()
{
	glDeleteTextures(GetTextureHeader()->numtextures, m_Textures.data());
	m_Textures.clear();
}

mstudioanim_t* CStudioModel::GetAnim(mstudioseqdesc_t* pseqdesc) const
{
	mstudioseqgroup_t* pseqgroup = m_pStudioHdr->GetSequenceGroup(pseqdesc->seqgroup);

	if (pseqdesc->seqgroup == 0)
	{
		return (mstudioanim_t*) ((byte*) m_pStudioHdr.get() + pseqgroup->unused2 + pseqdesc->animindex);
	}

	return (mstudioanim_t*) ((byte*) m_SequenceHeaders[pseqdesc->seqgroup - 1].get() + pseqdesc->animindex);
}

mstudiomodel_t* CStudioModel::GetModelByBodyPart(const int iBody, const int iBodyPart) const
{
	mstudiobodyparts_t* pbodypart = m_pStudioHdr->GetBodypart(iBodyPart);

	int index = iBody / pbodypart->base;
	index = index % pbodypart->nummodels;

	return (mstudiomodel_t*) ((byte*) m_pStudioHdr.get() + pbodypart->modelindex) + index;
}

const v2026::Model* CStudioModel::GetV2026Model(const mstudiomodel_t* pModel) const
{
	if (!m_pV2026Data)
	{
		return nullptr;
	}

	return m_pV2026Data->FindModel(m_pStudioHdr->GetData(), pModel);
}

bool CStudioModel::GetV2026SubMeshUVs(const mstudiomesh_t* pMesh, std::vector<glm::vec2>& uvs) const
{
	uvs.clear();

	if (!m_pV2026Data)
	{
		return false;
	}

	const auto ref = m_pV2026Data->FindSubMesh(pMesh);

	if (!ref)
	{
		return false;
	}

	const auto& mesh = m_pV2026Data->GetMesh(*ref);
	const auto& sub = m_pV2026Data->GetSubMesh(*ref);

	uvs.reserve(sub.indexCount);

	for (uint32_t i = 0; i < sub.indexCount; ++i)
	{
		uvs.push_back(mesh.vertices[mesh.indices[sub.firstIndex + i]].uv);
	}

	return true;
}

namespace
{
//Lowercase extension of a file name including the dot, or an empty string.
std::string GetLowerExtension(const char* pszFileName)
{
	const std::string fileName{pszFileName ? pszFileName : ""};

	const auto dot = fileName.find_last_of('.');
	const auto separator = fileName.find_last_of("/\\");

	if (dot == std::string::npos || (separator != std::string::npos && dot < separator))
	{
		return {};
	}

	std::string extension = fileName.substr(dot);

	for (auto& c : extension)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	return extension;
}
}

bool CStudioModel::ExportV2026Texture(const int iIndex, const char* pszFileName) const
{
	if (GetLowerExtension(pszFileName) == ".bmp")
	{
		return ExportV2026TextureBMP(iIndex, pszFileName);
	}

	return ExportV2026TextureDDS(iIndex, pszFileName);
}

std::string CStudioModel::ImportV2026Texture(const int iIndex, const char* pszFileName)
{
	const auto extension = GetLowerExtension(pszFileName);

	if (extension == ".dds")
	{
		return ImportV2026TextureDDS(iIndex, pszFileName);
	}

	if (extension == ".bmp")
	{
		int width, height;
		std::vector<uint8_t> rgba;
		std::string error;

		if (!graphics::bmpfile::LoadBMPFileRGBA(pszFileName, width, height, rgba, &error))
		{
			return error;
		}

		return ImportV2026TextureRGBA(iIndex, width, height, rgba.data());
	}

	return "Unsupported file type. Use a .dds or .bmp file";
}

bool CStudioModel::ExportV2026TextureDDS(const int iIndex, const char* pszFileName) const
{
	if (!m_pV2026Data || iIndex < 0 || static_cast<size_t>(iIndex) >= m_pV2026Data->textures.size() || !pszFileName)
	{
		return false;
	}

	const auto& texture = m_pV2026Data->textures[iIndex];

	const auto file = dds::WriteBC7(texture.width, texture.height, texture.mipLevels, texture.compressed.data());

	FILE* pFile = utf8_fopen(pszFileName, "wb");

	if (!pFile)
	{
		return false;
	}

	const bool bSuccess = fwrite(file.data(), 1, file.size(), pFile) == file.size();

	fclose(pFile);

	return bSuccess;
}

std::string CStudioModel::ImportV2026TextureDDS(const int iIndex, const char* pszFileName)
{
	if (!m_pV2026Data || iIndex < 0 || static_cast<size_t>(iIndex) >= m_pV2026Data->textures.size() || !pszFileName)
	{
		return "Invalid texture";
	}

	FILE* pFile = utf8_fopen(pszFileName, "rb");

	if (!pFile)
	{
		return "Could not open the file";
	}

	std::vector<byte> contents;

	{
		byte buffer[65536];
		size_t read;

		constexpr size_t MAX_FILE_SIZE = 512 * 1024 * 1024;

		while ((read = fread(buffer, 1, sizeof(buffer), pFile)) > 0)
		{
			contents.insert(contents.end(), buffer, buffer + read);

			if (contents.size() > MAX_FILE_SIZE)
			{
				fclose(pFile);
				return "The file is too large";
			}
		}
	}

	fclose(pFile);

	dds::Image image;
	std::string error;

	if (!dds::Parse(contents.data(), contents.size(), image, error))
	{
		return error;
	}

	if (!image.isBC7)
	{
		return ImportV2026TextureRGBA(iIndex, image.width, image.height, image.data.data());
	}

	try
	{
		m_pV2026Data->ReplaceTexture(iIndex, image.width, image.height, image.mipLevels, std::move(image.data));
	}
	catch (const v2026::StudioModelV2026Error& e)
	{
		return e.what();
	}

	OnV2026TextureReplaced(iIndex);

	return {};
}

std::string CStudioModel::ImportV2026TextureRGBA(const int iIndex, const int iWidth, const int iHeight, const uint8_t* pRGBA)
{
	if (!m_pV2026Data || iIndex < 0 || static_cast<size_t>(iIndex) >= m_pV2026Data->textures.size() || !pRGBA)
	{
		return "Invalid texture";
	}

	if (iWidth <= 0 || iHeight <= 0 || iWidth > 16384 || iHeight > 16384)
	{
		return "Unsupported image dimensions";
	}

	//Keep the number of mip levels the model uses for this texture
	const int levels = std::min(std::max(m_pV2026Data->textures[iIndex].mipLevels, 1), dds::FullMipCount(iWidth, iHeight));

	try
	{
		m_pV2026Data->ReplaceTexture(iIndex, iWidth, iHeight, levels, bc7::EncodeMipChain(pRGBA, iWidth, iHeight, levels));
	}
	catch (const v2026::StudioModelV2026Error& e)
	{
		return e.what();
	}

	OnV2026TextureReplaced(iIndex);

	return {};
}

void CStudioModel::OnV2026TextureReplaced(const int iIndex)
{
	const auto& texture = m_pV2026Data->textures[iIndex];

	//The records the rest of the program sees
	mstudiotexture_t* const pRecord = m_pStudioHdr->GetTexture(iIndex);

	pRecord->width = texture.width;
	pRecord->height = texture.height;

	ReuploadTexture(pRecord);
}

bool CStudioModel::ExportV2026TextureBMP(const int iIndex, const char* pszFileName) const
{
	if (!m_pV2026Data || iIndex < 0 || static_cast<size_t>(iIndex) >= m_pV2026Data->textures.size())
	{
		return false;
	}

	const auto& texture = m_pV2026Data->textures[iIndex];

	return graphics::bmpfile::SaveBMPFileRGBA(pszFileName, texture.width, texture.height, texture.rgba.data());
}

int CStudioModel::GetBodyValueForGroup(int compositeValue, int group) const
{
	if (group >= m_pStudioHdr->numbodyparts)
	{
		return -1;
	}

	const mstudiobodyparts_t* const bodyPart = m_pStudioHdr->GetBodypart(group);

	return (compositeValue / bodyPart->base) % bodyPart->nummodels;
}

bool CStudioModel::CalculateBodygroup(const int iGroup, const int iValue, int& iInOutBodygroup) const
{
	if (iGroup > m_pStudioHdr->numbodyparts)
		return false;

	const mstudiobodyparts_t* const pbodypart = m_pStudioHdr->GetBodypart(iGroup);

	int iCurrent = (iInOutBodygroup / pbodypart->base) % pbodypart->nummodels;

	if (iValue >= pbodypart->nummodels)
		return true;

	iInOutBodygroup = (iInOutBodygroup - (iCurrent * pbodypart->base) + (iValue * pbodypart->base));

	return true;
}

GLuint CStudioModel::GetTextureId(const int iIndex) const
{
	const studiohdr_t* const pHdr = GetTextureHeader();

	if (!pHdr)
		return GL_INVALID_TEXTURE_ID;

	if (iIndex < 0 || iIndex >= pHdr->numtextures)
		return GL_INVALID_TEXTURE_ID;

	return m_Textures[iIndex];
}

void CStudioModel::ReplaceTexture(mstudiotexture_t* ptexture, byte* data, byte* pal, GLuint textureId)
{
	if (IsV2026())
	{
		//Version 2026 textures are true color BC7, there is no 8 bit data to replace them with
		Error("CStudioModel::ReplaceTexture: Cannot replace textures of version 2026 models\n");
		return;
	}

	UploadTexture(ptexture, data, pal, textureId, r_filtertextures.GetBool(), r_powerof2textures.GetBool());
}

void CStudioModel::ReuploadTexture(mstudiotexture_t* ptexture)
{
	assert(ptexture);

	auto header = GetTextureHeader();

	const int iIndex = ptexture - header->GetTextures();

	if (iIndex < 0 || iIndex >= header->numtextures)
	{
		Error("CStudioModel::ReuploadTexture: Invalid texture!");
		return;
	}

	GLuint textureId = m_Textures[iIndex];

	if (IsV2026())
	{
		UploadV2026Texture(m_pV2026Data->textures[iIndex], ptexture->flags, textureId, r_filtertextures.GetBool(), r_powerof2textures.GetBool());
		return;
	}

	UploadTexture(ptexture,
		header->GetData() + ptexture->index,
		header->GetData() + ptexture->index + ptexture->width * ptexture->height, textureId, r_filtertextures.GetBool(), r_powerof2textures.GetBool());
}

namespace
{
template<typename T>
studio_ptr<T> LoadStudioHeader(const char* const pszFilename, const bool bAllowSeqGroup)
{
	// load the model
	FILE* pFile = utf8_fopen(pszFilename, "rb");

	if (!pFile)
	{
		throw StudioModelNotFound(std::string{"File \""} + pszFilename + "\" not found");
	}

	fseek(pFile, 0, SEEK_END);
	const size_t size = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);

	auto buffer = std::make_unique<byte[]>(size);

	auto pStudioHdr = reinterpret_cast<T*>(buffer.get());

	const size_t uiRead = fread(pStudioHdr, size, 1, pFile);
	fclose(pFile);

	if (uiRead != 1)
	{
		throw StudioModelInvalidFormat(std::string{"Error reading file\""} + pszFilename + "\"");
	}

	if (strncmp(reinterpret_cast<const char*>(&pStudioHdr->id), STUDIOMDL_HDR_ID, 4) &&
		strncmp(reinterpret_cast<const char*>(&pStudioHdr->id), STUDIOMDL_SEQ_ID, 4))
	{
		throw StudioModelInvalidFormat(std::string{"The file \""} + pszFilename + "\" is neither a studio header nor a sequence header");
	}

	if (!bAllowSeqGroup && !strncmp(reinterpret_cast<const char*>(&pStudioHdr->id), STUDIOMDL_SEQ_ID, 4))
	{
		throw StudioModelInvalidFormat(std::string{"File \""} + pszFilename + "\": Expected a main studio model file, got a sequence file");
	}

	if (pStudioHdr->version != STUDIO_VERSION && pStudioHdr->version != STUDIO_VERSION_V2026)
	{
		throw StudioModelVersionDiffers(std::string{"File \""} + pszFilename + "\": version differs: expected \"" +
			std::to_string(STUDIO_VERSION) + "\", got \"" + std::to_string(pStudioHdr->version) + "\"",
			pStudioHdr->version);
	}

	//Validate header length. This should always be valid since it's set by the compiler
	if (pStudioHdr->length < 0 || (static_cast<size_t>(pStudioHdr->length) != size))
	{
		throw StudioModelException(std::string{"File \""} + pszFilename + "\": length does not match file size: expected \""
			+ std::to_string(size) + "\", got \"" + std::to_string(pStudioHdr->length) + "\"");
	}

	buffer.release();

	return studio_ptr<T>(pStudioHdr);
}
}

std::unique_ptr<CStudioModel> LoadStudioModel(const char* const pszFilename)
{
	const std::filesystem::path fileName{std::filesystem::u8path(pszFilename)};

	std::filesystem::path baseFileName{fileName};

	baseFileName.replace_extension();

	const auto bIsDol = fileName.extension() == ".dol";

	//Load the model
	auto mainHeader = LoadStudioHeader<studiohdr_t>(pszFilename, false);

	if (mainHeader->name[0] == '\0')
	{
		//Only the main hader sets the name, so this must be something else (probably texture header, but could be anything)
		auto message = std::string{"The file \""} + pszFilename + "\" is not a studio model main header file";

		if (!baseFileName.empty() && std::toupper(baseFileName.u8string().back()) == 'T')
		{
			message += " (it is probably a texture file)";
		}

		throw StudioModelIsNotMainHeader(message);
	}

	//Version 2026 models keep their original data. The v2026 specific parts are parsed here, and version 10 style records
	//describing them are appended to the buffer so the rest of the program can list and count them. See StudioV2026.h.
	std::unique_ptr<v2026::StudioModelData> v2026Data;

	if (mainHeader->version == STUDIO_VERSION_V2026)
	{
		try
		{
			v2026Data = v2026::Parse(mainHeader->GetData(), mainHeader->length);

			size_t shadowSize = 0;

			auto shadowBuffer = v2026::BuildShadowBuffer(mainHeader->GetData(), mainHeader->length, *v2026Data, shadowSize);

			mainHeader.reset(reinterpret_cast<studiohdr_t*>(shadowBuffer.release()));
		}
		catch (const v2026::StudioModelV2026Error& e)
		{
			throw StudioModelInvalidFormat(std::string{"File \""} + pszFilename + "\": " + e.what());
		}

		for (const auto& warning : v2026Data->warnings)
		{
			Warning("%s: %s\n", pszFilename, warning.c_str());
		}
	}

	studio_ptr<studiohdr_t> textureHeader;

	// preload textures
	if (mainHeader->numtextures == 0 && !v2026Data)
	{
		const auto extension = bIsDol ? "T.dol" : "T.mdl";

		auto texturename = baseFileName;

		texturename += extension;

		textureHeader = LoadStudioHeader<studiohdr_t>(texturename.u8string().c_str(), true);
	}

	std::vector<studio_ptr<studioseqhdr_t>> sequenceHeaders;

	// preload animations
	if (mainHeader->numseqgroups > 1)
	{
		sequenceHeaders.reserve(mainHeader->numseqgroups - 1);

		std::stringstream seqgroupname;

		for (int i = 1; i < mainHeader->numseqgroups; ++i)
		{
			seqgroupname.str({});

			const auto suffix = bIsDol ? ".dol" : ".mdl";

			seqgroupname << baseFileName.u8string() <<
				std::setfill('0') << std::setw(2) << i <<
				std::setw(0) << suffix;

			sequenceHeaders.emplace_back(LoadStudioHeader<studioseqhdr_t>(seqgroupname.str().c_str(), true));
		}
	}

	std::vector<GLuint> textures;

	if (v2026Data)
	{
		UploadV2026Textures(*v2026Data, *mainHeader, textures, r_filtertextures.GetBool(), r_powerof2textures.GetBool());
	}
	else
	{
		UploadTextures(textureHeader ? *textureHeader : *mainHeader, textures, r_filtertextures.GetBool(), r_powerof2textures.GetBool(), bIsDol);
	}

	return std::make_unique<CStudioModel>(pszFilename, std::move(mainHeader), std::move(textureHeader),
		std::move(sequenceHeaders), std::move(textures), std::move(v2026Data));
}

void SaveStudioModel(const char* const pszFilename, CStudioModel& model, bool correctSequenceGroupFileNames)
{
	if (!pszFilename)
	{
		throw StudioModelException("Null filename provided");
	}

	if (!(*pszFilename))
	{
		throw StudioModelException("Empty filename provided");
	}

	studiohdr_t* const pStudioHdr = model.GetStudioHeader();

	if (correctSequenceGroupFileNames)
	{
		std::filesystem::path baseFileName{pszFilename};

		//Find the "models" directory to determine what the relative path is
		auto relativeTo = baseFileName;

		auto foundRelative = false;

		//This will loop forever unless we test to see if relativeTo is the root path
		while (!relativeTo.empty() && relativeTo != relativeTo.root_path())
		{
			relativeTo = relativeTo.parent_path();

			if (relativeTo.stem() == "models")
			{
				relativeTo = relativeTo.parent_path();
				foundRelative = true;
				break;
			}
		}

		if (!foundRelative)
		{
			throw StudioModelException("Could not find base directory \"models\" in model filename; needed to correct sequence group filenames");
		}

		std::error_code e;

		baseFileName = std::filesystem::relative(baseFileName, relativeTo, e);

		if (e)
		{
			throw StudioModelException("Could not determine base filename for model to correct sequence group filenames");
		}

		baseFileName.replace_extension();

		const auto baseFileNameString = baseFileName.u8string();

		std::stringstream seqgroupname;

		//Group 0 is the main file and is never used to load files, and also doesn't have a filename set in the seqgroup structure
		for (int i = 1; i < pStudioHdr->numseqgroups; i++)
		{
			seqgroupname.str({});

			seqgroupname << baseFileNameString <<
				std::setfill('0') << std::setw(2) << i <<
				std::setw(0) << ".mdl";

			const auto groupNameString = seqgroupname.str();

			auto seqGroup = pStudioHdr->GetSequenceGroup(i);

			if (groupNameString.length() >= sizeof(seqGroup->name))
			{
				throw StudioModelException("Sequence group filename is too long");
			}

			strncpy(seqGroup->name, groupNameString.c_str(), groupNameString.length());

			seqGroup->name[sizeof(seqGroup->name) - 1] = '\0';
		}
	}

	//Version 2026 models are rebuilt from the original file. Do it before anything is written so a failure leaves no partial file.
	std::vector<byte> v2026File;

	if (model.IsV2026())
	{
		try
		{
			v2026File = v2026::BuildSaveBuffer(pStudioHdr->GetData(), pStudioHdr->length, *model.GetV2026Data());
		}
		catch (const v2026::StudioModelV2026Error& e)
		{
			throw StudioModelException(std::string{"Could not save version 2026 model: "} + e.what());
		}
	}

	FILE* pFile = utf8_fopen(pszFilename, "wb");

	if (!pFile)
	{
		throw StudioModelException("Could not open main file for writing");
	}

	const byte* const pMainData = model.IsV2026() ? v2026File.data() : reinterpret_cast<const byte*>(pStudioHdr);
	const size_t mainSize = model.IsV2026() ? v2026File.size() : static_cast<size_t>(pStudioHdr->length);

	bool bSuccess = fwrite(pMainData, sizeof(byte), mainSize, pFile) == mainSize;

	fclose(pFile);

	if (!bSuccess)
	{
		throw StudioModelException("Error while writing to main file");
	}

	const std::filesystem::path fileName{std::filesystem::u8path(pszFilename)};

	auto baseFileName{fileName};

	baseFileName.replace_extension();

	// write texture model
	if (model.HasSeparateTextureHeader())
	{
		const studiohdr_t* const pTextureHdr = model.GetTextureHeader();

		auto texturename = baseFileName;

		texturename += "T.mdl";

		pFile = utf8_fopen(texturename.u8string().c_str(), "wb");

		if (!pFile)
		{
			throw StudioModelException("Could not open texture file for writing");
		}

		bSuccess = fwrite(pTextureHdr, sizeof(byte), pTextureHdr->length, pFile) == pTextureHdr->length;
		fclose(pFile);

		if (!bSuccess)
		{
			throw StudioModelException("Error while writing to texture file");
		}
	}

	// write seq groups
	if (pStudioHdr->numseqgroups > 1)
	{
		std::stringstream seqgroupname;

		for (int i = 1; i < pStudioHdr->numseqgroups; i++)
		{
			seqgroupname.str({});

			seqgroupname << baseFileName.u8string() <<
				std::setfill('0') << std::setw(2) << i <<
				std::setw(0) << ".mdl";

			pFile = utf8_fopen(seqgroupname.str().c_str(), "wb");

			if (!pFile)
			{
				throw StudioModelException("Could not open sequence file for writing");
			}

			const auto pAnimHdr = model.GetSeqGroupHeader(i - 1);

			bSuccess = fwrite(pAnimHdr, sizeof(byte), pAnimHdr->length, pFile) == pAnimHdr->length;
			fclose(pFile);

			if (!bSuccess)
			{
				throw StudioModelException("Error while writing to sequence file");
			}
		}
	}
}

void ScaleMeshes(CStudioModel* pStudioModel, const float flScale)
{
	assert(pStudioModel);

	auto pStudioHdr = pStudioModel->GetStudioHeader();

	int iBodygroup = 0;

	// scale verts
	if (pStudioModel->IsV2026())
	{
		//The version 10 style model records don't point to vertex data. Vertices are scaled in bone space like for version 10.
		pStudioModel->GetV2026Data()->ScaleMeshes(flScale);
	}
	else
	for (int i = 0; i < pStudioHdr->numbodyparts; i++)
	{
		mstudiobodyparts_t* pbodypart = pStudioHdr->GetBodypart(i);
		for (int j = 0; j < pbodypart->nummodels; j++)
		{
			pStudioModel->CalculateBodygroup(i, j, iBodygroup);

			int bodypart = i;

			if (bodypart > pStudioHdr->numbodyparts)
			{
				// Message( "studiomdl::ScaleMeshes: no such bodypart %d\n", bodypart );
				bodypart = 0;
			}

			mstudiomodel_t* pModel = pStudioModel->GetModelByBodyPart(iBodygroup, bodypart);

			glm::vec3* pstudioverts = (glm::vec3*)((byte*) pStudioHdr + pModel->vertindex);

			for (int k = 0; k < pModel->numverts; k++)
			{
				pstudioverts[k] *= flScale;
			}
		}
	}

	// scale complex hitboxes
	mstudiobbox_t* pbboxes = pStudioHdr->GetHitBoxes();

	for (int i = 0; i < pStudioHdr->numhitboxes; i++)
	{
		pbboxes[i].bbmin *= flScale;
		pbboxes[i].bbmax *= flScale;
	}

	// scale bounding boxes
	mstudioseqdesc_t* pseqdesc = pStudioHdr->GetSequences();

	for (int i = 0; i < pStudioHdr->numseq; i++)
	{
		pseqdesc[i].bbmin *= flScale;
		pseqdesc[i].bbmax *= flScale;
	}

	// maybe scale exeposition, pivots, attachments
}

void ScaleBones(CStudioModel* pStudioModel, const float flScale)
{
	assert(pStudioModel);

	const auto pStudioHdr = pStudioModel->GetStudioHeader();

	mstudiobone_t* const pbones = pStudioHdr->GetBones();

	for (int i = 0; i < pStudioHdr->numbones; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			pbones[i].value[j] *= flScale;
			pbones[i].scale[j] *= flScale;
		}
	}
}

const char* ControlToString(const int iControl)
{
	switch (iControl)
	{
	case STUDIO_X:		return "X";
	case STUDIO_Y:		return "Y";
	case STUDIO_Z:		return "Z";
	case STUDIO_XR:		return "XR";
	case STUDIO_YR:		return "YR";
	case STUDIO_ZR:		return "ZR";
	case STUDIO_LX:		return "LX";
	case STUDIO_LY:		return "LY";
	case STUDIO_LZ:		return "LZ";
	case STUDIO_AX:		return "AX";
	case STUDIO_AY:		return "AY";
	case STUDIO_AZ:		return "AZ";
	case STUDIO_AXR:	return "AXR";
	case STUDIO_AYR:	return "AYR";
	case STUDIO_AZR:	return "AZR";

	default:			return nullptr;
	}
}

const char* ControlToStringDescription(const int iControl)
{
	switch (iControl)
	{
	case STUDIO_X:		return "Sequence motiontype flag: Zeroes out movement in the X axis\nBone controller type flag: Sets X offset";
	case STUDIO_Y:		return "Sequence motiontype flag: Zeroes out movement in the Y axis\nBone controller type flag: Sets Y offset";
	case STUDIO_Z:		return "Sequence motiontype flag: Zeroes out movement in the Z axis\nBone controller type flag: Sets Z offset";
	case STUDIO_XR:		return "Bone controller type flag: Sets X rotation\nSequence blendtype: Controls blending range clamping in the X axis";
	case STUDIO_YR:		return "Bone controller type flag: Sets Y rotation\nSequence blendtype: Controls blending range clamping in the Y axis";
	case STUDIO_ZR:		return "Bone controller type flag: Sets Z rotation\nSequence blendtype: Controls blending range clamping in the Z axis";
	case STUDIO_LX:		return "StudioMdl Compiler flag: Sets the sequence's last frame's X axis position to the first frame's position";
	case STUDIO_LY:		return "StudioMdl Compiler flag: Sets the sequence's last frame's Y axis position to the first frame's position";
	case STUDIO_LZ:		return "StudioMdl Compiler flag: Sets the sequence's last frame's Z axis position to the first frame's position";
	case STUDIO_AX:		return "StudioMdl Compiler flag: Extract relative movement distance between the first and current frame in the X axis (Unused)";
	case STUDIO_AY:		return "StudioMdl Compiler flag: Extract relative movement distance between the first and current frame in the Y axis (Unused)";
	case STUDIO_AZ:		return "StudioMdl Compiler flag: Extract relative movement distance between the first and current frame in the Z axis (Unused)";
	case STUDIO_AXR:	return "StudioMdl Compiler flag: Extract relative rotation distance between the first and current frame around the X axis (Unused)";
	case STUDIO_AYR:	return "StudioMdl Compiler flag: Extract relative rotation distance between the first and current frame around the Y axis (Unused)";
	case STUDIO_AZR:	return "StudioMdl Compiler flag: Extract relative rotation distance between the first and current frame around the Z axis (Unused)";

	default:			return nullptr;
	}
}

int StringToControl(const char* const pszString)
{
	assert(pszString);

	if (strcasecmp(pszString, "X") == 0) return STUDIO_X;
	if (strcasecmp(pszString, "Y") == 0) return STUDIO_Y;
	if (strcasecmp(pszString, "Z") == 0) return STUDIO_Z;
	if (strcasecmp(pszString, "XR") == 0) return STUDIO_XR;
	if (strcasecmp(pszString, "YR") == 0) return STUDIO_YR;
	if (strcasecmp(pszString, "ZR") == 0) return STUDIO_ZR;
	if (strcasecmp(pszString, "LX") == 0) return STUDIO_LX;
	if (strcasecmp(pszString, "LY") == 0) return STUDIO_LY;
	if (strcasecmp(pszString, "LZ") == 0) return STUDIO_LZ;
	if (strcasecmp(pszString, "AX") == 0) return STUDIO_AX;
	if (strcasecmp(pszString, "AY") == 0) return STUDIO_AY;
	if (strcasecmp(pszString, "AZ") == 0) return STUDIO_AZ;
	if (strcasecmp(pszString, "AXR") == 0) return STUDIO_AXR;
	if (strcasecmp(pszString, "AYR") == 0) return STUDIO_AYR;
	if (strcasecmp(pszString, "AZR") == 0) return STUDIO_AZR;

	return -1;
}
}