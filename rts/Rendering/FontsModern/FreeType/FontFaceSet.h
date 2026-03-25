/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../FontTypes.h"
#include "FontFace.h"

namespace font {

/**
 * Ordered collection of faces used to resolve glyph coverage.
 *
 * The first entry is the primary face for a font descriptor. Remaining entries
 * are fallback faces consulted in insertion order when the primary face does
 * not provide a requested codepoint or glyph.
 *
 * This type is deliberately narrow: it knows about FreeType faces and glyph
 * lookup, but not about glyph atlases, rendering, shaping policy, or wrapping.
 */
class FontFaceSet {
public:
	using FacePtr = std::shared_ptr<FontFace>;
	using FaceList = std::vector<FacePtr>;

public:
	FontFaceSet() = default;
	explicit FontFaceSet(FacePtr primaryFace)
		: primary(std::move(primaryFace))
	{}

	FontFaceSet(FacePtr primaryFace, FaceList fallbackFaces)
		: primary(std::move(primaryFace))
		, fallbacks(std::move(fallbackFaces))
	{}

	bool Empty() const noexcept
	{
		return (primary == nullptr) && fallbacks.empty();
	}

	explicit operator bool() const noexcept
	{
		return !Empty();
	}

	void Clear() noexcept
	{
		primary.reset();
		fallbacks.clear();
	}

	void SetPrimaryFace(FacePtr face)
	{
		primary = std::move(face);
	}

	const FacePtr& GetPrimaryFace() const noexcept
	{
		return primary;
	}

	FacePtr& GetPrimaryFace() noexcept
	{
		return primary;
	}

	void AddFallbackFace(FacePtr face)
	{
		if (face == nullptr)
			return;

		if (ContainsFace(face))
			return;

		fallbacks.emplace_back(std::move(face));
	}

	void SetFallbackFaces(FaceList faces)
	{
		fallbacks.clear();
		fallbacks.reserve(faces.size());

		for (auto& face : faces) {
			AddFallbackFace(std::move(face));
		}
	}

	const FaceList& GetFallbackFaces() const noexcept
	{
		return fallbacks;
	}

	FaceList GetFaces() const
	{
		FaceList result;
		result.reserve((primary != nullptr ? 1u : 0u) + fallbacks.size());

		if (primary != nullptr)
			result.emplace_back(primary);

		result.insert(result.end(), fallbacks.begin(), fallbacks.end());
		return result;
	}

	std::size_t GetFaceCount() const noexcept
	{
		return (primary != nullptr ? 1u : 0u) + fallbacks.size();
	}

	bool ContainsFace(const FacePtr& face) const noexcept
	{
		if (face == nullptr)
			return false;

		if (primary == face)
			return true;

		return std::find(fallbacks.begin(), fallbacks.end(), face) != fallbacks.end();
	}

	/**
	 * Returns the first face in the ordered set that can render the given
	 * Unicode codepoint.
	 */
	FacePtr FindFaceForCodepoint(char32_t codepoint) const
	{
		return FindFaceIf([codepoint](const FacePtr& face) {
			return SupportsCodepoint(face, codepoint);
		});
	}

	/**
	 * Returns the first face in the ordered set that provides the given glyph
	 * index. This is the direct glyph-addressing path needed by HarfBuzz-driven
	 * shaping, where lookup is by glyph ID rather than Unicode codepoint.
	 */
	FacePtr FindFaceForGlyph(std::uint32_t glyphIndex) const
	{
		return FindFaceIf([glyphIndex](const FacePtr& face) {
			return SupportsGlyphIndex(face, glyphIndex);
		});
	}

	/**
	 * Resolves either a codepoint-addressed or glyph-index-addressed lookup.
	 */
	FacePtr FindFaceForGlyphKey(const GlyphKey& key) const
	{
		return key.IsGlyphIndex() ? FindFaceForGlyph(key.glyphIndex)
		                        : FindFaceForCodepoint(key.codepoint);
	}

	/**
	 * Returns the glyph index in the first supporting face for a codepoint.
	 * When no face supports the codepoint, returns {nullptr, 0}.
	 */
	std::pair<FacePtr, std::uint32_t> FindGlyphForCodepoint(char32_t codepoint) const
	{
		FacePtr face = FindFaceForCodepoint(codepoint);
		if (face == nullptr)
			return {nullptr, 0u};

		return {face, face->GetCharIndex(codepoint)};
	}

	/**
	 * Optional glyph-name lookup.
	 *
	 * This is retained as a compatibility helper because the current atlas path
	 * still derives string names from glyph identifiers. It should remain an
	 * edge helper rather than a central API.
	 */
	FacePtr FindFaceForGlyphName(const std::string& glyphName) const
	{
		if (glyphName.empty())
			return nullptr;

		return FindFaceIf([&glyphName](const FacePtr& face) {
			return SupportsGlyphName(face, glyphName);
		});
	}

	bool SupportsCodepoint(char32_t codepoint) const
	{
		return FindFaceForCodepoint(codepoint) != nullptr;
	}

	bool SupportsGlyph(std::uint32_t glyphIndex) const
	{
		return FindFaceForGlyph(glyphIndex) != nullptr;
	}

	static bool SupportsCodepoint(const FacePtr& face, char32_t codepoint)
	{
		return (face != nullptr) && (face->GetCharIndex(codepoint) != 0u);
	}

	static bool SupportsGlyphIndex(const FacePtr& face, std::uint32_t glyphIndex)
	{
		return (face != nullptr) && (glyphIndex > 0u) && (static_cast<long>(glyphIndex) < face->GetNumGlyphs());
	}

	static bool SupportsGlyphName(const FacePtr& face, const std::string& glyphName)
	{
		if (face == nullptr || glyphName.empty())
			return false;

#if defined(FT_HAS_GLYPH_NAMES)
		FT_Face ftFace = face->GetFTFace();
		if (ftFace == nullptr || !FT_HAS_GLYPH_NAMES(ftFace))
			return false;

		char buffer[128] = {0};
		for (FT_UInt glyphIndex = 0; glyphIndex < static_cast<FT_UInt>(face->GetNumGlyphs()); ++glyphIndex) {
			buffer[0] = '\0';
			if (FT_Get_Glyph_Name(ftFace, glyphIndex, buffer, sizeof(buffer)) != 0)
				continue;
			if (glyphName == buffer)
				return true;
		}
#endif
		return false;
	}

private:
	template <typename Predicate>
	FacePtr FindFaceIf(Predicate&& predicate) const
	{
		if (primary != nullptr && predicate(primary))
			return primary;

		const auto it = std::find_if(fallbacks.begin(), fallbacks.end(), std::forward<Predicate>(predicate));
		return (it != fallbacks.end()) ? *it : FacePtr{};
	}

private:
	FacePtr primary;
	FaceList fallbacks;
};

} // namespace font
