/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "ShaderGlyphAtlasCache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

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

namespace {

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

[[nodiscard]] std::string MakeAtlasGlyphKey(const fonts::GlyphCacheCore::GlyphIndexKey& glyphKey)
{
	std::string key = "glyph:";
	key += std::to_string(glyphKey.faceIdentity);
	key += ':';
	key += std::to_string(glyphKey.glyphIndex);
	key += ':';
	key += std::to_string(static_cast<std::uint32_t>(glyphKey.sourceCodepoint));
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

[[nodiscard]] int GetMaxAtlasTextureSize() noexcept
{
	return (globalRendering != nullptr) ? globalRendering->maxTextureSize : 2048;
}

} // namespace

namespace fonts {

ShaderGlyphAtlasCache::ShaderGlyphAtlasCache(GlyphCacheCore& core_)
	: core(core_)
{
	ClearAtlasState();
}

ShaderGlyphAtlasCache::~ShaderGlyphAtlasCache() = default;

void ShaderGlyphAtlasCache::EnsureGlyphs(std::span<const GlyphKey> glyphKeys, const FacePtr& defaultFace)
{
	bool builtGlyphs = false;

	for (const GlyphKey& glyphKey: glyphKeys) {
		const GlyphInfo* glyph = ResolveBackendGlyph(core, glyphKey, defaultFace);
		if (glyph == nullptr)
			continue;

		const auto it = payloadsByGlyphKey.find(GlyphCacheCore::MakeGlyphIndexKey(*glyph));
		if (it != payloadsByGlyphKey.end() && it->second.attempted)
			continue;

		BuildBitmapGlyph(*glyph);
		builtGlyphs = true;
	}

	if (builtGlyphs || NeedsAtlasUpdate())
		UpdateAtlases();
}

void ShaderGlyphAtlasCache::EnsureGlyphPayloads(std::span<const GlyphInfo* const> glyphs)
{
	bool builtGlyphs = false;

	for (const GlyphInfo* glyph: glyphs) {
		if (glyph == nullptr || !glyph->HasFace() || !glyph->HasGlyphIndex())
			continue;

		const auto it = payloadsByGlyphKey.find(GlyphCacheCore::MakeGlyphIndexKey(*glyph));
		if (it != payloadsByGlyphKey.end() && it->second.attempted)
			continue;

		BuildBitmapGlyph(*glyph);
		builtGlyphs = true;
	}

	if (builtGlyphs || NeedsAtlasUpdate())
		UpdateAtlases();
}

bool ShaderGlyphAtlasCache::Clear()
{
	const bool changed = !payloadsByGlyphKey.empty() || NeedsAtlasUpdate() || atlasTexture.HasTexture() || shadowAtlasTexture.HasTexture();
	payloadsByGlyphKey.clear();
	ClearAtlasState();
	return changed;
}

bool ShaderGlyphAtlasCache::NeedsAtlasUpdate() const noexcept
{
	return !pendingGlyphBitmaps.empty();
}

bool ShaderGlyphAtlasCache::NeedsAtlasUpload() const noexcept
{
	return atlasTexture.NeedsUpload() || shadowAtlasTexture.NeedsUpload();
}

void ShaderGlyphAtlasCache::UpdateAtlases()
{
	if (pendingGlyphBitmaps.empty())
		return;

	PackPendingGlyphs();
	UpdateAtlasRegions();
}

void ShaderGlyphAtlasCache::UploadAtlases()
{
	UpdateAtlases();
	UploadAtlasTextures();
}

void ShaderGlyphAtlasCache::ReallocAtlases(bool preserveContents)
{
	const GlyphAtlasTexture::Dimensions currentAtlasDims = atlasTexture.GetDimensions();
	const GlyphAtlasTexture::Dimensions currentShadowDims = shadowAtlasTexture.GetDimensions();

	if (!currentAtlasDims.Empty())
		atlasTexture.Reallocate(currentAtlasDims, preserveContents);
	if (!currentShadowDims.Empty())
		shadowAtlasTexture.Reallocate(currentShadowDims, preserveContents);
}

const ShaderGlyphPayload* ShaderGlyphAtlasCache::FindPayload(const GlyphInfo& glyph) const
{
	if (!glyph.HasFace() || !glyph.HasGlyphIndex())
		return nullptr;

	const auto it = payloadsByGlyphKey.find(GlyphCacheCore::MakeGlyphIndexKey(glyph));
	return (it != payloadsByGlyphKey.end() && it->second.available) ? &it->second.payload : nullptr;
}

const ShaderGlyphPayload& ShaderGlyphAtlasCache::GetPayload(const GlyphInfo& glyph)
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

void ShaderGlyphAtlasCache::BuildBitmapGlyph(const GlyphInfo& glyph)
{
	if (!glyph.HasFace() || !glyph.HasGlyphIndex())
		return;

	const auto glyphKey = GlyphCacheCore::MakeGlyphIndexKey(glyph);
	auto& payloadRecord = payloadsByGlyphKey[glyphKey];
	if (payloadRecord.attempted)
		return;

	payloadRecord = {};
	payloadRecord.attempted = true;

#ifndef HEADLESS
	FT_Face ftFace = glyph.face->GetFTFace();
	if (ftFace == nullptr)
		return;

	FT_Int32 loadFlags = FT_LOAD_RENDER;
	if (AllowColorFonts() && FT_HAS_COLOR(ftFace))
		loadFlags = FT_LOAD_COLOR;

	if (glyph.face->LoadGlyph(glyph.glyphIndex, loadFlags) != 0)
		return;

	FT_GlyphSlot slot = ftFace->glyph;
	if (slot == nullptr)
		return;

	if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
		needsColor = true;

	int bitmapWidth = slot->bitmap.width;
	int bitmapHeight = slot->bitmap.rows;
	if (bitmapWidth <= 0 || bitmapHeight <= 0)
		return;

	if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && slot->bitmap.pixel_mode != FT_PIXEL_MODE_BGRA) {
		LOG_L(L_WARNING, "[ShaderGlyphAtlasCache::%s] Unsupported glyph pixel mode %d", __func__, slot->bitmap.pixel_mode);
		return;
	}

	const int channels = (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) ? 4 : 1;
	if (slot->bitmap.pitch != bitmapWidth * channels) {
		LOG_L(L_WARNING, "[ShaderGlyphAtlasCache::%s] Unsupported glyph pitch %d for width %d and channels %d", __func__, slot->bitmap.pitch, bitmapWidth, channels);
		return;
	}

	CBitmap glyphBitmap;
	if (!FT_IS_SCALABLE(ftFace) && ftFace->num_fixed_sizes > 0) {
		CBitmap temp(slot->bitmap.buffer, bitmapWidth, bitmapHeight, channels);
		const float pixelSize = static_cast<float>(ftFace->available_sizes[0].y_ppem);
		const float ratio = 1.0f / (pixelSize * core.GetNormScale());

		bitmapWidth = std::max(1, static_cast<int>(std::floor(bitmapWidth * ratio)));
		bitmapHeight = std::max(1, static_cast<int>(std::floor(bitmapHeight * ratio)));
		glyphBitmap = temp.CreateRescaled(bitmapWidth, bitmapHeight);
	} else {
		glyphBitmap = CBitmap(slot->bitmap.buffer, bitmapWidth, bitmapHeight, channels);
	}

	CBitmap outlineBitmap;
	CBitmap* outlineBitmapPtr = nullptr;
	if (core.GetOutlineWidth() > 0) {
		outlineBitmap.Alloc(bitmapWidth + 2 * core.GetOutlineWidth(), bitmapHeight + 2 * core.GetOutlineWidth(), channels);
		outlineBitmap.CopySubImage(glyphBitmap, core.GetOutlineWidth(), core.GetOutlineWidth());
		outlineBitmap.Blur(core.GetOutlineWidth(), core.GetOutlineWeight(), 0, 0, outlineBitmap.xsize, outlineBitmap.ysize);
		outlineBitmapPtr = &outlineBitmap;
	}

	QueueGlyphBitmap(MakeAtlasGlyphKey(glyph), glyphBitmap, outlineBitmapPtr);
	payloadRecord.available = true;
#endif
}

void ShaderGlyphAtlasCache::QueueGlyphBitmap(const std::string& cacheKey, const CBitmap& bitmap, const CBitmap* outlineBitmap)
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

void ShaderGlyphAtlasCache::PackPendingGlyphs()
{
	if (pendingGlyphBitmaps.empty())
		return;

	if (!atlasAllocator->Allocate())
		LOG_L(L_WARNING, "[ShaderGlyphAtlasCache::%s] Atlas allocation reached the texture limit", __func__);

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
		for (auto& [glyphKey, payloadRecord]: payloadsByGlyphKey) {
			if (!payloadRecord.available)
				continue;

			RescaleGlyphRectUV(payloadRecord.payload.atlasUV, oldPrimaryDims.width, oldPrimaryDims.height, allocWidth, allocHeight);
			RescaleGlyphRectUV(payloadRecord.payload.shadowAtlasUV, oldShadowDims.width, oldShadowDims.height, allocWidth, allocHeight);
			(void)glyphKey;
		}
	}
}

void ShaderGlyphAtlasCache::UpdateAtlasRegions()
{
	if (pendingGlyphBitmaps.empty())
		return;

	for (const auto& [glyphName, bitmapIndex]: glyphNameToBitmapIndex) {
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

	for (auto& [glyphKey, payloadRecord]: payloadsByGlyphKey) {
		if (!payloadRecord.available)
			continue;

		const std::string atlasKey = MakeAtlasGlyphKey(glyphKey);
		if (atlasAllocator->contains(atlasKey))
			payloadRecord.payload.atlasUV = ToGlyphRectExclusive(atlasAllocator->GetEntry(atlasKey), atlasTexture);

		const std::string outlineKey = MakeOutlineGlyphKey(atlasKey);
		if (atlasAllocator->contains(outlineKey))
			payloadRecord.payload.shadowAtlasUV = ToGlyphRectExclusive(atlasAllocator->GetEntry(outlineKey), shadowAtlasTexture);
	}

	atlasAllocator->clear();
	glyphNameToBitmapIndex.clear();
	pendingGlyphBitmaps.clear();
	blurRectangles.clear();
}

void ShaderGlyphAtlasCache::UploadAtlasTextures()
{
	atlasTexture.Upload();
	shadowAtlasTexture.Upload();
}

void ShaderGlyphAtlasCache::ClearAtlasState()
{
	needsColor = false;
	isColor = false;

	atlasAllocator = std::make_unique<CRowAtlasAlloc>();
	atlasAllocator->SetMaxSize(GetMaxAtlasTextureSize(), GetMaxAtlasTextureSize());

	atlasTexture.Reset();
	shadowAtlasTexture.Reset();
	atlasTexture.SetPixelFormat(GlyphAtlasTexture::PixelFormat::Alpha);
	atlasTexture.SetContentType(GlyphAtlasTexture::ContentType::Bitmap);
	shadowAtlasTexture.SetPixelFormat(GlyphAtlasTexture::PixelFormat::Alpha);
	shadowAtlasTexture.SetContentType(GlyphAtlasTexture::ContentType::Bitmap);
	atlasTexture.Reallocate(32, 32, false);
	shadowAtlasTexture.Reallocate(32, 32, false);

	pendingGlyphBitmaps.clear();
	blurRectangles.clear();
	glyphNameToBitmapIndex.clear();
}

} // namespace fonts
