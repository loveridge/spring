/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <memory>

#include "IFontRenderer.h"

namespace spring::font::render {

class NullFontRenderer final : public IFontRenderer {
public:
	NullFontRenderer();
	~NullFontRenderer() override;

	NullFontRenderer(const NullFontRenderer&) = delete;
	NullFontRenderer& operator=(const NullFontRenderer&) = delete;
	NullFontRenderer(NullFontRenderer&&) noexcept;
	NullFontRenderer& operator=(NullFontRenderer&&) noexcept;

public:
	void AddPrimaryQuad(const PreparedGlyphQuad& quad) override;
	void AddOutlineQuad(const PreparedGlyphQuad& quad) override;
	void AddPrimaryGlyph(const spring::font::text::LaidOutGlyph& glyph) override;
	void AddOutlineGlyph(const spring::font::text::LaidOutGlyph& glyph) override;
	void DrawQueued() override;

	void HandleTextureUpdate(const GlyphAtlasTexture& primaryAtlas,
	                         const GlyphAtlasTexture* outlineAtlas = nullptr,
	                         bool onlyUpload = false) override;
	void PushState(const FontRenderState& state) override;
	void PopState(const FontRenderState& state) override;

	[[nodiscard]] bool IsValid() const override;
	[[nodiscard]] FontRendererStats GetStats() const override;
	void ClearStats() override;

	[[nodiscard]] std::size_t GetStateStackDepth() const noexcept;
	[[nodiscard]] const FontRenderState& GetLastPushedState() const noexcept;

private:
	FontRendererStats stats {};
	FontRenderState lastPushedState {};
	std::size_t stateStackDepth = 0;
};

using NullFontRendererPtr = std::unique_ptr<NullFontRenderer>;

} // namespace font::render
