/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <memory>

#include "../FontTypes.h"

namespace font {
class FontFace;
}

/**
 * Cached glyph record shared between glyph lookup, layout, and render prep.
 *
 * Intent:
 *  - data-only definition
 *  - no FreeType includes here
 *  - no atlas allocator / renderer / wrapping dependencies here
 *
 * Notes:
 *  - atlas rectangles are stored separately for the primary glyph image and the
 *    optional shadow/outline image, mirroring current cache behavior
 *  - sourceCodepoint is retained for legacy Unicode-driven lookup, while
 *    glyphIndex is the stable face-local identifier used by shaping/raster
 *  - face is the owning face reference that produced this cached glyph
 */
struct GlyphInfo {
	GlyphRect bitmapBounds;      //!< Glyph bitmap quad bounds in font-local space.
	GlyphRect atlasUV;           //!< Atlas coordinates/UV rect for the primary glyph.
	GlyphRect shadowAtlasUV;     //!< Atlas coordinates/UV rect for shadow/outline pass.

	float advance   = 0.0f;      //!< Horizontal advance in font-local units.
	float descender = 0.0f;      //!< Glyph descender in font-local units.
	float height    = 0.0f;      //!< Glyph height in font-local units.

	std::uint32_t glyphIndex   = 0; //!< FT/HarfBuzz glyph index within the owning face.
	char32_t sourceCodepoint   = 0; //!< Unicode source codepoint, if applicable.

	std::shared_ptr<font::FontFace> face; //!< Face that owns glyphIndex and metrics.

	constexpr bool HasGlyphIndex() const noexcept { return glyphIndex != 0; }
	constexpr bool HasSourceCodepoint() const noexcept { return sourceCodepoint != 0; }
	constexpr bool HasBitmap() const noexcept { return !bitmapBounds.Empty(); }
	constexpr bool HasAtlasUV() const noexcept { return !atlasUV.Empty(); }
	constexpr bool HasShadowAtlasUV() const noexcept { return !shadowAtlasUV.Empty(); }
	bool HasFace() const noexcept { return static_cast<bool>(face); }

	GlyphMetrics GetMetrics() const noexcept
	{
		GlyphMetrics metrics;
		metrics.bounds = bitmapBounds;
		metrics.advance = advance;
		metrics.height = height;
		metrics.descender = descender;
		return metrics;
	}

	GlyphKey GetGlyphKey() const noexcept
	{
		if (glyphIndex != 0)
			return GlyphKey::FromGlyphIndex(glyphIndex, sourceCodepoint);

		return GlyphKey::FromCodepoint(sourceCodepoint);
	}
};

