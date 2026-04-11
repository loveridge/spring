/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <memory>

#include "IFontRenderer.h"

namespace fonts::render {

/**
 * @brief Supported renderer backend kinds.
 *
 * Concrete backend availability is intentionally resolved in the factory
 * implementation rather than encoded into the public renderer interface.
 */
enum class FontRendererBackend : std::uint8_t {
	Auto,
	OpenGL,
	Slug,
	Null,
};

enum class FontRendererCacheKind : std::uint8_t {
	None,
	ShaderBitmap,
	SlugVector,
};

/**
 * @brief Renderer creation options.
 *
 * The factory decides whether the requested backend is available and falls
 * back to a null renderer when necessary.
 */
struct FontRendererCreateOptions {
	FontRendererBackend backend = FontRendererBackend::Auto;
	bool allowFallbackToNull = true;
	bool preferBufferedRendering = true;
	bool enableStatistics = true;
};

/**
 * @brief Creates the concrete font renderer backend.
 *
 * This is the only public entry point for backend selection. It should stay
 * free of implementation details so callers never need to include GL or null
 * backend headers directly.
 */
class FontRendererFactory {
public:
	[[nodiscard]] static FontRendererPtr Create();
	[[nodiscard]] static FontRendererPtr Create(const FontRendererCreateOptions& options);

	[[nodiscard]] static bool IsBackendSupported(FontRendererBackend backend);
	[[nodiscard]] static FontRendererBackend GetDefaultBackend();
	[[nodiscard]] static FontRendererCacheKind GetRequiredCacheKind(FontRendererBackend backend);
};

} // namespace font::render
