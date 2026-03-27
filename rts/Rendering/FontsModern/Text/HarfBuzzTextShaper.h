/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include "TextShaper.h"

namespace fonts {
class FontFace;
class FontFaceSet;
}

namespace fonts::text {

/**
 * HarfBuzz-backed TextShaper implementation.
 *
 * This type owns the HarfBuzz font objects needed to shape text from one
 * primary face or a face-set with fallbacks. It intentionally stops at shaping:
 * it produces ShapedRun records and exposes no renderer, atlas, or wrapping
 * concerns.
 */
class HarfBuzzTextShaper final : public TextShaper {
public:
	struct ScriptDetectionHooks {
		bool autoDetectScript = true;
		bool autoDetectDirection = true;
		bool autoDetectLanguage = false;
	};

	using HbFontPtr = std::unique_ptr<hb_font_t, void(*)(hb_font_t*)>;
	using HbBufferPtr = std::unique_ptr<hb_buffer_t, void(*)(hb_buffer_t*)>;

public:
	HarfBuzzTextShaper();
	explicit HarfBuzzTextShaper(FT_Face ftFace);
	explicit HarfBuzzTextShaper(std::shared_ptr<fonts::FontFace> primaryFace);
	explicit HarfBuzzTextShaper(std::shared_ptr<fonts::FontFaceSet> faceSet);
	~HarfBuzzTextShaper() override = default;

	HarfBuzzTextShaper(const HarfBuzzTextShaper&) = delete;
	HarfBuzzTextShaper& operator=(const HarfBuzzTextShaper&) = delete;
	HarfBuzzTextShaper(HarfBuzzTextShaper&&) noexcept = default;
	HarfBuzzTextShaper& operator=(HarfBuzzTextShaper&&) noexcept = default;

	void SetPrimaryFace(FT_Face ftFace);
	void SetPrimaryFace(std::shared_ptr<fonts::FontFace> primaryFace);
	void SetFaceSet(std::shared_ptr<fonts::FontFaceSet> faceSet);

	FT_Face GetPrimaryFTFace() const noexcept;
	const std::shared_ptr<fonts::FontFace>& GetPrimaryFace() const noexcept { return primaryFace; }
	const std::shared_ptr<fonts::FontFaceSet>& GetFaceSet() const noexcept { return faces; }

	const ScriptDetectionHooks& GetScriptDetectionHooks() const noexcept { return detectionHooks; }
	void SetScriptDetectionHooks(const ScriptDetectionHooks& hooks) noexcept { detectionHooks = hooks; }

	ShapeResult ShapeUtf8Span(std::string_view utf8) const override;
	ShapeResult ShapeUtf8Span(std::string_view utf8, const ShapeOptions& options) const override;
	ShapeResult ShapeSpan(const TextSpan& span) const override;
	ShapeResult ShapeSpan(const TextSpan& span, const ShapeOptions& options) const override;

private:
	struct HbFontCacheEntry {
		std::shared_ptr<fonts::FontFace> face;
		HbFontPtr hbFont{nullptr, &hb_font_destroy};
	};

	struct FacePlanSegment {
		std::size_t sourceByteStart = 0;
		std::size_t sourceByteLength = 0;
		std::size_t codepointOffset = 0;
		std::size_t codepointLength = 0;
		std::shared_ptr<fonts::FontFace> face;
	};

	struct BufferShapeData {
		std::vector<ShapedGlyph> glyphs;
		std::vector<ShapedCluster> clusters;
		std::vector<BreakOpportunity> breakOpportunities;
		float width = 0.0f;
		float ascent = 0.0f;
		float descent = 0.0f;
		float lineHeight = 0.0f;
		bool isRtl = false;
		bool hadMissingGlyphs = false;
	};

private:
	static hb_direction_t ToHbDirection(ShapeOptions::Direction direction) noexcept;
	static ShapeOptions::Direction FromHbDirection(hb_direction_t direction) noexcept;
	static hb_script_t ResolveScript(std::string_view utf8, const ShapeOptions& options, const ScriptDetectionHooks& hooks) noexcept;
	static hb_direction_t ResolveDirection(std::string_view utf8, const ShapeOptions& options, const ScriptDetectionHooks& hooks, hb_script_t script) noexcept;
	static hb_language_t ResolveLanguage(const ShapeOptions& options, const ScriptDetectionHooks& hooks) noexcept;

	HbBufferPtr CreateBuffer() const;
	HbFontPtr CreateHbFont(FT_Face ftFace) const;
	HbFontPtr CreateHbFont(const std::shared_ptr<fonts::FontFace>& face) const;
	void ConfigureBuffer(hb_buffer_t* buffer, std::string_view utf8, const ShapeOptions& options) const;
	void ApplyFeatures(hb_buffer_t* buffer, const ShapeOptions& options, ShapeResult& result, ShapedRun& run) const;

	hb_font_t* GetOrCreateHbFont(FT_Face ftFace) const;
	hb_font_t* GetOrCreateHbFont(const std::shared_ptr<fonts::FontFace>& face) const;
	std::shared_ptr<fonts::FontFace> GetPreferredFace() const;
	std::shared_ptr<fonts::FontFace> ResolveFallbackFaceForText(std::string_view utf8, const ShapeOptions& options) const;

	BufferShapeData ShapeBuffer(
		const TextSpan& span,
		const ShapeOptions& options,
		const std::shared_ptr<fonts::FontFace>& face,
		ShapeResult& result
	) const;

	std::vector<ShapedCluster> ExtractClusters(
		std::string_view utf8,
		const TextSpan& sourceSpan,
		hb_buffer_t* buffer
	) const;

	std::vector<FacePlanSegment> BuildFallbackPlan(
		const TextSpan& span,
		const ShapeOptions& options,
		const std::shared_ptr<fonts::FontFace>& primaryFace,
		const BufferShapeData& primaryShape
	) const;

	ShapedRun BuildRunFromShapeData(
		const TextSpan& sourceSpan,
		const std::shared_ptr<fonts::FontFace>& runFace,
		const BufferShapeData& shapeData
	) const;

	ShapeResult ShapeSpanWithFallbackPlan(const TextSpan& span, const ShapeOptions& options) const;

	void ClearCachedHbFonts() const;

private:
	std::shared_ptr<fonts::FontFace> primaryFace;
	std::shared_ptr<fonts::FontFaceSet> faces;
	ScriptDetectionHooks detectionHooks{};
	FT_Face primaryFTFace = nullptr;

	mutable std::mutex hbFontCacheMutex;
	// TODO: cached hb_font_t objects are only valid while the underlying FT_Face
	// size and variation state remain unchanged.
	mutable std::unordered_map<const FT_FaceRec_*, HbFontCacheEntry> hbFontCache;
};

using HarfBuzzTextShaperPtr = std::shared_ptr<HarfBuzzTextShaper>;

} // namespace font::text
