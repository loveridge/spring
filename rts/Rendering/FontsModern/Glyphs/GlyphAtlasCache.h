/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "GlyphInfo.h"
#include "GlyphAtlasTexture.h"
#include "../FontTypes.h"

class CBitmap;
class CRowAtlasAlloc;
struct SRectangle;

namespace font {
class FontFace;
class FontFaceSet;
}

/**
 * Main glyph cache and rasterization boundary.
 *
 * Responsibilities:
 *  - own the primary face set reference used for glyph resolution
 *  - cache glyphs by codepoint and by (face,glyph-index)
 *  - rasterize glyphs from FreeType-managed faces
 *  - pack rasterized bitmaps into atlas textures
 *  - generate optional outline/shadow atlas content
 *  - expose atlas upload/reallocation hooks to renderer-facing layers
 *
 * Non-responsibilities:
 *  - text shaping
 *  - wrapping / alignment / paragraph layout
 *  - renderer submission details
 */
class GlyphAtlasCache {
public:
	using FacePtr = std::shared_ptr<font::FontFace>;
	using FaceSetPtr = std::shared_ptr<font::FontFaceSet>;

	struct GlyphIndexKey {
		std::uintptr_t faceIdentity = 0;
		std::uint32_t glyphIndex = 0;
		char32_t sourceCodepoint = 0;

		friend bool operator==(const GlyphIndexKey&, const GlyphIndexKey&) = default;
	};

	struct GlyphIndexKeyHasher {
		std::size_t operator()(const GlyphIndexKey& key) const noexcept
		{
			std::size_t h = std::hash<std::uintptr_t>{}(key.faceIdentity);
			h ^= std::hash<std::uint32_t>{}(key.glyphIndex) + 0x9e3779b9u + (h << 6) + (h >> 2);
			h ^= std::hash<char32_t>{}(key.sourceCodepoint) + 0x9e3779b9u + (h << 6) + (h >> 2);
			return h;
		}
	};

public:
	GlyphAtlasCache() = default;
	GlyphAtlasCache(FaceSetPtr faceSet, int fontSize, int outlineSize = 0, float outlineWeight = 0.0f);
	~GlyphAtlasCache();

	GlyphAtlasCache(const GlyphAtlasCache&) = delete;
	GlyphAtlasCache& operator=(const GlyphAtlasCache&) = delete;
	GlyphAtlasCache(GlyphAtlasCache&&) noexcept = delete;
	GlyphAtlasCache& operator=(GlyphAtlasCache&&) noexcept = delete;

	void SetFaceSet(FaceSetPtr faceSet);
	const FaceSetPtr& GetFaceSet() const noexcept { return faces; }
	bool HasFaceSet() const noexcept { return static_cast<bool>(faces); }

	int GetFontSize() const noexcept { return fontSize; }
	void SetFontSize(int size) noexcept { fontSize = size; }

	int GetOutlineWidth() const noexcept { return outlineSize; }
	void SetOutlineWidth(int width) noexcept { outlineSize = width; }

	float GetOutlineWeight() const noexcept { return outlineWeight; }
	void SetOutlineWeight(float weight) noexcept { outlineWeight = weight; }

	float GetLineHeight() const noexcept { return lineHeight; }
	float GetDescender() const noexcept { return fontDescender; }
	float GetNormScale() const noexcept { return normScale; }
	bool HasColorGlyphs() const noexcept { return needsColor; }
	bool IsColorFont() const noexcept { return isColor; }

	const GlyphInfo& GetGlyphByCodepoint(char32_t codepoint);
	const GlyphInfo& GetGlyphByGlyphIndex(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint = 0);
	const GlyphInfo* FindGlyphByCodepoint(char32_t codepoint) const;
	const GlyphInfo* FindGlyphByGlyphIndex(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint = 0) const;

	void EnsureGlyphs(std::span<const char32_t> codepoints);
	void EnsureGlyphs(char32_t beginInclusive, char32_t endInclusive);
	void EnsureGlyphIndices(std::span<const std::uint32_t> glyphIndices, const FacePtr& face, char32_t sourceCodepoint = 0);
	void EnsureGlyphKeys(std::span<const GlyphKey> glyphKeys, const FacePtr& defaultFace = {});

	bool ClearGlyphs();
	void PreloadGlyphs();
	void ReallocAtlases(bool preserveContents);

	GlyphAtlasTexture& GetAtlasTexture() noexcept { return atlasTexture; }
	const GlyphAtlasTexture& GetAtlasTexture() const noexcept { return atlasTexture; }
	GlyphAtlasTexture& GetShadowAtlasTexture() noexcept { return shadowAtlasTexture; }
	const GlyphAtlasTexture& GetShadowAtlasTexture() const noexcept { return shadowAtlasTexture; }

	bool NeedsAtlasUpdate() const noexcept;
	bool NeedsAtlasUpload() const noexcept;
	void UpdateAtlases();
	void UploadAtlases();

	float GetKerning(const GlyphInfo& leftGlyph, const GlyphInfo& rightGlyph);

	const auto& GetGlyphsByCodepoint() const noexcept { return glyphsByCodepoint; }
	const auto& GetGlyphsByGlyphIndex() const noexcept { return glyphsByGlyphIndex; }

private:
	static GlyphIndexKey MakeGlyphIndexKey(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint = 0) noexcept;

	void ClearAtlasState();
	void ResetKerningCaches();
	void ConfigureFaceMetrics();

	void LoadGlyphByCodepoint(char32_t codepoint);
	void LoadGlyphByIndex(const FacePtr& face, char32_t sourceCodepoint, std::uint32_t glyphIndex);
	void CacheGlyphRecord(char32_t codepoint, GlyphInfo glyph);
	void CacheGlyphRecord(const GlyphIndexKey& key, GlyphInfo glyph);

	void QueueGlyphBitmap(const std::string& cacheKey, const CBitmap& bitmap, const CBitmap* outlineBitmap = nullptr);
	void PackPendingGlyphs();
	void UpdateAtlasRegions();
	void UploadAtlasTextures();

private:
	static inline const GlyphInfo dummyGlyph = {};

	FaceSetPtr faces;

	int fontSize = 0;
	int outlineSize = 0;
	float outlineWeight = 0.0f;
	float lineHeight = 0.0f;
	float fontDescender = 0.0f;
	float normScale = 1.0f;
	bool needsColor = false;
	bool isColor = false;

	std::array<float, 128 * 128> kerningPrecached = {};
	std::unordered_map<std::uint64_t, float> kerningDynamic;

	std::unordered_map<char32_t, int> failedAttemptsToReplace;
	std::unordered_map<char32_t, GlyphInfo> glyphsByCodepoint;
	std::unordered_map<GlyphIndexKey, GlyphInfo, GlyphIndexKeyHasher> glyphsByGlyphIndex;

	GlyphAtlasTexture atlasTexture;
	GlyphAtlasTexture shadowAtlasTexture;

	std::unique_ptr<CRowAtlasAlloc> atlasAllocator;
	std::vector<CBitmap> pendingGlyphBitmaps;
	std::vector<SRectangle> blurRectangles;
	std::unordered_map<std::string, std::size_t> glyphNameToBitmapIndex;
};

