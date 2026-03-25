/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GlyphAtlasTexture.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Rendering/GL/myGL.h"

namespace {

[[nodiscard]] GLenum ToInternalFormat(GlyphAtlasTexture::PixelFormat format) noexcept
{
	switch (format) {
		case GlyphAtlasTexture::PixelFormat::BGRA: return GL_RGBA8;
		case GlyphAtlasTexture::PixelFormat::Alpha: return GL_R8;
	}

	return GL_R8;
}

[[nodiscard]] GLenum ToExternalFormat(GlyphAtlasTexture::PixelFormat format) noexcept
{
	switch (format) {
		case GlyphAtlasTexture::PixelFormat::BGRA: return GL_BGRA;
		case GlyphAtlasTexture::PixelFormat::Alpha: return GL_RED;
	}

	return GL_RED;
}

} // namespace

GlyphAtlasTexture::~GlyphAtlasTexture()
{
	DeleteTexture();
}

GlyphAtlasTexture::GlyphAtlasTexture(GlyphAtlasTexture&& other) noexcept
{
	MoveFrom(std::move(other));
}

GlyphAtlasTexture& GlyphAtlasTexture::operator=(GlyphAtlasTexture&& other) noexcept
{
	if (this == &other)
		return *this;

	Reset();
	MoveFrom(std::move(other));
	return *this;
}

void GlyphAtlasTexture::SetPixelFormat(PixelFormat pixelFormat) noexcept
{
	if (format == pixelFormat)
		return;

	CBitmap previousBitmap = std::move(atlasBitmap);
	Dimensions previousSize = atlasSize;

	format = pixelFormat;
	textureStorageDirty = true;
	dirtyUpload = true;

	if (!atlasSize.Empty()) {
		AllocateBitmap(atlasSize.width, atlasSize.height);
		if (!previousSize.Empty() && !previousBitmap.Empty())
			atlasBitmap.CopySubImage(previousBitmap, 0, 0);
	}
}

void GlyphAtlasTexture::MarkDirty() noexcept
{
	dirtyBitmap = true;
	dirtyUpload = true;
}

void GlyphAtlasTexture::ClearDirty() noexcept
{
	dirtyBitmap = false;
	dirtyRegions.clear();
}

void GlyphAtlasTexture::MarkUploaded() noexcept
{
	dirtyBitmap = false;
	dirtyUpload = false;
	textureStorageDirty = false;
	dirtyRegions.clear();
}

void GlyphAtlasTexture::Reset()
{
	DeleteTexture();

	atlasSize = {};
	dirtyBitmap = false;
	dirtyUpload = false;
	textureStorageDirty = true;
	atlasBitmap = {};
	dirtyRegions.clear();
}

void GlyphAtlasTexture::DeleteTexture() noexcept
{
	if (textureId != 0) {
		glDeleteTextures(1, &textureId);
		textureId = 0;
	}
}

void GlyphAtlasTexture::Reallocate(int width, int height, bool preserveContents)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("GlyphAtlasTexture::Reallocate requires positive atlas dimensions");

	if (!textureStorageDirty && atlasSize.width == width && atlasSize.height == height && atlasBitmap.channels == GetChannelCount())
		return;

	CBitmap previousBitmap = std::move(atlasBitmap);
	Dimensions previousSize = atlasSize;

	AllocateBitmap(width, height);
	atlasSize = {width, height};

	if (preserveContents && !previousSize.Empty() && !previousBitmap.Empty()) {
		const int copyWidth = std::min(previousSize.width, width);
		const int copyHeight = std::min(previousSize.height, height);
		const int channels = GetChannelCount();
		const std::size_t rowBytes = static_cast<std::size_t>(copyWidth) * channels;

		const std::uint8_t* src = previousBitmap.GetRawMem();
		std::uint8_t* dst = atlasBitmap.GetRawMem();

		for (int row = 0; row < copyHeight; ++row) {
			const std::size_t srcOffset = static_cast<std::size_t>(row) * previousSize.width * channels;
			const std::size_t dstOffset = static_cast<std::size_t>(row) * width * channels;
			std::memcpy(dst + dstOffset, src + srcOffset, rowBytes);
		}
	}

	textureStorageDirty = true;
	dirtyUpload = true;
	dirtyBitmap = preserveContents && !previousSize.Empty() && !previousBitmap.Empty();
	dirtyRegions.clear();

	if (preserveContents && !previousSize.Empty() && !previousBitmap.Empty()) {
		dirtyRegions.push_back({0, 0, std::min(previousSize.width, width), std::min(previousSize.height, height)});
	}
}

void GlyphAtlasTexture::UpdateBitmapRegion(const CBitmap& bitmap, int x, int y)
{
	UpdateBitmapRegion(bitmap, Region{x, y, bitmap.xsize, bitmap.ysize});
}

void GlyphAtlasTexture::UpdateBitmapRegion(const CBitmap& bitmap, const Region& region)
{
	if (bitmap.Empty() || region.Empty())
		return;

	ValidateRegion(region);

	if (bitmap.channels != GetChannelCount())
		throw std::invalid_argument("GlyphAtlasTexture::UpdateBitmapRegion channel count mismatch");

	if (bitmap.xsize < region.width || bitmap.ysize < region.height)
		throw std::out_of_range("GlyphAtlasTexture::UpdateBitmapRegion source bitmap is smaller than requested region");

	const std::size_t channels = static_cast<std::size_t>(GetChannelCount());
	const std::size_t rowBytes = static_cast<std::size_t>(region.width) * channels;
	const std::uint8_t* src = bitmap.GetRawMem();
	std::uint8_t* dst = atlasBitmap.GetRawMem();

	for (int row = 0; row < region.height; ++row) {
		const std::size_t srcOffset = static_cast<std::size_t>(row) * bitmap.xsize * channels;
		const std::size_t dstOffset = (static_cast<std::size_t>(region.y + row) * atlasSize.width + region.x) * channels;
		std::memcpy(dst + dstOffset, src + srcOffset, rowBytes);
	}

	dirtyRegions.push_back(region);
	MarkDirty();
}

void GlyphAtlasTexture::UpdateBitmapRegion(const std::uint8_t* pixels, int srcWidth, int srcHeight, int channels, int dstX, int dstY)
{
	if (srcWidth <= 0 || srcHeight <= 0)
		return;

	if (pixels == nullptr)
		throw std::invalid_argument("GlyphAtlasTexture::UpdateBitmapRegion requires a non-null pixel buffer");

	if (channels != GetChannelCount())
		throw std::invalid_argument("GlyphAtlasTexture::UpdateBitmapRegion channel count mismatch");

	const Region region{dstX, dstY, srcWidth, srcHeight};
	ValidateRegion(region);

	const std::size_t rowBytes = static_cast<std::size_t>(srcWidth) * channels;
	std::uint8_t* dst = atlasBitmap.GetRawMem();

	for (int row = 0; row < srcHeight; ++row) {
		const std::size_t srcOffset = static_cast<std::size_t>(row) * srcWidth * channels;
		const std::size_t dstOffset = (static_cast<std::size_t>(dstY + row) * atlasSize.width + dstX) * channels;
		std::memcpy(dst + dstOffset, pixels + srcOffset, rowBytes);
	}

	dirtyRegions.push_back(region);
	MarkDirty();
}

void GlyphAtlasTexture::Upload()
{
	if (!dirtyUpload && !textureStorageDirty)
		return;

	if (atlasSize.Empty() || atlasBitmap.Empty())
		return;

	CreateTexture();
	glBindTexture(GL_TEXTURE_2D, textureId);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	const GLenum internalFormat = ToInternalFormat(format);
	const GLenum externalFormat = ToExternalFormat(format);

	if (textureStorageDirty || dirtyRegions.empty()) {
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			internalFormat,
			atlasSize.width,
			atlasSize.height,
			0,
			externalFormat,
			GL_UNSIGNED_BYTE,
			atlasBitmap.GetRawMem()
		);
	} else {
		const int channels = GetChannelCount();
		std::vector<std::uint8_t> regionPixels;

		for (const Region& region: dirtyRegions) {
			if (region.Empty())
				continue;

			const std::size_t rowBytes = static_cast<std::size_t>(region.width) * channels;
			regionPixels.resize(static_cast<std::size_t>(region.width) * region.height * channels);

			const std::uint8_t* src = atlasBitmap.GetRawMem();
			for (int row = 0; row < region.height; ++row) {
				const std::size_t srcOffset = (static_cast<std::size_t>(region.y + row) * atlasSize.width + region.x) * channels;
				const std::size_t dstOffset = static_cast<std::size_t>(row) * rowBytes;
				std::memcpy(regionPixels.data() + dstOffset, src + srcOffset, rowBytes);
			}

			glTexSubImage2D(
				GL_TEXTURE_2D,
				0,
				region.x,
				region.y,
				region.width,
				region.height,
				externalFormat,
				GL_UNSIGNED_BYTE,
				regionPixels.data()
			);
		}
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	MarkUploaded();
}

void GlyphAtlasTexture::CreateTexture()
{
	if (textureId == 0)
		glGenTextures(1, &textureId);

	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

	constexpr GLfloat borderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

	if (format == PixelFormat::Alpha) {
		constexpr GLint swizzleMask[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
	} else {
		constexpr GLint swizzleMask[] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

void GlyphAtlasTexture::AllocateBitmap(int width, int height)
{
	atlasBitmap = {};
	atlasBitmap.Alloc(width, height, GetChannelCount());
}

void GlyphAtlasTexture::ValidateRegion(const Region& region) const
{
	if (atlasSize.Empty() || atlasBitmap.Empty())
		throw std::logic_error("GlyphAtlasTexture::ValidateRegion atlas storage is not allocated");

	if (region.Empty())
		throw std::invalid_argument("GlyphAtlasTexture::ValidateRegion requires a non-empty region");

	if (region.x < 0 || region.y < 0)
		throw std::out_of_range("GlyphAtlasTexture::ValidateRegion requires non-negative coordinates");

	if ((region.x + region.width) > atlasSize.width || (region.y + region.height) > atlasSize.height)
		throw std::out_of_range("GlyphAtlasTexture::ValidateRegion region exceeds atlas dimensions");
}

void GlyphAtlasTexture::MoveFrom(GlyphAtlasTexture&& other) noexcept
{
	atlasSize = other.atlasSize;
	format = other.format;
	textureId = std::exchange(other.textureId, 0);
	dirtyBitmap = other.dirtyBitmap;
	dirtyUpload = other.dirtyUpload;
	textureStorageDirty = other.textureStorageDirty;
	atlasBitmap = std::move(other.atlasBitmap);
	dirtyRegions = std::move(other.dirtyRegions);

	other.atlasSize = {};
	other.dirtyBitmap = false;
	other.dirtyUpload = false;
	other.textureStorageDirty = true;
}
