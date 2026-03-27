/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "TextWrapper.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "System/StringUtil.h"

namespace {

static constexpr char32_t EllipsisCodepoint = 0x2026;
static const std::string EllipsisUtf8 = utf8::FromUnicode(EllipsisCodepoint);

using fonts::text::BreakOpportunity;
using fonts::text::ExtractedColorCode;
using fonts::text::ITextMeasurer;
using fonts::text::LayoutOptions;
using fonts::text::MeasuredBreakFragment;
using fonts::text::ParsedColorCode;
using fonts::text::TextSpan;
using fonts::text::WrapFragment;
using fonts::text::WrapLine;
using fonts::text::WrapOptions;
using fonts::text::WrappedText;
using fonts::text::WrappedWord;

[[nodiscard]] std::size_t CountUtf8Codepoints(std::string_view text)
{
	const std::string utf8Text(text);
	std::size_t count = 0;

	for (int pos = 0; pos < static_cast<int>(utf8Text.size()); ) {
		utf8::GetNextChar(utf8Text, pos);
		++count;
	}

	return count;
}

[[nodiscard]] WrapFragment MakeFragment(
	std::string text,
	std::size_t sourceByteOffset,
	std::size_t sourceByteLength,
	std::size_t printableOffset,
	std::size_t printableLength,
	float width,
	bool isWhitespace,
	bool isLineBreak,
	bool isControlCode,
	std::vector<BreakOpportunity> breakOpportunities = {}
)
{
	WrapFragment fragment;
	fragment.text = std::move(text);
	fragment.sourceSpan.text = fragment.text;
	fragment.sourceSpan.sourceOffset = sourceByteOffset;
	fragment.sourceSpan.sourceLength = sourceByteLength;
	fragment.sourceSpan.printableOffset = printableOffset;
	fragment.sourceSpan.printableLength = printableLength;
	fragment.sourceSpan.isWhitespace = isWhitespace;
	fragment.sourceSpan.isLineBreak = isLineBreak;
	fragment.sourceSpan.isControl = isControlCode;
	fragment.width = width;
	fragment.isWhitespace = isWhitespace;
	fragment.isLineBreak = isLineBreak;
	fragment.isControlCode = isControlCode;
	fragment.breakOpportunities = std::move(breakOpportunities);
	return fragment;
}

[[nodiscard]] WrapFragment MakeFragment(const MeasuredBreakFragment& fragment)
{
	return MakeFragment(
		fragment.text,
		fragment.sourceSpan.sourceOffset,
		fragment.sourceSpan.sourceLength,
		fragment.sourceSpan.printableOffset,
		fragment.sourceSpan.printableLength,
		fragment.width,
		fragment.isWhitespace,
		fragment.isLineBreak,
		fragment.isControlCode,
		fragment.breakOpportunities
	);
}

[[nodiscard]] WrapFragment MakeInsertedLineBreak(const WrapFragment& anchor)
{
	return MakeFragment("\n", anchor.sourceSpan.sourceOffset, 0, anchor.sourceSpan.printableOffset, 0, 0.0f, false, true, false);
}

[[nodiscard]] WrapFragment MakeEllipsisFragment(std::string_view ellipsis, float width, std::size_t sourceByteOffset, std::size_t printableOffset)
{
	return MakeFragment(std::string(ellipsis), sourceByteOffset, 0, printableOffset, 1, width, false, false, false);
}

[[nodiscard]] WrapFragment MakeColorCodeFragment(const ExtractedColorCode& colorCode)
{
	return MakeFragment(colorCode.colorText.ToString(), 0, 0, colorCode.printablePosition, 0, 0.0f, false, false, true);
}

[[nodiscard]] WrappedWord MakeWord(
	std::string text,
	std::size_t sourceByteOffset,
	std::size_t sourceByteLength,
	std::size_t printableOffset,
	std::size_t printableLength,
	float width,
	bool isWhitespace,
	bool isLineBreak,
	bool isControlCode
)
{
	WrappedWord word;
	word.text = std::move(text);
	word.sourceByteOffset = sourceByteOffset;
	word.sourceByteLength = sourceByteLength;
	word.printableOffset = printableOffset;
	word.printableLength = printableLength;
	word.word.sourceSpan.text = word.text;
	word.word.sourceSpan.sourceOffset = word.sourceByteOffset;
	word.word.sourceSpan.sourceLength = word.sourceByteLength;
	word.word.sourceSpan.printableOffset = word.printableOffset;
	word.word.sourceSpan.printableLength = word.printableLength;
	word.word.sourceSpan.isWhitespace = isWhitespace;
	word.word.sourceSpan.isLineBreak = isLineBreak;
	word.word.sourceSpan.isControl = isControlCode;
	word.word.width = width;
	word.word.numSpaces = isWhitespace ? static_cast<std::uint32_t>(printableLength) : 0u;
	word.word.isWhitespace = isWhitespace;
	word.word.isLineBreak = isLineBreak;
	word.word.isControlCode = isControlCode;
	return word;
}

[[nodiscard]] WrappedWord MakeWord(const WrapFragment& fragment)
{
	return MakeWord(
		fragment.text,
		fragment.sourceSpan.sourceOffset,
		fragment.sourceSpan.sourceLength,
		fragment.sourceSpan.printableOffset,
		fragment.sourceSpan.printableLength,
		fragment.width,
		fragment.isWhitespace,
		fragment.isLineBreak,
		fragment.isControlCode
	);
}

[[nodiscard]] std::size_t CountLineBreaks(std::string_view text)
{
	std::size_t count = 0;

	for (std::size_t i = 0; i < text.size(); ) {
		const std::size_t lineBreakLen = fonts::text::LineBreakLength(text, i);
		if (lineBreakLen == 0) {
			++i;
			continue;
		}

		++count;
		i += lineBreakLen;
	}

	return count;
}

[[nodiscard]] float MeasureLineHeight(const ITextMeasurer& measurer, const WrapOptions& options)
{
	LayoutOptions measureOptions = options.ToLayoutOptions();
	const auto measurement = measurer.MeasureText("Mg", measureOptions);
	if (measurement.lineHeight > 0.0f)
		return measurement.lineHeight;
	if (measurement.height > 0.0f)
		return measurement.height;
	return 1.0f;
}

void RebuildLines(std::vector<WrapFragment>& fragments, std::vector<WrapLine>& lines)
{
	lines.clear();

	if (fragments.empty())
		return;

	WrapLine current;
	current.firstWord = 0;
	current.lastWord = 0;
	current.width = 0.0f;

	bool lineHasFragments = false;

	for (std::size_t i = 0; i < fragments.size(); ++i) {
		const WrapFragment& fragment = fragments[i];

		if (fragment.isLineBreak) {
			current.lastWord = i;
			current.forceLineBreak = true;
			current.wasWrapped = (fragment.sourceSpan.sourceLength == 0);
			lines.emplace_back(current);

			current = {};
			current.firstWord = i + 1;
			current.lastWord = i + 1;
			lineHasFragments = false;
			continue;
		}

		current.lastWord = i;
		current.width += fragment.width;
		lineHasFragments = true;
	}

	if (lineHasFragments || current.firstWord < fragments.size() || (!fragments.empty() && fragments.back().isLineBreak))
		lines.emplace_back(current);
}

[[nodiscard]] float MeasureRangeWidth(const std::vector<WrapFragment>& fragments, std::size_t begin, std::size_t endExclusive)
{
	float width = 0.0f;

	for (std::size_t i = begin; i < endExclusive && i < fragments.size(); ++i) {
		if (fragments[i].isLineBreak)
			continue;
		width += fragments[i].width;
	}

	return width;
}

void TrimTrailingWhitespaceFromLine(std::vector<WrapFragment>& fragments, const WrapLine& line)
{
	if (fragments.empty() || line.firstWord >= fragments.size())
		return;

	std::size_t end = std::min(line.lastWord, fragments.size() - 1);

	while (end >= line.firstWord) {
		WrapFragment& fragment = fragments[end];
		if (fragment.isLineBreak || fragment.isControlCode) {
			if (end == 0)
				break;
			--end;
			continue;
		}

		if (!fragment.isWhitespace)
			break;

		fragment.text.clear();
		fragment.sourceSpan.text = fragment.text;
		fragment.sourceSpan.sourceLength = 0;
		fragment.sourceSpan.printableLength = 0;
		fragment.width = 0.0f;
		fragment.breakOpportunities.clear();

		if (end == 0)
			break;
		--end;
	}
}

} // namespace

namespace fonts::text {

std::size_t TextWrapper::WrapInPlace(std::string& text, const WrapOptions& options) const
{
	WrappedText wrapped = WrapDetailed(text, options);
	text = std::move(wrapped.text);
	return wrapped.lineCount;
}

std::string TextWrapper::Wrap(std::string_view text, const WrapOptions& options) const
{
	return WrapDetailed(text, options).text;
}

WrappedText TextWrapper::WrapDetailed(std::string_view text, const WrapOptions& options) const
{
	WrappedText wrapped;

	if (text.empty())
		return wrapped;

	if (!HasMeasurer()) {
		wrapped.text = std::string(text);
		wrapped.lineCount = CountLineBreaks(text) + 1;
		wrapped.printableLength = CountUtf8Codepoints(text);
		return wrapped;
	}

	WrapState state = AnalyzeTextForWrapping(text, options);
	std::size_t unconstrainedLineCount = 0;

	if (options.HasHeightConstraint()) {
		std::vector<WrapFragment> unconstrainedFragments = state.fragments;
		std::vector<WrapLine> unconstrainedLines;
		WrapOptions unconstrainedOptions = options;
		unconstrainedOptions.maxHeight = 0.0f;
		WrapConsole(unconstrainedFragments, unconstrainedLines, unconstrainedOptions);
		unconstrainedLineCount = unconstrainedLines.size();
	}

	WrapConsole(state.fragments, state.lines, options);
	wrapped = BuildWrappedText(std::move(state.fragments), std::move(state.lines), std::move(state.colorCodes), options);
	wrapped.printableLength = state.printableLength;
	wrapped.exceededHeight = options.HasHeightConstraint() && (wrapped.lineCount < unconstrainedLineCount);
	return wrapped;
}

TextWrapper::WrapState TextWrapper::AnalyzeTextForWrapping(std::string_view text, const WrapOptions& options) const
{
	WrapState state;
	const LayoutOptions measureOptions = MakeMeasurementOptions(options);

	std::vector<MeasuredBreakFragment> measured = GetMeasurerRef().AnalyzeWrapText(text, measureOptions);
	for (const MeasuredBreakFragment& measuredFragment : measured) {
		if (measuredFragment.isControlCode) {
			ParsedColorCode parsed;
			if (TryParseColorCode(measuredFragment.text, 0, &parsed, true)) {
				ExtractedColorCode colorCode;
				colorCode.colorText = parsed.raw;
				colorCode.printablePosition = measuredFragment.sourceSpan.printableOffset;
				state.colorCodes.emplace_back(std::move(colorCode));
				state.hadColorCodes = true;
			}
			continue;
		}

		if (measuredFragment.isLineBreak) {
			state.hadExplicitLineBreaks = true;
			if (!options.preserveLineBreaks)
				continue;
		}

		if (measuredFragment.text.empty() && !measuredFragment.isLineBreak)
			continue;

		state.fragments.emplace_back(MakeFragment(measuredFragment));
	}

	if (!state.fragments.empty()) {
		for (auto it = state.fragments.rbegin(); it != state.fragments.rend(); ++it) {
			if (it->isLineBreak || it->isControlCode)
				continue;

			state.printableLength = it->sourceSpan.printableOffset + it->sourceSpan.printableLength;
			break;
		}
	}

	return state;
}

WrapFragment TextWrapper::ReanalyzeFragment(std::string_view utf8, const TextSpan& sourceSpan, bool isWhitespace, const WrapOptions& options) const
{
	if (utf8.empty()) {
		return MakeFragment(
			"",
			sourceSpan.sourceOffset,
			0,
			sourceSpan.printableOffset,
			0,
			0.0f,
			isWhitespace,
			false,
			false
		);
	}

	LayoutOptions measureOptions = MakeMeasurementOptions(options);
	measureOptions.parseColorCodes = false;
	measureOptions.preserveLineBreaks = false;

	std::vector<MeasuredBreakFragment> analyzed = GetMeasurerRef().AnalyzeWrapText(utf8, measureOptions);
	for (MeasuredBreakFragment& measuredFragment : analyzed) {
		if (measuredFragment.isControlCode || measuredFragment.isLineBreak)
			continue;

		measuredFragment.sourceSpan.sourceOffset += sourceSpan.sourceOffset;
		measuredFragment.sourceSpan.printableOffset += sourceSpan.printableOffset;
		measuredFragment.sourceSpan.text = measuredFragment.text;

		for (BreakOpportunity& breakOpportunity : measuredFragment.breakOpportunities) {
			breakOpportunity.sourceByteOffset += sourceSpan.sourceOffset;
			breakOpportunity.codepointOffset += sourceSpan.printableOffset;
		}

		return MakeFragment(measuredFragment);
	}

	BreakOpportunity breakOpportunity;
	breakOpportunity.sourceByteOffset = sourceSpan.sourceOffset + sourceSpan.sourceLength;
	breakOpportunity.codepointOffset = sourceSpan.printableOffset + sourceSpan.printableLength;
	breakOpportunity.xAdvance = MeasureWidth(utf8, options);
	breakOpportunity.allowed = isWhitespace;

	return MakeFragment(
		std::string(utf8),
		sourceSpan.sourceOffset,
		sourceSpan.sourceLength,
		sourceSpan.printableOffset,
		sourceSpan.printableLength,
		breakOpportunity.xAdvance,
		isWhitespace,
		false,
		false,
		{breakOpportunity}
	);
}

std::pair<WrapFragment, WrapFragment> TextWrapper::SplitFragmentAtPosition(
	const WrapFragment& fragment,
	std::size_t leftByteLength,
	std::size_t leftCodepointLength,
	float leftWidth,
	const WrapOptions& options
) const
{
	if (fragment.isLineBreak || fragment.isControlCode)
		return {WrapFragment{}, fragment};
	if (leftByteLength == 0 || leftByteLength >= fragment.sourceSpan.sourceLength)
		return {WrapFragment{}, fragment};
	if (leftCodepointLength == 0 || leftCodepointLength >= fragment.sourceSpan.printableLength)
		return {WrapFragment{}, fragment};

	(void)leftWidth;

	TextSpan leftSpan;
	leftSpan.sourceOffset = fragment.sourceSpan.sourceOffset;
	leftSpan.sourceLength = leftByteLength;
	leftSpan.printableOffset = fragment.sourceSpan.printableOffset;
	leftSpan.printableLength = leftCodepointLength;
	leftSpan.isWhitespace = fragment.isWhitespace;

	TextSpan rightSpan;
	rightSpan.sourceOffset = fragment.sourceSpan.sourceOffset + leftByteLength;
	rightSpan.sourceLength = fragment.sourceSpan.sourceLength - leftByteLength;
	rightSpan.printableOffset = fragment.sourceSpan.printableOffset + leftCodepointLength;
	rightSpan.printableLength = fragment.sourceSpan.printableLength - leftCodepointLength;
	rightSpan.isWhitespace = fragment.isWhitespace;

	WrapFragment left = ReanalyzeFragment(fragment.text.substr(0, leftByteLength), leftSpan, fragment.isWhitespace, options);
	WrapFragment right = ReanalyzeFragment(fragment.text.substr(leftByteLength), rightSpan, fragment.isWhitespace, options);
	return {std::move(left), std::move(right)};
}

std::pair<WrapFragment, WrapFragment> TextWrapper::SplitFragmentAtBoundary(const WrapFragment& fragment, float availableWidth, const WrapOptions& options) const
{
	if (fragment.isLineBreak || fragment.isControlCode || fragment.sourceSpan.printableLength == 0 || availableWidth <= 0.0f)
		return {WrapFragment{}, fragment};
	if (fragment.width <= availableWidth)
		return {fragment, WrapFragment{}};

	for (auto it = fragment.breakOpportunities.rbegin(); it != fragment.breakOpportunities.rend(); ++it) {
		const BreakOpportunity& breakOpportunity = *it;
		if (breakOpportunity.xAdvance <= 0.0f)
			continue;
		if (breakOpportunity.sourceByteOffset <= fragment.sourceSpan.sourceOffset)
			continue;
		if (breakOpportunity.sourceByteOffset >= fragment.sourceSpan.sourceOffset + fragment.sourceSpan.sourceLength)
			continue;
		if (!breakOpportunity.allowed && !options.splitWords)
			continue;

		const auto split = SplitFragmentAtPosition(
			fragment,
			breakOpportunity.sourceByteOffset - fragment.sourceSpan.sourceOffset,
			breakOpportunity.codepointOffset - fragment.sourceSpan.printableOffset,
			breakOpportunity.xAdvance,
			options
		);

		if (split.first.Empty())
			continue;
		if (split.first.width > availableWidth)
			continue;

		return split;
	}

	return {WrapFragment{}, fragment};
}

void TextWrapper::WrapConsole(std::vector<WrapFragment>& fragments, std::vector<WrapLine>& lines, const WrapOptions& options) const
{
	lines.clear();

	if (fragments.empty())
		return;

	const float effectiveMaxWidth = options.HasWidthConstraint() ? std::max(options.maxWidth, 10.0f) : std::numeric_limits<float>::max();
	const float lineHeight = MeasureLineHeight(GetMeasurerRef(), options);
	const std::size_t maxLines = options.HasHeightConstraint()
		? static_cast<std::size_t>(std::floor(std::max(0.0f, options.maxHeight / lineHeight)))
		: std::numeric_limits<std::size_t>::max();

	float currentWidth = 0.0f;
	std::size_t currentLineCount = 1;
	bool exceededHeight = false;

	for (std::size_t fragmentIndex = 0; fragmentIndex < fragments.size(); ) {
		if (fragments[fragmentIndex].isLineBreak) {
			currentWidth = 0.0f;
			++currentLineCount;
			++fragmentIndex;

			if (currentLineCount > maxLines) {
				exceededHeight = true;
				break;
			}

			continue;
		}

		if (fragments[fragmentIndex].Empty()) {
			++fragmentIndex;
			continue;
		}

		if (!options.HasWidthConstraint()) {
			currentWidth += fragments[fragmentIndex].width;
			++fragmentIndex;
			continue;
		}

		if ((currentWidth + fragments[fragmentIndex].width) <= effectiveMaxWidth) {
			currentWidth += fragments[fragmentIndex].width;
			++fragmentIndex;
			continue;
		}

		const auto split = SplitFragmentAtBoundary(fragments[fragmentIndex], effectiveMaxWidth - currentWidth, options);
		if (!split.first.Empty()) {
			fragments[fragmentIndex] = split.second;
			fragments.insert(fragments.begin() + fragmentIndex, split.first);
			currentWidth += fragments[fragmentIndex].width;
			++fragmentIndex;
			continue;
		}

		if (currentWidth == 0.0f) {
			currentWidth = fragments[fragmentIndex].width;
			++fragmentIndex;
			continue;
		}

		fragments.insert(fragments.begin() + fragmentIndex, MakeInsertedLineBreak(fragments[fragmentIndex]));
		++fragmentIndex;

		while (fragmentIndex < fragments.size() && fragments[fragmentIndex].isWhitespace)
			fragments.erase(fragments.begin() + fragmentIndex);

		currentWidth = 0.0f;
		++currentLineCount;

		if (currentLineCount > maxLines) {
			exceededHeight = true;
			break;
		}
	}

	RebuildLines(fragments, lines);

	if (options.trimTrailingWhitespace) {
		for (const WrapLine& line : lines)
			TrimTrailingWhitespaceFromLine(fragments, line);

		RebuildLines(fragments, lines);
	}

	if (!exceededHeight || lines.empty())
		return;

	const std::size_t visibleLineCount = std::max<std::size_t>(1u, maxLines);
	if (lines.size() <= visibleLineCount)
		return;

	if (options.ellipsize) {
		AddEllipsis(lines, fragments, options);
		RebuildLines(fragments, lines);

		if (lines.size() > visibleLineCount) {
			const std::size_t lastVisibleFragment = std::min(lines[visibleLineCount - 1].lastWord, fragments.size() - 1);
			fragments.erase(fragments.begin() + lastVisibleFragment + 1, fragments.end());
			RebuildLines(fragments, lines);
		}

		return;
	}

	const std::size_t lastVisibleFragment = std::min(lines[visibleLineCount - 1].lastWord, fragments.size() - 1);
	fragments.erase(fragments.begin() + lastVisibleFragment + 1, fragments.end());
	RebuildLines(fragments, lines);
}

void TextWrapper::AddEllipsis(std::vector<WrapLine>& lines, std::vector<WrapFragment>& fragments, const WrapOptions& options) const
{
	if (lines.empty() || fragments.empty())
		return;

	const float lineHeight = MeasureLineHeight(GetMeasurerRef(), options);
	const std::size_t maxLines = options.HasHeightConstraint()
		? static_cast<std::size_t>(std::floor(std::max(0.0f, options.maxHeight / lineHeight)))
		: lines.size();
	const std::size_t visibleLineCount = std::max<std::size_t>(1u, maxLines);

	if (lines.size() <= visibleLineCount)
		return;

	const float ellipsisWidth = MeasureWidth(EllipsisUtf8, options);
	if (options.HasWidthConstraint() && ellipsisWidth > std::max(options.maxWidth, 10.0f))
		return;

	WrapLine targetLine = lines[visibleLineCount - 1];
	std::size_t truncateFrom = targetLine.lastWord + 1;

	if (targetLine.forceLineBreak && targetLine.lastWord < fragments.size() && fragments[targetLine.lastWord].isLineBreak) {
		fragments.erase(fragments.begin() + targetLine.lastWord);
		RebuildLines(fragments, lines);
		targetLine = lines[visibleLineCount - 1];
		truncateFrom = targetLine.lastWord + 1;
	}

	if (truncateFrom < fragments.size())
		fragments.erase(fragments.begin() + truncateFrom, fragments.end());

	RebuildLines(fragments, lines);
	if (lines.empty())
		return;

	targetLine = lines[std::min(visibleLineCount - 1, lines.size() - 1)];

	while (targetLine.lastWord >= targetLine.firstWord && targetLine.lastWord < fragments.size()) {
		const float lineWidth = MeasureRangeWidth(fragments, targetLine.firstWord, targetLine.lastWord + 1);
		if (!options.HasWidthConstraint() || (lineWidth + ellipsisWidth) <= std::max(options.maxWidth, 10.0f))
			break;

		WrapFragment& tail = fragments[targetLine.lastWord];
		if (tail.isControlCode || tail.isWhitespace || tail.Empty()) {
			if (targetLine.lastWord == 0)
				break;

			fragments.erase(fragments.begin() + targetLine.lastWord);
		} else {
			const float freeSpace = std::max(0.0f, std::max(options.maxWidth, 10.0f) - (lineWidth - tail.width) - ellipsisWidth);
			const auto split = SplitFragmentAtBoundary(tail, freeSpace, options);
			if (!split.first.Empty()) {
				tail = split.first;
			} else {
				fragments.erase(fragments.begin() + targetLine.lastWord);
			}
		}

		RebuildLines(fragments, lines);
		if (lines.empty())
			break;

		targetLine = lines[std::min(visibleLineCount - 1, lines.size() - 1)];
		if (targetLine.firstWord >= fragments.size())
			break;
	}

	std::size_t ellipsisSourceOffset = 0;
	std::size_t ellipsisPrintableOffset = 0;
	if (!fragments.empty() && targetLine.firstWord < fragments.size()) {
		const WrapFragment& anchor = fragments[std::min(targetLine.lastWord, fragments.size() - 1)];
		ellipsisSourceOffset = anchor.sourceSpan.sourceOffset;
		ellipsisPrintableOffset = anchor.sourceSpan.printableOffset + anchor.sourceSpan.printableLength;
	}

	const std::size_t insertPos = std::min(targetLine.lastWord + 1, fragments.size());
	fragments.insert(fragments.begin() + insertPos, MakeEllipsisFragment(EllipsisUtf8, ellipsisWidth, ellipsisSourceOffset, ellipsisPrintableOffset));
	RebuildLines(fragments, lines);

	if (lines.size() > visibleLineCount) {
		const std::size_t lastVisibleFragment = std::min(lines[visibleLineCount - 1].lastWord, fragments.size() - 1);
		fragments.erase(fragments.begin() + lastVisibleFragment + 1, fragments.end());
		RebuildLines(fragments, lines);
	}

	if (!lines.empty())
		lines[std::min(visibleLineCount - 1, lines.size() - 1)].wasEllipsized = true;
}

void TextWrapper::RemergeColorCodes(std::vector<WrapFragment>& fragments, const std::vector<ExtractedColorCode>& colorCodes, const WrapOptions& options) const
{
	(void)options;

	for (const ExtractedColorCode& colorCode : colorCodes) {
		std::size_t insertIndex = fragments.size();

		for (std::size_t i = 0; i < fragments.size(); ++i) {
			const WrapFragment& fragment = fragments[i];
			if (fragment.isLineBreak || fragment.isControlCode)
				continue;

			const std::size_t begin = fragment.sourceSpan.printableOffset;
			const std::size_t end = begin + fragment.sourceSpan.printableLength;

			if (colorCode.printablePosition <= begin) {
				insertIndex = i;
				break;
			}

			if (colorCode.printablePosition >= end)
				continue;

			const auto breakIt = std::find_if(fragment.breakOpportunities.begin(), fragment.breakOpportunities.end(), [&colorCode](const BreakOpportunity& breakOpportunity) {
				return breakOpportunity.codepointOffset == colorCode.printablePosition;
			});

			if (breakIt != fragment.breakOpportunities.end()) {
				const auto split = SplitFragmentAtPosition(
					fragment,
					breakIt->sourceByteOffset - fragment.sourceSpan.sourceOffset,
					breakIt->codepointOffset - fragment.sourceSpan.printableOffset,
					breakIt->xAdvance,
					options
				);

				if (!split.first.Empty()) {
					fragments[i] = split.second;
					fragments.insert(fragments.begin() + i, split.first);
					insertIndex = i + 1;
					break;
				}
			}

			insertIndex = i;
			break;
		}

		fragments.insert(fragments.begin() + insertIndex, MakeColorCodeFragment(colorCode));
	}
}

float TextWrapper::MeasureWidth(std::string_view utf8, const WrapOptions& options) const
{
	if (utf8.empty())
		return 0.0f;

	LayoutOptions measureOptions = MakeMeasurementOptions(options);
	measureOptions.maxWidth = 0.0f;
	measureOptions.maxHeight = 0.0f;
	return GetMeasurerRef().MeasureText(utf8, measureOptions).width;
}

std::size_t TextWrapper::CountOutputLines(std::span<const WrapLine> lines) const
{
	return lines.size();
}

WrappedText TextWrapper::BuildWrappedText(std::vector<WrapFragment> fragments, std::vector<WrapLine> lines, std::vector<ExtractedColorCode> colorCodes, const WrapOptions& options) const
{
	WrappedText wrapped;
	wrapped.colorCodes = std::move(colorCodes);

	if (options.remergeColorCodes)
		RemergeColorCodes(fragments, wrapped.colorCodes, options);

	wrapped.words.reserve(fragments.size());
	for (const WrapFragment& fragment : fragments)
		wrapped.words.emplace_back(MakeWord(fragment));

	for (const WrappedWord& word : wrapped.words) {
		if (word.word.isLineBreak) {
			wrapped.text += options.consoleStyle ? "\r\n" : (word.text.empty() ? "\n" : word.text);
			continue;
		}

		wrapped.text += word.text;
	}

	wrapped.lineCount = wrapped.text.empty() ? 0 : (lines.empty() ? (CountLineBreaks(wrapped.text) + 1) : CountOutputLines(lines));
	wrapped.wasWrapped = std::any_of(lines.begin(), lines.end(), [](const WrapLine& line) { return line.wasWrapped; });
	wrapped.wasEllipsized = std::any_of(lines.begin(), lines.end(), [](const WrapLine& line) { return line.wasEllipsized; });
	return wrapped;
}

} // namespace font::text
