/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GlyphAtlasCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_BBOX_H
#include FT_GLYPH_H
#include FT_STROKER_H

#include "Rendering/FontsModern/FreeType/FontFace.h"
#include "Rendering/FontsModern/FreeType/FontFaceSet.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Textures/Bitmap.h"
#include "Rendering/Textures/RowAtlasAlloc.h"
#include "System/Config/ConfigHandler.h"
#include "System/Log/ILog.h"
#include "System/Rectangle.h"

CONFIG(bool, AllowColorFonts).defaultValue(true).description("Allows fonts to use embedded color glyphs when available.");

#ifndef HEADLESS
static constexpr int FT_INTERNAL_DPI = 64;
#endif

namespace {

static constexpr char32_t PrimaryReplacementCodepoint = 0xfffd;
static constexpr char32_t SecondaryReplacementCodepoint = U'?';
static constexpr std::uint32_t SlugTextureWidth = 4096u;
static constexpr std::uint32_t SlugBandCount = 16u;
static constexpr float SlugOutlineStrokeScale = 0.2f;

struct SlugCurveData {
	float x1 = 0.0f;
	float y1 = 0.0f;
	float x2 = 0.0f;
	float y2 = 0.0f;
	float x3 = 0.0f;
	float y3 = 0.0f;
	std::uint32_t texelIndex = 0;
};

[[nodiscard]] float Min3(float a, float b, float c) noexcept
{
	return std::min(a, std::min(b, c));
}

[[nodiscard]] float Max3(float a, float b, float c) noexcept
{
	return std::max(a, std::max(b, c));
}

[[nodiscard]] bool NearlyEqual(float a, float b, float eps = 1.0e-6f) noexcept
{
	return std::fabs(a - b) <= eps;
}

struct SlugOutlineBuilder {
	std::vector<SlugCurveData> curves;
	FT_Vector currentPoint {};
	FT_Vector contourStart {};
	float minX = 0.0f;
	float minY = 0.0f;
	float scale = 1.0f;
	bool havePoint = false;
	bool contourOpen = false;

	[[nodiscard]] float ToX(const FT_Vector& point) const noexcept { return (point.x - minX) * scale; }
	[[nodiscard]] float ToY(const FT_Vector& point) const noexcept { return (point.y - minY) * scale; }

	void AddQuadratic(const FT_Vector& control, const FT_Vector& endPoint)
	{
		if (!havePoint)
			return;

		SlugCurveData curve;
		curve.x1 = ToX(currentPoint);
		curve.y1 = ToY(currentPoint);
		curve.x2 = ToX(control);
		curve.y2 = ToY(control);
		curve.x3 = ToX(endPoint);
		curve.y3 = ToY(endPoint);

		if ((NearlyEqual(curve.x1, curve.x2) && NearlyEqual(curve.y1, curve.y2)) ||
		    (NearlyEqual(curve.x2, curve.x3) && NearlyEqual(curve.y2, curve.y3))) {
			curve.x2 = 0.5f * (curve.x1 + curve.x3);
			curve.y2 = 0.5f * (curve.y1 + curve.y3);
		}

		curves.emplace_back(curve);
		currentPoint = endPoint;
		havePoint = true;
	}

	void AddLine(const FT_Vector& endPoint)
	{
		FT_Vector control;
		control.x = (currentPoint.x + endPoint.x) / 2;
		control.y = (currentPoint.y + endPoint.y) / 2;
		AddQuadratic(control, endPoint);
	}

	static FT_Vector LerpPoint(const FT_Vector& a, const FT_Vector& b, float t)
	{
		FT_Vector out;
		out.x = static_cast<FT_Pos>(std::lround((1.0 - t) * static_cast<double>(a.x) + t * static_cast<double>(b.x)));
		out.y = static_cast<FT_Pos>(std::lround((1.0 - t) * static_cast<double>(a.y) + t * static_cast<double>(b.y)));
		return out;
	}

	static FT_Vector EvalCubic(const FT_Vector& p0, const FT_Vector& p1, const FT_Vector& p2, const FT_Vector& p3, float t)
	{
		const float omt = 1.0f - t;
		const float omt2 = omt * omt;
		const float t2 = t * t;

		FT_Vector out;
		out.x = static_cast<FT_Pos>(std::lround(
			omt2 * omt * static_cast<double>(p0.x) +
			3.0 * omt2 * t * static_cast<double>(p1.x) +
			3.0 * omt * t2 * static_cast<double>(p2.x) +
			t2 * t * static_cast<double>(p3.x)
		));
		out.y = static_cast<FT_Pos>(std::lround(
			omt2 * omt * static_cast<double>(p0.y) +
			3.0 * omt2 * t * static_cast<double>(p1.y) +
			3.0 * omt * t2 * static_cast<double>(p2.y) +
			t2 * t * static_cast<double>(p3.y)
		));
		return out;
	}

	static FT_Vector EvalQuadratic(const FT_Vector& p0, const FT_Vector& p1, const FT_Vector& p2, float t)
	{
		const float omt = 1.0f - t;
		const float omt2 = omt * omt;
		const float t2 = t * t;

		FT_Vector out;
		out.x = static_cast<FT_Pos>(std::lround(
			omt2 * static_cast<double>(p0.x) +
			2.0 * omt * t * static_cast<double>(p1.x) +
			t2 * static_cast<double>(p2.x)
		));
		out.y = static_cast<FT_Pos>(std::lround(
			omt2 * static_cast<double>(p0.y) +
			2.0 * omt * t * static_cast<double>(p1.y) +
			t2 * static_cast<double>(p2.y)
		));
		return out;
	}

	static FT_Vector FitQuadraticControl(const FT_Vector& p0, const FT_Vector& p3, const FT_Vector& cubicMid)
	{
		FT_Vector control;
		control.x = static_cast<FT_Pos>(std::lround(2.0 * static_cast<double>(cubicMid.x) - 0.5 * static_cast<double>(p0.x + p3.x)));
		control.y = static_cast<FT_Pos>(std::lround(2.0 * static_cast<double>(cubicMid.y) - 0.5 * static_cast<double>(p0.y + p3.y)));
		return control;
	}

	static bool CubicFitsQuadratic(const FT_Vector& p0, const FT_Vector& c1, const FT_Vector& c2, const FT_Vector& p3, const FT_Vector& q1)
	{
		constexpr std::array<float, 2> sampleTs = {0.25f, 0.75f};
		constexpr double maxErrorPixels = 0.125;
		constexpr double ftUnitsPerPixel = 64.0;
		constexpr double maxErrorSq = maxErrorPixels * maxErrorPixels;

		for (float t: sampleTs) {
			const FT_Vector cubicPoint = EvalCubic(p0, c1, c2, p3, t);
			const FT_Vector quadraticPoint = EvalQuadratic(p0, q1, p3, t);
			const double dx = (static_cast<double>(cubicPoint.x) - static_cast<double>(quadraticPoint.x)) / ftUnitsPerPixel;
			const double dy = (static_cast<double>(cubicPoint.y) - static_cast<double>(quadraticPoint.y)) / ftUnitsPerPixel;

			if ((dx * dx + dy * dy) > maxErrorSq)
				return false;
		}

		return true;
	}

	void AddCubic(const FT_Vector& c1, const FT_Vector& c2, const FT_Vector& endPoint)
	{
		const FT_Vector p0 = currentPoint;
		const auto addCubicRecursive = [&](const auto& self, const FT_Vector& rp0, const FT_Vector& rc1, const FT_Vector& rc2, const FT_Vector& rp3, int depth) -> void {
			const FT_Vector cubicMid = EvalCubic(rp0, rc1, rc2, rp3, 0.5f);
			const FT_Vector quadraticControl = FitQuadraticControl(rp0, rp3, cubicMid);
			constexpr int maxSubdivisionDepth = 8;

			if (depth >= maxSubdivisionDepth || CubicFitsQuadratic(rp0, rc1, rc2, rp3, quadraticControl)) {
				AddQuadratic(quadraticControl, rp3);
				return;
			}

			const FT_Vector p01 = LerpPoint(rp0, rc1, 0.5f);
			const FT_Vector p12 = LerpPoint(rc1, rc2, 0.5f);
			const FT_Vector p23 = LerpPoint(rc2, rp3, 0.5f);
			const FT_Vector p012 = LerpPoint(p01, p12, 0.5f);
			const FT_Vector p123 = LerpPoint(p12, p23, 0.5f);
			const FT_Vector p0123 = LerpPoint(p012, p123, 0.5f);

			self(self, rp0, p01, p012, p0123, depth + 1);
			self(self, p0123, p123, p23, rp3, depth + 1);
		};

		addCubicRecursive(addCubicRecursive, p0, c1, c2, endPoint, 0);
	}

	void CloseContour()
	{
		if (!contourOpen || !havePoint)
			return;

		if (currentPoint.x != contourStart.x || currentPoint.y != contourStart.y)
			AddLine(contourStart);

		currentPoint = contourStart;
		contourOpen = false;
		havePoint = false;
	}

	static int MoveTo(const FT_Vector* to, void* user)
	{
		auto* builder = static_cast<SlugOutlineBuilder*>(user);
		builder->CloseContour();
		builder->contourStart = *to;
		builder->currentPoint = *to;
		builder->havePoint = true;
		builder->contourOpen = true;
		return 0;
	}

	static int LineTo(const FT_Vector* to, void* user)
	{
		static_cast<SlugOutlineBuilder*>(user)->AddLine(*to);
		return 0;
	}

	static int ConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
	{
		static_cast<SlugOutlineBuilder*>(user)->AddQuadratic(*control, *to);
		return 0;
	}

	static int CubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user)
	{
		static_cast<SlugOutlineBuilder*>(user)->AddCubic(*c1, *c2, *to);
		return 0;
	}
};

[[nodiscard]] std::uint64_t MakeAsciiKerningHash(char32_t left, char32_t right) noexcept
{
	return (static_cast<std::uint64_t>(left) << 7u) | static_cast<std::uint64_t>(right);
}

[[nodiscard]] std::uint64_t MakeDynamicKerningHash(const GlyphInfo& leftGlyph, const GlyphInfo& rightGlyph) noexcept
{
	std::uint64_t hash = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(leftGlyph.face.get()));
	hash ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(rightGlyph.face.get())) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
	hash ^= static_cast<std::uint64_t>(leftGlyph.glyphIndex) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
	hash ^= static_cast<std::uint64_t>(rightGlyph.glyphIndex) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
	return hash;
}

[[nodiscard]] std::string MakeAtlasGlyphKey(const GlyphInfo& glyph)
{
	std::string key = "glyph:";
	key += std::to_string(reinterpret_cast<std::uintptr_t>(glyph.face.get()));
	key += ':';
	key += std::to_string(glyph.glyphIndex);
	key += ':';
	key += std::to_string(static_cast<std::uint32_t>(glyph.sourceCodepoint));
	return key;
}

[[nodiscard]] std::string MakeOutlineGlyphKey(const std::string& glyphKey)
{
	return glyphKey + ":outline";
}

[[nodiscard]] GlyphRect ToGlyphRectExclusive(const AtlasedTexture& rect, const GlyphAtlasTexture& atlas) noexcept
{
	if (atlas.GetWidth() <= 0 || atlas.GetHeight() <= 0)
		return {};

	if (rect.x2 <= rect.x1 || rect.y2 <= rect.y1)
		return {};

	const float invW = 1.0f / atlas.GetWidth();
	const float invH = 1.0f / atlas.GetHeight();

	return GlyphRect(
		rect.x1 * invW,
		rect.y1 * invH,
		(rect.x2 - rect.x1) * invW,
		(rect.y2 - rect.y1) * invH
	);
}

void RescaleGlyphRectUV(GlyphRect& rect, int oldWidth, int oldHeight, int newWidth, int newHeight) noexcept
{
	if (rect.Empty())
		return;
	if (oldWidth <= 0 || oldHeight <= 0 || newWidth <= 0 || newHeight <= 0)
		return;
	if (oldWidth == newWidth && oldHeight == newHeight)
		return;

	const float scaleX = static_cast<float>(oldWidth) / static_cast<float>(newWidth);
	const float scaleY = static_cast<float>(oldHeight) / static_cast<float>(newHeight);

	rect.x *= scaleX;
	rect.y *= scaleY;
	rect.w *= scaleX;
	rect.h *= scaleY;
}

[[nodiscard]] bool AllowColorFonts() noexcept
{
	return (configHandler != nullptr) ? configHandler->GetBool("AllowColorFonts") : false;
}

[[nodiscard]] int GetMaxAtlasTextureSize() noexcept
{
	return (globalRendering != nullptr) ? globalRendering->maxTextureSize : 2048;
}

} // namespace

namespace fonts {
GlyphAtlasCache::GlyphAtlasCache(FaceSetPtr faceSet, int fontSize_, int outlineSize_, float outlineWeight_, bool enableSlugData)
	: faces(std::move(faceSet))
	, fontSize(fontSize_ > 0 ? fontSize_ : 14)
	, outlineSize(std::max(outlineSize_, 0))
	, outlineWeight(outlineWeight_)
	, slugDataEnabled(enableSlugData)
{
	ClearAtlasState();
	ConfigureFaceMetrics();
	PreloadGlyphs();
}

GlyphAtlasCache::~GlyphAtlasCache()
{
	DeleteSlugTextures();
}

void GlyphAtlasCache::SetFaceSet(FaceSetPtr faceSet)
{
	if (faces == faceSet)
		return;

	faces = std::move(faceSet);
	ClearGlyphs();
}

const GlyphInfo& GlyphAtlasCache::GetGlyphByCodepoint(char32_t codepoint)
{
	if (const GlyphInfo* glyph = FindGlyphByCodepoint(codepoint); glyph != nullptr)
		return *glyph;

	LoadGlyphByCodepoint(codepoint);
	UpdateAtlases();

	if (const GlyphInfo* glyph = FindGlyphByCodepoint(codepoint); glyph != nullptr)
		return *glyph;

	return GetResolvedGlyphByCodepoint(codepoint);
}

const GlyphInfo& GlyphAtlasCache::GetGlyphByGlyphIndex(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint)
{
	if (glyphIndex == 0u) {
		if (sourceCodepoint != 0)
			return GetGlyphByCodepoint(sourceCodepoint);

		return dummyGlyph;
	}

	if (const GlyphInfo* glyph = FindGlyphByGlyphIndex(glyphIndex, face, sourceCodepoint); glyph != nullptr)
		return *glyph;

	LoadGlyphByIndex(face, sourceCodepoint, glyphIndex);
	UpdateAtlases();

	if (const GlyphInfo* glyph = FindGlyphByGlyphIndex(glyphIndex, face, sourceCodepoint); glyph != nullptr)
		return *glyph;

	return dummyGlyph;
}

const GlyphInfo* GlyphAtlasCache::FindGlyphByCodepoint(char32_t codepoint) const
{
	const auto it = glyphsByCodepoint.find(codepoint);
	return (it != glyphsByCodepoint.end()) ? &it->second : nullptr;
}

const GlyphInfo* GlyphAtlasCache::FindGlyphByGlyphIndex(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint) const
{
	if (face == nullptr)
		return nullptr;

	const auto it = glyphsByGlyphIndex.find(MakeGlyphIndexKey(glyphIndex, face, sourceCodepoint));
	return (it != glyphsByGlyphIndex.end()) ? &it->second : nullptr;
}

void GlyphAtlasCache::EnsureGlyphs(std::span<const char32_t> codepoints)
{
	for (char32_t codepoint: codepoints)
		LoadGlyphByCodepoint(codepoint);

	UpdateAtlases();
}

void GlyphAtlasCache::EnsureGlyphs(char32_t beginInclusive, char32_t endInclusive)
{
	if (endInclusive < beginInclusive)
		return;

	std::vector<char32_t> codepoints;
	codepoints.reserve(static_cast<std::size_t>(endInclusive - beginInclusive + 1));

	for (char32_t codepoint = beginInclusive; codepoint <= endInclusive; ++codepoint) {
		codepoints.emplace_back(codepoint);

		if (codepoint == std::numeric_limits<char32_t>::max())
			break;
	}

	EnsureGlyphs(std::span<const char32_t>(codepoints));
}

void GlyphAtlasCache::EnsureGlyphIndices(std::span<const std::uint32_t> glyphIndices, const FacePtr& face, char32_t sourceCodepoint)
{
	for (std::uint32_t glyphIndex: glyphIndices)
		LoadGlyphByIndex(face, sourceCodepoint, glyphIndex);

	UpdateAtlases();
}

void GlyphAtlasCache::EnsureGlyphKeys(std::span<const GlyphKey> glyphKeys, const FacePtr& defaultFace)
{
	for (const GlyphKey& glyphKey: glyphKeys) {
		if (glyphKey.IsGlyphIndex()) {
			if (glyphKey.glyphIndex == 0u) {
				if (glyphKey.codepoint != 0)
					GetResolvedGlyphByCodepoint(glyphKey.codepoint);
				continue;
			}

			FacePtr face = defaultFace;
			if (face == nullptr && faces != nullptr)
				face = faces->GetPrimaryFace();

			LoadGlyphByIndex(face, glyphKey.codepoint, glyphKey.glyphIndex);
		} else {
			LoadGlyphByCodepoint(glyphKey.codepoint);
		}
	}

	UpdateAtlases();
}

bool GlyphAtlasCache::ClearGlyphs()
{
	const bool changed = !glyphsByCodepoint.empty() || !glyphsByGlyphIndex.empty() || !failedAttemptsToReplace.empty();

	failedAttemptsToReplace.clear();
	glyphsByCodepoint.clear();
	glyphsByGlyphIndex.clear();

	ResetKerningCaches();
	ClearAtlasState();
	ConfigureFaceMetrics();
	PreloadGlyphs();

	return changed;
}

void GlyphAtlasCache::PreloadGlyphs()
{
#ifndef HEADLESS
	if (faces == nullptr)
		return;

	const FacePtr& primaryFace = faces->GetPrimaryFace();
	if (primaryFace == nullptr || primaryFace->GetFTFace() == nullptr)
		return;

	if (primaryFace->GetCharIndex(U'a') == 0u)
		return;

	EnsureGlyphs(U' ', U'~');

	if (!primaryFace->HasKerning())
		return;

	for (char32_t left = U' '; left <= U'~'; ++left) {
		const GlyphInfo* leftGlyph = FindGlyphByCodepoint(left);
		if (leftGlyph == nullptr)
			continue;

		for (char32_t right = U' '; right <= U'~'; ++right) {
			const GlyphInfo* rightGlyph = FindGlyphByCodepoint(right);
			if (rightGlyph == nullptr)
				continue;

			FT_Vector kerning = {};
			if (leftGlyph->face == primaryFace && rightGlyph->face == primaryFace)
				FT_Get_Kerning(primaryFace->GetFTFace(), leftGlyph->glyphIndex, rightGlyph->glyphIndex, FT_KERNING_DEFAULT, &kerning);

			kerningPrecached[MakeAsciiKerningHash(left, right)] = leftGlyph->advance + normScale * kerning.x;
		}
	}
#endif
}

void GlyphAtlasCache::ReallocAtlases(bool preserveContents)
{
	const GlyphAtlasTexture::Dimensions currentAtlasDims = atlasTexture.GetDimensions();
	const GlyphAtlasTexture::Dimensions currentShadowDims = shadowAtlasTexture.GetDimensions();

	if (!currentAtlasDims.Empty())
		atlasTexture.Reallocate(currentAtlasDims, preserveContents);
	if (!currentShadowDims.Empty())
		shadowAtlasTexture.Reallocate(currentShadowDims, preserveContents);
}

bool GlyphAtlasCache::NeedsAtlasUpdate() const noexcept
{
	return !pendingGlyphBitmaps.empty() || atlasTexture.NeedsUpdate() || shadowAtlasTexture.NeedsUpdate();
}

bool GlyphAtlasCache::NeedsAtlasUpload() const noexcept
{
	return NeedsAtlasUpdate() || atlasTexture.NeedsUpload() || shadowAtlasTexture.NeedsUpload();
}

void GlyphAtlasCache::UpdateAtlases()
{
	if (pendingGlyphBitmaps.empty())
		return;

	PackPendingGlyphs();
	UpdateAtlasRegions();
}

void GlyphAtlasCache::UploadAtlases()
{
	UpdateAtlases();
	UploadAtlasTextures();
}

bool GlyphAtlasCache::NeedsSlugUpload() const noexcept
{
	return slugDataEnabled && slugTexturesDirty && !slugCurveTexels.empty() && !slugBandTexels.empty();
}

void GlyphAtlasCache::UploadSlugTextures()
{
#ifdef HEADLESS
	return;
#else
	if (!NeedsSlugUpload())
		return;

	if (slugCurveTextureHeight == 0u || slugBandTextureHeight == 0u)
		return;

	const std::size_t curveTexelCount = static_cast<std::size_t>(slugCurveTextureWidth) * slugCurveTextureHeight;
	std::vector<float> curveUpload(curveTexelCount * 4u, 0.0f);
	std::memcpy(curveUpload.data(), slugCurveTexels.data(), slugCurveTexels.size() * sizeof(float));

	const std::size_t bandTexelCount = static_cast<std::size_t>(slugBandTextureWidth) * slugBandTextureHeight;
	std::vector<std::uint16_t> bandUpload(bandTexelCount * 2u, 0u);
	std::memcpy(bandUpload.data(), slugBandTexels.data(), slugBandTexels.size() * sizeof(std::uint16_t));

	if (slugCurveTextureId == 0u)
		glGenTextures(1, &slugCurveTextureId);
	if (slugBandTextureId == 0u)
		glGenTextures(1, &slugBandTextureId);

	GLint prevTextureBinding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glBindTexture(GL_TEXTURE_2D, slugCurveTextureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, static_cast<GLsizei>(slugCurveTextureWidth), static_cast<GLsizei>(slugCurveTextureHeight), 0, GL_RGBA, GL_FLOAT, curveUpload.data());

	glBindTexture(GL_TEXTURE_2D, slugBandTextureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16UI, static_cast<GLsizei>(slugBandTextureWidth), static_cast<GLsizei>(slugBandTextureHeight), 0, GL_RG_INTEGER, GL_UNSIGNED_SHORT, bandUpload.data());

	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTextureBinding));
	slugTexturesDirty = false;
#endif
}

GlyphAtlasCache::SlugTextureInfo GlyphAtlasCache::GetSlugTextureInfo() const noexcept
{
	return SlugTextureInfo {
		.curveTextureId = slugCurveTextureId,
		.bandTextureId = slugBandTextureId,
		.curveTextureWidth = slugCurveTextureWidth,
		.curveTextureHeight = slugCurveTextureHeight,
		.bandTextureWidth = slugBandTextureWidth,
		.bandTextureHeight = slugBandTextureHeight,
	};
}

float GlyphAtlasCache::GetKerning(const GlyphInfo& leftGlyph, const GlyphInfo& rightGlyph)
{
#ifndef HEADLESS
	if (leftGlyph.face == nullptr || rightGlyph.face == nullptr)
		return leftGlyph.advance;

	if (leftGlyph.face != rightGlyph.face)
		return leftGlyph.advance;

	if (!leftGlyph.face->HasKerning())
		return leftGlyph.advance;

	if (leftGlyph.sourceCodepoint < 128u && rightGlyph.sourceCodepoint < 128u) {
		return kerningPrecached[MakeAsciiKerningHash(leftGlyph.sourceCodepoint, rightGlyph.sourceCodepoint)];
	}

	const std::uint64_t hash = MakeDynamicKerningHash(leftGlyph, rightGlyph);
	if (const auto it = kerningDynamic.find(hash); it != kerningDynamic.end())
		return it->second;

	FT_Vector kerning = {};
	FT_Get_Kerning(leftGlyph.face->GetFTFace(), leftGlyph.glyphIndex, rightGlyph.glyphIndex, FT_KERNING_DEFAULT, &kerning);

	const float value = leftGlyph.advance + normScale * kerning.x;
	kerningDynamic[hash] = value;
	return value;
#else
	return leftGlyph.advance;
#endif
}

GlyphAtlasCache::GlyphIndexKey GlyphAtlasCache::MakeGlyphIndexKey(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint) noexcept
{
	return GlyphIndexKey{
		.faceIdentity = reinterpret_cast<std::uintptr_t>(face.get()),
		.glyphIndex = glyphIndex,
		.sourceCodepoint = sourceCodepoint,
	};
}

std::pair<GlyphAtlasCache::FacePtr, std::uint32_t> GlyphAtlasCache::ResolveGlyphForCodepoint(char32_t codepoint, char32_t* resolvedCodepoint) const
{
	auto setResolvedCodepoint = [resolvedCodepoint](char32_t value) {
		if (resolvedCodepoint != nullptr)
			*resolvedCodepoint = value;
	};

	if (faces == nullptr) {
		setResolvedCodepoint(0);
		return {nullptr, 0u};
	}

	if (auto [face, glyphIndex] = faces->FindGlyphForCodepoint(codepoint); face != nullptr && glyphIndex != 0u) {
		setResolvedCodepoint(codepoint);
		return {face, glyphIndex};
	}

	for (char32_t replacementCodepoint: {PrimaryReplacementCodepoint, SecondaryReplacementCodepoint}) {
		if (replacementCodepoint == codepoint)
			continue;

		if (auto [face, glyphIndex] = faces->FindGlyphForCodepoint(replacementCodepoint); face != nullptr && glyphIndex != 0u) {
			setResolvedCodepoint(replacementCodepoint);
			return {face, glyphIndex};
		}
	}

	setResolvedCodepoint(0);
	return {nullptr, 0u};
}

const GlyphInfo* GlyphAtlasCache::FindResolvedGlyphByCodepoint(char32_t codepoint) const
{
	if (const GlyphInfo* glyph = FindGlyphByCodepoint(codepoint); glyph != nullptr)
		return glyph;

	char32_t resolvedCodepoint = 0;
	auto [face, glyphIndex] = ResolveGlyphForCodepoint(codepoint, &resolvedCodepoint);
	if (face == nullptr || glyphIndex == 0u)
		return nullptr;

	return FindGlyphByGlyphIndex(glyphIndex, face, resolvedCodepoint);
}

const GlyphInfo& GlyphAtlasCache::GetResolvedGlyphByCodepoint(char32_t codepoint)
{
	if (const GlyphInfo* glyph = FindResolvedGlyphByCodepoint(codepoint); glyph != nullptr)
		return *glyph;

	char32_t resolvedCodepoint = 0;
	auto [face, glyphIndex] = ResolveGlyphForCodepoint(codepoint, &resolvedCodepoint);
	if (face == nullptr || glyphIndex == 0u)
		return dummyGlyph;

	LoadGlyphByIndex(face, resolvedCodepoint, glyphIndex);
	UpdateAtlases();

	if (const GlyphInfo* glyph = FindGlyphByGlyphIndex(glyphIndex, face, resolvedCodepoint); glyph != nullptr)
		return *glyph;

	return dummyGlyph;
}

void GlyphAtlasCache::ClearAtlasState()
{
	atlasAllocator = std::make_unique<CRowAtlasAlloc>();
	atlasAllocator->SetMaxSize(GetMaxAtlasTextureSize(), GetMaxAtlasTextureSize());

	atlasTexture.Reset();
	shadowAtlasTexture.Reset();
	atlasTexture.SetPixelFormat(isColor ? GlyphAtlasTexture::PixelFormat::BGRA : GlyphAtlasTexture::PixelFormat::Alpha);
	atlasTexture.SetContentType(GlyphAtlasTexture::ContentType::Bitmap);
	shadowAtlasTexture.SetPixelFormat(isColor ? GlyphAtlasTexture::PixelFormat::BGRA : GlyphAtlasTexture::PixelFormat::Alpha);
	shadowAtlasTexture.SetContentType(GlyphAtlasTexture::ContentType::Bitmap);
	atlasTexture.Reallocate(32, 32, false);
	shadowAtlasTexture.Reallocate(32, 32, false);

	pendingGlyphBitmaps.clear();
	blurRectangles.clear();
	glyphNameToBitmapIndex.clear();

	ClearSlugState();
}

void GlyphAtlasCache::ClearSlugState()
{
	DeleteSlugTextures();
	slugCurveTexels.clear();
	slugBandTexels.clear();
	slugCurveTextureWidth = SlugTextureWidth;
	slugCurveTextureHeight = 0;
	slugBandTextureWidth = SlugTextureWidth;
	slugBandTextureHeight = 0;
	slugTexturesDirty = false;
}

void GlyphAtlasCache::ResetKerningCaches()
{
	kerningPrecached.fill(0.0f);
	kerningDynamic.clear();
}

void GlyphAtlasCache::ConfigureFaceMetrics()
{
	lineHeight = 0.0f;
	fontDescender = 0.0f;
	normScale = 1.0f / std::max(fontSize, 1);

#ifndef HEADLESS
	if (faces == nullptr)
		return;

	const FacePtr& primaryFace = faces->GetPrimaryFace();
	if (primaryFace == nullptr)
		return;

	FT_Face ftFace = primaryFace->GetFTFace();
	if (ftFace == nullptr || ftFace->size == nullptr)
		return;

	normScale = 1.0f / (std::max(fontSize, 1) * FT_INTERNAL_DPI);

	float pixelScale = 1.0f;
	bool canScale = false;
	if (!FT_IS_SCALABLE(ftFace)) {
		if (ftFace->num_fixed_sizes > 0) {
			pixelScale = std::floor(1.0f / (ftFace->available_sizes[0].y_ppem * normScale));
			canScale = true;
		} else {
			normScale = 1.0f;
		}
	}

	fontDescender = normScale * FT_MulFix(ftFace->descender, ftFace->size->metrics.y_scale);
	lineHeight = (ftFace->units_per_EM > 0) ? (static_cast<float>(ftFace->height) / ftFace->units_per_EM) : 0.0f;

	if (lineHeight <= 0.0f)
		lineHeight = 1.25f * (ftFace->bbox.yMax - ftFace->bbox.yMin);

	if (canScale) {
		fontDescender *= (pixelScale / normScale);
		lineHeight *= pixelScale;
	}
#endif
}

void GlyphAtlasCache::LoadGlyphByCodepoint(char32_t codepoint)
{
	if (FindGlyphByCodepoint(codepoint) != nullptr)
		return;

	GlyphInfo glyph;
	glyph.sourceCodepoint = codepoint;

#ifndef HEADLESS
	if (faces != nullptr) {
		auto [face, glyphIndex] = faces->FindGlyphForCodepoint(codepoint);

		if (face != nullptr && glyphIndex != 0u) {
			LoadGlyphByIndex(face, codepoint, glyphIndex);

			if (const GlyphInfo* glyphByIndex = FindGlyphByGlyphIndex(glyphIndex, face, codepoint); glyphByIndex != nullptr)
				glyph = *glyphByIndex;

			if (glyph.face == nullptr) {
				glyph.face = std::move(face);
				glyph.glyphIndex = glyphIndex;
				glyph.sourceCodepoint = codepoint;
			}
		} else {
			failedAttemptsToReplace[codepoint] += 1;
			return;
		}
	} else {
		return;
	}
#endif

	CacheGlyphRecord(codepoint, std::move(glyph));
}

void GlyphAtlasCache::LoadGlyphByIndex(const FacePtr& face, char32_t sourceCodepoint, std::uint32_t glyphIndex)
{
	if (face == nullptr)
		return;

	const GlyphIndexKey glyphKey = MakeGlyphIndexKey(glyphIndex, face, sourceCodepoint);
	if (glyphsByGlyphIndex.contains(glyphKey))
		return;

	GlyphInfo glyph;
	glyph.face = face;
	glyph.glyphIndex = glyphIndex;
	glyph.sourceCodepoint = sourceCodepoint;

#ifndef HEADLESS
	FT_Face ftFace = face->GetFTFace();
	if (ftFace == nullptr) {
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

	FT_Int32 loadFlags = FT_LOAD_RENDER;
	if (AllowColorFonts() && FT_HAS_COLOR(ftFace))
		loadFlags = FT_LOAD_COLOR;

	if (face->LoadGlyph(glyphIndex, loadFlags) != 0) {
		LOG_L(L_WARNING, "[GlyphAtlasCache::%s] Failed to load glyph %u", __func__, glyphIndex);
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

	FT_GlyphSlot slot = ftFace->glyph;
	if (slot == nullptr) {
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

	if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
		needsColor = true;

	const float xBearing = slot->metrics.horiBearingX * normScale;
	const float yBearing = slot->metrics.horiBearingY * normScale;

	glyph.bitmapBounds.x = xBearing;
	glyph.bitmapBounds.y = yBearing - fontDescender;
	glyph.bitmapBounds.w = slot->metrics.width * normScale;
	glyph.bitmapBounds.h = -slot->metrics.height * normScale;
	glyph.advance = slot->advance.x * normScale;
	glyph.height = slot->metrics.height * normScale;
	glyph.descender = yBearing - glyph.height;

	if (glyph.advance == 0.0f && glyph.bitmapBounds.w > 0.0f)
		glyph.advance = glyph.bitmapBounds.w;

	int bitmapWidth = slot->bitmap.width;
	int bitmapHeight = slot->bitmap.rows;
	if (bitmapWidth <= 0 || bitmapHeight <= 0) {
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

	if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && slot->bitmap.pixel_mode != FT_PIXEL_MODE_BGRA) {
		LOG_L(L_WARNING, "[GlyphAtlasCache::%s] Unsupported glyph pixel mode %d", __func__, slot->bitmap.pixel_mode);
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

	const int channels = (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) ? 4 : 1;
	if (slot->bitmap.pitch != bitmapWidth * channels) {
		LOG_L(L_WARNING, "[GlyphAtlasCache::%s] Unsupported glyph pitch %d for width %d and channels %d", __func__, slot->bitmap.pitch, bitmapWidth, channels);
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

	CBitmap glyphBitmap;
	if (!FT_IS_SCALABLE(ftFace) && ftFace->num_fixed_sizes > 0) {
		CBitmap temp(slot->bitmap.buffer, bitmapWidth, bitmapHeight, channels);
		const float pixelSize = static_cast<float>(ftFace->available_sizes[0].y_ppem);
		const float ratio = 1.0f / (pixelSize * normScale);

		bitmapWidth = std::max(1, static_cast<int>(std::floor(bitmapWidth * ratio)));
		bitmapHeight = std::max(1, static_cast<int>(std::floor(bitmapHeight * ratio)));
		glyph.advance *= ratio;
		glyph.descender *= ratio;
		glyph.bitmapBounds.y = yBearing * ratio - fontDescender;
		glyph.bitmapBounds.x *= ratio;
		glyph.bitmapBounds.w *= ratio;
		glyph.bitmapBounds.h *= ratio;
		glyph.height *= ratio;

		glyphBitmap = temp.CreateRescaled(bitmapWidth, bitmapHeight);
	} else {
		glyphBitmap = CBitmap(slot->bitmap.buffer, bitmapWidth, bitmapHeight, channels);
	}

	CBitmap outlineBitmap;
	CBitmap* outlineBitmapPtr = nullptr;
	if (outlineSize > 0) {
		outlineBitmap.Alloc(bitmapWidth + 2 * outlineSize, bitmapHeight + 2 * outlineSize, channels);
		outlineBitmap.CopySubImage(glyphBitmap, outlineSize, outlineSize);
		outlineBitmap.Blur(outlineSize, outlineWeight, 0, 0, outlineBitmap.xsize, outlineBitmap.ysize);
		outlineBitmapPtr = &outlineBitmap;
	}

	QueueGlyphBitmap(MakeAtlasGlyphKey(glyph), glyphBitmap, outlineBitmapPtr);

	if (slugDataEnabled) {
		BuildSlugFillGlyph(face, glyphIndex, glyph);
		BuildSlugOutlineGlyph(face, glyphIndex, glyph);
	}
#endif

	CacheGlyphRecord(glyphKey, std::move(glyph));
}

void GlyphAtlasCache::CacheGlyphRecord(char32_t codepoint, GlyphInfo glyph)
{
	glyphsByCodepoint[codepoint] = std::move(glyph);
}

void GlyphAtlasCache::CacheGlyphRecord(const GlyphIndexKey& key, GlyphInfo glyph)
{
	glyphsByGlyphIndex[key] = std::move(glyph);
}

void GlyphAtlasCache::QueueGlyphBitmap(const std::string& cacheKey, const CBitmap& bitmap, const CBitmap* outlineBitmap)
{
	if (bitmap.Empty())
		return;

	if (!glyphNameToBitmapIndex.contains(cacheKey)) {
		glyphNameToBitmapIndex[cacheKey] = pendingGlyphBitmaps.size();
		pendingGlyphBitmaps.emplace_back(bitmap);
		atlasAllocator->AddEntry(cacheKey, {bitmap.xsize, bitmap.ysize});
	}

	if (outlineBitmap != nullptr && !outlineBitmap->Empty()) {
		const std::string outlineKey = MakeOutlineGlyphKey(cacheKey);
		if (!glyphNameToBitmapIndex.contains(outlineKey)) {
			glyphNameToBitmapIndex[outlineKey] = pendingGlyphBitmaps.size();
			pendingGlyphBitmaps.emplace_back(*outlineBitmap);
			atlasAllocator->AddEntry(outlineKey, {outlineBitmap->xsize, outlineBitmap->ysize});
		}
	}
}

bool GlyphAtlasCache::BuildSlugGlyphFromFTOutline(const FT_Outline& outline, SlugGlyphInfo& outInfo)
{
#ifdef HEADLESS
	(void)outline;
	(void)outInfo;
	return false;
#else
	outInfo = {};

	if (!slugDataEnabled || outline.n_contours <= 0 || outline.n_points <= 0)
		return false;

	FT_BBox bbox {};
	if (FT_Outline_Get_BBox(const_cast<FT_Outline*>(&outline), &bbox) != 0)
		return false;

	const float width = (bbox.xMax - bbox.xMin) * normScale;
	const float height = (bbox.yMax - bbox.yMin) * normScale;
	if (width <= 0.0f || height <= 0.0f)
		return false;

	SlugOutlineBuilder builder;
	builder.minX = static_cast<float>(bbox.xMin);
	builder.minY = static_cast<float>(bbox.yMin);
	builder.scale = normScale;

	FT_Outline_Funcs funcs {};
	funcs.move_to = &SlugOutlineBuilder::MoveTo;
	funcs.line_to = &SlugOutlineBuilder::LineTo;
	funcs.conic_to = &SlugOutlineBuilder::ConicTo;
	funcs.cubic_to = &SlugOutlineBuilder::CubicTo;
	funcs.shift = 0;
	funcs.delta = 0;

	if (FT_Outline_Decompose(const_cast<FT_Outline*>(&outline), &funcs, &builder) != 0)
		return false;

	builder.CloseContour();
	if (builder.curves.empty())
		return false;

	auto storeCurve = [this](SlugCurveData& curve) {
		std::uint32_t texelIndex = static_cast<std::uint32_t>(slugCurveTexels.size() / 4u);

		if ((texelIndex % SlugTextureWidth) == (SlugTextureWidth - 1u)) {
			slugCurveTexels.insert(slugCurveTexels.end(), 4u, 0.0f);
			texelIndex += 1u;
		}

		curve.texelIndex = texelIndex;
		slugCurveTexels.push_back(curve.x1);
		slugCurveTexels.push_back(curve.y1);
		slugCurveTexels.push_back(curve.x2);
		slugCurveTexels.push_back(curve.y2);
		slugCurveTexels.push_back(curve.x3);
		slugCurveTexels.push_back(curve.y3);
		slugCurveTexels.push_back(0.0f);
		slugCurveTexels.push_back(0.0f);
	};

	for (auto& curve: builder.curves)
		storeCurve(curve);

	const std::uint32_t bandCount = SlugBandCount;
	const float bandDimX = width / static_cast<float>(bandCount);
	const float bandDimY = height / static_cast<float>(bandCount);
	if (bandDimX <= 0.0f || bandDimY <= 0.0f)
		return false;

	const std::size_t glyphBaseTexel = slugBandTexels.size() / 2u;
	slugBandTexels.resize(slugBandTexels.size() + static_cast<std::size_t>(bandCount * 2u * 2u), 0u);

	auto writeBandHeader = [this, glyphBaseTexel](std::uint32_t bandIndex, std::uint16_t curveCount, std::uint16_t relativeOffset) {
		const std::size_t header = (glyphBaseTexel + bandIndex) * 2u;
		slugBandTexels[header + 0u] = curveCount;
		slugBandTexels[header + 1u] = relativeOffset;
	};

	auto appendBandCurves = [this, glyphBaseTexel](const std::vector<SlugCurveData*>& sortedCurves, bool horizontal, float bandSize, float glyphExtent) -> std::vector<std::pair<std::uint16_t, std::uint16_t>> {
		std::vector<std::pair<std::uint16_t, std::uint16_t>> descriptors;
		descriptors.reserve(SlugBandCount);

		for (std::uint32_t band = 0; band < SlugBandCount; ++band) {
			const float bandMin = band * bandSize;
			const float bandMax = (band == (SlugBandCount - 1u)) ? glyphExtent : ((band + 1u) * bandSize);
			const std::uint16_t relativeOffset = static_cast<std::uint16_t>(slugBandTexels.size() / 2u - glyphBaseTexel);
			std::uint16_t curveCount = 0u;

			for (const SlugCurveData* curve: sortedCurves) {
				const float c1 = horizontal ? curve->y1 : curve->x1;
				const float c2 = horizontal ? curve->y2 : curve->x2;
				const float c3 = horizontal ? curve->y3 : curve->x3;

				if (NearlyEqual(c1, c2) && NearlyEqual(c2, c3))
					continue;

				if (Min3(c1, c2, c3) > bandMax || Max3(c1, c2, c3) < bandMin)
					continue;

				const std::uint16_t curveX = static_cast<std::uint16_t>(curve->texelIndex % SlugTextureWidth);
				const std::uint16_t curveY = static_cast<std::uint16_t>(curve->texelIndex / SlugTextureWidth);
				slugBandTexels.push_back(curveX);
				slugBandTexels.push_back(curveY);
				curveCount += 1u;
			}

			descriptors.emplace_back(curveCount, relativeOffset);
		}

		return descriptors;
	};

	std::vector<SlugCurveData*> horizontalCurves;
	std::vector<SlugCurveData*> verticalCurves;
	horizontalCurves.reserve(builder.curves.size());
	verticalCurves.reserve(builder.curves.size());

	for (auto& curve: builder.curves) {
		horizontalCurves.push_back(&curve);
		verticalCurves.push_back(&curve);
	}

	std::stable_sort(horizontalCurves.begin(), horizontalCurves.end(), [](const SlugCurveData* lhs, const SlugCurveData* rhs) {
		return Max3(lhs->x1, lhs->x2, lhs->x3) > Max3(rhs->x1, rhs->x2, rhs->x3);
	});
	std::stable_sort(verticalCurves.begin(), verticalCurves.end(), [](const SlugCurveData* lhs, const SlugCurveData* rhs) {
		return Max3(lhs->y1, lhs->y2, lhs->y3) > Max3(rhs->y1, rhs->y2, rhs->y3);
	});

	const auto horizontalBands = appendBandCurves(horizontalCurves, true, bandDimY, height);
	const auto verticalBands = appendBandCurves(verticalCurves, false, bandDimX, width);

	for (std::uint32_t i = 0; i < bandCount; ++i) {
		writeBandHeader(i, horizontalBands[i].first, horizontalBands[i].second);
		writeBandHeader(bandCount + i, verticalBands[i].first, verticalBands[i].second);
	}

	outInfo.offsetX = bbox.xMin * normScale;
	outInfo.offsetY = bbox.yMin * normScale - fontDescender;
	outInfo.width = width;
	outInfo.height = height;
	outInfo.bandScaleX = 1.0f / bandDimX;
	outInfo.bandScaleY = 1.0f / bandDimY;
	outInfo.bandOffsetX = 0.0f;
	outInfo.bandOffsetY = 0.0f;
	outInfo.bandTexelX = static_cast<std::uint32_t>(glyphBaseTexel % SlugTextureWidth);
	outInfo.bandTexelY = static_cast<std::uint32_t>(glyphBaseTexel / SlugTextureWidth);
	outInfo.bandMaxX = bandCount - 1u;
	outInfo.bandMaxY = bandCount - 1u;
	outInfo.flags = 0u;

	slugCurveTextureHeight = static_cast<std::uint32_t>((slugCurveTexels.size() / 4u + SlugTextureWidth - 1u) / SlugTextureWidth);
	slugBandTextureHeight = static_cast<std::uint32_t>((slugBandTexels.size() / 2u + SlugTextureWidth - 1u) / SlugTextureWidth);
	slugTexturesDirty = true;
	return true;
#endif
}

bool GlyphAtlasCache::BuildSlugFillGlyph(const FacePtr& face, std::uint32_t glyphIndex, GlyphInfo& glyph)
{
#ifdef HEADLESS
	(void)face;
	(void)glyphIndex;
	(void)glyph;
	return false;
#else
	glyph.slugFillInfo = {};

	if (!slugDataEnabled || face == nullptr)
		return false;

	FT_Face ftFace = face->GetFTFace();
	if (ftFace == nullptr)
		return false;

	if (face->LoadGlyph(glyphIndex, FT_LOAD_NO_BITMAP) != 0)
		return false;

	FT_GlyphSlot slot = ftFace->glyph;
	if (slot == nullptr)
		return false;

	if (slot->format != FT_GLYPH_FORMAT_OUTLINE || slot->outline.n_contours <= 0 || slot->outline.n_points <= 0)
		return false;

	return BuildSlugGlyphFromFTOutline(slot->outline, glyph.slugFillInfo);
#endif
}

bool GlyphAtlasCache::BuildSlugOutlineGlyph(const FacePtr& face, std::uint32_t glyphIndex, GlyphInfo& glyph)
{
#ifdef HEADLESS
	(void)face;
	(void)glyphIndex;
	(void)glyph;
	return false;
#else
	glyph.slugOutlineInfo = glyph.slugFillInfo;

	if (!slugDataEnabled || face == nullptr)
		return false;

	if (outlineSize <= 0)
		return !glyph.slugOutlineInfo.Empty();

	FT_Face ftFace = face->GetFTFace();
	if (ftFace == nullptr)
		return false;

	if (face->LoadGlyph(glyphIndex, FT_LOAD_NO_BITMAP) != 0)
		return false;

	FT_GlyphSlot slot = ftFace->glyph;
	if (slot == nullptr)
		return false;

	if (slot->format != FT_GLYPH_FORMAT_OUTLINE || slot->outline.n_contours <= 0 || slot->outline.n_points <= 0)
		return false;

	FT_Glyph glyphHandle = nullptr;
	if (FT_Get_Glyph(slot, &glyphHandle) != 0 || glyphHandle == nullptr)
		return false;

	struct ScopedGlyph {
		FT_Glyph value = nullptr;
		~ScopedGlyph() { if (value != nullptr) FT_Done_Glyph(value); }
	} scopedGlyph {glyphHandle};

	FT_Stroker stroker = nullptr;
	if (FT_Stroker_New(ftFace->glyph->library, &stroker) != 0)
		return false;

	struct ScopedStroker {
		FT_Stroker value = nullptr;
		~ScopedStroker() { if (value != nullptr) FT_Stroker_Done(value); }
	} scopedStroker {stroker};

	const FT_Fixed outlineRadius = static_cast<FT_Fixed>(std::lround(outlineSize * SlugOutlineStrokeScale * 64.0f));

	FT_Stroker_Set(
		stroker,
		std::max<FT_Fixed>(outlineRadius, 1),
		FT_STROKER_LINECAP_ROUND,
		FT_STROKER_LINEJOIN_ROUND,
		0
	);

	if (FT_Glyph_StrokeBorder(&glyphHandle, stroker, 0, 1) != 0 || glyphHandle == nullptr)
		return false;

	scopedGlyph.value = glyphHandle;

	if (glyphHandle->format != FT_GLYPH_FORMAT_OUTLINE)
		return false;

	const FT_OutlineGlyph outlineGlyph = reinterpret_cast<FT_OutlineGlyph>(glyphHandle);
	if (outlineGlyph == nullptr)
		return false;

	SlugGlyphInfo strokedInfo;
	if (!BuildSlugGlyphFromFTOutline(outlineGlyph->outline, strokedInfo))
		return false;

	glyph.slugOutlineInfo = strokedInfo;
	return true;
#endif
}

void GlyphAtlasCache::PackPendingGlyphs()
{
	if (pendingGlyphBitmaps.empty())
		return;

	if (!atlasAllocator->Allocate()) {
		LOG_L(L_WARNING, "[GlyphAtlasCache::%s] Atlas allocation reached the texture limit", __func__);
	}

	if (needsColor && !isColor) {
		atlasTexture.SetPixelFormat(GlyphAtlasTexture::PixelFormat::BGRA);
		shadowAtlasTexture.SetPixelFormat(GlyphAtlasTexture::PixelFormat::BGRA);
		isColor = true;
	}

	const auto& atlasSize = atlasAllocator->GetAtlasSize();
	const int allocWidth = std::max(32, static_cast<int>(atlasSize.x));
	const int allocHeight = std::max(32, static_cast<int>(atlasSize.y));
	const auto oldPrimaryDims = atlasTexture.GetDimensions();
	const auto oldShadowDims = shadowAtlasTexture.GetDimensions();

	atlasTexture.Reallocate(allocWidth, allocHeight, true);
	shadowAtlasTexture.Reallocate(allocWidth, allocHeight, true);

	if (oldPrimaryDims.width != allocWidth || oldPrimaryDims.height != allocHeight ||
	    oldShadowDims.width != allocWidth || oldShadowDims.height != allocHeight) {
		for (auto& [codepoint, glyph] : glyphsByCodepoint) {
			RescaleGlyphRectUV(glyph.atlasUV, oldPrimaryDims.width, oldPrimaryDims.height, allocWidth, allocHeight);
			RescaleGlyphRectUV(glyph.shadowAtlasUV, oldShadowDims.width, oldShadowDims.height, allocWidth, allocHeight);
			(void)codepoint;
		}

		for (auto& [glyphKey, glyph] : glyphsByGlyphIndex) {
			RescaleGlyphRectUV(glyph.atlasUV, oldPrimaryDims.width, oldPrimaryDims.height, allocWidth, allocHeight);
			RescaleGlyphRectUV(glyph.shadowAtlasUV, oldShadowDims.width, oldShadowDims.height, allocWidth, allocHeight);
			(void)glyphKey;
		}
	}
}

void GlyphAtlasCache::UpdateAtlasRegions()
{
	if (pendingGlyphBitmaps.empty())
		return;

	for (const auto& [glyphName, bitmapIndex] : glyphNameToBitmapIndex) {
		if (bitmapIndex >= pendingGlyphBitmaps.size())
			continue;

		const AtlasedTexture& region = atlasAllocator->GetEntry(glyphName);
		if (region.x2 <= region.x1 || region.y2 <= region.y1)
			continue;

		const int dstX = static_cast<int>(region.x1);
		const int dstY = static_cast<int>(region.y1);

		if (glyphName.ends_with(":outline")) {
			shadowAtlasTexture.UpdateBitmapRegion(pendingGlyphBitmaps[bitmapIndex], dstX, dstY);
		} else {
			atlasTexture.UpdateBitmapRegion(pendingGlyphBitmaps[bitmapIndex], dstX, dstY);
		}
	}

	for (auto& [codepoint, glyph] : glyphsByCodepoint) {
		const std::string glyphKey = MakeAtlasGlyphKey(glyph);
		if (atlasAllocator->contains(glyphKey))
			glyph.atlasUV = ToGlyphRectExclusive(atlasAllocator->GetEntry(glyphKey), atlasTexture);

		const std::string outlineKey = MakeOutlineGlyphKey(glyphKey);
		if (atlasAllocator->contains(outlineKey))
			glyph.shadowAtlasUV = ToGlyphRectExclusive(atlasAllocator->GetEntry(outlineKey), shadowAtlasTexture);

		(void)codepoint;
	}

	for (auto& [glyphKey, glyph] : glyphsByGlyphIndex) {
		const std::string atlasKey = MakeAtlasGlyphKey(glyph);
		if (atlasAllocator->contains(atlasKey))
			glyph.atlasUV = ToGlyphRectExclusive(atlasAllocator->GetEntry(atlasKey), atlasTexture);

		const std::string outlineKey = MakeOutlineGlyphKey(atlasKey);
		if (atlasAllocator->contains(outlineKey))
			glyph.shadowAtlasUV = ToGlyphRectExclusive(atlasAllocator->GetEntry(outlineKey), shadowAtlasTexture);

		(void)glyphKey;
	}

	atlasAllocator->clear();
	glyphNameToBitmapIndex.clear();
	pendingGlyphBitmaps.clear();
	blurRectangles.clear();
}

void GlyphAtlasCache::UploadAtlasTextures()
{
	atlasTexture.Upload();
	shadowAtlasTexture.Upload();
}

void GlyphAtlasCache::DeleteSlugTextures() noexcept
{
#ifndef HEADLESS
	if (slugCurveTextureId != 0u) {
		glDeleteTextures(1, &slugCurveTextureId);
		slugCurveTextureId = 0u;
	}

	if (slugBandTextureId != 0u) {
		glDeleteTextures(1, &slugBandTextureId);
		slugBandTextureId = 0u;
	}
#else
	slugCurveTextureId = 0u;
	slugBandTextureId = 0u;
#endif
}
}
