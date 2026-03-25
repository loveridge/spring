#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Render/IFontRenderer.h"

class Shader;
class ShaderProgram;
class VAO;
class VBO;
class CMatrix44f;

namespace font {
class GlyphAtlasTexture;
}

namespace font::render {

class ShaderFontRenderer final : public IFontRenderer {
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
		float outlineWeight = 0.0f;
		float outlineRadius = 0.0f;
		bool usePixelAlignedCoordinates = false;
		bool enableSDF = false;
	};

	struct QueuedQuadBuffers {
		std::size_t primaryQuadCount = 0;
		std::size_t outlineQuadCount = 0;
		std::size_t primaryVertexCount = 0;
		std::size_t outlineVertexCount = 0;
	};

public:
	explicit ShaderFontRenderer(const CreateOptions& options = {});
	~ShaderFontRenderer() override;

	ShaderFontRenderer(const ShaderFontRenderer&) = delete;
	ShaderFontRenderer& operator=(const ShaderFontRenderer&) = delete;
	ShaderFontRenderer(ShaderFontRenderer&&) noexcept;
	ShaderFontRenderer& operator=(ShaderFontRenderer&&) noexcept;

	void AddPrimaryQuad(const PreparedGlyphQuad& quad) override;
	void AddOutlineQuad(const PreparedGlyphQuad& quad) override;
	void DrawQueued() override;

	void HandleTextureUpdate(const font::GlyphAtlasTexture& atlas) override;
	void PushState(const FontRenderState& state) override;
	void PopState() override;

	[[nodiscard]] FontRendererStats GetStats() const override;
	void ResetStats() override;
	void ClearQueued() override;

	[[nodiscard]] bool IsReady() const;
	[[nodiscard]] bool HasQueuedGeometry() const;
	[[nodiscard]] QueuedQuadBuffers GetQueuedBufferStats() const;

	void EnsureInitialized();
	void ReleaseDeviceResources();

	void SetUniformState(const UniformState& state);
	[[nodiscard]] const UniformState& GetUniformState() const;

	void SetPrimaryTextureBinding(const TextureBinding& binding);
	void SetOutlineTextureBinding(const TextureBinding& binding);
	[[nodiscard]] const TextureBinding& GetPrimaryTextureBinding() const;
	[[nodiscard]] const TextureBinding& GetOutlineTextureBinding() const;

	[[nodiscard]] const CreateOptions& GetCreateOptions() const;

private:
	struct Vertex {
		float posX = 0.0f;
		float posY = 0.0f;
		float posZ = 0.0f;
		float uvX = 0.0f;
		float uvY = 0.0f;
		float colorR = 1.0f;
		float colorG = 1.0f;
		float colorB = 1.0f;
		float colorA = 1.0f;
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
		std::unique_ptr<Shader> vertexShader;
		std::unique_ptr<Shader> fragmentShader;
		std::unique_ptr<ShaderProgram> program;
	};

	void InitializeProgram();
	void InitializeBuffers();
	void EnsureBufferCapacity(RenderBatch& batch, BufferResources& bufferResources);
	void QueueQuad(RenderBatch& batch, const PreparedGlyphQuad& quad);
	void UploadBatch(RenderBatch& batch, BufferResources& bufferResources);
	void SubmitBatch(const RenderBatch& batch, const BufferResources& bufferResources, const TextureBinding& binding, bool outlinePass);
	void ApplyUniforms(bool outlinePass);
	void BindTexture(const TextureBinding& binding);
	void UpdateTextureBindingFromAtlas(const font::GlyphAtlasTexture& atlas);

private:
	CreateOptions createOptions;
	UniformState uniformState;
	TextureBinding primaryTextureBinding;
	TextureBinding outlineTextureBinding;
	FontRendererStats stats;
	std::vector<FontRenderState> stateStack;
	ProgramResources programResources;
	RenderBatch primaryBatch;
	RenderBatch outlineBatch;
	BufferResources primaryBuffers;
	BufferResources outlineBuffers;
	bool initialized = false;
};

} // namespace font::render

