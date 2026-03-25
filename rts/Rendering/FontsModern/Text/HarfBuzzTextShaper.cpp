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

[[nodiscard]] GlyphMetrics LoadGlyphMetrics(const std::shared_ptr<font::FontFace>& face, hb_codepoint_t glyphIndex)
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

} // namespace

namespace font::text {

HarfBuzzTextShaper::HarfBuzzTextShaper() = default;

HarfBuzzTextShaper::HarfBuzzTextShaper(FT_Face ftFace)
{
	SetPrimaryFace(ftFace);
}

HarfBuzzTextShaper::HarfBuzzTextShaper(std::shared_ptr<font::FontFace> primaryFace_)
{
	SetPrimaryFace(std::move(primaryFace_));
}

HarfBuzzTextShaper::HarfBuzzTextShaper(std::shared_ptr<font::FontFaceSet> faceSet)
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

void HarfBuzzTextShaper::SetPrimaryFace(std::shared_ptr<font::FontFace> primaryFace_)
{
	primaryFace = std::move(primaryFace_);
	faces.reset();
	primaryFTFace = (primaryFace != nullptr) ? primaryFace->GetFTFace() : nullptr;
	ClearCachedHbFonts();
}

void HarfBuzzTextShaper::SetFaceSet(std::shared_ptr<font::FontFaceSet> faceSet)
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
	span.printableLength = utf8.size();
	return ShapeSpan(span, options);
}

ShapeResult HarfBuzzTextShaper::ShapeSpan(const TextSpan& span) const
{
	return ShapeSpan(span, ShapeOptions{});
}

ShapeResult HarfBuzzTextShaper::ShapeSpan(const TextSpan& span, const ShapeOptions& options) const
{
	ShapeResult result;

	if (span.text.empty())
		return result;

	const FT_Face initialFTFace = GetPrimaryFTFace();
	if (initialFTFace == nullptr && (faces == nullptr || faces->Empty()))
		return result;

	struct FaceSegment {
		std::size_t start = 0;
		std::size_t end = 0;
		std::shared_ptr<font::FontFace> face;
		FT_Face ftFace = nullptr;
	};

	std::vector<FaceSegment> segments;
	const std::string utf8Copy(span.text);

	std::size_t segmentStart = 0;
	std::shared_ptr<font::FontFace> currentFace = nullptr;
	FT_Face currentFTFace = initialFTFace;
	bool haveSegment = false;

	for (int pos = 0; pos < static_cast<int>(utf8Copy.size()); ) {
		const int charStart = pos;
		const char32_t codepoint = utf8::GetNextChar(utf8Copy, pos);

		std::shared_ptr<font::FontFace> resolvedFace = ResolveFaceForCodepoint(codepoint);
		FT_Face resolvedFTFace = (resolvedFace != nullptr) ? resolvedFace->GetFTFace() : GetPrimaryFTFace();

		if (!haveSegment) {
			segmentStart = static_cast<std::size_t>(charStart);
			currentFace = resolvedFace;
			currentFTFace = resolvedFTFace;
			haveSegment = true;
			continue;
		}

		if ((resolvedFace != currentFace) || (resolvedFTFace != currentFTFace)) {
			segments.push_back(FaceSegment{
				.start = segmentStart,
				.end = static_cast<std::size_t>(charStart),
				.face = currentFace,
				.ftFace = currentFTFace,
			});

			segmentStart = static_cast<std::size_t>(charStart);
			currentFace = resolvedFace;
			currentFTFace = resolvedFTFace;
		}
	}

	if (haveSegment) {
		segments.push_back(FaceSegment{
			.start = segmentStart,
			.end = span.text.size(),
			.face = currentFace,
			.ftFace = currentFTFace,
		});
	}

	if (segments.empty()) {
		segments.push_back(FaceSegment{
			.start = 0,
			.end = span.text.size(),
			.face = primaryFace,
			.ftFace = GetPrimaryFTFace(),
		});
	}

	for (const FaceSegment& segment : segments) {
		if (segment.end <= segment.start || segment.ftFace == nullptr)
			continue;

		HbBufferPtr buffer = CreateBuffer();
		if (buffer == nullptr)
			continue;

		const std::string_view segmentText = span.text.substr(segment.start, segment.end - segment.start);
		TextSpan segmentSpan = span;
		segmentSpan.text = segmentText;
		segmentSpan.sourceOffset += segment.start;
		segmentSpan.sourceLength = segmentText.size();
		segmentSpan.printableOffset += segment.start;
		segmentSpan.printableLength = segmentText.size();

		ConfigureBuffer(buffer.get(), segmentText, options);

		std::vector<hb_feature_t> features;
		if (!options.allowLigatures) {
			features.emplace_back(MakeFeature("liga", 0));
			features.emplace_back(MakeFeature("clig", 0));
			features.emplace_back(MakeFeature("rlig", 0));
		}
		if (!options.allowKerning)
			features.emplace_back(MakeFeature("kern", 0));

		hb_shape(GetOrCreateHbFont(segment.ftFace), buffer.get(), features.empty() ? nullptr : features.data(), features.size());

		ShapedRun run = ConvertBufferToRun(segmentText, segmentSpan, options, segment.face, buffer.get(), result);
		if (run.Empty())
			continue;

		run.primaryFace = segment.face;
		run.endsWithLineBreak = (!segmentText.empty()) && (segmentText.back() == '\n' || segmentText.back() == '\r');
		run.containsControlCodes = false;

		if (primaryFace != nullptr && segment.face != nullptr && segment.face != primaryFace)
			result.usedFallbackFaces = true;

		result.width += run.width;
		result.ascent = std::max(result.ascent, run.ascent);
		result.descent = std::min(result.descent, run.descent);
		result.lineHeight = std::max(result.lineHeight, run.lineHeight);
		result.runs.emplace_back(std::move(run));
	}

	return result;
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
	return HbFontPtr(hb_ft_font_create_referenced(ftFace), &hb_font_destroy);
}

HarfBuzzTextShaper::HbFontPtr HarfBuzzTextShaper::CreateHbFont(const std::shared_ptr<font::FontFace>& face) const
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

hb_font_t* HarfBuzzTextShaper::GetOrCreateHbFont(const std::shared_ptr<font::FontFace>& face) const
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

std::shared_ptr<font::FontFace> HarfBuzzTextShaper::ResolveFaceForCodepoint(char32_t codepoint) const
{
	if (faces != nullptr) {
		if (auto face = faces->FindFaceForCodepoint(codepoint); face != nullptr)
			return face;

		return faces->GetPrimaryFace();
	}

	return primaryFace;
}

std::shared_ptr<font::FontFace> HarfBuzzTextShaper::ResolveFaceForGlyphInfo(char32_t codepoint, hb_codepoint_t glyphIndex) const
{
	if (faces != nullptr) {
		if (glyphIndex != 0u) {
			if (auto face = faces->FindFaceForGlyph(glyphIndex); face != nullptr)
				return face;
		}

		if (auto face = faces->FindFaceForCodepoint(codepoint); face != nullptr)
			return face;

		return faces->GetPrimaryFace();
	}

	return primaryFace;
}

ShapedRun HarfBuzzTextShaper::ConvertBufferToRun(
	std::string_view utf8,
	const TextSpan& sourceSpan,
	const ShapeOptions& options,
	const std::shared_ptr<font::FontFace>& runFace,
	hb_buffer_t* buffer,
	ShapeResult& result
) const {
	ShapedRun run;
	run.sourceSpan = sourceSpan;
	run.primaryFace = runFace;

	if (buffer == nullptr)
		return run;

	unsigned int glyphCount = 0;
	hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
	hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);
	if (glyphCount == 0 || infos == nullptr || positions == nullptr)
		return run;

	run.glyphs.reserve(glyphCount);
	run.isRtl = (FromHbDirection(hb_buffer_get_direction(buffer)) == ShapeOptions::Direction::RightToLeft);

	FT_Face metricsFace = (runFace != nullptr) ? runFace->GetFTFace() : GetPrimaryFTFace();
	if (metricsFace != nullptr && metricsFace->size != nullptr) {
		const float normScale = GetNormScale(metricsFace);
		run.ascent = metricsFace->size->metrics.ascender * normScale;
		run.descent = metricsFace->size->metrics.descender * normScale;
		run.lineHeight = metricsFace->size->metrics.height * normScale;
	}

	for (unsigned int i = 0; i < glyphCount; ++i) {
		const hb_glyph_info_t& info = infos[i];
		const hb_glyph_position_t& position = positions[i];

		ShapedGlyph glyph;
		glyph.cluster = static_cast<std::uint32_t>(sourceSpan.sourceOffset + info.cluster);
		glyph.logicalIndex = i;
		glyph.sourceCodepoint = DecodeCodepointAt(utf8, std::min<std::size_t>(info.cluster, utf8.size()));
		glyph.face = (runFace != nullptr) ? runFace : ResolveFaceForGlyphInfo(glyph.sourceCodepoint, info.codepoint);
		glyph.glyphKey = GlyphKey::FromGlyphIndex(info.codepoint, glyph.sourceCodepoint);

		const float normScale = (glyph.face != nullptr) ? GetNormScale(glyph.face->GetFTFace()) : GetNormScale(GetPrimaryFTFace());
		glyph.xAdvance = position.x_advance * normScale;
		glyph.yAdvance = position.y_advance * normScale;
		glyph.xOffset = position.x_offset * normScale;
		glyph.yOffset = position.y_offset * normScale;
		glyph.metrics = LoadGlyphMetrics(glyph.face, info.codepoint);

		if (info.codepoint == 0)
			result.hadMissingGlyphs = true;

		run.width += glyph.xAdvance;
		run.glyphs.emplace_back(std::move(glyph));
	}

	ApplyFeatures(buffer, options, result, run);
	return run;
}

void HarfBuzzTextShaper::ClearCachedHbFonts() const
{
	std::lock_guard<std::mutex> lock(hbFontCacheMutex);
	hbFontCache.clear();
}

} // namespace font::text
