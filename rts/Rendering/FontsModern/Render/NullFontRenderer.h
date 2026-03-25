#pragma once

#include <memory>

#include "Render/IFontRenderer.h"

namespace font {

class NullFontRenderer final : public IFontRenderer {
public:
	NullFontRenderer() = default;
	~NullFontRenderer() override = default;

	NullFontRenderer(const NullFontRenderer&) = delete;
	NullFontRenderer& operator=(const NullFontRenderer&) = delete;
	NullFontRenderer(NullFontRenderer&&) noexcept = default;
	NullFontRenderer& operator=(NullFontRenderer&&) noexcept = default;

public:
	void AddPrimaryQuad(const PreparedGlyphQuad& quad) override {}
	void AddOutlineQuad(const PreparedGlyphQuad& quad) override {}

	void DrawQueued() override {}

	void HandleTextureUpdate(std::uint32_t textureId, bool fullUpload, bool hasOutline) override {}

	void PushState(const FontRenderState& state) override {
		stateStackDepth += 1;
		lastPushedState = state;
	}

	void PopState() override {
		if (stateStackDepth > 0)
			stateStackDepth -= 1;
	}

	[[nodiscard]] FontRendererStats GetStats() const override { return stats; }
	void ResetStats() override { stats = {}; }
	void ClearQueued() override {}

	[[nodiscard]] std::size_t GetStateStackDepth() const noexcept { return stateStackDepth; }
	[[nodiscard]] const FontRenderState& GetLastPushedState() const noexcept { return lastPushedState; }

private:
	FontRendererStats stats {};
	FontRenderState lastPushedState {};
	std::size_t stateStackDepth = 0;
};

using NullFontRendererPtr = std::shared_ptr<NullFontRenderer>;

} // namespace font

