/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Rendering/Textures/Bitmap.h"


/**
 * Owns the CPU atlas bitmap(s) and the GPU texture resource used to expose them
 * to font renderers.
 *
 * Responsibilities:
 *  - track atlas dimensions and pixel format
 *  - own the CPU-side atlas bitmap backing store
 *  - track dirty/upload state
 *  - manage GL texture creation and reallocation boundaries
 *
 * Non-responsibilities:
 *  - glyph lookup or cache indexing
 *  - atlas packing / allocator policy
 *  - text shaping / layout / wrapping
 */
class GlyphAtlasTexture {
public:
	enum class PixelFormat : std::uint8_t {
		Alpha = 1,
		BGRA  = 4,
	};

	enum class ContentType : std::uint8_t {
		Bitmap,
		SDF,
	};

	struct Region {
		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;

		constexpr bool Empty() const noexcept {
			return (width <= 0) || (height <= 0);
		}
	};

	struct Dimensions {
		int width = 0;
		int height = 0;

		constexpr bool Empty() const noexcept {
			return (width <= 0) || (height <= 0);
		}
	};

public:
	GlyphAtlasTexture() = default;
	explicit GlyphAtlasTexture(PixelFormat pixelFormat)
		: format(pixelFormat)
	{}

	~GlyphAtlasTexture();

	GlyphAtlasTexture(const GlyphAtlasTexture&) = delete;
	GlyphAtlasTexture& operator=(const GlyphAtlasTexture&) = delete;

	GlyphAtlasTexture(GlyphAtlasTexture&& other) noexcept;
	GlyphAtlasTexture& operator=(GlyphAtlasTexture&& other) noexcept;

	bool NeedsUpdate() const noexcept { return dirtyBitmap; }
	bool NeedsUpload() const noexcept { return dirtyUpload; }
	bool HasTexture() const noexcept { return textureId != 0; }
	bool IsAllocated() const noexcept { return !atlasSize.Empty(); }

	PixelFormat GetPixelFormat() const noexcept { return format; }
	void SetPixelFormat(PixelFormat pixelFormat);
	ContentType GetContentType() const noexcept { return contentType; }
	void SetContentType(ContentType type) noexcept;

	int GetChannelCount() const noexcept { return static_cast<int>(format); }
	int GetTextureId() const noexcept { return static_cast<int>(textureId); }
	Dimensions GetDimensions() const noexcept { return atlasSize; }
	int GetWidth() const noexcept { return atlasSize.width; }
	int GetHeight() const noexcept { return atlasSize.height; }

	const CBitmap& GetBitmap() const noexcept { return atlasBitmap; }
	CBitmap& GetBitmap() noexcept { return atlasBitmap; }

	void MarkDirty() noexcept;
	void ClearDirty() noexcept;
	void MarkUploaded() noexcept;

	void Reset();
	void DeleteTexture() noexcept;

	void Reallocate(int width, int height, bool preserveContents = false);
	void Reallocate(const Dimensions& dims, bool preserveContents = false) {
		Reallocate(dims.width, dims.height, preserveContents);
	}

	void UpdateBitmapRegion(const CBitmap& bitmap, int x, int y);
	void UpdateBitmapRegion(const CBitmap& bitmap, const Region& region);
	void UpdateBitmapRegion(const std::uint8_t* pixels, int srcWidth, int srcHeight, int channels, int dstX, int dstY);

	void Upload();

private:
	void CreateTexture();
	void AllocateBitmap(int width, int height);
	void ValidateRegion(const Region& region) const;
	void MoveFrom(GlyphAtlasTexture&& other) noexcept;

private:
	Dimensions atlasSize;
	PixelFormat format = PixelFormat::Alpha;
	ContentType contentType = ContentType::Bitmap;

	unsigned int textureId = 0;
	bool dirtyBitmap = false;
	bool dirtyUpload = true;
	bool textureStorageDirty = true;

	CBitmap atlasBitmap;
	std::vector<Region> dirtyRegions;
};
