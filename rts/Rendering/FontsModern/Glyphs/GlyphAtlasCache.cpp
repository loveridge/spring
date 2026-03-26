/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GlyphAtlasCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Rendering/FontsModern/FreeType/FontFace.h"
#include "Rendering/FontsModern/FreeType/FontFaceSet.h"
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

[[nodiscard]] GlyphRect ToGlyphRect(const AtlasedTexture& uv)
{
	return GlyphRect(uv.x1, uv.y1, uv.x2 - uv.x1, uv.y2 - uv.y1);
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
GlyphAtlasCache::GlyphAtlasCache(FaceSetPtr faceSet, int fontSize_, int outlineSize_, float outlineWeight_)
	: faces(std::move(faceSet))
	, fontSize(fontSize_ > 0 ? fontSize_ : 14)
	, outlineSize(std::max(outlineSize_, 0))
	, outlineWeight(outlineWeight_)
{
	ClearAtlasState();
	ConfigureFaceMetrics();
	PreloadGlyphs();
}

GlyphAtlasCache::~GlyphAtlasCache() = default;

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

	return dummyGlyph;
}

const GlyphInfo& GlyphAtlasCache::GetGlyphByGlyphIndex(std::uint32_t glyphIndex, const FacePtr& face, char32_t sourceCodepoint)
{
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

void GlyphAtlasCache::ClearAtlasState()
{
	atlasAllocator = std::make_unique<CRowAtlasAlloc>();
	atlasAllocator->SetMaxSize(GetMaxAtlasTextureSize(), GetMaxAtlasTextureSize());

	atlasTexture.SetPixelFormat(isColor ? GlyphAtlasTexture::PixelFormat::BGRA : GlyphAtlasTexture::PixelFormat::Alpha);
	shadowAtlasTexture.SetPixelFormat(isColor ? GlyphAtlasTexture::PixelFormat::BGRA : GlyphAtlasTexture::PixelFormat::Alpha);

	atlasTexture.Reset();
	shadowAtlasTexture.Reset();
	atlasTexture.Reallocate(32, 32, false);
	shadowAtlasTexture.Reallocate(32, 32, false);

	pendingGlyphBitmaps.clear();
	blurRectangles.clear();
	glyphNameToBitmapIndex.clear();
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
	FacePtr face;
	std::uint32_t glyphIndex = 0;

	if (faces != nullptr) {
		auto [matchedFace, matchedGlyphIndex] = faces->FindGlyphForCodepoint(codepoint);
		face = std::move(matchedFace);
		glyphIndex = matchedGlyphIndex;

		if (face == nullptr)
			face = faces->GetPrimaryFace();
	}

	if (face != nullptr) {
		LoadGlyphByIndex(face, codepoint, glyphIndex);

		if (const GlyphInfo* glyphByIndex = FindGlyphByGlyphIndex(glyphIndex, face, codepoint); glyphByIndex != nullptr)
			glyph = *glyphByIndex;

		if (glyph.face == nullptr) {
			glyph.face = std::move(face);
			glyph.glyphIndex = glyphIndex;
		}
	} else {
		failedAttemptsToReplace[codepoint] += 1;
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

	atlasTexture.Reallocate(allocWidth, allocHeight, true);
	shadowAtlasTexture.Reallocate(allocWidth, allocHeight, true);
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
			glyph.atlasUV = ToGlyphRect(atlasAllocator->GetTexCoordsEdge(glyphKey));

		const std::string outlineKey = MakeOutlineGlyphKey(glyphKey);
		if (atlasAllocator->contains(outlineKey))
			glyph.shadowAtlasUV = ToGlyphRect(atlasAllocator->GetTexCoordsEdge(outlineKey));

		(void)codepoint;
	}

	for (auto& [glyphKey, glyph] : glyphsByGlyphIndex) {
		const std::string atlasKey = MakeAtlasGlyphKey(glyph);
		if (atlasAllocator->contains(atlasKey))
			glyph.atlasUV = ToGlyphRect(atlasAllocator->GetTexCoordsEdge(atlasKey));

		const std::string outlineKey = MakeOutlineGlyphKey(atlasKey);
		if (atlasAllocator->contains(outlineKey))
			glyph.shadowAtlasUV = ToGlyphRect(atlasAllocator->GetTexCoordsEdge(outlineKey));

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
}
