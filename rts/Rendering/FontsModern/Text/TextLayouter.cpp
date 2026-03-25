/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "TextLayouter.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "Rendering/FontsModern/Glyphs/GlyphAtlasCache.h"
#include "System/StringUtil.h"

namespace {

[[nodiscard]] float ResolveLayoutScale(const font::text::LayoutOptions& options)
{
	return (options.fontSize > 0.0f) ? options.fontSize : 1.0f;
}

[[nodiscard]] bool IsWhitespaceOnly(std::string_view text)
{
	return std::all_of(text.begin(), text.end(), [](unsigned char c) {
		return std::isspace(c) != 0;
	});
}

[[nodiscard]] std::string ExpandTabs(std::string_view text, float tabWidth)
{
	const int tabSpaces = std::max(1, static_cast<int>(tabWidth));
	std::string expanded;
	expanded.reserve(text.size());

	for (char c : text) {
		if (c == '\t') {
			expanded.append(static_cast<std::size_t>(tabSpaces), ' ');
		} else {
			expanded.push_back(c);
		}
	}

	return expanded;
}

[[nodiscard]] std::size_t CountPrintableCodepoints(std::string_view text)
{
	const std::string utf8(text);
	std::size_t count = 0;

	for (int pos = 0; pos < static_cast<int>(utf8.size()); ) {
		utf8::GetNextChar(utf8, pos);
		++count;
	}

	return count;
}

[[nodiscard]] std::size_t ResolveLineSourceOffset(std::span<const font::text::TextSpan> spans, std::size_t begin)
{
	if (begin < spans.size())
		return spans[begin].sourceOffset;
	if (!spans.empty())
		return spans.back().sourceOffset + spans.back().sourceLength;
	return 0;
}

} // namespace

namespace font::text {

TextLayouter::TextLayouter(TextShaperPtr shaper, GlyphCachePtr glyphCache_)
	: textShaper(std::move(shaper))
	, glyphCache(std::move(glyphCache_))
{}

TextMeasurement TextLayouter::MeasureText(std::string_view utf8, const LayoutOptions& options) const
{
	return LayoutText(utf8, options).measurement;
}

TextLayout TextLayouter::LayoutText(std::string_view utf8, const LayoutOptions& options) const
{
	TextLayout layout;
	layout.options = options;
	layout.sourceLength = utf8.size();

	ParsedSpanBuffer parsed = ParseTextSpansInternal(utf8, options.parseColorCodes);
	layout.spans = parsed.spans;
	layout.printableLength = parsed.printableLength;

	const std::vector<LineSpanRange> lineRanges = BuildLineRanges(parsed.spans);
	if (lineRanges.empty()) {
		layout.measurement = BuildMeasurement({}, parsed, options);
		layout.valid = true;
		return layout;
	}

	LayoutContext ctx;
	ctx.options = options;
	if (glyphCache != nullptr) {
		ctx.defaultLineHeight = glyphCache->GetLineHeight();
		ctx.defaultDescender = glyphCache->GetDescender();
	}

	layout.lines.reserve(lineRanges.size());

	for (const LineSpanRange& range : lineRanges) {
		const std::span<const TextSpan> lineSpans(parsed.spans.data() + range.begin, range.end - range.begin);
		std::vector<ShapedRun> runs = ShapeLineSpans(lineSpans, options);
		if (options.preloadGlyphs)
			EnsureGlyphsForRuns(runs);

		const std::size_t sourceOffset = ResolveLineSourceOffset(parsed.spans, range.begin);
		const std::size_t sourceEnd = range.Empty()
			? sourceOffset
			: (parsed.spans[range.end - 1].sourceOffset + parsed.spans[range.end - 1].sourceLength);

		LaidOutLine line = PositionRuns(runs, ctx, sourceOffset, sourceEnd - sourceOffset, range.endsWithExplicitBreak);
		layout.lines.emplace_back(std::move(line));
	}

	layout.measurement = BuildMeasurement(layout.lines, parsed, options);
	ApplyHorizontalAlignment(layout.lines, options);
	ApplyVerticalAlignment(layout.lines, layout.measurement, options, ctx.defaultDescender);
	layout.valid = true;
	return layout;
}

std::vector<TextSpan> TextLayouter::ParseTextSpans(std::string_view utf8, bool parseColorCodes) const
{
	return ParseTextSpansInternal(utf8, parseColorCodes).spans;
}

std::vector<TextSpan> TextLayouter::SplitIntoLines(std::string_view utf8, bool parseColorCodes) const
{
	return SplitIntoLines(ParseTextSpans(utf8, parseColorCodes));
}

std::vector<TextSpan> TextLayouter::SplitIntoLines(std::span<const TextSpan> spans) const
{
	std::vector<TextSpan> lines;

	auto makeEmptyLine = [&spans](std::size_t begin) {
		TextSpan line;
		line.sourceOffset = ResolveLineSourceOffset(spans, begin);
		line.sourceLength = 0;
		line.printableOffset = (begin < spans.size())
			? spans[begin].printableOffset
			: (!spans.empty() ? (spans.back().printableOffset + spans.back().printableLength) : 0);
		line.printableLength = 0;
		return line;
	};

	std::size_t lineBegin = 0;
	for (std::size_t i = 0; i < spans.size(); ++i) {
		if (!spans[i].isLineBreak)
			continue;

		if (i > lineBegin) {
			TextSpan merged;
			merged.text = std::string_view(spans[lineBegin].text.data(), spans[i - 1].text.data() + spans[i - 1].text.size() - spans[lineBegin].text.data());
			merged.sourceOffset = spans[lineBegin].sourceOffset;
			merged.sourceLength = (spans[i - 1].sourceOffset + spans[i - 1].sourceLength) - spans[lineBegin].sourceOffset;
			merged.printableOffset = spans[lineBegin].printableOffset;
			merged.printableLength = spans[i - 1].printableOffset + spans[i - 1].printableLength - spans[lineBegin].printableOffset;
			lines.emplace_back(std::move(merged));
		} else {
			lines.emplace_back(makeEmptyLine(lineBegin));
		}

		lineBegin = i + 1;
	}

	if (lineBegin < spans.size()) {
		TextSpan merged;
		merged.text = std::string_view(spans[lineBegin].text.data(), spans.back().text.data() + spans.back().text.size() - spans[lineBegin].text.data());
		merged.sourceOffset = spans[lineBegin].sourceOffset;
		merged.sourceLength = (spans.back().sourceOffset + spans.back().sourceLength) - spans[lineBegin].sourceOffset;
		merged.printableOffset = spans[lineBegin].printableOffset;
		merged.printableLength = spans.back().printableOffset + spans.back().printableLength - spans[lineBegin].printableOffset;
		lines.emplace_back(std::move(merged));
	} else if (!spans.empty() && spans.back().isLineBreak) {
		lines.emplace_back(makeEmptyLine(lineBegin));
	}

	return lines;
}

LaidOutLine TextLayouter::LayoutSingleLine(std::span<const TextSpan> lineSpans, const LayoutOptions& options) const
{
	LayoutContext ctx;
	ctx.options = options;
	if (glyphCache != nullptr) {
		ctx.defaultLineHeight = glyphCache->GetLineHeight();
		ctx.defaultDescender = glyphCache->GetDescender();
	}

	std::vector<ShapedRun> runs = ShapeLineSpans(lineSpans, options);
	if (options.preloadGlyphs)
		EnsureGlyphsForRuns(runs);

	const std::size_t sourceOffset = lineSpans.empty() ? 0 : lineSpans.front().sourceOffset;
	const std::size_t sourceLength = lineSpans.empty() ? 0 : (lineSpans.back().sourceOffset + lineSpans.back().sourceLength - sourceOffset);

	return PositionRuns(runs, ctx, sourceOffset, sourceLength, false);
}

TextMeasurement TextLayouter::MeasureLaidOutLines(std::span<const LaidOutLine> lines, const LayoutOptions& options) const
{
	ParsedSpanBuffer parsed;
	return BuildMeasurement(lines, parsed, options);
}

TextLayouter::ParsedSpanBuffer TextLayouter::ParseTextSpansInternal(std::string_view utf8, bool parseColorCodes) const
{
	ParsedSpanBuffer parsed;
	std::size_t offset = 0;
	std::size_t printableOffset = 0;

	while (offset < utf8.size()) {
		if (parseColorCodes) {
			ParsedColorCode colorCode;
			if (TryParseColorCode(utf8, offset, &colorCode, true)) {
				TextSpan span;
				span.text = utf8.substr(offset, colorCode.size);
				span.sourceOffset = offset;
				span.sourceLength = colorCode.size;
				span.printableOffset = printableOffset;
				span.printableLength = 0;
				span.isControl = true;
				parsed.hadColorCodes = true;
				parsed.spans.emplace_back(std::move(span));
				offset += colorCode.size;
				continue;
			}
		}

		const std::size_t lineBreakLen = LineBreakLength(utf8, offset);
		if (lineBreakLen > 0) {
			TextSpan span;
			span.text = utf8.substr(offset, lineBreakLen);
			span.sourceOffset = offset;
			span.sourceLength = lineBreakLen;
			span.printableOffset = printableOffset;
			span.printableLength = 0;
			span.isLineBreak = true;
			span.isParagraphBreak = (lineBreakLen == 2);
			parsed.hadExplicitLineBreaks = true;
			parsed.spans.emplace_back(std::move(span));
			offset += lineBreakLen;
			continue;
		}

		const std::size_t start = offset;
		while (offset < utf8.size()) {
			if (parseColorCodes && IsColorCodeIndicator(static_cast<char8_t>(utf8[offset]), true))
				break;
			if (LineBreakLength(utf8, offset) > 0)
				break;
			++offset;
		}

		if (offset == start)
			++offset;

		TextSpan span;
		span.text = utf8.substr(start, offset - start);
		span.sourceOffset = start;
		span.sourceLength = offset - start;
		span.printableOffset = printableOffset;
		span.printableLength = CountPrintableCodepoints(span.text);
		span.isWhitespace = IsWhitespaceOnly(span.text);
		printableOffset += span.printableLength;
		parsed.spans.emplace_back(std::move(span));
	}

	parsed.printableLength = printableOffset;
	return parsed;
}

std::vector<TextLayouter::LineSpanRange> TextLayouter::BuildLineRanges(std::span<const TextSpan> spans) const
{
	std::vector<LineSpanRange> ranges;
	if (spans.empty())
		return ranges;

	std::size_t begin = 0;
	for (std::size_t i = 0; i < spans.size(); ++i) {
		if (!spans[i].isLineBreak)
			continue;

		ranges.push_back(LineSpanRange{begin, i, true});
		begin = i + 1;
	}

	if (begin < spans.size() || ranges.empty() || spans.back().isLineBreak)
		ranges.push_back(LineSpanRange{begin, spans.size(), false});

	return ranges;
}

std::vector<ShapedRun> TextLayouter::ShapeLineSpans(std::span<const TextSpan> lineSpans, const LayoutOptions& options) const
{
	std::vector<ShapedRun> runs;
	if (!textShaper)
		return runs;

	for (const TextSpan& span : lineSpans) {
		if (!IsRenderableSpan(span))
			continue;

		TextSpan shapeSpan = span;
		std::string expandedStorage;
		if (options.expandTabs && span.text.find('\t') != std::string_view::npos) {
			expandedStorage = ExpandTabs(span.text, options.tabWidth);
			shapeSpan.text = expandedStorage;
			shapeSpan.printableLength = CountPrintableCodepoints(shapeSpan.text);
		}

		ShapeResult result = textShaper->ShapeSpan(shapeSpan, options.shapeOptions);
		for (ShapedRun& run : result.runs) {
			// ShapeSpan may preserve a view into the provided span text. When tabs
			// were expanded locally, that backing storage dies at the end of this
			// loop iteration, so keep renderer-facing source metadata anchored to
			// the original caller-owned span instead.
			if (!expandedStorage.empty())
				run.sourceSpan = span;

			runs.emplace_back(std::move(run));
		}
	}

	return runs;
}

void TextLayouter::EnsureGlyphsForRuns(std::span<const ShapedRun> runs) const
{
	if (!glyphCache)
		return;

	for (const ShapedRun& run : runs) {
		for (const ShapedGlyph& glyph : run.glyphs) {
			if (glyph.glyphKey.IsGlyphIndex()) {
				glyphCache->GetGlyphByGlyphIndex(glyph.glyphKey.glyphIndex, glyph.face, glyph.sourceCodepoint);
			} else {
				glyphCache->GetGlyphByCodepoint(glyph.glyphKey.codepoint);
			}
		}
	}
}

LaidOutLine TextLayouter::PositionRuns(std::span<const ShapedRun> runs, const LayoutContext& ctx, std::size_t sourceOffset, std::size_t sourceLength, bool explicitBreak) const
{
	LaidOutLine line;
	line.sourceOffset = sourceOffset;
	line.sourceLength = sourceLength;
	line.endsWithExplicitBreak = explicitBreak;

	const float scale = ResolveLayoutScale(ctx.options);
	float penX = 0.0f;
	float maxAscent = 0.0f;
	float minDescender = 0.0f;
	float maxLineHeight = (ctx.defaultLineHeight > 0.0f) ? ctx.defaultLineHeight * scale : scale;

	for (const ShapedRun& shapedRun : runs) {
		LaidOutRun run;
		run.sourceSpan = shapedRun.sourceSpan;
		run.x = penX;
		run.isRtl = shapedRun.isRtl;
		run.containsControlCodes = shapedRun.containsControlCodes;
		run.ascent = shapedRun.ascent * scale;
		run.descent = shapedRun.descent * scale;

		float runPenX = penX;
		for (const ShapedGlyph& shapedGlyph : shapedRun.glyphs) {
			LaidOutGlyph glyph;
			glyph.shaped = shapedGlyph;
			glyph.x = runPenX + (shapedGlyph.xOffset * scale);
			glyph.y = shapedGlyph.yOffset * scale;
			glyph.z = ctx.options.z;
			glyph.penX = runPenX;
			glyph.penY = 0.0f;
			glyph.lineOffsetX = 0.0f;
			glyph.lineOffsetY = 0.0f;
			glyph.visible = shapedGlyph.IsRenderable();
			glyph.usesOutline = HasOption(ctx.options.options, FontOption::Outline);
			glyph.usesShadow = HasOption(ctx.options.options, FontOption::Shadow);

			if (glyphCache != nullptr) {
				const GlyphInfo& glyphInfo = shapedGlyph.glyphKey.IsGlyphIndex()
					? glyphCache->GetGlyphByGlyphIndex(shapedGlyph.glyphKey.glyphIndex, shapedGlyph.face, shapedGlyph.sourceCodepoint)
					: glyphCache->GetGlyphByCodepoint(shapedGlyph.glyphKey.codepoint);

				glyph.atlasUV = glyphInfo.atlasUV;
				glyph.outlineAtlasUV = glyphInfo.shadowAtlasUV;
			}

			runPenX += shapedGlyph.xAdvance * scale;
			run.width = std::max(run.width, runPenX - penX);
			maxAscent = std::max(maxAscent, shapedGlyph.metrics.bounds.y * scale);
			minDescender = std::min(minDescender, shapedGlyph.metrics.descender * scale);
			maxLineHeight = std::max(maxLineHeight, shapedRun.lineHeight * scale);
			run.glyphs.emplace_back(std::move(glyph));
		}

		penX = runPenX;
		run.width = runPenX - run.x;
		line.width = std::max(line.width, penX);
		line.runs.emplace_back(std::move(run));
	}

	line.ascent = maxAscent;
	line.descent = minDescender;
	line.height = (maxLineHeight > 0.0f) ? maxLineHeight : (maxAscent - minDescender);
	line.baselineY = 0.0f;
	line.originX = ctx.options.x;
	line.originY = ctx.options.y;

	for (LaidOutRun& run : line.runs) {
		run.y = 0.0f;
		for (LaidOutGlyph& glyph : run.glyphs) {
			glyph.x += ctx.options.x;
			glyph.y += ctx.options.y;
			glyph.lineOffsetX = 0.0f;
			glyph.lineOffsetY = 0.0f;
		}
	}

	return line;
}

float TextLayouter::ComputeHorizontalLineOffset(float lineWidth, const LayoutOptions& options) const
{
	if (HasOption(options.options, FontOption::Center))
		return -0.5f * lineWidth;
	if (HasOption(options.options, FontOption::Right))
		return -lineWidth;
	return 0.0f;
}

float TextLayouter::ComputeVerticalBlockOffset(const TextMeasurement& measurement, const LayoutOptions& options, float fontDescender) const
{
	const float baselineDescender = (fontDescender != 0.0f) ? fontDescender : measurement.descent;

	if (HasOption(options.options, FontOption::Descender)) {
		return -baselineDescender;
	} else if (HasOption(options.options, FontOption::VCenter)) {
		return -0.5f * measurement.height - 0.5f * measurement.descent;
	} else if (HasOption(options.options, FontOption::Top)) {
		return -measurement.height;
	} else if (HasOption(options.options, FontOption::Ascender)) {
		return -(baselineDescender + 1.0f);
	} else if (HasOption(options.options, FontOption::Bottom)) {
		return -measurement.descent;
	}

	return 0.0f;
}

void TextLayouter::ApplyHorizontalAlignment(std::vector<LaidOutLine>& lines, const LayoutOptions& options) const
{
	for (LaidOutLine& line : lines) {
		const float offsetX = ComputeHorizontalLineOffset(line.width, options);
		line.originX += offsetX;

		for (LaidOutRun& run : line.runs) {
			run.x += offsetX;
			for (LaidOutGlyph& glyph : run.glyphs) {
				glyph.x += offsetX;
				glyph.lineOffsetX += offsetX;
			}
		}
	}
}

void TextLayouter::ApplyVerticalAlignment(std::vector<LaidOutLine>& lines, const TextMeasurement& measurement, const LayoutOptions& options, float fontDescender) const
{
	const float blockOffsetY = ComputeVerticalBlockOffset(measurement, options, fontDescender);
	float penY = blockOffsetY;

	for (LaidOutLine& line : lines) {
		line.originY += penY;
		line.baselineY += penY;

		for (LaidOutRun& run : line.runs) {
			run.y += penY;
			for (LaidOutGlyph& glyph : run.glyphs) {
				glyph.y += penY;
				glyph.penY = penY;
				glyph.lineOffsetY += penY;
			}
		}

		penY -= (line.height + options.lineSpacing);
	}
}

TextMeasurement TextLayouter::BuildMeasurement(std::span<const LaidOutLine> lines, const ParsedSpanBuffer& parsed, const LayoutOptions& options) const
{
	(void)options;

	TextMeasurement measurement;
	measurement.lineCount = lines.size();
	measurement.printableCodepointCount = parsed.printableLength;
	measurement.hadColorCodes = parsed.hadColorCodes;
	measurement.hadExplicitLineBreaks = parsed.hadExplicitLineBreaks;

	float totalHeight = 0.0f;
	for (const LaidOutLine& line : lines) {
		measurement.width = std::max(measurement.width, line.width);
		measurement.ascent = std::max(measurement.ascent, line.ascent);
		measurement.descent = std::min(measurement.descent, line.descent);
		measurement.lineHeight = std::max(measurement.lineHeight, line.height);
		totalHeight += line.height;

		for (const LaidOutRun& run : line.runs)
			measurement.glyphCount += run.glyphs.size();
	}

	if (!lines.empty())
		totalHeight += (lines.size() - 1) * options.lineSpacing;

	measurement.height = totalHeight;
	return measurement;
}

} // namespace font::text
