/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../FontTypes.h"
#include "../Text/TextRun.h"
#include "System/Matrix44f.h"

class GlyphAtlasTexture;
struct SColor;
struct float3;
struct float4;

namespace fonts::render {

/**
 * Small renderer-agnostic vertex payload for a prepared glyph quad.
 *
 * Coordinates are already laid out in world or screen space; the renderer only
 * needs to map atlas UVs to the active backend.
 */
struct PreparedGlyphQuad {
	GlyphRect position;
	GlyphRect atlasUV;
	FontColor color{};
	float z = 0.0f;
	bool visible = true;

	bool Empty() const noexcept {
		return position.Empty() || atlasUV.Empty() || !visible;
	}
};

/**
 * Renderer statistics useful for debugging and instrumentation.
 */
struct FontRendererStats {
	std::size_t queuedPrimaryQuads = 0;
	std::size_t queuedOutlineQuads = 0;
	std::size_t drawCalls = 0;
	std::size_t primaryTextureUploads = 0;
	std::size_t outlineTextureUploads = 0;
	std::size_t statePushes = 0;
	std::size_t statePops = 0;
	std::size_t droppedQuads = 0;
};

/**
 * Lightweight render-state snapshot supplied by the public font facade.
 *
 * This keeps the renderer interface independent from CglFont and avoids
 * exposing backend details in the public font header.
 */
struct FontRenderState {
	FontColor primaryColor{};
	FontColor outlineColor{};
	FontDepth depth{};

	CMatrix44f modelViewMatrix = CMatrix44f::Identity();
	CMatrix44f projectionMatrix = CMatrix44f::Identity();
	bool hasModelViewMatrix = false;
	bool hasProjectionMatrix = false;

	const float3* worldPos = nullptr;
	const float4* clipPlane = nullptr;
	const SColor* rawPrimaryColor = nullptr;
	const SColor* rawOutlineColor = nullptr;

	bool useWorldSpace = false;
	bool normalizedCoordinates = false;
	bool useColorAtlas = false;
	bool useOutline = false;
	bool useShadow = false;
	bool buffered = false;
	bool userDefinedBlending = false;
};

/**
 * Renderer interface only.
 *
 * Implementations consume already-prepared quads or laid-out glyphs and own the
 * backend-specific GL state handling. They do not shape, wrap, or cache glyphs.
 */
class IFontRenderer {
public:
	virtual ~IFontRenderer() = default;

	virtual void AddPrimaryQuad(const PreparedGlyphQuad& quad) = 0;
	virtual void AddOutlineQuad(const PreparedGlyphQuad& quad) = 0;

	virtual void AddPrimaryGlyph(const fonts::text::LaidOutGlyph& glyph) = 0;
	virtual void AddOutlineGlyph(const fonts::text::LaidOutGlyph& glyph) = 0;

	virtual void AddPrimaryGlyphs(const std::vector<fonts::text::LaidOutGlyph>& glyphs);
	virtual void AddOutlineGlyphs(const std::vector<fonts::text::LaidOutGlyph>& glyphs);

	virtual void DrawQueued() = 0;

	virtual void HandleTextureUpdate(const GlyphAtlasTexture& primaryAtlas,
	                                 const GlyphAtlasTexture* outlineAtlas = nullptr,
	                                 bool onlyUpload = false) = 0;

	virtual void PushState(const FontRenderState& state) = 0;
	virtual void PopState(const FontRenderState& state) = 0;

	virtual bool IsValid() const = 0;
	virtual bool IsLegacy() const { return false; }

	virtual FontRendererStats GetStats() const = 0;
	virtual void ClearStats() = 0;
};

using FontRendererPtr = std::unique_ptr<IFontRenderer>;

inline void IFontRenderer::AddPrimaryGlyphs(const std::vector<fonts::text::LaidOutGlyph>& glyphs)
{
	for (const auto& glyph: glyphs)
		AddPrimaryGlyph(glyph);
}

inline void IFontRenderer::AddOutlineGlyphs(const std::vector<fonts::text::LaidOutGlyph>& glyphs)
{
	for (const auto& glyph: glyphs)
		AddOutlineGlyph(glyph);
}

} // namespace font::render
