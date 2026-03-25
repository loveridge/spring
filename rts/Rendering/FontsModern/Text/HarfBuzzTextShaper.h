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

namespace spring::font {
class FontFace;
class FontFaceSet;
}

namespace spring::font::text {

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
	explicit HarfBuzzTextShaper(std::shared_ptr<spring::font::FontFace> primaryFace);
	explicit HarfBuzzTextShaper(std::shared_ptr<spring::font::FontFaceSet> faceSet);
	~HarfBuzzTextShaper() override = default;

	HarfBuzzTextShaper(const HarfBuzzTextShaper&) = delete;
	HarfBuzzTextShaper& operator=(const HarfBuzzTextShaper&) = delete;
	HarfBuzzTextShaper(HarfBuzzTextShaper&&) noexcept = default;
	HarfBuzzTextShaper& operator=(HarfBuzzTextShaper&&) noexcept = default;

	void SetPrimaryFace(FT_Face ftFace);
	void SetPrimaryFace(std::shared_ptr<spring::font::FontFace> primaryFace);
	void SetFaceSet(std::shared_ptr<spring::font::FontFaceSet> faceSet);

	FT_Face GetPrimaryFTFace() const noexcept;
	const std::shared_ptr<spring::font::FontFace>& GetPrimaryFace() const noexcept { return primaryFace; }
	const std::shared_ptr<spring::font::FontFaceSet>& GetFaceSet() const noexcept { return faces; }

	const ScriptDetectionHooks& GetScriptDetectionHooks() const noexcept { return detectionHooks; }
	void SetScriptDetectionHooks(const ScriptDetectionHooks& hooks) noexcept { detectionHooks = hooks; }

	ShapeResult ShapeUtf8Span(std::string_view utf8) const override;
	ShapeResult ShapeUtf8Span(std::string_view utf8, const ShapeOptions& options) const override;
	ShapeResult ShapeSpan(const TextSpan& span) const override;
	ShapeResult ShapeSpan(const TextSpan& span, const ShapeOptions& options) const override;

private:
	struct HbFontCacheEntry {
		std::shared_ptr<spring::font::FontFace> face;
		HbFontPtr hbFont{nullptr, &hb_font_destroy};
	};

private:
	static hb_direction_t ToHbDirection(ShapeOptions::Direction direction) noexcept;
	static ShapeOptions::Direction FromHbDirection(hb_direction_t direction) noexcept;
	static hb_script_t ResolveScript(std::string_view utf8, const ShapeOptions& options, const ScriptDetectionHooks& hooks) noexcept;
	static hb_direction_t ResolveDirection(std::string_view utf8, const ShapeOptions& options, const ScriptDetectionHooks& hooks, hb_script_t script) noexcept;
	static hb_language_t ResolveLanguage(const ShapeOptions& options, const ScriptDetectionHooks& hooks) noexcept;

	HbBufferPtr CreateBuffer() const;
	HbFontPtr CreateHbFont(FT_Face ftFace) const;
	HbFontPtr CreateHbFont(const std::shared_ptr<spring::font::FontFace>& face) const;
	void ConfigureBuffer(hb_buffer_t* buffer, std::string_view utf8, const ShapeOptions& options) const;
	void ApplyFeatures(hb_buffer_t* buffer, const ShapeOptions& options, ShapeResult& result, ShapedRun& run) const;

	hb_font_t* GetOrCreateHbFont(FT_Face ftFace) const;
	hb_font_t* GetOrCreateHbFont(const std::shared_ptr<spring::font::FontFace>& face) const;
	std::shared_ptr<spring::font::FontFace> ResolveFaceForCodepoint(char32_t codepoint) const;
	std::shared_ptr<spring::font::FontFace> ResolveFaceForGlyphInfo(char32_t codepoint, hb_codepoint_t glyphIndex) const;

	ShapedRun ConvertBufferToRun(
		std::string_view utf8,
		const TextSpan& sourceSpan,
		const ShapeOptions& options,
		const std::shared_ptr<spring::font::FontFace>& runFace,
		hb_buffer_t* buffer,
		ShapeResult& result
	) const;

	void ClearCachedHbFonts() const;

private:
	std::shared_ptr<spring::font::FontFace> primaryFace;
	std::shared_ptr<spring::font::FontFaceSet> faces;
	ScriptDetectionHooks detectionHooks{};
	FT_Face primaryFTFace = nullptr;

	mutable std::mutex hbFontCacheMutex;
	mutable std::unordered_map<const FT_FaceRec_*, HbFontCacheEntry> hbFontCache;
};

using HarfBuzzTextShaperPtr = std::shared_ptr<HarfBuzzTextShaper>;

} // namespace font::text
