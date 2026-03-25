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

using fonts::text::WrappedWord;
using fonts::text::WrapLine;
using fonts::text::WrapOptions;
using fonts::text::ExtractedColorCode;
using fonts::text::LayoutOptions;
using fonts::text::ParsedColorCode;
using fonts::text::TextSpan;
using fonts::text::ITextMeasurer;

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

[[nodiscard]] bool IsUpperCase(char32_t c)
{
	return
		(c >= 0x41 && c <= 0x5A) ||
		(c >= 0xC0 && c <= 0xD6) ||
		(c >= 0xD8 && c <= 0xDE) ||
		(c == 0x8A) ||
		(c == 0x8C) ||
		(c == 0x8E) ||
		(c == 0x9F);
}

[[nodiscard]] bool IsLowerCase(char32_t c)
{
	return (c >= 0x61 && c <= 0x7A);
}

[[nodiscard]] float GetSplitPenalty(char32_t c, unsigned int strpos, unsigned int strlen)
{
	const float dist = strlen - strpos;

	if (dist > (strlen / 2) && dist < 4)
		return 1e9f;
	if (IsLowerCase(c))
		return 1.0f + (strlen - strpos);
	if (c >= 0x30 && c <= 0x39)
		return 1.0f + (strlen - strpos) * 0.9f;
	if (IsUpperCase(c))
		return 1.0f + (strlen - strpos) * 0.75f;

	return (dist / 4.0f) * (dist / 4.0f);
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

[[nodiscard]] WrappedWord MakeInsertedLineBreak(const WrappedWord& anchor)
{
	return MakeWord("\n", anchor.sourceByteOffset, 0, anchor.printableOffset, 0, 0.0f, false, true, false);
}

[[nodiscard]] WrappedWord MakeEllipsisWord(std::string_view ellipsis, float width, std::size_t sourceByteOffset, std::size_t printableOffset)
{
	return MakeWord(std::string(ellipsis), sourceByteOffset, 0, printableOffset, 1, width, false, false, false);
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

[[nodiscard]] float MeasureTextWidth(const ITextMeasurer& measurer, std::string_view utf8, const WrapOptions& options)
{
	if (utf8.empty())
		return 0.0f;

	LayoutOptions measureOptions = options.ToLayoutOptions();
	measureOptions.maxWidth = 0.0f;
	measureOptions.maxHeight = 0.0f;
	return measurer.MeasureText(utf8, measureOptions).width;
}

[[nodiscard]] std::pair<WrappedWord, WrappedWord> SplitWordPair(const ITextMeasurer& measurer, const WrappedWord& word, float wantedWidth, const WrapOptions& options, bool smart)
{
	if (word.word.isLineBreak || word.word.isControlCode || word.printableLength == 0 || wantedWidth <= 0.0f)
		return {WrappedWord{}, word};

	if (word.word.width <= wantedWidth)
		return {word, WrappedWord{}};

	const std::string& text = word.text;
	int pos = 0;
	int previousBytePos = 0;
	std::size_t codepointIndex = 0;
	std::size_t bestByte = 0;
	std::size_t bestCodepoints = 0;
	float bestPenalty = std::numeric_limits<float>::max();

	while (pos < static_cast<int>(text.size())) {
		previousBytePos = pos;
		const char32_t cp = utf8::GetNextChar(text, pos);
		const std::string_view prefix(text.data(), static_cast<std::size_t>(pos));
		const float prefixWidth = MeasureTextWidth(measurer, prefix, options);

		if (prefixWidth > wantedWidth)
			break;

		++codepointIndex;
		if (word.word.isWhitespace || !smart) {
			bestByte = static_cast<std::size_t>(pos);
			bestCodepoints = codepointIndex;
			continue;
		}

		const float penalty = GetSplitPenalty(cp, static_cast<unsigned int>(previousBytePos), static_cast<unsigned int>(text.size()));
		if (penalty <= bestPenalty) {
			bestPenalty = penalty;
			bestByte = static_cast<std::size_t>(pos);
			bestCodepoints = codepointIndex;
		}
	}

	if (bestByte == 0)
		return {WrappedWord{}, word};

	const std::string leftText = text.substr(0, bestByte);
	const std::string rightText = text.substr(bestByte);
	const float leftWidth = MeasureTextWidth(measurer, leftText, options);
	const float rightWidth = MeasureTextWidth(measurer, rightText, options);

	WrappedWord left = MakeWord(
		leftText,
		word.sourceByteOffset,
		bestByte,
		word.printableOffset,
		bestCodepoints,
		leftWidth,
		word.word.isWhitespace,
		false,
		false
	);

	WrappedWord right = MakeWord(
		rightText,
		word.sourceByteOffset + bestByte,
		word.sourceByteLength - bestByte,
		word.printableOffset + bestCodepoints,
		word.printableLength - bestCodepoints,
		rightWidth,
		word.word.isWhitespace,
		false,
		false
	);

	return {std::move(left), std::move(right)};
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

void RebuildLines(std::vector<WrappedWord>& words, std::vector<WrapLine>& lines)
{
	lines.clear();

	if (words.empty())
		return;

	WrapLine current;
	current.firstWord = 0;
	current.lastWord = 0;
	current.width = 0.0f;

	bool lineHasWords = false;

	for (std::size_t i = 0; i < words.size(); ++i) {
		const WrappedWord& word = words[i];

		if (word.word.isLineBreak) {
			current.lastWord = i;
			current.forceLineBreak = true;
			current.wasWrapped = (word.sourceByteLength == 0);
			lines.emplace_back(current);

			current = {};
			current.firstWord = i + 1;
			current.lastWord = i + 1;
			lineHasWords = false;
			continue;
		}

		current.lastWord = i;
		current.width += word.word.width;
		lineHasWords = true;
	}

	if (lineHasWords || current.firstWord < words.size() || (!words.empty() && words.back().word.isLineBreak))
		lines.emplace_back(current);
}

[[nodiscard]] float MeasureRangeWidth(const std::vector<WrappedWord>& words, std::size_t begin, std::size_t endExclusive)
{
	float width = 0.0f;

	for (std::size_t i = begin; i < endExclusive && i < words.size(); ++i) {
		if (words[i].word.isLineBreak)
			continue;
		width += words[i].word.width;
	}

	return width;
}

void TrimTrailingWhitespaceFromLine(std::vector<WrappedWord>& words, const WrapLine& line)
{
	if (words.empty() || line.firstWord >= words.size())
		return;

	std::size_t end = std::min(line.lastWord, words.size() - 1);

	while (end >= line.firstWord) {
		WrappedWord& word = words[end];
		if (word.word.isLineBreak || word.word.isControlCode) {
			if (end == 0)
				break;
			--end;
			continue;
		}

		if (!word.word.isWhitespace)
			break;

		word.text.clear();
		word.word.width = 0.0f;
		word.printableLength = 0;
		word.word.numSpaces = 0;
		word.word.sourceSpan.text = word.text;

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

	WrapState state = SplitTextInWords(text, options);
	std::size_t unconstrainedLineCount = 0;

	if (options.HasHeightConstraint()) {
		std::vector<WrappedWord> unconstrainedWords = state.words;
		std::vector<WrapLine> unconstrainedLines;
		WrapOptions unconstrainedOptions = options;
		unconstrainedOptions.maxHeight = 0.0f;
		WrapConsole(unconstrainedWords, unconstrainedLines, unconstrainedOptions);
		unconstrainedLineCount = unconstrainedLines.size();
	}

	WrapConsole(state.words, state.lines, options);
	wrapped = BuildWrappedText(std::move(state.words), std::move(state.lines), std::move(state.colorCodes), options);
	wrapped.printableLength = state.printableLength;
	wrapped.exceededHeight = options.HasHeightConstraint() && (wrapped.lineCount < unconstrainedLineCount);
	return wrapped;
}

TextWrapper::WrapState TextWrapper::SplitTextInWords(std::string_view text, const WrapOptions& options) const
{
	WrapState state;

	for (const TextSpan& span : GetMeasurerRef().ParseTextSpans(text, options.parseColorCodes)) {
		if (span.isControl) {
			ParsedColorCode parsed;
			if (TryParseColorCode(span.text, 0, &parsed, true)) {
				ExtractedColorCode colorCode;
				colorCode.colorText = parsed.raw;
				colorCode.printablePosition = span.printableOffset;
				state.colorCodes.emplace_back(std::move(colorCode));
				state.hadColorCodes = true;
			}
			continue;
		}

		if (span.isLineBreak) {
			state.hadExplicitLineBreaks = true;
			if (!options.preserveLineBreaks)
				continue;

			state.words.emplace_back(MakeWord(
				std::string(span.text),
				span.sourceOffset,
				span.sourceLength,
				span.printableOffset,
				0,
				0.0f,
				false,
				true,
				false
			));
			continue;
		}

		if (span.text.empty())
			continue;

		const std::string spanText(span.text);
		int bytePos = 0;
		std::size_t codepointOffset = 0;

		while (bytePos < static_cast<int>(spanText.size())) {
			const int tokenStart = bytePos;
			const bool isWhitespace = IsSpaceByte(spanText[bytePos]);
			std::size_t tokenCodepoints = 0;

			while (bytePos < static_cast<int>(spanText.size())) {
				const bool sameKind = IsSpaceByte(spanText[bytePos]) == isWhitespace;
				if (!sameKind)
					break;

				utf8::GetNextChar(spanText, bytePos);
				++tokenCodepoints;
			}

			const std::string tokenText = spanText.substr(tokenStart, bytePos - tokenStart);
			const float tokenWidth = MeasureWidth(tokenText, options);

			state.words.emplace_back(MakeWord(
				tokenText,
				span.sourceOffset + static_cast<std::size_t>(tokenStart),
				static_cast<std::size_t>(bytePos - tokenStart),
				span.printableOffset + codepointOffset,
				tokenCodepoints,
				tokenWidth,
				isWhitespace,
				false,
				false
			));

			codepointOffset += tokenCodepoints;
		}
	}

	if (!state.words.empty()) {
		const WrappedWord& lastWord = state.words.back();
		state.printableLength = lastWord.printableOffset + lastWord.printableLength;
	}

	return state;
}

WrappedWord TextWrapper::SplitWord(const WrappedWord& word, float wantedWidth, const WrapOptions& options) const
{
	return SplitWordPair(GetMeasurerRef(), word, wantedWidth, options, options.smartWordSplit).first;
}

void TextWrapper::WrapConsole(std::vector<WrappedWord>& words, std::vector<WrapLine>& lines, const WrapOptions& options) const
{
	lines.clear();

	if (words.empty())
		return;

	const float effectiveMaxWidth = options.HasWidthConstraint() ? std::max(options.maxWidth, 10.0f) : std::numeric_limits<float>::max();
	const float lineHeight = MeasureLineHeight(GetMeasurerRef(), options);
	const std::size_t maxLines = options.HasHeightConstraint()
		? static_cast<std::size_t>(std::floor(std::max(0.0f, options.maxHeight / lineHeight)))
		: std::numeric_limits<std::size_t>::max();

	float currentWidth = 0.0f;
	std::size_t currentLineCount = 1;
	bool exceededHeight = false;

	for (std::size_t wi = 0; wi < words.size(); ) {
		if (words[wi].word.isLineBreak) {
			currentWidth = 0.0f;
			++currentLineCount;
			++wi;

			if (currentLineCount > maxLines) {
				exceededHeight = true;
				break;
			}

			continue;
		}

		currentWidth += words[wi].word.width;
		if (currentWidth <= effectiveMaxWidth || !options.HasWidthConstraint()) {
			++wi;
			continue;
		}

		currentWidth -= words[wi].word.width;

		const bool splitCandidate =
			!words[wi].word.isControlCode &&
			(
				words[wi].word.isWhitespace ||
				(options.splitWords && (words[wi].word.width > (0.5f * effectiveMaxWidth)))
			);

		if (splitCandidate) {
			auto split = SplitWordPair(GetMeasurerRef(), words[wi], effectiveMaxWidth - currentWidth, options, options.smartWordSplit);
			if (split.first.Empty() && options.smartWordSplit)
				split = SplitWordPair(GetMeasurerRef(), words[wi], effectiveMaxWidth - currentWidth, options, false);

			if (!split.first.Empty()) {
				words[wi] = std::move(split.second);
				words.insert(words.begin() + wi, std::move(split.first));
				currentWidth += words[wi].word.width;
				++wi;
			}
		}

		if (currentWidth == 0.0f) {
			currentWidth = words[wi].word.width;
			++wi;
			continue;
		}

		words.insert(words.begin() + wi, MakeInsertedLineBreak(words[wi]));
		++wi;

		while (wi < words.size() && words[wi].word.isWhitespace)
			words.erase(words.begin() + wi);

		currentWidth = 0.0f;
		++currentLineCount;

		if (currentLineCount > maxLines) {
			exceededHeight = true;
			break;
		}
	}

	RebuildLines(words, lines);

	if (options.trimTrailingWhitespace) {
		for (const WrapLine& line : lines)
			TrimTrailingWhitespaceFromLine(words, line);

		RebuildLines(words, lines);
	}

	if (!exceededHeight || lines.empty())
		return;

	const std::size_t visibleLineCount = std::max<std::size_t>(1u, maxLines);
	if (lines.size() <= visibleLineCount)
		return;

	if (options.ellipsize) {
		AddEllipsis(lines, words, options);
		RebuildLines(words, lines);

		if (lines.size() > visibleLineCount) {
			const std::size_t lastVisibleWord = std::min(lines[visibleLineCount - 1].lastWord, words.size() - 1);
			words.erase(words.begin() + lastVisibleWord + 1, words.end());
			RebuildLines(words, lines);
		}

		return;
	}

	const std::size_t lastVisibleWord = std::min(lines[visibleLineCount - 1].lastWord, words.size() - 1);
	words.erase(words.begin() + lastVisibleWord + 1, words.end());
	RebuildLines(words, lines);
}

void TextWrapper::AddEllipsis(std::vector<WrapLine>& lines, std::vector<WrappedWord>& words, const WrapOptions& options) const
{
	if (lines.empty() || words.empty())
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

	if (targetLine.forceLineBreak && targetLine.lastWord < words.size() && words[targetLine.lastWord].word.isLineBreak) {
		words.erase(words.begin() + targetLine.lastWord);
		RebuildLines(words, lines);
		targetLine = lines[visibleLineCount - 1];
		truncateFrom = targetLine.lastWord + 1;
	}

	if (truncateFrom < words.size())
		words.erase(words.begin() + truncateFrom, words.end());

	RebuildLines(words, lines);
	if (lines.empty())
		return;

	targetLine = lines[std::min(visibleLineCount - 1, lines.size() - 1)];

	while (targetLine.lastWord >= targetLine.firstWord && targetLine.lastWord < words.size()) {
		const float lineWidth = MeasureRangeWidth(words, targetLine.firstWord, targetLine.lastWord + 1);
		if (!options.HasWidthConstraint() || (lineWidth + ellipsisWidth) <= std::max(options.maxWidth, 10.0f))
			break;

		WrappedWord& tail = words[targetLine.lastWord];
		if (tail.word.isControlCode) {
			if (targetLine.lastWord == 0)
				break;
			words.erase(words.begin() + targetLine.lastWord);
		} else if (tail.word.isWhitespace) {
			words.erase(words.begin() + targetLine.lastWord);
		} else {
			const float freeSpace = std::max(0.0f, std::max(options.maxWidth, 10.0f) - (lineWidth - tail.word.width) - ellipsisWidth);
			auto split = SplitWordPair(GetMeasurerRef(), tail, freeSpace, options, false);
			if (!split.first.Empty()) {
				tail = std::move(split.first);
			} else {
				words.erase(words.begin() + targetLine.lastWord);
			}
		}

		RebuildLines(words, lines);
		if (lines.empty())
			break;

		targetLine = lines[std::min(visibleLineCount - 1, lines.size() - 1)];
		if (targetLine.firstWord >= words.size())
			break;
	}

	std::size_t ellipsisSourceOffset = 0;
	std::size_t ellipsisPrintableOffset = 0;
	if (!words.empty() && targetLine.firstWord < words.size()) {
		const WrappedWord& anchor = words[std::min(targetLine.lastWord, words.size() - 1)];
		ellipsisSourceOffset = anchor.sourceByteOffset;
		ellipsisPrintableOffset = anchor.printableOffset + anchor.printableLength;
	}

	const std::size_t insertPos = std::min(targetLine.lastWord + 1, words.size());
	words.insert(words.begin() + insertPos, MakeEllipsisWord(EllipsisUtf8, ellipsisWidth, ellipsisSourceOffset, ellipsisPrintableOffset));
	RebuildLines(words, lines);

	if (lines.size() > visibleLineCount) {
		const std::size_t lastVisibleWord = std::min(lines[visibleLineCount - 1].lastWord, words.size() - 1);
		words.erase(words.begin() + lastVisibleWord + 1, words.end());
		RebuildLines(words, lines);
	}

	if (!lines.empty())
		lines[std::min(visibleLineCount - 1, lines.size() - 1)].wasEllipsized = true;
}

void TextWrapper::RemergeColorCodes(std::vector<WrappedWord>& words, const std::vector<ExtractedColorCode>& colorCodes, const WrapOptions& options) const
{
	for (const ExtractedColorCode& colorCode : colorCodes) {
		std::size_t insertIndex = words.size();

		for (std::size_t i = 0; i < words.size(); ++i) {
			const WrappedWord& word = words[i];
			if (word.word.isLineBreak || word.word.isControlCode)
				continue;

			const std::size_t begin = word.printableOffset;
			const std::size_t end = begin + word.printableLength;

			if (colorCode.printablePosition <= begin) {
				insertIndex = i;
				break;
			}

			if (colorCode.printablePosition >= end)
				continue;

			const std::size_t splitAt = colorCode.printablePosition - begin;
			if (splitAt == 0) {
				insertIndex = i;
				break;
			}

			int pos = 0;
			std::size_t codepoints = 0;
			while (pos < static_cast<int>(word.text.size()) && codepoints < splitAt) {
				utf8::GetNextChar(word.text, pos);
				++codepoints;
			}

			WrappedWord left = MakeWord(
				word.text.substr(0, pos),
				word.sourceByteOffset,
				static_cast<std::size_t>(pos),
				word.printableOffset,
				splitAt,
				0.0f,
				word.word.isWhitespace,
				false,
				false
			);
			left.word.width = MeasureWidth(left.text, options);

			WrappedWord right = MakeWord(
				word.text.substr(pos),
				word.sourceByteOffset + static_cast<std::size_t>(pos),
				word.sourceByteLength - static_cast<std::size_t>(pos),
				word.printableOffset + splitAt,
				word.printableLength - splitAt,
				0.0f,
				word.word.isWhitespace,
				false,
				false
			);
			right.word.width = MeasureWidth(right.text, options);

			words[i] = std::move(right);
			words.insert(words.begin() + i, std::move(left));
			insertIndex = i + 1;
			break;
		}

		WrappedWord colorWord = MakeWord(
			colorCode.colorText.ToString(),
			0,
			0,
			colorCode.printablePosition,
			0,
			0.0f,
			false,
			false,
			true
		);
		words.insert(words.begin() + insertIndex, std::move(colorWord));
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

WrappedText TextWrapper::BuildWrappedText(std::vector<WrappedWord> words, std::vector<WrapLine> lines, std::vector<ExtractedColorCode> colorCodes, const WrapOptions& options) const
{
	WrappedText wrapped;
	wrapped.words = std::move(words);
	wrapped.colorCodes = std::move(colorCodes);

	if (options.remergeColorCodes)
		RemergeColorCodes(wrapped.words, wrapped.colorCodes, options);

	for (const WrappedWord& word : wrapped.words) {
		if (word.word.isLineBreak) {
			wrapped.text += options.consoleStyle ? "\r\n" : (word.text.empty() ? "\n" : word.text);
			continue;
		}

		wrapped.text += word.text;
	}

	wrapped.lineCount = wrapped.text.empty() ? 0 : (CountLineBreaks(wrapped.text) + 1);
	wrapped.wasWrapped = std::any_of(lines.begin(), lines.end(), [](const WrapLine& line) { return line.wasWrapped; });
	wrapped.wasEllipsized = std::any_of(lines.begin(), lines.end(), [](const WrapLine& line) { return line.wasEllipsized; });
	return wrapped;
}

} // namespace font::text
