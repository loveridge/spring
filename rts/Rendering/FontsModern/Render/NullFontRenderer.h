/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <memory>

#include "Render/IFontRenderer.h"

namespace font::render {

class NullFontRenderer final : public IFontRenderer {
public:
	NullFontRenderer() = default;
	~NullFontRenderer() override = default;

	NullFontRenderer(const NullFontRenderer&) = delete;
	NullFontRenderer& operator=(const NullFontRenderer&) = delete;
	NullFontRenderer(NullFontRenderer&&) noexcept = default;
	NullFontRenderer& operator=(NullFontRenderer&&) noexcept = default;

public:
	void AddPrimaryQuad(const PreparedGlyphQuad& quad) override
	{
		if (quad.Empty()) {
			stats.droppedQuads += 1;
			return;
		}

		stats.queuedPrimaryQuads += 1;
	}

	void AddOutlineQuad(const PreparedGlyphQuad& quad) override
	{
		if (quad.Empty()) {
			stats.droppedQuads += 1;
			return;
		}

		stats.queuedOutlineQuads += 1;
	}

	void AddPrimaryGlyph(const font::text::LaidOutGlyph& glyph) override
	{
		if (!glyph.visible || glyph.atlasUV.Empty()) {
			stats.droppedQuads += 1;
			return;
		}

		stats.queuedPrimaryQuads += 1;
	}

	void AddOutlineGlyph(const font::text::LaidOutGlyph& glyph) override
	{
		if (!glyph.visible || !glyph.usesOutline || glyph.outlineAtlasUV.Empty()) {
			stats.droppedQuads += 1;
			return;
		}

		stats.queuedOutlineQuads += 1;
	}

	void DrawQueued() override {}

	void HandleTextureUpdate(const GlyphAtlasTexture& primaryAtlas, const GlyphAtlasTexture* outlineAtlas = nullptr, bool onlyUpload = false) override
	{
		stats.primaryTextureUploads += 1;

		if (outlineAtlas != nullptr)
			stats.outlineTextureUploads += 1;

		(void)primaryAtlas;
		(void)onlyUpload;
	}

	void PushState(const FontRenderState& state) override
	{
		stateStackDepth += 1;
		stats.statePushes += 1;
		lastPushedState = state;
	}

	void PopState(const FontRenderState& state) override
	{
		if (stateStackDepth > 0) {
			stateStackDepth -= 1;
			stats.statePops += 1;
		}

		(void)state;
	}

	[[nodiscard]] bool IsValid() const override { return true; }
	[[nodiscard]] FontRendererStats GetStats() const override { return stats; }
	void ClearStats() override { stats = {}; }

	[[nodiscard]] std::size_t GetStateStackDepth() const noexcept { return stateStackDepth; }
	[[nodiscard]] const FontRenderState& GetLastPushedState() const noexcept { return lastPushedState; }

private:
	FontRendererStats stats {};
	FontRenderState lastPushedState {};
	std::size_t stateStackDepth = 0;
};

using NullFontRendererPtr = std::unique_ptr<NullFontRenderer>;

} // namespace font::render
