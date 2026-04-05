/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../FontOptions.h"
#include "../FontTypes.h"
#include "TextControlCodes.h"
#include "TextRun.h"
#include "TextShaper.h"

namespace fonts {
class GlyphAtlasCache;
}

namespace fonts::text {

/**
 * High-level layout configuration.
 *
 * This coordinates parsing, shaping, glyph-cache realization, line-breaking,
 * alignment, and measurement without owning any GL renderer state.
 */
struct LayoutOptions {
	FontOption options = FontOption::Left | FontOption::Baseline;
	ShapeOptions shapeOptions{};

	float fontSize = 0.0f;          // absolute size or scale depending on options
	float maxWidth = 0.0f;          // <= 0 means unbounded
	float maxHeight = 0.0f;         // <= 0 means unbounded
	float lineSpacing = 0.0f;       // extra spacing added between laid out lines
	float tabWidth = 4.0f;          // in logical space / implementation-defined units
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	bool enableWrapping = true;
	bool preserveLineBreaks = true;
	bool parseColorCodes = true;
	bool expandTabs = true;
	bool trimTrailingWhitespace = false;
	bool ellipsize = false;
	bool justify = false;
	bool preloadGlyphs = true;

	constexpr bool HasWidthConstraint() const noexcept { return maxWidth > 0.0f; }
	constexpr bool HasHeightConstraint() const noexcept { return maxHeight > 0.0f; }
};


/**
 * Aggregate measurement result for a text block.
 */
struct TextMeasurement {
	float width = 0.0f;
	float height = 0.0f;
	float ascent = 0.0f;
	float descent = 0.0f;
	float lineHeight = 0.0f;
	std::size_t lineCount = 0;
	std::size_t glyphCount = 0;
	std::size_t printableCodepointCount = 0;
	bool hadColorCodes = false;
	bool hadExplicitLineBreaks = false;
	bool wasWrapped = false;
	bool wasEllipsized = false;
};


/**
 * Full layout output consumed by measurement and rendering stages.
 */
struct TextLayout {
	std::vector<TextSpan> spans;
	std::vector<LaidOutLine> lines;
	TextMeasurement measurement{};
	LayoutOptions options{};

	std::size_t sourceLength = 0;
	std::size_t printableLength = 0; // logical UTF-8 codepoint count, kept for compatibility with existing callers
	bool valid = false;

	bool Empty() const noexcept { return lines.empty(); }
};


/**
 * Measured wrapping fragment derived from shaped text.
 *
 * Offsets remain in the caller's logical UTF-8 source stream. Break
 * opportunities carry source/codepoint offsets and widths measured from the
 * start of this fragment.
 */
struct MeasuredBreakFragment {
	TextSpan sourceSpan{};
	std::string text;
	float width = 0.0f;
	bool isWhitespace = false;
	bool isLineBreak = false;
	bool isControlCode = false;
	std::vector<BreakOpportunity> breakOpportunities;
};


/**
 * Core coordination layer between shaping and rendering.
 *
 * Responsibilities:
 *  - parse UTF-8 input into printable spans while recognizing Spring control codes
 *  - split spans into explicit lines and optional wrap words/segments
 *  - shape printable spans through TextShaper
 *  - ensure shaped glyphs exist in GlyphAtlasCache
 *  - compute text measurements and aligned line positions
 *  - emit positioned glyphs for render backends
 *
 * Non-responsibilities:
 *  - GL state ownership or draw submission
 *  - glyph raster cache ownership policy beyond using GlyphAtlasCache
 */
class TextLayouter {
public:
	using GlyphCachePtr = std::shared_ptr<GlyphAtlasCache>;

public:
	TextLayouter() = default;
	TextLayouter(TextShaperPtr shaper, GlyphCachePtr glyphCache);
	virtual ~TextLayouter() = default;

	TextLayouter(const TextLayouter&) = delete;
	TextLayouter& operator=(const TextLayouter&) = delete;
	TextLayouter(TextLayouter&&) noexcept = default;
	TextLayouter& operator=(TextLayouter&&) noexcept = default;

	void SetShaper(TextShaperPtr shaper) { textShaper = std::move(shaper); }
	const TextShaperPtr& GetShaper() const noexcept { return textShaper; }
	bool HasShaper() const noexcept { return static_cast<bool>(textShaper); }

	void SetGlyphCache(GlyphCachePtr cache) { glyphCache = std::move(cache); }
	const GlyphCachePtr& GetGlyphCache() const noexcept { return glyphCache; }
	bool HasGlyphCache() const noexcept { return static_cast<bool>(glyphCache); }

	TextMeasurement MeasureText(std::string_view utf8, const LayoutOptions& options = {}) const;
	TextLayout LayoutText(std::string_view utf8, const LayoutOptions& options = {}) const;
	std::vector<MeasuredBreakFragment> AnalyzeWrapText(std::string_view utf8, const LayoutOptions& options = {}) const;
	std::vector<MeasuredBreakFragment> AnalyzeWrapSpans(std::span<const TextSpan> spans, const LayoutOptions& options = {}) const;

	std::vector<TextSpan> ParseTextSpans(std::string_view utf8, bool parseColorCodes = true) const;
	std::vector<TextSpan> SplitIntoLines(std::string_view utf8, bool parseColorCodes = true) const;
	std::vector<TextSpan> SplitIntoLines(std::span<const TextSpan> spans) const;

	LaidOutLine LayoutSingleLine(std::span<const TextSpan> lineSpans, const LayoutOptions& options = {}) const;
	TextMeasurement MeasureLaidOutLines(std::span<const LaidOutLine> lines, const LayoutOptions& options = {}) const;

protected:
	struct ParsedSpanBuffer {
		std::vector<TextSpan> spans;
		bool hadColorCodes = false;
		bool hadExplicitLineBreaks = false;
		std::size_t printableLength = 0;
	};

	struct LineSpanRange {
		std::size_t begin = 0;
		std::size_t end = 0;
		bool endsWithExplicitBreak = false;
		bool Empty() const noexcept { return begin >= end; }
	};

	struct LayoutContext {
		LayoutOptions options{};
		float defaultLineHeight = 0.0f;
		float defaultAscender = 0.0f;
		float defaultDescender = 0.0f;
		bool hadMissingGlyphs = false;
	};

protected:
	ParsedSpanBuffer ParseTextSpansInternal(std::string_view utf8, bool parseColorCodes) const;
	std::vector<LineSpanRange> BuildLineRanges(std::span<const TextSpan> spans) const;

	std::vector<ShapedRun> ShapeLineSpans(std::span<const TextSpan> lineSpans, const LayoutOptions& options) const;
	void EnsureGlyphsForRuns(std::span<const ShapedRun> runs) const;
	LaidOutLine PositionRuns(std::span<const ShapedRun> runs, const LayoutContext& ctx, std::size_t sourceOffset, std::size_t sourceLength, bool explicitBreak) const;

	float ComputeHorizontalLineOffset(float lineWidth, const LayoutOptions& options) const;
	float ComputeVerticalBlockOffset(const TextMeasurement& measurement, const LayoutOptions& options, float fontAscender, float fontDescender) const;
	void ApplyHorizontalAlignment(std::vector<LaidOutLine>& lines, const LayoutOptions& options) const;
	void ApplyVerticalAlignment(std::vector<LaidOutLine>& lines, const TextMeasurement& measurement, const LayoutOptions& options, float fontAscender, float fontDescender) const;

	TextMeasurement BuildMeasurement(std::span<const LaidOutLine> lines, const ParsedSpanBuffer& parsed, const LayoutOptions& options) const;

	static bool IsRenderableSpan(const TextSpan& span) noexcept {
		return !span.isControl && !span.isLineBreak && !span.Empty();
	}

private:
	TextShaperPtr textShaper;
	GlyphCachePtr glyphCache;
};

} // namespace fonts::text
