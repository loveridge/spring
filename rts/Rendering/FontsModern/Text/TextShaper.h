/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "TextRun.h"

namespace spring::font::text {

/**
 * Lightweight shaping hints passed to a TextShaper implementation.
 *
 * The interface deliberately uses simple string views and small enums so the
 * public shaping boundary does not need to expose HarfBuzz headers or types.
 */
struct ShapeOptions {
	enum class Direction : std::uint8_t {
		Auto = 0,
		LeftToRight,
		RightToLeft,
		TopToBottom,
		BottomToTop,
	};

	std::string_view language;
	std::string_view script;
	Direction direction = Direction::Auto;
	bool allowLigatures = true;
	bool allowKerning = true;
	bool preserveClusters = true;
};


/**
 * Aggregate result of shaping a UTF-8 span.
 *
 * A shaper may return multiple runs when script, direction, face, or shaping
 * properties change within the input span.
 */
struct ShapeResult {
	std::vector<ShapedRun> runs;
	float width = 0.0f;
	float ascent = 0.0f;
	float descent = 0.0f;
	float lineHeight = 0.0f;
	bool hadMissingGlyphs = false;
	bool usedFallbackFaces = false;

	bool Empty() const noexcept { return runs.empty(); }
};


/**
 * Generic text shaping interface.
 *
 * Implementations translate UTF-8 text into one or more shaped runs without
 * exposing their backend (for example HarfBuzz) through this header.
 */
class TextShaper {
public:
	virtual ~TextShaper() = default;

	/**
	 * Shapes a UTF-8 span using default or implementation-defined shaping hints.
	 */
	virtual ShapeResult ShapeUtf8Span(std::string_view utf8) const = 0;

	/**
	 * Shapes a UTF-8 span using explicit language/script/direction hints.
	 */
	virtual ShapeResult ShapeUtf8Span(std::string_view utf8, const ShapeOptions& options) const = 0;

	/**
	 * Shapes an already parsed source span.
	 *
	 * This overload lets the layout pipeline preserve source offsets and span
	 * metadata while still depending only on the abstract shaping interface.
	 */
	virtual ShapeResult ShapeSpan(const TextSpan& span) const {
		return ShapeUtf8Span(span.text);
	}

	/**
	 * Shapes an already parsed source span with explicit shaping hints.
	 */
	virtual ShapeResult ShapeSpan(const TextSpan& span, const ShapeOptions& options) const {
		return ShapeUtf8Span(span.text, options);
	}
};

using TextShaperPtr = std::shared_ptr<TextShaper>;

} // namespace font::text
