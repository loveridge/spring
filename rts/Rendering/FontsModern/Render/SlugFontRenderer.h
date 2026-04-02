/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "IFontRenderer.h"

namespace Shader {
	struct IProgramObject;
}
class VAO;
class VBO;
class CMatrix44f;

namespace fonts::render {

class SlugFontRenderer final : public IFontRenderer {
public:
	struct CreateOptions {
		bool bufferedRendering = true;
		bool enableStatistics = true;
		bool autoUploadTextures = true;
		bool autoPushPopState = false;
	};

	struct TextureBinding {
		unsigned int textureId = 0;
		int textureUnit = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	struct UniformState {
		const CMatrix44f* matrix = nullptr;
		float depth = 0.0f;
		float gamma = 1.0f;
		bool usePixelAlignedCoordinates = false;
	};

public:
	SlugFontRenderer();
	explicit SlugFontRenderer(const CreateOptions& options);
	~SlugFontRenderer() override;

	SlugFontRenderer(const SlugFontRenderer&) = delete;
	SlugFontRenderer& operator=(const SlugFontRenderer&) = delete;
	SlugFontRenderer(SlugFontRenderer&&) noexcept;
	SlugFontRenderer& operator=(SlugFontRenderer&&) noexcept;

	void AddPrimaryQuad(const PreparedGlyphQuad& quad) override;
	void AddOutlineQuad(const PreparedGlyphQuad& quad) override;
	void AddPrimaryGlyph(const fonts::text::LaidOutGlyph& glyph) override;
	void AddOutlineGlyph(const fonts::text::LaidOutGlyph& glyph) override;
	void DrawQueued() override;

	void HandleGlyphCacheUpdate(fonts::GlyphAtlasCache& glyphCache, bool onlyUpload = false) override;
	void PushState(const FontRenderState& state) override;
	void PopState() override;

	[[nodiscard]] FontRendererStats GetStats() const override;
	void ClearStats() override;

	[[nodiscard]] bool IsValid() const override;

	void EnsureInitialized();

private:
	struct Vertex {
		float posX = 0.0f;
		float posY = 0.0f;
		float posZ = 0.0f;
		float localX = 0.0f;
		float localY = 0.0f;
		float bandScaleX = 0.0f;
		float bandScaleY = 0.0f;
		float bandOffsetX = 0.0f;
		float bandOffsetY = 0.0f;
		float colorR = 1.0f;
		float colorG = 1.0f;
		float colorB = 1.0f;
		float colorA = 1.0f;
		std::uint32_t glyphX = 0;
		std::uint32_t glyphY = 0;
		std::uint32_t bandMaxX = 0;
		std::uint32_t bandMaxY = 0;
	};

	struct RenderBatch {
		std::vector<Vertex> vertices;
		std::size_t quadCount = 0;
		bool dirty = false;
	};

	struct BufferResources {
		std::unique_ptr<VAO> vao;
		std::unique_ptr<VBO> vbo;
		std::size_t uploadedVertexCapacity = 0;
	};

	struct ProgramResources {
		std::unique_ptr<Shader::IProgramObject> program;
	};

	void InitializeProgram();
	void InitializeBuffers();
	void ConfigureVertexAttributes(BufferResources& bufferResources);
	void EnsureBufferCapacity(RenderBatch& batch, BufferResources& bufferResources);
	void QueueQuad(RenderBatch& batch, const PreparedGlyphQuad& quad);
	void UploadBatch(RenderBatch& batch, BufferResources& bufferResources);
	void SubmitBatch(const RenderBatch& batch, const BufferResources& bufferResources);
	void ApplyUniforms();
	void BindTextures() const;
	void ClearQueuedBatches();

private:
	CreateOptions createOptions;
	UniformState uniformState;
	TextureBinding curveTextureBinding;
	TextureBinding bandTextureBinding;
	FontRendererStats stats;
	std::vector<FontRenderState> stateStack;
	ProgramResources programResources;
	RenderBatch primaryBatch;
	RenderBatch outlineBatch;
	BufferResources primaryBuffers;
	BufferResources outlineBuffers;
	bool initialized = false;
};

} // namespace fonts::render
