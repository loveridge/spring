/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "ShaderFontRenderer.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <utility>

#include "Rendering/Fonts/FontLogSection.h"
#include "Rendering/FontsModern/Glyphs/GlyphAtlasTexture.h"
#include "Rendering/GL/VAO.h"
#include "Rendering/GL/VBO.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Shaders/Shader.h"
#include "System/Color.h"
#include "System/Matrix44f.h"

namespace fonts::render {
namespace {

static constexpr const char* vsFont330 = R"(
#version 150 compatibility
#extension GL_ARB_explicit_attrib_location : enable

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

uniform mat4 uMVP;
uniform float uDepthBias;
uniform int uUsePixelAlignedCoordinates;

out Data {
	vec4 vColor;
	vec2 vUV;
};

void main()
{
	vec3 pos = aPos;

	if (uUsePixelAlignedCoordinates != 0)
		pos.xy = floor(pos.xy) + vec2(0.5);

	pos.z += uDepthBias;

	vColor = aColor;
	vUV = aUV;
	gl_Position = uMVP * vec4(pos, 1.0);
}
)";

static constexpr const char* fsFont330 = R"(
#version 150

uniform sampler2D uTex;
uniform float uGamma;
uniform float uOutlineWeight;
uniform float uOutlineRadius;
uniform int uUseColorAtlas;
uniform int uOutlinePass;
uniform int uEnableSDF;

in Data {
	vec4 vColor;
	vec2 vUV;
};

out vec4 outColor;

float ComputeCoverage(vec4 texel)
{
	float coverage = texel.a;

	if (uEnableSDF != 0) {
		float edge = max(fwidth(coverage), 0.0001);
		float radius = max(edge, uOutlineRadius);

		if (uOutlinePass != 0) {
			float threshold = clamp(0.5 - uOutlineWeight, 0.0, 1.0);
			coverage = 1.0 - smoothstep(threshold, threshold + radius, coverage);
		} else {
			coverage = smoothstep(0.5 - radius, 0.5 + radius, coverage);
		}
	}

	return pow(clamp(coverage, 0.0, 1.0), 1.0 / max(uGamma, 0.0001));
}

void main()
{
	vec4 texel = texture(uTex, vUV);

	if (uUseColorAtlas != 0) {
		outColor = texel * vColor;
		return;
	}

	outColor = vec4(vColor.rgb, vColor.a * ComputeCoverage(texel));
}
)";

static constexpr const char* vsFont130 = R"(
#version 130

in vec3 aPos;
in vec2 aUV;
in vec4 aColor;

uniform mat4 uMVP;
uniform float uDepthBias;
uniform int uUsePixelAlignedCoordinates;

out vec4 vColor;
out vec2 vUV;

void main()
{
	vec3 pos = aPos;

	if (uUsePixelAlignedCoordinates != 0)
		pos.xy = floor(pos.xy) + vec2(0.5);

	pos.z += uDepthBias;

	vColor = aColor;
	vUV = aUV;
	gl_Position = uMVP * vec4(pos, 1.0);
}
)";

static constexpr const char* fsFont130 = R"(
#version 130

uniform sampler2D uTex;
uniform float uGamma;
uniform float uOutlineWeight;
uniform float uOutlineRadius;
uniform int uUseColorAtlas;
uniform int uOutlinePass;
uniform int uEnableSDF;

in vec4 vColor;
in vec2 vUV;

float ComputeCoverage(vec4 texel)
{
	float coverage = texel.a;

	if (uEnableSDF != 0) {
		float radius = max(uOutlineRadius, 0.0001);

		if (uOutlinePass != 0) {
			float threshold = clamp(0.5 - uOutlineWeight, 0.0, 1.0);
			coverage = 1.0 - smoothstep(threshold, threshold + radius, coverage);
		} else {
			coverage = smoothstep(0.5 - radius, 0.5 + radius, coverage);
		}
	}

	return pow(clamp(coverage, 0.0, 1.0), 1.0 / max(uGamma, 0.0001));
}

void main()
{
	vec4 texel = texture(uTex, vUV);

	if (uUseColorAtlas != 0) {
		gl_FragColor = texel * vColor;
		return;
	}

	gl_FragColor = vec4(vColor.rgb, vColor.a * ComputeCoverage(texel));
}
)";

[[nodiscard]] FontColor ToFontColor(const SColor& color) noexcept
{
	constexpr float inv = 1.0f / 255.0f;

	return FontColor {
		color.r * inv,
		color.g * inv,
		color.b * inv,
		color.a * inv,
	};
}

[[nodiscard]] const FontRenderState* GetActiveState(const std::vector<FontRenderState>& stateStack) noexcept
{
	if (stateStack.empty())
		return nullptr;

	return &stateStack.back();
}

[[nodiscard]] FontColor ResolveGlyphColor(const FontRenderState* state, bool outlinePass) noexcept
{
	if (state == nullptr)
		return {};

	if (outlinePass) {
		if (state->rawOutlineColor != nullptr)
			return ToFontColor(*state->rawOutlineColor);

		return state->outlineColor;
	}

	if (state->rawPrimaryColor != nullptr)
		return ToFontColor(*state->rawPrimaryColor);

	return state->primaryColor;
}

[[nodiscard]] float ResolveGlyphDepth(const fonts::text::LaidOutGlyph& glyph, const FontRenderState* state, bool outlinePass) noexcept
{
	if (glyph.z != 0.0f)
		return glyph.z;

	if (state == nullptr)
		return 0.0f;

	return outlinePass ? state->depth.outline : state->depth.text;
}

[[nodiscard]] PreparedGlyphQuad MakePreparedGlyphQuad(const fonts::text::LaidOutGlyph& glyph, const FontRenderState* state, bool outlinePass)
{
	PreparedGlyphQuad quad;
	quad.position = glyph.shaped.metrics.bounds;
	quad.position.x += glyph.x;
	quad.position.y += glyph.y;
	quad.atlasUV = outlinePass ? glyph.outlineAtlasUV : glyph.atlasUV;
	quad.color = ResolveGlyphColor(state, outlinePass);
	quad.z = ResolveGlyphDepth(glyph, state, outlinePass);
	quad.visible = glyph.visible && !quad.atlasUV.Empty();
	return quad;
}

[[nodiscard]] GLenum GetBufferUsage(const ShaderFontRenderer::CreateOptions& options) noexcept
{
	return options.bufferedRendering ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW;
}

} // namespace

ShaderFontRenderer::ShaderFontRenderer()
	: ShaderFontRenderer(CreateOptions {})
{}

ShaderFontRenderer::ShaderFontRenderer(const CreateOptions& options)
	: createOptions(options)
{
#ifndef HEADLESS
	EnsureInitialized();
#endif
}

ShaderFontRenderer::~ShaderFontRenderer()
{
	ReleaseDeviceResources();
}

ShaderFontRenderer::ShaderFontRenderer(ShaderFontRenderer&& other) noexcept
{
	*this = std::move(other);
}

ShaderFontRenderer& ShaderFontRenderer::operator=(ShaderFontRenderer&& other) noexcept
{
	if (this == &other)
		return *this;

	ReleaseDeviceResources();

	createOptions = other.createOptions;
	uniformState = other.uniformState;
	primaryTextureBinding = other.primaryTextureBinding;
	outlineTextureBinding = other.outlineTextureBinding;
	stats = other.stats;
	stateStack = std::move(other.stateStack);
	programResources = std::move(other.programResources);
	primaryBatch = std::move(other.primaryBatch);
	outlineBatch = std::move(other.outlineBatch);
	primaryBuffers = std::move(other.primaryBuffers);
	outlineBuffers = std::move(other.outlineBuffers);
	initialized = other.initialized;

	other.primaryTextureBinding = {};
	other.outlineTextureBinding = {};
	other.stats = {};
	other.initialized = false;

	return *this;
}

void ShaderFontRenderer::AddPrimaryQuad(const PreparedGlyphQuad& quad)
{
	QueueQuad(primaryBatch, quad);

	if (createOptions.enableStatistics && !quad.Empty())
		stats.queuedPrimaryQuads += 1;
}

void ShaderFontRenderer::AddOutlineQuad(const PreparedGlyphQuad& quad)
{
	QueueQuad(outlineBatch, quad);

	if (createOptions.enableStatistics && !quad.Empty())
		stats.queuedOutlineQuads += 1;
}

void ShaderFontRenderer::AddPrimaryGlyph(const fonts::text::LaidOutGlyph& glyph)
{
	AddPrimaryQuad(MakePreparedGlyphQuad(glyph, GetActiveState(stateStack), false));
}

void ShaderFontRenderer::AddOutlineGlyph(const fonts::text::LaidOutGlyph& glyph)
{
	if (!glyph.usesOutline) {
		if (createOptions.enableStatistics)
			stats.droppedQuads += 1;
		return;
	}

	AddOutlineQuad(MakePreparedGlyphQuad(glyph, GetActiveState(stateStack), true));
}

void ShaderFontRenderer::DrawQueued()
{
	if (!HasQueuedGeometry())
		return;

	EnsureInitialized();

	if (!IsValid()) {
		if (createOptions.enableStatistics)
			stats.droppedQuads += (primaryBatch.quadCount + outlineBatch.quadCount);

		primaryBatch.vertices.clear();
		primaryBatch.quadCount = 0;
		primaryBatch.dirty = false;

		outlineBatch.vertices.clear();
		outlineBatch.quadCount = 0;
		outlineBatch.dirty = false;
		return;
	}

	bool autoPushedState = false;
	if (createOptions.autoPushPopState && stateStack.empty()) {
		PushState(FontRenderState {});
		autoPushedState = true;
	}

	UploadBatch(outlineBatch, outlineBuffers);
	UploadBatch(primaryBatch, primaryBuffers);

#ifndef HEADLESS
	GLint prevProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);

	const FontRenderState* state = GetActiveState(stateStack);
	if (state == nullptr || !state->userDefinedBlending)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	programResources.program->Enable();

	SubmitBatch(outlineBatch, outlineBuffers, outlineTextureBinding, true);
	SubmitBatch(primaryBatch, primaryBuffers, primaryTextureBinding, false);

	programResources.program->Disable();

	if (prevProgram > 0)
		glUseProgram(prevProgram);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glPopAttrib();
#endif

	primaryBatch.vertices.clear();
	primaryBatch.quadCount = 0;
	primaryBatch.dirty = false;

	outlineBatch.vertices.clear();
	outlineBatch.quadCount = 0;
	outlineBatch.dirty = false;

	if (autoPushedState)
		PopState(FontRenderState {});
}

void ShaderFontRenderer::HandleTextureUpdate(const GlyphAtlasTexture& primaryAtlas, const GlyphAtlasTexture* outlineAtlas, bool onlyUpload)
{
	if (createOptions.autoUploadTextures) {
		GlyphAtlasTexture& mutablePrimaryAtlas = const_cast<GlyphAtlasTexture&>(primaryAtlas);
		if (onlyUpload || mutablePrimaryAtlas.NeedsUpload() || !mutablePrimaryAtlas.HasTexture())
			mutablePrimaryAtlas.Upload();

		if (outlineAtlas != nullptr) {
			GlyphAtlasTexture& mutableOutlineAtlas = const_cast<GlyphAtlasTexture&>(*outlineAtlas);
			if (onlyUpload || mutableOutlineAtlas.NeedsUpload() || !mutableOutlineAtlas.HasTexture())
				mutableOutlineAtlas.Upload();
		}
	}

	UpdateTextureBindingFromAtlas(primaryAtlas);

	if (outlineAtlas != nullptr) {
		outlineTextureBinding.textureId = outlineAtlas->GetTextureId();
		outlineTextureBinding.textureUnit = 1;
		outlineTextureBinding.width = std::max(outlineAtlas->GetWidth(), 0);
		outlineTextureBinding.height = std::max(outlineAtlas->GetHeight(), 0);
	} else {
		outlineTextureBinding = {};
	}

	if (createOptions.enableStatistics) {
		stats.primaryTextureUploads += 1;
		if (outlineAtlas != nullptr)
			stats.outlineTextureUploads += 1;
	}
}

void ShaderFontRenderer::PushState(const FontRenderState& state)
{
	stateStack.push_back(state);

	if (createOptions.enableStatistics)
		stats.statePushes += 1;
}

void ShaderFontRenderer::PopState(const FontRenderState& state)
{
	(void)state;

	if (!stateStack.empty())
		stateStack.pop_back();

	if (createOptions.enableStatistics)
		stats.statePops += 1;
}

FontRendererStats ShaderFontRenderer::GetStats() const
{
	return stats;
}

void ShaderFontRenderer::ClearStats()
{
	stats = {};
}

bool ShaderFontRenderer::IsValid() const
{
#ifdef HEADLESS
	return false;
#else
	return initialized && (programResources.program != nullptr) && programResources.program->IsValid();
#endif
}

bool ShaderFontRenderer::IsReady() const
{
	return IsValid() && (primaryBuffers.vbo != nullptr) && (outlineBuffers.vbo != nullptr);
}

bool ShaderFontRenderer::HasQueuedGeometry() const
{
	return (primaryBatch.quadCount > 0) || (outlineBatch.quadCount > 0);
}

ShaderFontRenderer::QueuedQuadBuffers ShaderFontRenderer::GetQueuedBufferStats() const
{
	return QueuedQuadBuffers {
		.primaryQuadCount = primaryBatch.quadCount,
		.outlineQuadCount = outlineBatch.quadCount,
		.primaryVertexCount = primaryBatch.vertices.size(),
		.outlineVertexCount = outlineBatch.vertices.size(),
	};
}

void ShaderFontRenderer::EnsureInitialized()
{
	if (initialized)
		return;

#ifdef HEADLESS
	return;
#else
	if (globalRendering == nullptr)
		return;

	InitializeProgram();
	InitializeBuffers();

	initialized = (programResources.program != nullptr) && programResources.program->IsValid();
#endif
}

void ShaderFontRenderer::ReleaseDeviceResources()
{
	programResources.program = nullptr;

	primaryBuffers.vao = nullptr;
	primaryBuffers.vbo = nullptr;
	primaryBuffers.uploadedVertexCapacity = 0;

	outlineBuffers.vao = nullptr;
	outlineBuffers.vbo = nullptr;
	outlineBuffers.uploadedVertexCapacity = 0;

	initialized = false;
}

void ShaderFontRenderer::SetUniformState(const UniformState& state)
{
	uniformState = state;
}

const ShaderFontRenderer::UniformState& ShaderFontRenderer::GetUniformState() const
{
	return uniformState;
}

void ShaderFontRenderer::SetPrimaryTextureBinding(const TextureBinding& binding)
{
	primaryTextureBinding = binding;
}

void ShaderFontRenderer::SetOutlineTextureBinding(const TextureBinding& binding)
{
	outlineTextureBinding = binding;
}

const ShaderFontRenderer::TextureBinding& ShaderFontRenderer::GetPrimaryTextureBinding() const
{
	return primaryTextureBinding;
}

const ShaderFontRenderer::TextureBinding& ShaderFontRenderer::GetOutlineTextureBinding() const
{
	return outlineTextureBinding;
}

const ShaderFontRenderer::CreateOptions& ShaderFontRenderer::GetCreateOptions() const
{
	return createOptions;
}

void ShaderFontRenderer::InitializeProgram()
{
#ifdef HEADLESS
	return;
#else
	if (programResources.program != nullptr)
		return;

	auto program = std::make_unique<Shader::GLSLProgramObject>("[FontModern::ShaderFontRenderer]");

	if (!globalRendering->supportExplicitAttribLoc) {
		program->BindAttribLocation("aPos", 0);
		program->BindAttribLocation("aUV", 1);
		program->BindAttribLocation("aColor", 2);
	}

	program->AttachShaderObject(new Shader::GLSLShaderObject(GL_VERTEX_SHADER, globalRendering->supportExplicitAttribLoc ? vsFont330 : vsFont130));
	program->AttachShaderObject(new Shader::GLSLShaderObject(GL_FRAGMENT_SHADER, globalRendering->supportExplicitAttribLoc ? fsFont330 : fsFont130));
	program->Link();

	if (!program->IsValid()) {
		LOG_L(L_WARNING, "[ShaderFontRenderer::%s] Failed to link font shader program", __func__);
		return;
	}

	program->Enable();
	static_cast<Shader::IProgramObject*>(program.get())->SetUniform("uTex", 0);
	program->Disable();

	if (!program->Validate()) {
		LOG_L(L_WARNING, "[ShaderFontRenderer::%s] Font shader validation failed", __func__);
		return;
	}

	programResources.program = std::move(program);
#endif
}

void ShaderFontRenderer::InitializeBuffers()
{
#ifdef HEADLESS
	return;
#else
	auto initBufferResources = [this](BufferResources& bufferResources) {
		if (bufferResources.vbo == nullptr)
			bufferResources.vbo = std::make_unique<VBO>(GL_ARRAY_BUFFER, false);

		if (VAO::IsSupported() && bufferResources.vao == nullptr)
			bufferResources.vao = std::make_unique<VAO>();

		bufferResources.vbo->Bind();
		bufferResources.vbo->New(sizeof(Vertex), GetBufferUsage(createOptions), nullptr);
		bufferResources.vbo->Unbind();
		bufferResources.uploadedVertexCapacity = 1;

		ConfigureVertexAttributes(bufferResources);
	};

	initBufferResources(primaryBuffers);
	initBufferResources(outlineBuffers);
#endif
}

void ShaderFontRenderer::ConfigureVertexAttributes(BufferResources& bufferResources)
{
	if (bufferResources.vbo == nullptr)
		return;

	if (bufferResources.vao != nullptr)
		bufferResources.vao->Bind();

	bufferResources.vbo->Bind();

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, posX)));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, uvX)));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, colorR)));

	bufferResources.vbo->Unbind();

	if (bufferResources.vao != nullptr)
		bufferResources.vao->Unbind();
}

void ShaderFontRenderer::EnsureBufferCapacity(RenderBatch& batch, BufferResources& bufferResources)
{
	if (bufferResources.vbo == nullptr)
		return;

	const std::size_t requiredVertices = std::max<std::size_t>(batch.vertices.size(), 1u);
	if (requiredVertices <= bufferResources.uploadedVertexCapacity)
		return;

	const std::size_t newCapacity = std::max<std::size_t>(std::bit_ceil(requiredVertices), 4u);

	bufferResources.vbo->Bind();
	bufferResources.vbo->New(newCapacity * sizeof(Vertex), GetBufferUsage(createOptions), nullptr);
	bufferResources.vbo->Unbind();
	bufferResources.uploadedVertexCapacity = newCapacity;

	ConfigureVertexAttributes(bufferResources);
}

void ShaderFontRenderer::QueueQuad(RenderBatch& batch, const PreparedGlyphQuad& srcQuad)
{
	const PreparedGlyphQuad& quad = srcQuad;

	float x0 = quad.position.x;
	float y0 = quad.position.y;
	float x1 = quad.position.x + quad.position.w;
	float y1 = quad.position.y + quad.position.h;

	float u0 = quad.atlasUV.x;
	float v0 = quad.atlasUV.y;
	float u1 = quad.atlasUV.x + quad.atlasUV.w;
	float v1 = quad.atlasUV.y + quad.atlasUV.h;

	if (x1 < x0) {
		std::swap(x0, x1);
		std::swap(u0, u1);
	}

	if (y1 < y0) {
		std::swap(y0, y1);
		std::swap(v0, v1);
	}

	if (!quad.visible || x0 == x1 || y0 == y1 || u0 == u1 || v0 == v1) {
		if (createOptions.enableStatistics)
			stats.droppedQuads += 1;
		return;
	}

	const Vertex tl {x0, y0, quad.z, u0, v0, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
	const Vertex tr {x1, y0, quad.z, u1, v0, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
	const Vertex br {x1, y1, quad.z, u1, v1, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
	const Vertex bl {x0, y1, quad.z, u0, v1, quad.color.r, quad.color.g, quad.color.b, quad.color.a};

	batch.vertices.emplace_back(tl);
	batch.vertices.emplace_back(bl);
	batch.vertices.emplace_back(tr);
	batch.vertices.emplace_back(tr);
	batch.vertices.emplace_back(bl);
	batch.vertices.emplace_back(br);

	batch.quadCount += 1;
	batch.dirty = true;
}

void ShaderFontRenderer::UploadBatch(RenderBatch& batch, BufferResources& bufferResources)
{
	if (bufferResources.vbo == nullptr || batch.vertices.empty()) {
		batch.dirty = false;
		return;
	}

	if (!batch.dirty)
		return;

	EnsureBufferCapacity(batch, bufferResources);

	bufferResources.vbo->Bind();
	bufferResources.vbo->SetBufferSubData(0, batch.vertices.size() * sizeof(Vertex), batch.vertices.data());
	bufferResources.vbo->Unbind();
	batch.dirty = false;
}

void ShaderFontRenderer::SubmitBatch(const RenderBatch& batch, const BufferResources& bufferResources, const TextureBinding& binding, bool outlinePass)
{
	if (batch.vertices.empty() || binding.textureId == 0 || programResources.program == nullptr)
		return;

	BindTexture(binding);
	static_cast<Shader::IProgramObject*>(programResources.program.get())->SetUniform("uTex", std::max(binding.textureUnit, 0));
	ApplyUniforms(outlinePass);

	if (bufferResources.vao != nullptr) {
		bufferResources.vao->Bind();
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
		bufferResources.vao->Unbind();
	} else if (bufferResources.vbo != nullptr) {
		bufferResources.vbo->Bind();

		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, posX)));
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, uvX)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, colorR)));

		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);
		bufferResources.vbo->Unbind();
	}

	if (createOptions.enableStatistics)
		stats.drawCalls += 1;
}

void ShaderFontRenderer::ApplyUniforms(bool outlinePass)
{
	if (programResources.program == nullptr)
		return;

	const FontRenderState* state = GetActiveState(stateStack);

	CMatrix44f mvp = CMatrix44f::Identity();
	const CMatrix44f* matrix = uniformState.matrix;

	if (matrix == nullptr && state != nullptr) {
		if (state->hasProjectionMatrix && state->hasModelViewMatrix) {
			mvp = state->projectionMatrix * state->modelViewMatrix;
			matrix = &mvp;
		} else if (state->hasProjectionMatrix) {
			matrix = &state->projectionMatrix;
		} else if (state->hasModelViewMatrix) {
			matrix = &state->modelViewMatrix;
		}
	}

	if (matrix == nullptr)
		matrix = &mvp;

	const bool usePixelAlignedCoordinates = uniformState.usePixelAlignedCoordinates ||
		(state != nullptr && !state->useWorldSpace && !state->normalizedCoordinates);
	const bool useColorAtlas = (state != nullptr) && state->useColorAtlas;
	const float depthBias = (state != nullptr)
		? (outlinePass ? state->depth.outline : state->depth.text)
		: uniformState.depth;

	programResources.program->SetUniformMatrix4x4("uMVP", false, matrix->m);
	programResources.program->SetUniform("uDepthBias", depthBias);
	programResources.program->SetUniform("uGamma", uniformState.gamma);
	programResources.program->SetUniform("uOutlineWeight", uniformState.outlineWeight);
	programResources.program->SetUniform("uOutlineRadius", uniformState.outlineRadius);
	programResources.program->SetUniform("uUsePixelAlignedCoordinates", static_cast<int>(usePixelAlignedCoordinates));
	programResources.program->SetUniform("uUseColorAtlas", static_cast<int>(useColorAtlas));
	programResources.program->SetUniform("uOutlinePass", static_cast<int>(outlinePass));
	programResources.program->SetUniform("uEnableSDF", static_cast<int>(uniformState.enableSDF));
}

void ShaderFontRenderer::BindTexture(const TextureBinding& binding)
{
	const int textureUnit = std::max(binding.textureUnit, 0);
	glActiveTexture(GL_TEXTURE0 + textureUnit);
	glBindTexture(GL_TEXTURE_2D, binding.textureId);
}

void ShaderFontRenderer::UpdateTextureBindingFromAtlas(const GlyphAtlasTexture& atlas)
{
	primaryTextureBinding.textureId = atlas.GetTextureId();
	primaryTextureBinding.textureUnit = 0;
	primaryTextureBinding.width = std::max(atlas.GetWidth(), 0);
	primaryTextureBinding.height = std::max(atlas.GetHeight(), 0);
}

} // namespace font::render
