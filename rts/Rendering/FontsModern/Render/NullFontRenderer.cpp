/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "NullFontRenderer.h"
#include "FontRendererFactory.h"

namespace fonts::render {

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

void NullFontRenderer::AddPrimaryGlyph(const fonts::text::LaidOutGlyph& glyph)
{
	if (!glyph.visible || (glyph.atlasUV.Empty() && glyph.slugFillInfo.Empty())) {
		stats.droppedQuads += 1;
		return;
	}

	stats.queuedPrimaryQuads += 1;
}

void NullFontRenderer::AddOutlineGlyph(const fonts::text::LaidOutGlyph& glyph)
{
	if (!glyph.visible || !glyph.usesOutline || (glyph.outlineAtlasUV.Empty() && glyph.slugFillInfo.Empty())) {
		stats.droppedQuads += 1;
		return;
	}

	stats.queuedOutlineQuads += 1;
}

void NullFontRenderer::DrawQueued()
{
}

void NullFontRenderer::PushState(const FontRenderState& state)
{
	stateStackDepth += 1;
	stats.statePushes += 1;
	lastPushedState = state;
}

void NullFontRenderer::PopState()
{
	if (stateStackDepth > 0) {
		stateStackDepth -= 1;
		stats.statePops += 1;
	}
}

FontRendererBackend NullFontRenderer::GetBackend() const noexcept
{
	return FontRendererBackend::Null;
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

} // namespace fonts::render
