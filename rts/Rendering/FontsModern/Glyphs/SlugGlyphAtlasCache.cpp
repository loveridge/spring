/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "SlugGlyphAtlasCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_BBOX_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_STROKER_H

#include "Rendering/FontsModern/FreeType/FontFace.h"
#include "Rendering/FontsModern/FreeType/FontFaceSet.h"
#include "Rendering/GL/myGL.h"

namespace {

static constexpr std::uint32_t SlugTextureWidth = 4096u;
static constexpr std::uint32_t SlugBandCount = 16u;
static constexpr float SlugOutlineStrokeScale = 0.2f;

[[nodiscard]] const GlyphInfo* ResolveBackendGlyph(fonts::GlyphCacheCore& core, const GlyphKey& glyphKey, const fonts::GlyphCacheCore::FacePtr& defaultFace)
{
	if (!glyphKey.IsGlyphIndex())
		return &core.GetGlyphByCodepoint(glyphKey.codepoint);

	if (glyphKey.glyphIndex == 0u) {
		if (glyphKey.codepoint != 0u)
			return &core.GetGlyphByCodepoint(glyphKey.codepoint);

		return nullptr;
	}

	auto face = defaultFace;
	if (face == nullptr) {
		const auto& faceSet = core.GetFaceSet();
		if (faceSet != nullptr)
			face = faceSet->GetPrimaryFace();
	}

	if (face == nullptr)
		return nullptr;

	return &core.GetGlyphByGlyphIndex(glyphKey.glyphIndex, face, glyphKey.codepoint);
}

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

} // namespace

namespace fonts {

SlugGlyphAtlasCache::SlugGlyphAtlasCache(GlyphCacheCore& core_)
	: core(core_)
{
	ClearSlugState();
}

SlugGlyphAtlasCache::~SlugGlyphAtlasCache()
{
	DeleteTextures();
}

void SlugGlyphAtlasCache::EnsureGlyphs(std::span<const GlyphKey> glyphKeys, const FacePtr& defaultFace)
{
	for (const GlyphKey& glyphKey: glyphKeys) {
		const GlyphInfo* glyph = ResolveBackendGlyph(core, glyphKey, defaultFace);
		if (glyph == nullptr)
			continue;

		const auto it = payloadsByGlyphKey.find(GlyphCacheCore::MakeGlyphIndexKey(*glyph));
		if (it != payloadsByGlyphKey.end() && it->second.attempted)
			continue;

		BuildSlugGlyph(*glyph);
	}
}

void SlugGlyphAtlasCache::EnsureGlyphPayloads(std::span<const GlyphInfo* const> glyphs)
{
	for (const GlyphInfo* glyph: glyphs) {
		if (glyph == nullptr || !glyph->HasFace() || !glyph->HasGlyphIndex())
			continue;

		const auto it = payloadsByGlyphKey.find(GlyphCacheCore::MakeGlyphIndexKey(*glyph));
		if (it != payloadsByGlyphKey.end() && it->second.attempted)
			continue;

		BuildSlugGlyph(*glyph);
	}
}

bool SlugGlyphAtlasCache::Clear()
{
	const bool changed = !payloadsByGlyphKey.empty() || !slugCurveTexels.empty() || !slugBandTexels.empty() || slugCurveTextureId != 0u || slugBandTextureId != 0u;
	payloadsByGlyphKey.clear();
	ClearSlugState();
	return changed;
}

bool SlugGlyphAtlasCache::NeedsUpload() const noexcept
{
	return slugTexturesDirty && !slugCurveTexels.empty() && !slugBandTexels.empty();
}

void SlugGlyphAtlasCache::UploadTextures()
{
#ifdef HEADLESS
	return;
#else
	if (!NeedsUpload())
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
	GLint prevUnpackAlignment = 4;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);

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

	glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTextureBinding));
	slugTexturesDirty = false;
#endif
}

SlugGlyphAtlasCache::SlugTextureInfo SlugGlyphAtlasCache::GetTextureInfo() const noexcept
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

const SlugGlyphPayload* SlugGlyphAtlasCache::FindPayload(const GlyphInfo& glyph) const
{
	if (!glyph.HasFace() || !glyph.HasGlyphIndex())
		return nullptr;

	const auto it = payloadsByGlyphKey.find(GlyphCacheCore::MakeGlyphIndexKey(glyph));
	return (it != payloadsByGlyphKey.end() && it->second.available) ? &it->second.payload : nullptr;
}

const SlugGlyphPayload& SlugGlyphAtlasCache::GetPayload(const GlyphInfo& glyph)
{
	if (!glyph.HasFace() || !glyph.HasGlyphIndex())
		return dummyPayload;

	const auto glyphKey = GlyphCacheCore::MakeGlyphIndexKey(glyph);
	if (const auto it = payloadsByGlyphKey.find(glyphKey); it != payloadsByGlyphKey.end()) {
		return it->second.available ? it->second.payload : dummyPayload;
	}

	const GlyphInfo* glyphPtr = &glyph;
	EnsureGlyphPayloads(std::span<const GlyphInfo* const>(&glyphPtr, 1u));

	if (const auto it = payloadsByGlyphKey.find(glyphKey); it != payloadsByGlyphKey.end()) {
		return it->second.available ? it->second.payload : dummyPayload;
	}

	return dummyPayload;
}

void SlugGlyphAtlasCache::BuildSlugGlyph(const GlyphInfo& glyph)
{
	if (!glyph.HasFace() || !glyph.HasGlyphIndex())
		return;

	const auto glyphKey = GlyphCacheCore::MakeGlyphIndexKey(glyph);
	auto& payloadRecord = payloadsByGlyphKey[glyphKey];
	if (payloadRecord.attempted)
		return;

	payloadRecord = {};
	payloadRecord.attempted = true;

	BuildSlugFillGlyph(glyph.face, glyph.glyphIndex, payloadRecord.payload);
	BuildSlugOutlineGlyph(glyph.face, glyph.glyphIndex, payloadRecord.payload);
	payloadRecord.available = payloadRecord.payload.HasFill();
}

bool SlugGlyphAtlasCache::BuildSlugGlyphFromFTOutline(const FT_Outline& outline, SlugGlyphInfo& outInfo)
{
#ifdef HEADLESS
	(void)outline;
	(void)outInfo;
	return false;
#else
	outInfo = {};

	if (outline.n_contours <= 0 || outline.n_points <= 0)
		return false;

	FT_BBox bbox {};
	if (FT_Outline_Get_BBox(const_cast<FT_Outline*>(&outline), &bbox) != 0)
		return false;

	const float width = (bbox.xMax - bbox.xMin) * core.GetNormScale();
	const float height = (bbox.yMax - bbox.yMin) * core.GetNormScale();
	if (width <= 0.0f || height <= 0.0f)
		return false;

	SlugOutlineBuilder builder;
	builder.minX = static_cast<float>(bbox.xMin);
	builder.minY = static_cast<float>(bbox.yMin);
	builder.scale = core.GetNormScale();

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

	const std::size_t initialCurveTexelCount = slugCurveTexels.size();
	const std::size_t initialBandTexelCount = slugBandTexels.size();
	const std::uint32_t initialCurveTextureHeight = slugCurveTextureHeight;
	const std::uint32_t initialBandTextureHeight = slugBandTextureHeight;
	const bool initialSlugTexturesDirty = slugTexturesDirty;
	const auto rollbackSlugData = [this, initialCurveTexelCount, initialBandTexelCount, initialCurveTextureHeight, initialBandTextureHeight, initialSlugTexturesDirty]() {
		slugCurveTexels.resize(initialCurveTexelCount);
		slugBandTexels.resize(initialBandTexelCount);
		slugCurveTextureHeight = initialCurveTextureHeight;
		slugBandTextureHeight = initialBandTextureHeight;
		slugTexturesDirty = initialSlugTexturesDirty;
	};

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

	auto appendBandCurves = [this, glyphBaseTexel](const std::vector<SlugCurveData*>& sortedCurves, bool horizontal, float bandSize, float glyphExtent, bool& overflowed) -> std::vector<std::pair<std::uint16_t, std::uint16_t>> {
		std::vector<std::pair<std::uint16_t, std::uint16_t>> descriptors;
		descriptors.reserve(SlugBandCount);

		for (std::uint32_t band = 0; band < SlugBandCount; ++band) {
			const float bandMin = band * bandSize;
			const float bandMax = (band == (SlugBandCount - 1u)) ? glyphExtent : ((band + 1u) * bandSize);
			const std::size_t relativeOffsetValue = slugBandTexels.size() / 2u - glyphBaseTexel;
			if (relativeOffsetValue > std::numeric_limits<std::uint16_t>::max()) {
				overflowed = true;
				return {};
			}

			const std::uint16_t relativeOffset = static_cast<std::uint16_t>(relativeOffsetValue);
			std::uint16_t curveCount = 0u;

			for (const SlugCurveData* curve: sortedCurves) {
				const float c1 = horizontal ? curve->y1 : curve->x1;
				const float c2 = horizontal ? curve->y2 : curve->x2;
				const float c3 = horizontal ? curve->y3 : curve->x3;

				if (NearlyEqual(c1, c2) && NearlyEqual(c2, c3))
					continue;

				if (Min3(c1, c2, c3) > bandMax || Max3(c1, c2, c3) < bandMin)
					continue;

				if (curveCount == std::numeric_limits<std::uint16_t>::max()) {
					overflowed = true;
					return {};
				}

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

	bool bandDescriptorOverflowed = false;
	const auto horizontalBands = appendBandCurves(horizontalCurves, true, bandDimY, height, bandDescriptorOverflowed);
	if (bandDescriptorOverflowed) {
		rollbackSlugData();
		return false;
	}

	const auto verticalBands = appendBandCurves(verticalCurves, false, bandDimX, width, bandDescriptorOverflowed);
	if (bandDescriptorOverflowed) {
		rollbackSlugData();
		return false;
	}

	for (std::uint32_t i = 0; i < bandCount; ++i) {
		writeBandHeader(i, horizontalBands[i].first, horizontalBands[i].second);
		writeBandHeader(bandCount + i, verticalBands[i].first, verticalBands[i].second);
	}

	outInfo.offsetX = bbox.xMin * core.GetNormScale();
	outInfo.offsetY = bbox.yMin * core.GetNormScale() - core.GetDescender();
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

bool SlugGlyphAtlasCache::BuildSlugFillGlyph(const FacePtr& face, std::uint32_t glyphIndex, SlugGlyphPayload& payload)
{
#ifdef HEADLESS
	(void)face;
	(void)glyphIndex;
	(void)payload;
	return false;
#else
	payload.fill = {};

	if (face == nullptr)
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

	return BuildSlugGlyphFromFTOutline(slot->outline, payload.fill);
#endif
}

bool SlugGlyphAtlasCache::BuildSlugOutlineGlyph(const FacePtr& face, std::uint32_t glyphIndex, SlugGlyphPayload& payload)
{
#ifdef HEADLESS
	(void)face;
	(void)glyphIndex;
	(void)payload;
	return false;
#else
	payload.outline = {};

	if (face == nullptr)
		return false;

	if (core.GetOutlineWidth() <= 0) {
		payload.outline = payload.fill;
		return !payload.outline.Empty();
	}

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

	const FT_Fixed outlineRadius = static_cast<FT_Fixed>(std::lround(core.GetOutlineWidth() * SlugOutlineStrokeScale * 64.0f));

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

	payload.outline = strokedInfo;
	return true;
#endif
}

void SlugGlyphAtlasCache::ClearSlugState()
{
	DeleteTextures();
	slugCurveTexels.clear();
	slugBandTexels.clear();
	slugCurveTextureWidth = SlugTextureWidth;
	slugCurveTextureHeight = 0;
	slugBandTextureWidth = SlugTextureWidth;
	slugBandTextureHeight = 0;
	slugTexturesDirty = false;
}

void SlugGlyphAtlasCache::DeleteTextures() noexcept
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

} // namespace fonts
