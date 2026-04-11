/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_OUTLINE_H

#include "GlyphCacheCore.h"

namespace fonts {

/**
 * SLUG vector payload cache.
 *
 * This owns curve/band texture data, GL texture objects, and the per-glyph
 * SLUG payload keyed by GlyphCacheCore identity.
 */
class SlugGlyphAtlasCache {
public:
	using FacePtr = GlyphCacheCore::FacePtr;

	struct PayloadRecord {
		SlugGlyphPayload payload;
		bool attempted = false;
		bool available = false;
	};

	struct SlugTextureInfo {
		unsigned int curveTextureId = 0;
		unsigned int bandTextureId = 0;
		std::uint32_t curveTextureWidth = 0;
		std::uint32_t curveTextureHeight = 0;
		std::uint32_t bandTextureWidth = 0;
		std::uint32_t bandTextureHeight = 0;

		constexpr bool Empty() const noexcept
		{
			return (curveTextureId == 0) || (bandTextureId == 0) ||
				(curveTextureWidth == 0) || (curveTextureHeight == 0) ||
				(bandTextureWidth == 0) || (bandTextureHeight == 0);
		}
	};

public:
	explicit SlugGlyphAtlasCache(GlyphCacheCore& core);
	~SlugGlyphAtlasCache();

	SlugGlyphAtlasCache(const SlugGlyphAtlasCache&) = delete;
	SlugGlyphAtlasCache& operator=(const SlugGlyphAtlasCache&) = delete;
	SlugGlyphAtlasCache(SlugGlyphAtlasCache&&) noexcept = delete;
	SlugGlyphAtlasCache& operator=(SlugGlyphAtlasCache&&) noexcept = delete;

	void EnsureGlyphs(std::span<const GlyphKey> glyphKeys, const FacePtr& defaultFace = {});
	void EnsureGlyphPayloads(std::span<const GlyphInfo* const> glyphs);
	bool Clear();

	bool NeedsUpload() const noexcept;
	void UploadTextures();

	SlugTextureInfo GetTextureInfo() const noexcept;

	const SlugGlyphPayload* FindPayload(const GlyphInfo& glyph) const;
	const SlugGlyphPayload& GetPayload(const GlyphInfo& glyph);

private:
	void BuildSlugGlyph(const GlyphInfo& glyph);
	bool BuildSlugFillGlyph(const FacePtr& face, std::uint32_t glyphIndex, SlugGlyphPayload& payload);
	bool BuildSlugOutlineGlyph(const FacePtr& face, std::uint32_t glyphIndex, SlugGlyphPayload& payload);
	bool BuildSlugGlyphFromFTOutline(const FT_Outline& outline, SlugGlyphInfo& outInfo);
	void ClearSlugState();
	void DeleteTextures() noexcept;

private:
	static inline const SlugGlyphPayload dummyPayload = {};

	GlyphCacheCore& core;
	std::unordered_map<GlyphCacheCore::GlyphIndexKey, PayloadRecord, GlyphCacheCore::GlyphIndexKeyHasher> payloadsByGlyphKey;

	unsigned int slugCurveTextureId = 0;
	unsigned int slugBandTextureId = 0;
	std::vector<float> slugCurveTexels;
	std::vector<std::uint16_t> slugBandTexels;
	std::uint32_t slugCurveTextureWidth = 4096;
	std::uint32_t slugCurveTextureHeight = 0;
	std::uint32_t slugBandTextureWidth = 4096;
	std::uint32_t slugBandTextureHeight = 0;
	bool slugTexturesDirty = false;
};

} // namespace fonts
