/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "HarfBuzzTextShaper.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "Rendering/FontsModern/FreeType/FontFace.h"
#include "Rendering/FontsModern/FreeType/FontFaceSet.h"
#include "System/StringUtil.h"

namespace {

static constexpr float FTMetricScale = 64.0f;

[[nodiscard]] char32_t DecodeCodepointAt(std::string_view utf8, std::size_t byteOffset)
{
	if (byteOffset >= utf8.size())
		return 0;

	const std::string utf8Copy(utf8);
	int pos = static_cast<int>(byteOffset);
	return utf8::GetNextChar(utf8Copy, pos);
}

[[nodiscard]] std::size_t CountUtf8Codepoints(std::string_view utf8)
{
	const std::string utf8Copy(utf8);
	std::size_t count = 0;

	for (int pos = 0; pos < static_cast<int>(utf8Copy.size()); ) {
		utf8::GetNextChar(utf8Copy, pos);
		++count;
	}

	return count;
}

[[nodiscard]] std::vector<std::size_t> CollectUtf8CodepointByteOffsets(std::string_view utf8)
{
	std::vector<std::size_t> offsets;
	offsets.reserve(CountUtf8Codepoints(utf8) + 1);

	const std::string utf8Copy(utf8);
	for (int pos = 0; pos < static_cast<int>(utf8Copy.size()); ) {
		offsets.emplace_back(static_cast<std::size_t>(pos));
		utf8::GetNextChar(utf8Copy, pos);
	}

	offsets.emplace_back(utf8.size());
	return offsets;
}

[[nodiscard]] hb_feature_t MakeFeature(const char* tag, std::uint32_t value)
{
	hb_feature_t feature{};
	feature.tag = hb_tag_from_string(tag, -1);
	feature.value = value;
	feature.start = HB_FEATURE_GLOBAL_START;
	feature.end = HB_FEATURE_GLOBAL_END;
	return feature;
}

[[nodiscard]] float GetNormScale(FT_Face face) noexcept
{
	if (face == nullptr || face->size == nullptr)
		return 1.0f / FTMetricScale;

	const auto yPpem = std::max<unsigned int>(1u, face->size->metrics.y_ppem);
	return 1.0f / (static_cast<float>(yPpem) * FTMetricScale);
}

[[nodiscard]] GlyphMetrics LoadGlyphMetrics(const std::shared_ptr<fonts::FontFace>& face, hb_codepoint_t glyphIndex)
{
	GlyphMetrics metrics{};

	if (face == nullptr || face->GetFTFace() == nullptr)
		return metrics;

	if (face->LoadGlyph(glyphIndex, FT_LOAD_DEFAULT) != 0)
		return metrics;

	const FT_GlyphSlot slot = face->GetFTFace()->glyph;
	if (slot == nullptr)
		return metrics;

	const float normScale = GetNormScale(face->GetFTFace());
	const float xBearing = slot->metrics.horiBearingX * normScale;
	const float yBearing = slot->metrics.horiBearingY * normScale;

	metrics.bounds.x = xBearing;
	metrics.bounds.y = yBearing;
	metrics.bounds.w = slot->metrics.width * normScale;
	metrics.bounds.h = -slot->metrics.height * normScale;
	metrics.advance = slot->advance.x * normScale;
	metrics.height = slot->metrics.height * normScale;
	metrics.descender = yBearing - metrics.height;

	if (metrics.advance == 0.0f && metrics.bounds.w > 0.0f)
		metrics.advance = metrics.bounds.w;

	return metrics;
}

[[nodiscard]] std::vector<hb_feature_t> BuildShapeFeatures(const fonts::text::ShapeOptions& options)
{
	std::vector<hb_feature_t> features;

	if (!options.allowLigatures) {
		features.emplace_back(MakeFeature("liga", 0));
		features.emplace_back(MakeFeature("clig", 0));
	}
	if (!options.allowKerning)
		features.emplace_back(MakeFeature("kern", 0));

	return features;
}

[[nodiscard]] fonts::text::TextSpan MakeSubSpan(
	const fonts::text::TextSpan& span,
	std::size_t relativeByteStart,
	std::size_t byteLength,
	std::size_t relativeCodepointOffset,
	std::size_t codepointLength
)
{
	fonts::text::TextSpan subSpan = span;
	subSpan.text = span.text.substr(relativeByteStart, byteLength);
	subSpan.sourceOffset = span.sourceOffset + relativeByteStart;
	subSpan.sourceLength = byteLength;
	subSpan.printableOffset = span.printableOffset + relativeCodepointOffset;
	subSpan.printableLength = codepointLength;
	return subSpan;
}

} // namespace

namespace fonts::text {

HarfBuzzTextShaper::HarfBuzzTextShaper() = default;

HarfBuzzTextShaper::HarfBuzzTextShaper(FT_Face ftFace)
{
	SetPrimaryFace(ftFace);
}

HarfBuzzTextShaper::HarfBuzzTextShaper(std::shared_ptr<fonts::FontFace> primaryFace_)
{
	SetPrimaryFace(std::move(primaryFace_));
}

HarfBuzzTextShaper::HarfBuzzTextShaper(std::shared_ptr<fonts::FontFaceSet> faceSet)
{
	SetFaceSet(std::move(faceSet));
}

void HarfBuzzTextShaper::SetPrimaryFace(FT_Face ftFace)
{
	primaryFace.reset();
	faces.reset();
	primaryFTFace = ftFace;
	ClearCachedHbFonts();
}

void HarfBuzzTextShaper::SetPrimaryFace(std::shared_ptr<fonts::FontFace> primaryFace_)
{
	primaryFace = std::move(primaryFace_);
	faces.reset();
	primaryFTFace = (primaryFace != nullptr) ? primaryFace->GetFTFace() : nullptr;
	ClearCachedHbFonts();
}

void HarfBuzzTextShaper::SetFaceSet(std::shared_ptr<fonts::FontFaceSet> faceSet)
{
	faces = std::move(faceSet);
	primaryFace = (faces != nullptr) ? faces->GetPrimaryFace() : nullptr;
	primaryFTFace = (primaryFace != nullptr) ? primaryFace->GetFTFace() : nullptr;
	ClearCachedHbFonts();
}

FT_Face HarfBuzzTextShaper::GetPrimaryFTFace() const noexcept
{
	if (primaryFace != nullptr)
		return primaryFace->GetFTFace();

	return primaryFTFace;
}

ShapeResult HarfBuzzTextShaper::ShapeUtf8Span(std::string_view utf8) const
{
	return ShapeUtf8Span(utf8, ShapeOptions{});
}

ShapeResult HarfBuzzTextShaper::ShapeUtf8Span(std::string_view utf8, const ShapeOptions& options) const
{
	TextSpan span;
	span.text = utf8;
	span.sourceLength = utf8.size();
	span.printableLength = CountUtf8Codepoints(utf8);
	return ShapeSpan(span, options);
}

ShapeResult HarfBuzzTextShaper::ShapeSpan(const TextSpan& span) const
{
	return ShapeSpan(span, ShapeOptions{});
}

ShapeResult HarfBuzzTextShaper::ShapeSpan(const TextSpan& span, const ShapeOptions& options) const
{
	return ShapeSpanWithFallbackPlan(span, options);
}

hb_direction_t HarfBuzzTextShaper::ToHbDirection(ShapeOptions::Direction direction) noexcept
{
	switch (direction) {
		case ShapeOptions::Direction::LeftToRight: return HB_DIRECTION_LTR;
		case ShapeOptions::Direction::RightToLeft: return HB_DIRECTION_RTL;
		case ShapeOptions::Direction::TopToBottom: return HB_DIRECTION_TTB;
		case ShapeOptions::Direction::BottomToTop: return HB_DIRECTION_BTT;
		case ShapeOptions::Direction::Auto: break;
	}

	return HB_DIRECTION_INVALID;
}

ShapeOptions::Direction HarfBuzzTextShaper::FromHbDirection(hb_direction_t direction) noexcept
{
	switch (direction) {
		case HB_DIRECTION_LTR: return ShapeOptions::Direction::LeftToRight;
		case HB_DIRECTION_RTL: return ShapeOptions::Direction::RightToLeft;
		case HB_DIRECTION_TTB: return ShapeOptions::Direction::TopToBottom;
		case HB_DIRECTION_BTT: return ShapeOptions::Direction::BottomToTop;
		default: break;
	}

	return ShapeOptions::Direction::Auto;
}

hb_script_t HarfBuzzTextShaper::ResolveScript(std::string_view utf8, const ShapeOptions& options, const ScriptDetectionHooks& hooks) noexcept
{
	if (!options.script.empty())
		return hb_script_from_string(options.script.data(), options.script.size());

	if (!hooks.autoDetectScript)
		return HB_SCRIPT_UNKNOWN;

	for (std::size_t i = 0; i < utf8.size(); ) {
		const char32_t codepoint = DecodeCodepointAt(utf8, i);
		if (codepoint == 0)
			break;

		const hb_script_t script = hb_unicode_script(hb_unicode_funcs_get_default(), codepoint);
		if (script != HB_SCRIPT_COMMON && script != HB_SCRIPT_INHERITED && script != HB_SCRIPT_UNKNOWN)
			return script;

		const std::string utf8Copy(utf8);
		int pos = static_cast<int>(i);
		utf8::GetNextChar(utf8Copy, pos);
		i = static_cast<std::size_t>(pos);
	}

	return HB_SCRIPT_UNKNOWN;
}

hb_direction_t HarfBuzzTextShaper::ResolveDirection(std::string_view utf8, const ShapeOptions& options, const ScriptDetectionHooks& hooks, hb_script_t script) noexcept
{
	const hb_direction_t explicitDirection = ToHbDirection(options.direction);
	if (explicitDirection != HB_DIRECTION_INVALID)
		return explicitDirection;

	if (!hooks.autoDetectDirection)
		return HB_DIRECTION_LTR;

	if (script != HB_SCRIPT_UNKNOWN) {
		const hb_direction_t scriptDirection = hb_script_get_horizontal_direction(script);
		if (scriptDirection != HB_DIRECTION_INVALID)
			return scriptDirection;
	}

	(void)utf8;
	return HB_DIRECTION_LTR;
}

hb_language_t HarfBuzzTextShaper::ResolveLanguage(const ShapeOptions& options, const ScriptDetectionHooks& hooks) noexcept
{
	if (!options.language.empty())
		return hb_language_from_string(options.language.data(), options.language.size());

	if (!hooks.autoDetectLanguage)
		return nullptr;

	return hb_language_get_default();
}

HarfBuzzTextShaper::HbBufferPtr HarfBuzzTextShaper::CreateBuffer() const
{
	return HbBufferPtr(hb_buffer_create(), &hb_buffer_destroy);
}

HarfBuzzTextShaper::HbFontPtr HarfBuzzTextShaper::CreateHbFont(FT_Face ftFace) const
{
	if (ftFace == nullptr)
		return HbFontPtr(nullptr, &hb_font_destroy);

	return HbFontPtr(hb_ft_font_create_referenced(ftFace), &hb_font_destroy);
}

HarfBuzzTextShaper::HbFontPtr HarfBuzzTextShaper::CreateHbFont(const std::shared_ptr<fonts::FontFace>& face) const
{
	return (face != nullptr) ? CreateHbFont(face->GetFTFace()) : HbFontPtr(nullptr, &hb_font_destroy);
}

void HarfBuzzTextShaper::ConfigureBuffer(hb_buffer_t* buffer, std::string_view utf8, const ShapeOptions& options) const
{
	hb_buffer_reset(buffer);
	hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
	hb_buffer_add_utf8(buffer, utf8.data(), utf8.size(), 0, utf8.size());
	hb_buffer_set_cluster_level(
		buffer,
		options.preserveClusters ? HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS : HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES
	);

	const hb_script_t script = ResolveScript(utf8, options, detectionHooks);
	const hb_direction_t direction = ResolveDirection(utf8, options, detectionHooks, script);
	const hb_language_t language = ResolveLanguage(options, detectionHooks);

	if (script != HB_SCRIPT_UNKNOWN)
		hb_buffer_set_script(buffer, script);
	if (direction != HB_DIRECTION_INVALID)
		hb_buffer_set_direction(buffer, direction);
	if (language != nullptr)
		hb_buffer_set_language(buffer, language);

	if ((script == HB_SCRIPT_UNKNOWN) || (direction == HB_DIRECTION_INVALID) || (language == nullptr && detectionHooks.autoDetectLanguage))
		hb_buffer_guess_segment_properties(buffer);
}

void HarfBuzzTextShaper::ApplyFeatures(hb_buffer_t* buffer, const ShapeOptions& options, ShapeResult& result, ShapedRun& run) const
{
	(void)buffer;
	(void)options;
	(void)result;
	(void)run;
}

hb_font_t* HarfBuzzTextShaper::GetOrCreateHbFont(FT_Face ftFace) const
{
	if (ftFace == nullptr)
		return nullptr;

	std::lock_guard<std::mutex> lock(hbFontCacheMutex);

	auto it = hbFontCache.find(ftFace);
	if (it != hbFontCache.end() && it->second.hbFont != nullptr)
		return it->second.hbFont.get();

	HbFontCacheEntry entry;
	entry.hbFont = CreateHbFont(ftFace);

	auto [insertedIt, inserted] = hbFontCache.emplace(ftFace, std::move(entry));
	(void)inserted;
	return insertedIt->second.hbFont.get();
}

hb_font_t* HarfBuzzTextShaper::GetOrCreateHbFont(const std::shared_ptr<fonts::FontFace>& face) const
{
	if (face == nullptr)
		return GetOrCreateHbFont(GetPrimaryFTFace());

	const FT_Face ftFace = face->GetFTFace();
	if (ftFace == nullptr)
		return nullptr;

	std::lock_guard<std::mutex> lock(hbFontCacheMutex);

	auto it = hbFontCache.find(ftFace);
	if (it != hbFontCache.end() && it->second.hbFont != nullptr)
		return it->second.hbFont.get();

	HbFontCacheEntry entry;
	entry.face = face;
	entry.hbFont = CreateHbFont(face);

	auto [insertedIt, inserted] = hbFontCache.emplace(ftFace, std::move(entry));
	(void)inserted;
	return insertedIt->second.hbFont.get();
}

std::shared_ptr<fonts::FontFace> HarfBuzzTextShaper::GetPreferredFace() const
{
	if (faces != nullptr && faces->GetPrimaryFace() != nullptr)
		return faces->GetPrimaryFace();

	return primaryFace;
}

std::shared_ptr<fonts::FontFace> HarfBuzzTextShaper::ResolveFallbackFaceForText(std::string_view utf8, const ShapeOptions& options) const
{
	const std::shared_ptr<fonts::FontFace> preferredFace = GetPreferredFace();
	if (utf8.empty())
		return preferredFace;

	std::vector<std::shared_ptr<fonts::FontFace>> candidateFaces;
	if (faces != nullptr) {
		candidateFaces = faces->GetFaces();
	} else if (preferredFace != nullptr) {
		candidateFaces.emplace_back(preferredFace);
	}

	if (candidateFaces.empty())
		return preferredFace;

	TextSpan span;
	span.text = utf8;
	span.sourceLength = utf8.size();
	span.printableLength = CountUtf8Codepoints(utf8);

	for (const std::shared_ptr<fonts::FontFace>& face : candidateFaces) {
		if (face == nullptr)
			continue;

		ShapeResult probeResult;
		const BufferShapeData probe = ShapeBuffer(span, options, face, probeResult);
		if (!probe.hadMissingGlyphs)
			return face;
	}

	return preferredFace;
}

HarfBuzzTextShaper::BufferShapeData HarfBuzzTextShaper::ShapeBuffer(
	const TextSpan& span,
	const ShapeOptions& options,
	const std::shared_ptr<fonts::FontFace>& face,
	ShapeResult& result
) const
{
	BufferShapeData shapeData;

	if (span.text.empty())
		return shapeData;

	HbBufferPtr buffer = CreateBuffer();
	if (buffer == nullptr)
		return shapeData;

	ConfigureBuffer(buffer.get(), span.text, options);

	const std::vector<hb_feature_t> features = BuildShapeFeatures(options);
	hb_font_t* hbFont = GetOrCreateHbFont(face);
	if (hbFont == nullptr)
		return shapeData;

	hb_shape(hbFont, buffer.get(), features.empty() ? nullptr : features.data(), features.size());

	unsigned int glyphCount = 0;
	hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer.get(), &glyphCount);
	hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer.get(), &glyphCount);
	if (glyphCount == 0 || infos == nullptr || positions == nullptr)
		return shapeData;

	shapeData.glyphs.reserve(glyphCount);
	shapeData.isRtl = (FromHbDirection(hb_buffer_get_direction(buffer.get())) == ShapeOptions::Direction::RightToLeft);

	FT_Face metricsFace = (face != nullptr) ? face->GetFTFace() : GetPrimaryFTFace();
	if (metricsFace != nullptr && metricsFace->size != nullptr) {
		const float normScale = GetNormScale(metricsFace);
		shapeData.ascent = metricsFace->size->metrics.ascender * normScale;
		shapeData.descent = metricsFace->size->metrics.descender * normScale;
		shapeData.lineHeight = metricsFace->size->metrics.height * normScale;
	}

	for (unsigned int i = 0; i < glyphCount; ++i) {
		const hb_glyph_info_t& info = infos[i];
		const hb_glyph_position_t& position = positions[i];

		ShapedGlyph glyph;
		glyph.cluster = static_cast<std::uint32_t>(span.sourceOffset + info.cluster);
		glyph.logicalIndex = i;
		glyph.sourceCodepoint = DecodeCodepointAt(span.text, std::min<std::size_t>(info.cluster, span.text.size()));
		glyph.face = face;
		glyph.glyphKey = GlyphKey::FromGlyphIndex(info.codepoint, glyph.sourceCodepoint);

		const float normScale = (glyph.face != nullptr) ? GetNormScale(glyph.face->GetFTFace()) : GetNormScale(GetPrimaryFTFace());
		glyph.xAdvance = position.x_advance * normScale;
		glyph.yAdvance = position.y_advance * normScale;
		glyph.xOffset = position.x_offset * normScale;
		glyph.yOffset = position.y_offset * normScale;
		glyph.metrics = LoadGlyphMetrics(glyph.face, info.codepoint);

		if (info.codepoint == 0) {
			shapeData.hadMissingGlyphs = true;
			result.hadMissingGlyphs = true;
		}

		shapeData.width += glyph.xAdvance;
		shapeData.glyphs.emplace_back(std::move(glyph));
	}

	shapeData.clusters = ExtractClusters(span.text, span, buffer.get());
	return shapeData;
}

std::vector<ShapedCluster> HarfBuzzTextShaper::ExtractClusters(std::string_view utf8, const TextSpan& sourceSpan, hb_buffer_t* buffer) const
{
	std::vector<ShapedCluster> clusters;
	if (buffer == nullptr)
		return clusters;

	unsigned int glyphCount = 0;
	hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
	if (glyphCount == 0 || infos == nullptr)
		return clusters;

	struct ClusterAccumulator {
		std::size_t byteStart = 0;
		std::size_t glyphStart = 0;
		std::size_t glyphCount = 0;
		bool hasMissingGlyph = false;
	};

	std::vector<ClusterAccumulator> groupedClusters;
	groupedClusters.reserve(glyphCount);

	for (unsigned int i = 0; i < glyphCount; ) {
		const std::size_t byteStart = std::min<std::size_t>(infos[i].cluster, utf8.size());

		ClusterAccumulator cluster;
		cluster.byteStart = byteStart;
		cluster.glyphStart = i;

		while (i < glyphCount && std::min<std::size_t>(infos[i].cluster, utf8.size()) == byteStart) {
			cluster.hasMissingGlyph = cluster.hasMissingGlyph || (infos[i].codepoint == 0);
			++cluster.glyphCount;
			++i;
		}

		groupedClusters.emplace_back(std::move(cluster));
	}

	std::vector<std::size_t> orderedIndices(groupedClusters.size());
	for (std::size_t i = 0; i < orderedIndices.size(); ++i)
		orderedIndices[i] = i;

	std::sort(orderedIndices.begin(), orderedIndices.end(), [&groupedClusters](std::size_t lhs, std::size_t rhs) {
		if (groupedClusters[lhs].byteStart != groupedClusters[rhs].byteStart)
			return groupedClusters[lhs].byteStart < groupedClusters[rhs].byteStart;

		return groupedClusters[lhs].glyphStart < groupedClusters[rhs].glyphStart;
	});

	const std::vector<std::size_t> codepointByteOffsets = CollectUtf8CodepointByteOffsets(utf8);
	clusters.reserve(groupedClusters.size());

	for (std::size_t orderedIndex = 0; orderedIndex < orderedIndices.size(); ++orderedIndex) {
		const ClusterAccumulator& groupedCluster = groupedClusters[orderedIndices[orderedIndex]];
		const std::size_t nextByteStart = (orderedIndex + 1 < orderedIndices.size())
			? groupedClusters[orderedIndices[orderedIndex + 1]].byteStart
			: utf8.size();
		const std::size_t byteEnd = std::max(groupedCluster.byteStart, nextByteStart);

		const auto codepointBeginIt = std::lower_bound(codepointByteOffsets.begin(), codepointByteOffsets.end(), groupedCluster.byteStart);
		const auto codepointEndIt = std::lower_bound(codepointByteOffsets.begin(), codepointByteOffsets.end(), byteEnd);

		ShapedCluster cluster;
		cluster.sourceByteStart = sourceSpan.sourceOffset + groupedCluster.byteStart;
		cluster.sourceByteLength = byteEnd - groupedCluster.byteStart;
		cluster.glyphStart = groupedCluster.glyphStart;
		cluster.glyphCount = groupedCluster.glyphCount;
		cluster.codepointOffset = sourceSpan.printableOffset + static_cast<std::size_t>(std::distance(codepointByteOffsets.begin(), codepointBeginIt));
		cluster.codepointLength = static_cast<std::size_t>(std::distance(codepointBeginIt, codepointEndIt));
		cluster.hasMissingGlyph = groupedCluster.hasMissingGlyph;
		cluster.unsafeToBreakInside = true;
		clusters.emplace_back(std::move(cluster));
	}

	return clusters;
}

std::vector<HarfBuzzTextShaper::FacePlanSegment> HarfBuzzTextShaper::BuildFallbackPlan(
	const TextSpan& span,
	const ShapeOptions& options,
	const std::shared_ptr<fonts::FontFace>& preferredFace,
	const BufferShapeData& primaryShape
) const
{
	std::vector<FacePlanSegment> plan;

	if (primaryShape.clusters.empty()) {
		plan.push_back(FacePlanSegment{
			.sourceByteStart = span.sourceOffset,
			.sourceByteLength = span.sourceLength,
			.codepointOffset = span.printableOffset,
			.codepointLength = span.printableLength,
			.face = preferredFace,
		});
		return plan;
	}

	for (const ShapedCluster& cluster : primaryShape.clusters) {
		std::shared_ptr<fonts::FontFace> segmentFace = preferredFace;

		if (cluster.hasMissingGlyph) {
			const std::size_t relativeByteStart = cluster.sourceByteStart - span.sourceOffset;
			const std::size_t relativeCodepointOffset = cluster.codepointOffset - span.printableOffset;
			const TextSpan clusterSpan = MakeSubSpan(span, relativeByteStart, cluster.sourceByteLength, relativeCodepointOffset, cluster.codepointLength);
			segmentFace = ResolveFallbackFaceForText(clusterSpan.text, options);
		}

		if (!plan.empty()) {
			FacePlanSegment& tail = plan.back();
			const bool adjacent = (tail.sourceByteStart + tail.sourceByteLength) == cluster.sourceByteStart;
			if (adjacent && tail.face == segmentFace) {
				tail.sourceByteLength += cluster.sourceByteLength;
				tail.codepointLength += cluster.codepointLength;
				continue;
			}
		}

		plan.push_back(FacePlanSegment{
			.sourceByteStart = cluster.sourceByteStart,
			.sourceByteLength = cluster.sourceByteLength,
			.codepointOffset = cluster.codepointOffset,
			.codepointLength = cluster.codepointLength,
			.face = segmentFace,
		});
	}

	return plan;
}

ShapedRun HarfBuzzTextShaper::BuildRunFromShapeData(
	const TextSpan& sourceSpan,
	const std::shared_ptr<fonts::FontFace>& runFace,
	const BufferShapeData& shapeData
) const {
	ShapedRun run;
	run.sourceSpan = sourceSpan;
	run.glyphs = shapeData.glyphs;
	run.clusters = shapeData.clusters;
	run.breakOpportunities = shapeData.breakOpportunities;
	run.primaryFace = runFace;
	run.width = shapeData.width;
	run.ascent = shapeData.ascent;
	run.descent = shapeData.descent;
	run.lineHeight = shapeData.lineHeight;
	run.isRtl = shapeData.isRtl;
	run.hadMissingGlyphs = shapeData.hadMissingGlyphs;
	run.endsWithLineBreak = (!sourceSpan.text.empty()) && (sourceSpan.text.back() == '\n' || sourceSpan.text.back() == '\r');
	run.containsControlCodes = false;
	return run;
}

ShapeResult HarfBuzzTextShaper::ShapeSpanWithFallbackPlan(const TextSpan& span, const ShapeOptions& options) const
{
	ShapeResult result;

	if (span.text.empty())
		return result;

	const FT_Face initialFTFace = GetPrimaryFTFace();
	if (initialFTFace == nullptr && (faces == nullptr || faces->Empty()))
		return result;

	const std::shared_ptr<fonts::FontFace> preferredFace = GetPreferredFace();

	ShapeResult primaryProbeResult;
	const BufferShapeData primaryShape = ShapeBuffer(span, options, preferredFace, primaryProbeResult);
	if (primaryShape.glyphs.empty())
		return result;

	auto appendRun = [&result](ShapedRun run) {
		if (run.Empty())
			return;

		result.width += run.width;
		result.ascent = std::max(result.ascent, run.ascent);
		result.descent = std::min(result.descent, run.descent);
		result.lineHeight = std::max(result.lineHeight, run.lineHeight);
		result.hadMissingGlyphs = result.hadMissingGlyphs || run.hadMissingGlyphs;
		result.runs.emplace_back(std::move(run));
	};

	if (!primaryShape.hadMissingGlyphs) {
		appendRun(BuildRunFromShapeData(span, preferredFace, primaryShape));
		return result;
	}

	const std::vector<FacePlanSegment> plan = BuildFallbackPlan(span, options, preferredFace, primaryShape);
	for (const FacePlanSegment& segment : plan) {
		if (segment.sourceByteLength == 0)
			continue;

		const std::size_t relativeByteStart = segment.sourceByteStart - span.sourceOffset;
		const std::size_t relativeCodepointOffset = segment.codepointOffset - span.printableOffset;
		const TextSpan segmentSpan = MakeSubSpan(span, relativeByteStart, segment.sourceByteLength, relativeCodepointOffset, segment.codepointLength);

		ShapeResult segmentShapeResult;
		const BufferShapeData segmentShape =
			(segment.face == preferredFace && segment.sourceByteStart == span.sourceOffset && segment.sourceByteLength == span.sourceLength)
				? primaryShape
				: ShapeBuffer(segmentSpan, options, segment.face, segmentShapeResult);

		appendRun(BuildRunFromShapeData(segmentSpan, segment.face, segmentShape));

		if (segment.face != nullptr && segment.face != preferredFace)
			result.usedFallbackFaces = true;
	}

	return result;
}

void HarfBuzzTextShaper::ClearCachedHbFonts() const
{
	std::lock_guard<std::mutex> lock(hbFontCacheMutex);
	hbFontCache.clear();
}

} // namespace font::text
