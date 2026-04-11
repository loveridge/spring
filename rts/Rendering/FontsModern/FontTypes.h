/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <string>
#include <utility>

/**
 * Shared low-level font data types.
 *
 * Intent:
 *  - keep this header dependency-light
 *  - avoid FreeType / HarfBuzz / GL includes here
 *  - provide small POD-style types that can be reused by face, glyph,
 *    shaping, layout, and renderer layers
 */

struct GlyphRect {
	constexpr GlyphRect() = default;
	constexpr GlyphRect(float px, float py, float pw, float ph)
		: x(px)
		, y(py)
		, w(pw)
		, h(ph)
	{}

	constexpr float x0() const { return x; }
	constexpr float y0() const { return y; }
	constexpr float x1() const { return x + w; }
	constexpr float y1() const { return y + h; }

	constexpr bool Empty() const { return (w <= 0.0f) || (h <= 0.0f); }

	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
};


/**
 * Basic vertical metrics shared across layout stages.
 *
 * All values are expected to be normalized to the font's internal logical
 * coordinate space, matching current CFontTexture/CglFont conventions.
 */
struct FontMetrics {
	float lineHeight = 0.0f;
	float descender  = 0.0f;
};


/**
 * Per-glyph metadata for Slug-style vector rendering.
 *
 * offsetX/offsetY locate the glyph's packed bbox in the shared glyph-local
 * coordinate space used by layout and renderer placement. width/height are the
 * bbox dimensions for the specific packed representation.
 */
struct SlugGlyphInfo {
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
	float bandScaleX = 0.0f;
	float bandScaleY = 0.0f;
	float bandOffsetX = 0.0f;
	float bandOffsetY = 0.0f;

	std::uint32_t bandTexelX = 0;
	std::uint32_t bandTexelY = 0;
	std::uint32_t bandMaxX = 0;
	std::uint32_t bandMaxY = 0;
	std::uint32_t flags = 0;

	constexpr bool Empty() const noexcept
	{
		return (width <= 0.0f) || (height <= 0.0f);
	}

	constexpr GlyphRect Bounds() const noexcept
	{
		return GlyphRect(offsetX, offsetY, width, height);
	}
};


/**
 * Per-glyph payload for the bitmap-atlas renderer backend.
 */
struct ShaderGlyphPayload {
	GlyphRect atlasUV;
	GlyphRect shadowAtlasUV;

	constexpr bool HasAtlasUV() const noexcept { return !atlasUV.Empty(); }
	constexpr bool HasShadowAtlasUV() const noexcept { return !shadowAtlasUV.Empty(); }
};


/**
 * Per-glyph payload for the Slug vector renderer backend.
 */
struct SlugGlyphPayload {
	SlugGlyphInfo fill;
	SlugGlyphInfo outline;

	constexpr bool HasFill() const noexcept { return !fill.Empty(); }
	constexpr bool HasOutline() const noexcept { return !outline.Empty(); }
};


/**
 * Per-glyph metrics independent from renderer/backend ownership.
 */
struct GlyphMetrics {
	GlyphRect bounds;
	float advance   = 0.0f;
	float height    = 0.0f;
	float descender = 0.0f;

	constexpr bool Empty() const { return bounds.Empty() && advance == 0.0f; }
};


/**
 * Stable identity for a loaded font instance.
 *
 * Mirrors the externally visible fields currently used to locate/reuse fonts:
 *  - file path
 *  - pixel size
 *  - outline width
 *  - outline weight
 */
struct FontDescriptor {
	std::string filePath;
	int pixelSize       = 0;
	int outlineSize     = 0;
	float outlineWeight = 0.0f;

	bool operator==(const FontDescriptor& other) const {
		return
			(filePath == other.filePath) &&
			(pixelSize == other.pixelSize) &&
			(outlineSize == other.outlineSize) &&
			(outlineWeight == other.outlineWeight);
	}

	bool operator!=(const FontDescriptor& other) const {
		return !(*this == other);
	}

	bool Empty() const {
		return filePath.empty() && pixelSize <= 0 && outlineSize <= 0 && outlineWeight == 0.0f;
	}
};


/**
 * Small POD used when text shaping addresses glyphs directly by glyph index,
 * while legacy callers still address them by Unicode codepoint.
 *
 * Glyph indices are face-local. When kind == GlyphIndex, the key must stay
 * paired with the originating FontFace; a FontFaceSet cannot resolve it across
 * fallback faces by glyph index alone.
 */
struct GlyphKey {
	enum class Kind : std::uint8_t {
		Codepoint = 0,
		GlyphIndex = 1,
	};

	Kind kind = Kind::Codepoint;
	char32_t codepoint = 0;
	std::uint32_t glyphIndex = 0;

	static constexpr GlyphKey FromCodepoint(char32_t cp) {
		GlyphKey key;
		key.kind = Kind::Codepoint;
		key.codepoint = cp;
		return key;
	}

	static constexpr GlyphKey FromGlyphIndex(std::uint32_t gi, char32_t sourceCodepoint = 0) {
		GlyphKey key;
		key.kind = Kind::GlyphIndex;
		key.codepoint = sourceCodepoint;
		key.glyphIndex = gi;
		return key;
	}

	constexpr bool IsCodepoint() const { return kind == Kind::Codepoint; }
	constexpr bool IsGlyphIndex() const { return kind == Kind::GlyphIndex; }

	bool operator==(const GlyphKey& other) const {
		return
			(kind == other.kind) &&
			(codepoint == other.codepoint) &&
			(glyphIndex == other.glyphIndex);
	}

	bool operator!=(const GlyphKey& other) const {
		return !(*this == other);
	}
};


/**
 * Optional small PODs for color/depth values that may be reused internally by
 * layout and render preparation without depending on engine math/color types.
 */
struct FontColor {
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

struct FontDepth {
	float text    = 0.0f;
	float outline = 0.0f;
};


/**
 * Compatibility alias for incremental migration away from IGlyphRect.
 *
 * New code should prefer GlyphRect directly.
 */
using IGlyphRect = GlyphRect;
