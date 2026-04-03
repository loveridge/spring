/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FontFace.h"

namespace fonts {

/**
 * Ordered collection of faces used to resolve glyph coverage.
 *
 * The first entry is the primary face for a font descriptor. Remaining entries
 * are fallback faces consulted in insertion order when the primary face does
 * not provide a requested codepoint. Glyph indices remain face-local and must
 * be resolved against a known face rather than searched across the set.
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
	explicit FontFaceSet(FacePtr primaryFace);
	FontFaceSet(FacePtr primaryFace, FaceList fallbackFaces);

	bool Empty() const noexcept;
	explicit operator bool() const noexcept;

	void Clear() noexcept;
	void SetPrimaryFace(FacePtr face);

	const FacePtr& GetPrimaryFace() const noexcept;
	FacePtr& GetPrimaryFace() noexcept;

	void AddFallbackFace(FacePtr face);
	void SetFallbackFaces(FaceList faces);

	const FaceList& GetFallbackFaces() const noexcept;
	FaceList GetFaces() const;

	std::size_t GetFaceCount() const noexcept;
	bool ContainsFace(const FacePtr& face) const noexcept;

	/**
	 * Returns the first face in the ordered set that can render the given
	 * Unicode codepoint.
	 */
	FacePtr FindFaceForCodepoint(char32_t codepoint) const;

	/**
	 * Returns the glyph index in the first supporting face for a codepoint.
	 * When no face supports the codepoint, returns {nullptr, 0}.
	 */
	std::pair<FacePtr, std::uint32_t> FindGlyphForCodepoint(char32_t codepoint) const;

	/**
	 * Optional glyph-name lookup.
	 *
	 * This is retained as a compatibility helper because the current atlas path
	 * still derives string names from glyph identifiers. It remains a linear
	 * scan over the face's glyph table, so it should stay a compatibility helper
	 * rather than a central API.
	 */
	FacePtr FindFaceForGlyphName(const std::string& glyphName) const;

	bool SupportsCodepoint(char32_t codepoint) const;

	static bool SupportsCodepoint(const FacePtr& face, char32_t codepoint);
	static bool SupportsGlyphName(const FacePtr& face, const std::string& glyphName);

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

} // namespace fonts
