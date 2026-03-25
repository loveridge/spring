/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TextControlCodes.h"
#include "TextLayouter.h"
#include "TextRun.h"

namespace font::text {

/**
 * Wrapping / truncation policy for a text block.
 *
 * This is intentionally separate from glyph-cache and renderer concerns.
 * The wrapper consumes text plus measurement/layout services and produces
 * wrapped UTF-8 text while preserving Spring inline color codes.
 */
struct WrapOptions {
	float fontSize = 0.0f;
	float maxWidth = 0.0f;
	float maxHeight = 1000.0f;
	float lineSpacing = 0.0f;

	bool splitWords = true;
	bool smartWordSplit = true;
	bool preserveLineBreaks = true;
	bool parseColorCodes = true;
	bool remergeColorCodes = true;
	bool trimTrailingWhitespace = false;
	bool ellipsize = true;
	bool consoleStyle = true;
	bool expandTabs = true;

	LayoutOptions ToLayoutOptions() const
	{
		LayoutOptions layout;
		layout.fontSize = fontSize;
		layout.maxWidth = maxWidth;
		layout.maxHeight = maxHeight;
		layout.lineSpacing = lineSpacing;
		layout.enableWrapping = false; // wrapper controls wrapping explicitly
		layout.preserveLineBreaks = preserveLineBreaks;
		layout.parseColorCodes = parseColorCodes;
		layout.expandTabs = expandTabs;
		layout.trimTrailingWhitespace = trimTrailingWhitespace;
		layout.ellipsize = false; // wrapper owns ellipsis insertion
		return layout;
	}

	constexpr bool HasWidthConstraint() const noexcept { return maxWidth > 0.0f; }
	constexpr bool HasHeightConstraint() const noexcept { return maxHeight > 0.0f; }
};


/**
 * One extracted inline color control sequence and its printable-text position.
 *
 * The position is expressed in printable characters / codepoints rather than
 * raw byte offsets, matching the old TextWrap behavior used for color-code
 * remerging after wrapping.
 */
struct ExtractedColorCode {
	ColorCodeText colorText{};
	std::size_t printablePosition = 0;
};


/**
 * The wrapping token unit.
 *
 * This extends the generic WrapWord with enough source/printable metadata to
 * support split-word wrapping and color-code remerge without exposing glyph
 * cache internals.
 */
struct WrappedWord {
	WrapWord word{};
	std::string text;
	std::size_t sourceByteOffset = 0;
	std::size_t sourceByteLength = 0;
	std::size_t printableOffset = 0;
	std::size_t printableLength = 0;

	bool Empty() const noexcept { return text.empty() && printableLength == 0; }
};


/**
 * Final wrapper output.
 */
struct WrappedText {
	std::string text;
	std::vector<WrappedWord> words;
	std::vector<ExtractedColorCode> colorCodes;
	std::size_t lineCount = 0;
	std::size_t printableLength = 0;
	bool wasWrapped = false;
	bool wasEllipsized = false;
	bool exceededHeight = false;

	bool Empty() const noexcept { return text.empty(); }
};


/**
 * Narrow measuring interface used by the wrapper.
 *
 * TextWrapper depends on TextLayouter or any compatible implementation of this
 * interface, but never on glyph caches directly.
 */
class ITextMeasurer {
public:
	virtual ~ITextMeasurer() = default;

	virtual TextMeasurement MeasureText(std::string_view utf8, const LayoutOptions& options) const = 0;
	virtual std::vector<TextSpan> ParseTextSpans(std::string_view utf8, bool parseColorCodes) const = 0;
};


/**
 * Adapter for using TextLayouter as the wrapper's measurement service.
 */
class TextLayouterMeasurer final : public ITextMeasurer {
public:
	explicit TextLayouterMeasurer(std::shared_ptr<TextLayouter> layouter_)
		: layouter(std::move(layouter_))
	{}

	explicit TextLayouterMeasurer(const TextLayouter* layouter_)
		: layouterRaw(layouter_)
	{}

	TextMeasurement MeasureText(std::string_view utf8, const LayoutOptions& options) const override
	{
		return GetLayouter().MeasureText(utf8, options);
	}

	std::vector<TextSpan> ParseTextSpans(std::string_view utf8, bool parseColorCodes) const override
	{
		return GetLayouter().ParseTextSpans(utf8, parseColorCodes);
	}

	const TextLayouter& GetLayouter() const
	{
		if (layouter)
			return *layouter;
		return *layouterRaw;
	}

private:
	std::shared_ptr<TextLayouter> layouter;
	const TextLayouter* layouterRaw = nullptr;
};


/**
 * Pure wrapping / ellipsis algorithm service.
 *
 * Responsibilities:
 *  - split input into wrapping tokens (words, spaces, line breaks, control codes)
 *  - wrap to max width / max height
 *  - split oversized words when needed
 *  - insert ellipsis when the block exceeds max height
 *  - remerge inline color codes into the wrapped output
 *
 * Non-responsibilities:
 *  - glyph atlas access
 *  - shaping implementation details
 *  - GL rendering state
 */
class TextWrapper {
public:
	static constexpr float MAX_HEIGHT_DEFAULT = 1000.0f;

public:
	TextWrapper() = default;
	explicit TextWrapper(std::shared_ptr<ITextMeasurer> measurer_)
		: measurer(std::move(measurer_))
	{}
	virtual ~TextWrapper() = default;

	TextWrapper(const TextWrapper&) = delete;
	TextWrapper& operator=(const TextWrapper&) = delete;
	TextWrapper(TextWrapper&&) noexcept = default;
	TextWrapper& operator=(TextWrapper&&) noexcept = default;

	void SetMeasurer(std::shared_ptr<ITextMeasurer> measurer_) { measurer = std::move(measurer_); }
	const std::shared_ptr<ITextMeasurer>& GetMeasurer() const noexcept { return measurer; }
	bool HasMeasurer() const noexcept { return static_cast<bool>(measurer); }

	std::size_t WrapInPlace(std::string& text, const WrapOptions& options = {}) const;
	std::string Wrap(std::string_view text, const WrapOptions& options = {}) const;
	WrappedText WrapDetailed(std::string_view text, const WrapOptions& options = {}) const;

protected:
	struct WrapState {
		std::vector<WrappedWord> words;
		std::vector<WrapLine> lines;
		std::vector<ExtractedColorCode> colorCodes;
		std::size_t printableLength = 0;
		bool hadExplicitLineBreaks = false;
		bool hadColorCodes = false;
	};

protected:
	WrapState SplitTextInWords(std::string_view text, const WrapOptions& options) const;
	WrappedWord SplitWord(const WrappedWord& word, float wantedWidth, const WrapOptions& options) const;
	void WrapConsole(std::vector<WrappedWord>& words, std::vector<WrapLine>& lines, const WrapOptions& options) const;
	void AddEllipsis(std::vector<WrapLine>& lines, std::vector<WrappedWord>& words, const WrapOptions& options) const;
	void RemergeColorCodes(std::vector<WrappedWord>& words, const std::vector<ExtractedColorCode>& colorCodes) const;

	float MeasureWidth(std::string_view utf8, const WrapOptions& options) const;
	std::size_t CountOutputLines(std::span<const WrapLine> lines) const;
	WrappedText BuildWrappedText(std::vector<WrappedWord> words, std::vector<WrapLine> lines, std::vector<ExtractedColorCode> colorCodes, const WrapOptions& options) const;

	LayoutOptions MakeMeasurementOptions(const WrapOptions& options) const { return options.ToLayoutOptions(); }
	const ITextMeasurer& GetMeasurerRef() const { return *measurer; }

	static bool IsSpaceByte(char c) noexcept
	{
		return c == ' ' || c == '\t';
	}

private:
	std::shared_ptr<ITextMeasurer> measurer;
};

} // namespace font::text
