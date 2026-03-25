/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "NullFontRenderer.h"

namespace spring::font::render {

NullFontRenderer::NullFontRenderer() = default;
NullFontRenderer::~NullFontRenderer() = default;

NullFontRenderer::NullFontRenderer(NullFontRenderer&&) noexcept = default;
NullFontRenderer& NullFontRenderer::operator=(NullFontRenderer&&) noexcept = default;

void NullFontRenderer::AddPrimaryQuad(const PreparedGlyphQuad& quad)
{
	if (quad.Empty()) {
		stats.droppedQuads += 1;
		return;
	}

	stats.queuedPrimaryQuads += 1;
}

void NullFontRenderer::AddOutlineQuad(const PreparedGlyphQuad& quad)
{
	if (quad.Empty()) {
		stats.droppedQuads += 1;
		return;
	}

	stats.queuedOutlineQuads += 1;
}

void NullFontRenderer::AddPrimaryGlyph(const spring::font::text::LaidOutGlyph& glyph)
{
	if (!glyph.visible || glyph.atlasUV.Empty()) {
		stats.droppedQuads += 1;
		return;
	}

	stats.queuedPrimaryQuads += 1;
}

void NullFontRenderer::AddOutlineGlyph(const spring::font::text::LaidOutGlyph& glyph)
{
	if (!glyph.visible || !glyph.usesOutline || glyph.outlineAtlasUV.Empty()) {
		stats.droppedQuads += 1;
		return;
	}

	stats.queuedOutlineQuads += 1;
}

void NullFontRenderer::DrawQueued()
{
}

void NullFontRenderer::HandleTextureUpdate(const GlyphAtlasTexture& primaryAtlas, const GlyphAtlasTexture* outlineAtlas, bool onlyUpload)
{
	stats.primaryTextureUploads += 1;

	if (outlineAtlas != nullptr)
		stats.outlineTextureUploads += 1;

	(void)primaryAtlas;
	(void)onlyUpload;
}

void NullFontRenderer::PushState(const FontRenderState& state)
{
	stateStackDepth += 1;
	stats.statePushes += 1;
	lastPushedState = state;
}

void NullFontRenderer::PopState(const FontRenderState& state)
{
	if (stateStackDepth > 0) {
		stateStackDepth -= 1;
		stats.statePops += 1;
	}

	(void)state;
}

bool NullFontRenderer::IsValid() const
{
	return true;
}

FontRendererStats NullFontRenderer::GetStats() const
{
	return stats;
}

void NullFontRenderer::ClearStats()
{
	stats = {};
}

std::size_t NullFontRenderer::GetStateStackDepth() const noexcept
{
	return stateStackDepth;
}

const FontRenderState& NullFontRenderer::GetLastPushedState() const noexcept
{
	return lastPushedState;
}

} // namespace spring::font::render
