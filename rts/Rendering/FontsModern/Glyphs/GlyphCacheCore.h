/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>

#include "GlyphInfo.h"

namespace fonts {
class FontFace;
class FontFaceSet;

/**
 * Renderer-agnostic glyph cache core.
 *
 * Responsibilities:
 *  - resolve glyph identity by codepoint or by face-local glyph index
 *  - cache shared glyph metrics and fallback resolution results
 *  - expose font-level metrics and kerning
 *
 * Non-responsibilities:
 *  - bitmap atlas allocation or upload
 *  - SLUG curve/band generation or upload
 */
class GlyphCacheCore {
public:
	using FacePtr = std::shared_ptr<fonts::FontFace>;
	using FaceSetPtr = std::shared_ptr<fonts::FontFaceSet>;

	struct GlyphIndexKey {
		std::uintptr_t faceIdentity = 0;
		std::uint32_t glyphIndex = 0;
		char32_t sourceCodepoint = 0;

		friend bool operator==(const GlyphIndexKey&, const GlyphIndexKey&) = default;
	};

	struct GlyphIndexKeyHasher {
		std::size_t operator()(const GlyphIndexKey& key) const noexcept;
	};

public:
	GlyphCacheCore() = default;
	GlyphCacheCore(FaceSetPtr faceSet, int fontSize, int outlineSize = 0, float outlineWeight = 0.0f);
	~GlyphCacheCore() = default;

	GlyphCacheCore(const GlyphCacheCore&) = delete;
	GlyphCacheCore& operator=(const GlyphCacheCore&) = delete;
	GlyphCacheCore(GlyphCacheCore&&) noexcept = delete;
	GlyphCacheCore& operator=(GlyphCacheCore&&) noexcept = delete;

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
	float GetAscender() const noexcept { return fontAscender; }
	float GetDescender() const noexcept { return fontDescender; }
	float GetNormScale() const noexcept { return normScale; }

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

	float GetKerning(const GlyphInfo& leftGlyph, const GlyphInfo& rightGlyph);

	const auto& GetGlyphsByCodepoint() const noexcept { return glyphsByCodepoint; }
	const auto& GetGlyphsByGlyphIndex() const noexcept { return glyphsByGlyphIndex; }

	static GlyphIndexKey MakeGlyphIndexKey(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint = 0) noexcept;
	static GlyphIndexKey MakeGlyphIndexKey(const GlyphInfo& glyph) noexcept;

private:
	const GlyphInfo* FindResolvedGlyphByCodepoint(char32_t codepoint) const;
	const GlyphInfo& GetResolvedGlyphByCodepoint(char32_t codepoint);
	std::pair<FacePtr, std::uint32_t> ResolveGlyphForCodepoint(char32_t codepoint, char32_t* resolvedCodepoint = nullptr) const;
	void ResetKerningCaches();
	void ConfigureFaceMetrics();

	void LoadGlyphByCodepoint(char32_t codepoint);
	void LoadGlyphByIndex(const FacePtr& face, char32_t sourceCodepoint, std::uint32_t glyphIndex);
	void CacheGlyphRecord(char32_t codepoint, GlyphInfo glyph);
	void CacheGlyphRecord(const GlyphIndexKey& key, GlyphInfo glyph);

private:
	static inline const GlyphInfo dummyGlyph = {};

	FaceSetPtr faces;

	int fontSize = 0;
	int outlineSize = 0;
	float outlineWeight = 0.0f;
	float lineHeight = 0.0f;
	float fontAscender = 0.0f;
	float fontDescender = 0.0f;
	float normScale = 1.0f;

	std::array<float, 128 * 128> kerningPrecached = {};
	std::unordered_map<std::uint64_t, float> kerningDynamic;

	std::unordered_map<char32_t, int> failedAttemptsToReplace;
	std::unordered_map<char32_t, GlyphInfo> glyphsByCodepoint;
	std::unordered_map<GlyphIndexKey, GlyphInfo, GlyphIndexKeyHasher> glyphsByGlyphIndex;
};

} // namespace fonts
