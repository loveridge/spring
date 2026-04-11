/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GlyphCacheCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Rendering/FontsModern/FreeType/FontFace.h"
#include "Rendering/FontsModern/FreeType/FontFaceSet.h"

namespace {

static constexpr char32_t PrimaryReplacementCodepoint = 0xfffd;
static constexpr char32_t SecondaryReplacementCodepoint = U'?';

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

#ifndef HEADLESS
static constexpr int FT_INTERNAL_DPI = 64;
#endif

} // namespace

namespace fonts {

std::size_t GlyphCacheCore::GlyphIndexKeyHasher::operator()(const GlyphIndexKey& key) const noexcept
{
	std::size_t h = std::hash<std::uintptr_t>{}(key.faceIdentity);
	h ^= std::hash<std::uint32_t>{}(key.glyphIndex) + 0x9e3779b9u + (h << 6) + (h >> 2);
	h ^= std::hash<char32_t>{}(key.sourceCodepoint) + 0x9e3779b9u + (h << 6) + (h >> 2);
	return h;
}

GlyphCacheCore::GlyphCacheCore(FaceSetPtr faceSet, int fontSize_, int outlineSize_, float outlineWeight_)
	: faces(std::move(faceSet))
	, fontSize(fontSize_ > 0 ? fontSize_ : 14)
	, outlineSize(std::max(outlineSize_, 0))
	, outlineWeight(outlineWeight_)
{
	ConfigureFaceMetrics();
	PreloadGlyphs();
}

void GlyphCacheCore::SetFaceSet(FaceSetPtr faceSet)
{
	if (faces == faceSet)
		return;

	faces = std::move(faceSet);
	ClearGlyphs();
}

const GlyphInfo& GlyphCacheCore::GetGlyphByCodepoint(char32_t codepoint)
{
	if (const GlyphInfo* glyph = FindGlyphByCodepoint(codepoint); glyph != nullptr)
		return *glyph;

	LoadGlyphByCodepoint(codepoint);

	if (const GlyphInfo* glyph = FindGlyphByCodepoint(codepoint); glyph != nullptr)
		return *glyph;

	return GetResolvedGlyphByCodepoint(codepoint);
}

const GlyphInfo& GlyphCacheCore::GetGlyphByGlyphIndex(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint)
{
	if (glyphIndex == 0u) {
		if (sourceCodepoint != 0)
			return GetGlyphByCodepoint(sourceCodepoint);

		return dummyGlyph;
	}

	if (const GlyphInfo* glyph = FindGlyphByGlyphIndex(glyphIndex, face, sourceCodepoint); glyph != nullptr)
		return *glyph;

	LoadGlyphByIndex(face, sourceCodepoint, glyphIndex);

	if (const GlyphInfo* glyph = FindGlyphByGlyphIndex(glyphIndex, face, sourceCodepoint); glyph != nullptr)
		return *glyph;

	return dummyGlyph;
}

const GlyphInfo* GlyphCacheCore::FindGlyphByCodepoint(char32_t codepoint) const
{
	const auto it = glyphsByCodepoint.find(codepoint);
	return (it != glyphsByCodepoint.end()) ? &it->second : nullptr;
}

const GlyphInfo* GlyphCacheCore::FindGlyphByGlyphIndex(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint) const
{
	if (face == nullptr)
		return nullptr;

	const auto it = glyphsByGlyphIndex.find(MakeGlyphIndexKey(glyphIndex, face, sourceCodepoint));
	return (it != glyphsByGlyphIndex.end()) ? &it->second : nullptr;
}

void GlyphCacheCore::EnsureGlyphs(std::span<const char32_t> codepoints)
{
	for (char32_t codepoint: codepoints)
		LoadGlyphByCodepoint(codepoint);
}

void GlyphCacheCore::EnsureGlyphs(char32_t beginInclusive, char32_t endInclusive)
{
	if (endInclusive < beginInclusive)
		std::swap(beginInclusive, endInclusive);

	for (char32_t codepoint = beginInclusive; ; ++codepoint) {
		LoadGlyphByCodepoint(codepoint);

		if (codepoint == endInclusive || codepoint == std::numeric_limits<char32_t>::max())
			break;
	}
}

void GlyphCacheCore::EnsureGlyphIndices(std::span<const std::uint32_t> glyphIndices, const FacePtr& face, char32_t sourceCodepoint)
{
	for (std::uint32_t glyphIndex: glyphIndices)
		LoadGlyphByIndex(face, sourceCodepoint, glyphIndex);
}

void GlyphCacheCore::EnsureGlyphKeys(std::span<const GlyphKey> glyphKeys, const FacePtr& defaultFace)
{
	for (const GlyphKey& glyphKey: glyphKeys) {
		if (glyphKey.IsGlyphIndex()) {
			if (glyphKey.glyphIndex == 0u) {
				if (glyphKey.codepoint != 0u)
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
}

bool GlyphCacheCore::ClearGlyphs()
{
	const bool changed = !glyphsByCodepoint.empty() || !glyphsByGlyphIndex.empty() || !failedAttemptsToReplace.empty();

	failedAttemptsToReplace.clear();
	glyphsByCodepoint.clear();
	glyphsByGlyphIndex.clear();

	ResetKerningCaches();
	ConfigureFaceMetrics();
	PreloadGlyphs();

	return changed;
}

void GlyphCacheCore::PreloadGlyphs()
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

float GlyphCacheCore::GetKerning(const GlyphInfo& leftGlyph, const GlyphInfo& rightGlyph)
{
#ifndef HEADLESS
	if (leftGlyph.face == nullptr || rightGlyph.face == nullptr)
		return leftGlyph.advance;

	if (leftGlyph.face != rightGlyph.face)
		return leftGlyph.advance;

	if (!leftGlyph.face->HasKerning())
		return leftGlyph.advance;

	const FacePtr primaryFace = (faces != nullptr) ? faces->GetPrimaryFace() : nullptr;
	if (leftGlyph.sourceCodepoint < 128u && rightGlyph.sourceCodepoint < 128u &&
	    leftGlyph.face == primaryFace && rightGlyph.face == primaryFace) {
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

GlyphCacheCore::GlyphIndexKey GlyphCacheCore::MakeGlyphIndexKey(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint) noexcept
{
	return GlyphIndexKey {
		.faceIdentity = reinterpret_cast<std::uintptr_t>(face.get()),
		.glyphIndex = glyphIndex,
		.sourceCodepoint = sourceCodepoint,
	};
}

GlyphCacheCore::GlyphIndexKey GlyphCacheCore::MakeGlyphIndexKey(const GlyphInfo& glyph) noexcept
{
	return MakeGlyphIndexKey(glyph.glyphIndex, glyph.face, glyph.sourceCodepoint);
}

std::pair<GlyphCacheCore::FacePtr, std::uint32_t> GlyphCacheCore::ResolveGlyphForCodepoint(char32_t codepoint, char32_t* resolvedCodepoint) const
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

const GlyphInfo* GlyphCacheCore::FindResolvedGlyphByCodepoint(char32_t codepoint) const
{
	if (const GlyphInfo* glyph = FindGlyphByCodepoint(codepoint); glyph != nullptr)
		return glyph;

	char32_t resolvedCodepoint = 0;
	auto [face, glyphIndex] = ResolveGlyphForCodepoint(codepoint, &resolvedCodepoint);
	if (face == nullptr || glyphIndex == 0u)
		return nullptr;

	return FindGlyphByGlyphIndex(glyphIndex, face, resolvedCodepoint);
}

const GlyphInfo& GlyphCacheCore::GetResolvedGlyphByCodepoint(char32_t codepoint)
{
	if (const GlyphInfo* glyph = FindResolvedGlyphByCodepoint(codepoint); glyph != nullptr)
		return *glyph;

	char32_t resolvedCodepoint = 0;
	auto [face, glyphIndex] = ResolveGlyphForCodepoint(codepoint, &resolvedCodepoint);
	if (face == nullptr || glyphIndex == 0u)
		return dummyGlyph;

	LoadGlyphByIndex(face, resolvedCodepoint, glyphIndex);

	if (const GlyphInfo* glyph = FindGlyphByGlyphIndex(glyphIndex, face, resolvedCodepoint); glyph != nullptr)
		return *glyph;

	return dummyGlyph;
}

void GlyphCacheCore::ResetKerningCaches()
{
	kerningPrecached.fill(0.0f);
	kerningDynamic.clear();
}

void GlyphCacheCore::ConfigureFaceMetrics()
{
	lineHeight = 0.0f;
	fontAscender = 0.0f;
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

	fontAscender = normScale * FT_MulFix(ftFace->ascender, ftFace->size->metrics.y_scale);
	fontDescender = normScale * FT_MulFix(ftFace->descender, ftFace->size->metrics.y_scale);
	lineHeight = (ftFace->units_per_EM > 0) ? (static_cast<float>(ftFace->height) / ftFace->units_per_EM) : 0.0f;

	if (lineHeight <= 0.0f)
		lineHeight = 1.25f * (ftFace->bbox.yMax - ftFace->bbox.yMin);

	if (canScale) {
		fontAscender *= (pixelScale / normScale);
		fontDescender *= (pixelScale / normScale);
		lineHeight *= pixelScale;
	}
#endif
}

void GlyphCacheCore::LoadGlyphByCodepoint(char32_t codepoint)
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

void GlyphCacheCore::LoadGlyphByIndex(const FacePtr& face, char32_t sourceCodepoint, std::uint32_t glyphIndex)
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

	// Preserve the legacy cache behavior: metrics come from a rendered,
	// hinted load rather than a metrics-only load. Small-size SLUG output is
	// sensitive to these hinted extents.
	if (face->LoadGlyph(glyphIndex, FT_LOAD_RENDER) != 0) {
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

	FT_GlyphSlot slot = ftFace->glyph;
	if (slot == nullptr) {
		CacheGlyphRecord(glyphKey, std::move(glyph));
		return;
	}

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

	if (!FT_IS_SCALABLE(ftFace) && ftFace->num_fixed_sizes > 0) {
		const float pixelSize = static_cast<float>(ftFace->available_sizes[0].y_ppem);
		const float ratio = 1.0f / (pixelSize * normScale);

		glyph.advance *= ratio;
		glyph.descender *= ratio;
		glyph.bitmapBounds.y = yBearing * ratio - fontDescender;
		glyph.bitmapBounds.x *= ratio;
		glyph.bitmapBounds.w *= ratio;
		glyph.bitmapBounds.h *= ratio;
		glyph.height *= ratio;
	}
#endif

	CacheGlyphRecord(glyphKey, std::move(glyph));
}

void GlyphCacheCore::CacheGlyphRecord(char32_t codepoint, GlyphInfo glyph)
{
	glyphsByCodepoint[codepoint] = std::move(glyph);
}

void GlyphCacheCore::CacheGlyphRecord(const GlyphIndexKey& key, GlyphInfo glyph)
{
	glyphsByGlyphIndex[key] = std::move(glyph);
}

} // namespace fonts
