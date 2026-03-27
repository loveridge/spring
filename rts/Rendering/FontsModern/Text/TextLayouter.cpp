/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "TextLayouter.h"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "Rendering/FontsModern/Glyphs/GlyphAtlasCache.h"
#include "System/StringUtil.h"

namespace {

[[nodiscard]] float ResolveLayoutScale(const fonts::text::LayoutOptions& options)
{
	return (options.fontSize > 0.0f) ? options.fontSize : 1.0f;
}

[[nodiscard]] bool IsWhitespaceOnly(std::string_view text)
{
	return std::all_of(text.begin(), text.end(), [](unsigned char c) {
		return std::isspace(c) != 0;
	});
}

[[nodiscard]] bool IsWrapWhitespaceByte(char c) noexcept
{
	return c == ' ' || c == '\t';
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

[[nodiscard]] std::vector<std::size_t> CollectUtf8CodepointByteOffsets(std::string_view text)
{
	std::vector<std::size_t> offsets;
	offsets.reserve(CountPrintableCodepoints(text) + 1);

	const std::string utf8(text);
	for (int pos = 0; pos < static_cast<int>(utf8.size()); ) {
		offsets.emplace_back(static_cast<std::size_t>(pos));
		utf8::GetNextChar(utf8, pos);
	}

	offsets.emplace_back(text.size());
	return offsets;
}

[[nodiscard]] fonts::text::TextSpan MakeSubSpan(
	const fonts::text::TextSpan& span,
	std::size_t relativeByteOffset,
	std::size_t byteLength,
	std::size_t relativeCodepointOffset,
	std::size_t codepointLength
)
{
	fonts::text::TextSpan subSpan = span;
	subSpan.text = span.text.substr(relativeByteOffset, byteLength);
	subSpan.sourceOffset = span.sourceOffset + relativeByteOffset;
	subSpan.sourceLength = byteLength;
	subSpan.printableOffset = span.printableOffset + relativeCodepointOffset;
	subSpan.printableLength = codepointLength;
	subSpan.isWhitespace = IsWhitespaceOnly(subSpan.text);
	subSpan.isControl = false;
	subSpan.isLineBreak = false;
	subSpan.isParagraphBreak = false;
	return subSpan;
}

struct ClusterFragmentData {
	fonts::text::ShapedCluster cluster{};
	float width = 0.0f;
};

[[nodiscard]] std::vector<ClusterFragmentData> CollectClusterFragments(std::span<const fonts::text::ShapedRun> runs)
{
	std::vector<ClusterFragmentData> clusters;

	for (const fonts::text::ShapedRun& run : runs) {
		for (const fonts::text::ShapedCluster& cluster : run.clusters) {
			ClusterFragmentData data;
			data.cluster = cluster;

			const std::size_t glyphBegin = std::min(cluster.glyphStart, run.glyphs.size());
			const std::size_t glyphEnd = std::min(cluster.glyphStart + cluster.glyphCount, run.glyphs.size());
			for (std::size_t glyphIndex = glyphBegin; glyphIndex < glyphEnd; ++glyphIndex)
				data.width += run.glyphs[glyphIndex].xAdvance;

			clusters.emplace_back(std::move(data));
		}
	}

	std::sort(clusters.begin(), clusters.end(), [](const ClusterFragmentData& lhs, const ClusterFragmentData& rhs) {
		if (lhs.cluster.sourceByteStart != rhs.cluster.sourceByteStart)
			return lhs.cluster.sourceByteStart < rhs.cluster.sourceByteStart;

		return lhs.cluster.glyphStart < rhs.cluster.glyphStart;
	});

	return clusters;
}

[[nodiscard]] std::size_t ResolveLineSourceOffset(std::span<const fonts::text::TextSpan> spans, std::size_t begin)
{
	if (begin < spans.size())
		return spans[begin].sourceOffset;
	if (!spans.empty())
		return spans.back().sourceOffset + spans.back().sourceLength;
	return 0;
}

} // namespace

namespace fonts::text {

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

std::vector<MeasuredBreakFragment> TextLayouter::AnalyzeWrapText(std::string_view utf8, const LayoutOptions& options) const
{
	return AnalyzeWrapSpans(ParseTextSpansInternal(utf8, options.parseColorCodes).spans, options);
}

std::vector<MeasuredBreakFragment> TextLayouter::AnalyzeWrapSpans(std::span<const TextSpan> spans, const LayoutOptions& options) const
{
	std::vector<MeasuredBreakFragment> fragments;
	if (spans.empty())
		return fragments;

	LayoutOptions measureOptions = options;
	measureOptions.maxWidth = 0.0f;
	measureOptions.maxHeight = 0.0f;
	measureOptions.enableWrapping = false;
	measureOptions.ellipsize = false;

	auto appendMeasuredFallbackSpan = [this, &fragments, &measureOptions](const TextSpan& span) {
		const std::vector<std::size_t> codepointByteOffsets = CollectUtf8CodepointByteOffsets(span.text);
		std::size_t tokenByteStart = 0;
		std::size_t tokenCodepointStart = 0;

		while (tokenByteStart < span.text.size()) {
			const bool isWhitespace = IsWrapWhitespaceByte(span.text[tokenByteStart]);
			std::size_t tokenByteEnd = tokenByteStart;
			std::size_t tokenCodepointLength = 0;

			while (tokenByteEnd < span.text.size() && IsWrapWhitespaceByte(span.text[tokenByteEnd]) == isWhitespace) {
				const auto it = std::upper_bound(codepointByteOffsets.begin(), codepointByteOffsets.end(), tokenByteEnd);
				if (it == codepointByteOffsets.end())
					break;

				tokenByteEnd = *it;
				++tokenCodepointLength;
			}

			if (tokenByteEnd <= tokenByteStart)
				break;

			const TextSpan tokenSpan = MakeSubSpan(
				span,
				tokenByteStart,
				tokenByteEnd - tokenByteStart,
				tokenCodepointStart,
				tokenCodepointLength
			);

			MeasuredBreakFragment fragment;
			fragment.sourceSpan = tokenSpan;
			fragment.text = std::string(tokenSpan.text);
			fragment.width = MeasureText(tokenSpan.text, measureOptions).width;
			fragment.isWhitespace = isWhitespace;
			fragment.isLineBreak = false;
			fragment.isControlCode = false;

			if (tokenSpan.printableLength > 0) {
				BreakOpportunity breakOpportunity;
				breakOpportunity.sourceByteOffset = tokenSpan.sourceOffset + tokenSpan.sourceLength;
				breakOpportunity.codepointOffset = tokenSpan.printableOffset + tokenSpan.printableLength;
				breakOpportunity.xAdvance = fragment.width;
				breakOpportunity.allowed = isWhitespace;
				fragment.breakOpportunities.emplace_back(std::move(breakOpportunity));
			}

			fragments.emplace_back(std::move(fragment));
			tokenByteStart = tokenByteEnd;
			tokenCodepointStart += tokenCodepointLength;
		}
	};

	for (const TextSpan& span : spans) {
		if (span.isControl) {
			MeasuredBreakFragment fragment;
			fragment.sourceSpan = span;
			fragment.text = std::string(span.text);
			fragment.width = 0.0f;
			fragment.isWhitespace = false;
			fragment.isLineBreak = false;
			fragment.isControlCode = true;
			fragments.emplace_back(std::move(fragment));
			continue;
		}

		if (span.isLineBreak) {
			if (!options.preserveLineBreaks)
				continue;

			MeasuredBreakFragment fragment;
			fragment.sourceSpan = span;
			fragment.text = std::string(span.text);
			fragment.width = 0.0f;
			fragment.isWhitespace = false;
			fragment.isLineBreak = true;
			fragment.isControlCode = false;
			fragments.emplace_back(std::move(fragment));
			continue;
		}

		if (!IsRenderableSpan(span))
			continue;

		const bool spanHasTabs = (options.expandTabs && span.text.find('\t') != std::string_view::npos);
		if (textShaper == nullptr || spanHasTabs) {
			appendMeasuredFallbackSpan(span);
			continue;
		}

		const ShapeResult shapeResult = textShaper->ShapeSpan(span, options.shapeOptions);
		const std::vector<ClusterFragmentData> clusterData = CollectClusterFragments(shapeResult.runs);
		if (clusterData.empty()) {
			appendMeasuredFallbackSpan(span);
			continue;
		}

		bool haveFragment = false;
		bool fragmentIsWhitespace = false;
		std::size_t fragmentRelByteStart = 0;
		std::size_t fragmentRelByteEnd = 0;
		std::size_t fragmentRelCodepointStart = 0;
		std::size_t fragmentCodepointLength = 0;
		float fragmentWidth = 0.0f;
		std::vector<BreakOpportunity> fragmentBreaks;

		auto flushFragment = [&]() {
			if (!haveFragment)
				return;

			const TextSpan fragmentSpan = MakeSubSpan(
				span,
				fragmentRelByteStart,
				fragmentRelByteEnd - fragmentRelByteStart,
				fragmentRelCodepointStart,
				fragmentCodepointLength
			);

			MeasuredBreakFragment fragment;
			fragment.sourceSpan = fragmentSpan;
			fragment.text = std::string(fragmentSpan.text);
			fragment.width = fragmentWidth;
			fragment.isWhitespace = fragmentIsWhitespace;
			fragment.isLineBreak = false;
			fragment.isControlCode = false;
			fragment.breakOpportunities = std::move(fragmentBreaks);
			fragments.emplace_back(std::move(fragment));

			haveFragment = false;
			fragmentBreaks.clear();
		};

		for (const ClusterFragmentData& clusterDataEntry : clusterData) {
			const std::size_t clusterRelByteStart = clusterDataEntry.cluster.sourceByteStart - span.sourceOffset;
			const std::size_t clusterRelByteEnd = clusterRelByteStart + clusterDataEntry.cluster.sourceByteLength;
			const std::size_t clusterRelCodepointStart = clusterDataEntry.cluster.codepointOffset - span.printableOffset;
			const bool clusterIsWhitespace = IsWhitespaceOnly(span.text.substr(clusterRelByteStart, clusterDataEntry.cluster.sourceByteLength));

			if (!haveFragment) {
				haveFragment = true;
				fragmentIsWhitespace = clusterIsWhitespace;
				fragmentRelByteStart = clusterRelByteStart;
				fragmentRelByteEnd = clusterRelByteStart;
				fragmentRelCodepointStart = clusterRelCodepointStart;
				fragmentCodepointLength = 0;
				fragmentWidth = 0.0f;
				fragmentBreaks.clear();
			} else if (fragmentIsWhitespace != clusterIsWhitespace || fragmentRelByteEnd != clusterRelByteStart) {
				flushFragment();

				haveFragment = true;
				fragmentIsWhitespace = clusterIsWhitespace;
				fragmentRelByteStart = clusterRelByteStart;
				fragmentRelByteEnd = clusterRelByteStart;
				fragmentRelCodepointStart = clusterRelCodepointStart;
				fragmentCodepointLength = 0;
				fragmentWidth = 0.0f;
				fragmentBreaks.clear();
			}

			fragmentRelByteEnd = clusterRelByteEnd;
			fragmentCodepointLength += clusterDataEntry.cluster.codepointLength;
			fragmentWidth += clusterDataEntry.width;

			BreakOpportunity breakOpportunity;
			breakOpportunity.sourceByteOffset = clusterDataEntry.cluster.sourceByteStart + clusterDataEntry.cluster.sourceByteLength;
			breakOpportunity.codepointOffset = clusterDataEntry.cluster.codepointOffset + clusterDataEntry.cluster.codepointLength;
			breakOpportunity.xAdvance = fragmentWidth;
			breakOpportunity.allowed = fragmentIsWhitespace;
			fragmentBreaks.emplace_back(std::move(breakOpportunity));
		}

		flushFragment();
	}

	return fragments;
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
