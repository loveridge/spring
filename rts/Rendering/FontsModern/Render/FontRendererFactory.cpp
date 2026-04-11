/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "FontRendererFactory.h"

#include "NullFontRenderer.h"
#include "ShaderFontRenderer.h"
#include "SlugFontRenderer.h"

#include "Rendering/Fonts/FontLogSection.h"

namespace fonts::render {
namespace {

[[nodiscard]] const char* BackendToString(FontRendererBackend backend) noexcept
{
	switch (backend) {
		case FontRendererBackend::Auto:   return "auto";
		case FontRendererBackend::OpenGL: return "opengl";
		case FontRendererBackend::Slug:   return "slug";
		case FontRendererBackend::Null:   return "null";
	}

	return "unknown";
}

[[nodiscard]] bool SupportsOpenGLBackend() noexcept
{
#if defined(HEADLESS)
	return false;
#else
	return true;
#endif
}

[[nodiscard]] FontRendererPtr CreateBackendRenderer(FontRendererBackend backend, const FontRendererCreateOptions& options)
{
	switch (backend) {
		case FontRendererBackend::Auto:
			return CreateBackendRenderer(FontRendererFactory::GetDefaultBackend(), options);

		case FontRendererBackend::OpenGL:
			if (!SupportsOpenGLBackend())
				return nullptr;

			{
				ShaderFontRenderer::CreateOptions createOptions;
				createOptions.bufferedRendering = options.preferBufferedRendering;
				createOptions.enableStatistics = options.enableStatistics;

				auto renderer = std::make_unique<ShaderFontRenderer>(createOptions);
				if (renderer->IsValid())
					return renderer;
			}

			return nullptr;

		case FontRendererBackend::Slug:
			if (!SupportsOpenGLBackend())
				return nullptr;

			{
				SlugFontRenderer::CreateOptions createOptions;
				createOptions.bufferedRendering = options.preferBufferedRendering;
				createOptions.enableStatistics = options.enableStatistics;

				auto renderer = std::make_unique<SlugFontRenderer>(createOptions);
				if (renderer->IsValid())
					return renderer;
			}

			return nullptr;

		case FontRendererBackend::Null:
			(void)options;
			return std::make_unique<NullFontRenderer>();
	}

	return nullptr;
}

} // namespace

FontRendererPtr FontRendererFactory::Create()
{
	return Create(FontRendererCreateOptions {});
}

FontRendererPtr FontRendererFactory::Create(const FontRendererCreateOptions& options)
{
	const FontRendererBackend requestedBackend = options.backend;
	const FontRendererBackend resolvedBackend = (requestedBackend == FontRendererBackend::Auto)
		? GetDefaultBackend()
		: requestedBackend;

	if (auto renderer = CreateBackendRenderer(resolvedBackend, options); renderer != nullptr)
		return renderer;

	if (!options.allowFallbackToNull) {
		LOG_L(L_WARNING,
			"[FontRendererFactory::%s] Backend \"%s\" is unavailable and null fallback is disabled",
			__func__,
			BackendToString(resolvedBackend)
		);
		return nullptr;
	}

	if (resolvedBackend != FontRendererBackend::Null) {
		LOG_L(L_WARNING,
			"[FontRendererFactory::%s] Backend \"%s\" is unavailable, falling back to \"%s\"",
			__func__,
			BackendToString(resolvedBackend),
			BackendToString(FontRendererBackend::Null)
		);
	}

	return CreateBackendRenderer(FontRendererBackend::Null, options);
}

bool FontRendererFactory::IsBackendSupported(FontRendererBackend backend)
{
	switch (backend) {
		case FontRendererBackend::Auto:
			return true;

		case FontRendererBackend::OpenGL:
			return SupportsOpenGLBackend();

		case FontRendererBackend::Slug:
			return SupportsOpenGLBackend();

		case FontRendererBackend::Null:
			return true;
	}

	return false;
}

FontRendererBackend FontRendererFactory::GetDefaultBackend()
{
	if (IsBackendSupported(FontRendererBackend::OpenGL))
		return FontRendererBackend::OpenGL;

	return FontRendererBackend::Null;
}

FontRendererCacheKind FontRendererFactory::GetRequiredCacheKind(FontRendererBackend backend)
{
	switch (backend) {
		case FontRendererBackend::Auto:
			return GetRequiredCacheKind(GetDefaultBackend());

		case FontRendererBackend::OpenGL:
			return FontRendererCacheKind::ShaderBitmap;

		case FontRendererBackend::Slug:
			return FontRendererCacheKind::SlugVector;

		case FontRendererBackend::Null:
			return FontRendererCacheKind::None;
	}

	return FontRendererCacheKind::None;
}

} // namespace font::render
