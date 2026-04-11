/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "GlyphAtlasTexture.h"
#include "GlyphCacheCore.h"
#include "Rendering/Textures/Bitmap.h"
#include "System/Rectangle.h"

class CRowAtlasAlloc;

namespace fonts {

/**
 * Bitmap-atlas payload cache for the shader renderer backend.
 *
 * This owns rasterized glyph bitmaps, atlas packing state, atlas textures, and
 * the per-glyph shader payload keyed by GlyphCacheCore identity.
 */
class ShaderGlyphAtlasCache {
public:
	using FacePtr = GlyphCacheCore::FacePtr;

	struct PayloadRecord {
		ShaderGlyphPayload payload;
		bool attempted = false;
		bool available = false;
	};

public:
	explicit ShaderGlyphAtlasCache(GlyphCacheCore& core);
	~ShaderGlyphAtlasCache();

	ShaderGlyphAtlasCache(const ShaderGlyphAtlasCache&) = delete;
	ShaderGlyphAtlasCache& operator=(const ShaderGlyphAtlasCache&) = delete;
	ShaderGlyphAtlasCache(ShaderGlyphAtlasCache&&) noexcept = delete;
	ShaderGlyphAtlasCache& operator=(ShaderGlyphAtlasCache&&) noexcept = delete;

	void EnsureGlyphs(std::span<const GlyphKey> glyphKeys, const FacePtr& defaultFace = {});
	void EnsureGlyphPayloads(std::span<const GlyphInfo* const> glyphs);
	bool Clear();

	GlyphAtlasTexture& GetAtlasTexture() noexcept { return atlasTexture; }
	GlyphAtlasTexture& GetShadowAtlasTexture() noexcept { return shadowAtlasTexture; }
	const GlyphAtlasTexture& GetAtlasTexture() const noexcept { return atlasTexture; }
	const GlyphAtlasTexture& GetShadowAtlasTexture() const noexcept { return shadowAtlasTexture; }

	bool NeedsAtlasUpdate() const noexcept;
	bool NeedsAtlasUpload() const noexcept;
	void UpdateAtlases();
	void UploadAtlases();
	void ReallocAtlases(bool preserveContents);

	bool HasColorGlyphs() const noexcept { return needsColor; }
	bool IsColorFont() const noexcept { return isColor; }

	const ShaderGlyphPayload* FindPayload(const GlyphInfo& glyph) const;
	const ShaderGlyphPayload& GetPayload(const GlyphInfo& glyph);

private:
	void BuildBitmapGlyph(const GlyphInfo& glyph);
	void QueueGlyphBitmap(const std::string& cacheKey, const CBitmap& bitmap, const CBitmap* outlineBitmap = nullptr);
	void PackPendingGlyphs();
	void UpdateAtlasRegions();
	void UploadAtlasTextures();
	void ClearAtlasState();

private:
	static inline const ShaderGlyphPayload dummyPayload = {};

	GlyphCacheCore& core;
	bool needsColor = false;
	bool isColor = false;

	std::unordered_map<GlyphCacheCore::GlyphIndexKey, PayloadRecord, GlyphCacheCore::GlyphIndexKeyHasher> payloadsByGlyphKey;
	GlyphAtlasTexture atlasTexture;
	GlyphAtlasTexture shadowAtlasTexture;

	std::unique_ptr<CRowAtlasAlloc> atlasAllocator;
	std::vector<CBitmap> pendingGlyphBitmaps;
	std::vector<SRectangle> blurRectangles;
	std::unordered_map<std::string, std::size_t> glyphNameToBitmapIndex;
};

} // namespace fonts
