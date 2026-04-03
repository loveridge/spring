/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "../FontTypes.h"

namespace fonts { class FontFace; }

namespace fonts::text {

/**
 * A source span in the original text stream.
 *
 * Offsets are byte offsets into the original UTF-8 text. The span may include
 * printable text, whitespace, line breaks, or inline control codes depending on
 * the producer stage.
 */
struct TextSpan {
	std::string_view text;
	std::size_t sourceOffset = 0;
	std::size_t sourceLength = 0;
	std::size_t printableOffset = 0;  // logical UTF-8 codepoint offset, not a grapheme/cluster index
	std::size_t printableLength = 0;  // logical UTF-8 codepoint length, not a grapheme/cluster length
	bool isControl = false;
	bool isWhitespace = false;
	bool isLineBreak = false;
	bool isParagraphBreak = false;

	bool Empty() const noexcept { return text.empty() || sourceLength == 0; }
};


/**
 * One shaped glyph before layout positioning is applied.
 *
 * This is the common output of a shaping stage such as HarfBuzz, but it also
 * works for legacy one-codepoint-to-one-glyph paths.
 */
struct ShapedGlyph {
	GlyphKey glyphKey{};
	std::uint32_t cluster = 0;          // UTF-8 byte index / shaping cluster in source text
	std::uint32_t logicalIndex = 0;     // visual/logical glyph position inside the run
	char32_t sourceCodepoint = 0;

	float xAdvance = 0.0f;
	float yAdvance = 0.0f;
	float xOffset = 0.0f;
	float yOffset = 0.0f;

	GlyphMetrics metrics{};
	std::shared_ptr<fonts::FontFace> face;

	bool IsRenderable() const noexcept {
		return (glyphKey.IsGlyphIndex() && glyphKey.glyphIndex != 0) ||
		       (glyphKey.IsCodepoint() && glyphKey.codepoint != 0);
	}
};


/**
 * Logical shaping cluster metadata for a shaped run.
 *
 * Source offsets remain in UTF-8 bytes so callers can map back into the
 * original string without re-decoding. Printable offsets remain logical
 * codepoint counts for compatibility with existing layout and wrapping code.
 */
struct ShapedCluster {
	std::size_t sourceByteStart = 0;
	std::size_t sourceByteLength = 0;
	std::size_t glyphStart = 0;
	std::size_t glyphCount = 0;
	std::size_t codepointOffset = 0;
	std::size_t codepointLength = 0;
	bool hasMissingGlyph = false;
	bool unsafeToBreakInside = true;
};


/**
 * One logical break boundary within a shaped run.
 */
struct BreakOpportunity {
	std::size_t sourceByteOffset = 0;
	std::size_t codepointOffset = 0;
	float xAdvance = 0.0f;
	bool required = false;
	bool allowed = false;
};


/**
 * A shaped run with uniform shaping/font properties.
 */
struct ShapedRun {
	TextSpan sourceSpan{};
	std::vector<ShapedGlyph> glyphs;
	std::vector<ShapedCluster> clusters;
	std::vector<BreakOpportunity> breakOpportunities;
	std::shared_ptr<fonts::FontFace> primaryFace;

	float width = 0.0f;
	float ascent = 0.0f;
	float descent = 0.0f;
	float lineHeight = 0.0f;

	bool isRtl = false;
	bool hadMissingGlyphs = false;
	bool endsWithLineBreak = false;
	bool containsControlCodes = false;

	bool Empty() const noexcept { return glyphs.empty(); }
};


/**
 * A glyph after line layout and final positioning.
 *
 * Renderer-facing code should consume this or a derived quad representation,
 * rather than re-running shaping or width logic.
 */
struct LaidOutGlyph {
	ShapedGlyph shaped{};
	GlyphRect atlasUV{};
	GlyphRect outlineAtlasUV{};
	SlugGlyphInfo slugFillInfo{};
	SlugGlyphInfo slugOutlineInfo{};

	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	float penX = 0.0f;
	float penY = 0.0f;
	float lineOffsetX = 0.0f;
	float lineOffsetY = 0.0f;

	bool visible = true;
	bool usesOutline = false;
	bool usesShadow = false;
};


/**
 * A laid-out run positioned within a line.
 */
struct LaidOutRun {
	TextSpan sourceSpan{};
	std::vector<LaidOutGlyph> glyphs;

	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float ascent = 0.0f;
	float descent = 0.0f;

	bool isRtl = false;
	bool containsControlCodes = false;

	bool Empty() const noexcept { return glyphs.empty(); }
};


/**
 * One fully laid out line.
 */
struct LaidOutLine {
	std::vector<LaidOutRun> runs;

	float width = 0.0f;
	float height = 0.0f;
	float ascent = 0.0f;
	float descent = 0.0f;
	float baselineY = 0.0f;
	float originX = 0.0f;
	float originY = 0.0f;

	std::size_t sourceOffset = 0;
	std::size_t sourceLength = 0;

	bool endsWithExplicitBreak = false;
	bool wasWrapped = false;
	bool wasEllipsized = false;

	bool Empty() const noexcept { return runs.empty(); }
};


/**
 * Optional wrapping primitives shared between wrapper and layouter.
 *
 * These replace the old ad hoc word/line structs in TextWrap with common,
 * dependency-light data types that can be consumed by measurement, wrapping,
 * and final layout code.
 */
struct WrapWord {
	TextSpan sourceSpan{};
	float width = 0.0f;
	std::uint32_t numSpaces = 0;
	bool isWhitespace = false;
	bool isLineBreak = false;
	bool isControlCode = false;
	bool isHyphenationPoint = false;
};

struct WrapLine {
	std::size_t firstWord = 0;
	std::size_t lastWord = 0;
	float width = 0.0f;
	float cost = 0.0f;
	bool forceLineBreak = false;
	bool wasWrapped = false;
	bool wasEllipsized = false;
};

} // namespace font::text
