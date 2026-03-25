/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FontTypes.h"

namespace spring::font {

class FontFace;
class FontLibrary;

/**
 * Process-wide font subsystem registry.
 *
 * Responsibilities:
 *  - one-time bootstrap/shutdown for the font stack
 *  - global fallback-font registration
 *  - optional shared registry of loaded faces for reuse
 *  - optional system-font lookup hooks
 *
 * This is the intended destination for the current static/global font work in
 * CFontTexture and the legacy FtLibraryHandlerProxy bootstrap path.
 */
class FontRegistry {
public:
	using FacePtr = std::shared_ptr<FontFace>;
	using FallbackList = std::vector<std::string>;

	FontRegistry() = delete;
	~FontRegistry() = delete;

	FontRegistry(const FontRegistry&) = delete;
	FontRegistry& operator=(const FontRegistry&) = delete;
	FontRegistry(FontRegistry&&) = delete;
	FontRegistry& operator=(FontRegistry&&) = delete;

	/**
	 * One-time process-wide initialization.
	 *
	 * Expected to bootstrap the FreeType library and, when enabled, optional
	 * Fontconfig state used for font discovery and fallback resolution.
	 */
	static void Init(bool initializeFontconfig = true, bool consoleOutput = false);

	/**
	 * Releases process-wide registry state.
	 *
	 * Intended to clear fallback registrations, shared face caches, and any
	 * library-level subsystem state owned by the font stack.
	 */
	static void Shutdown();

	static bool IsInitialized() noexcept;

	/**
	 * Access to the global FreeType subsystem wrapper.
	 *
	 * Valid after Init() and may lazily initialize in implementation for
	 * compatibility with legacy call sites.
	 */
	static FontLibrary& GetLibrary();

	/**
	 * Registers an explicit fallback font file path.
	 *
	 * Returns true when the font was accepted into the fallback list.
	 */
	static bool AddFallbackFont(std::string path);

	/**
	 * Removes all explicitly registered fallback fonts.
	 */
	static void ClearFallbackFonts();

	/**
	 * Returns the currently registered explicit fallback font paths.
	 */
	static const FallbackList& GetFallbackFonts();

	/**
	 * Shared loaded-face registry.
	 *
	 * This is optional infrastructure for reusing FreeType faces across callers.
	 * Implementations may use weak ownership internally and promote to shared
	 * ownership when a cached face is still alive.
	 */
	static void RegisterLoadedFace(const FontDescriptor& descriptor, const FacePtr& face);
	static FacePtr FindLoadedFace(const FontDescriptor& descriptor);
	static void UnregisterLoadedFace(const FontDescriptor& descriptor);
	static void ClearLoadedFaces();

	/**
	 * Optional system-font discovery hook.
	 *
	 * The implementation may consult Fontconfig or platform-specific discovery
	 * services to resolve a family/style request into a concrete file path.
	 * Returns an empty string when no match is found.
	 */
	static std::string FindSystemFont(std::string_view family,
	                                 std::string_view style = {},
	                                 bool allowSubstitutions = true);

private:
	using LoadedFaceRegistry = std::unordered_map<std::string, std::weak_ptr<FontFace>>;

	static std::string MakeRegistryKey(const FontDescriptor& descriptor);

	static std::once_flag initOnce;
	static bool initialized;
	static std::mutex registryMutex;
	static FallbackList fallbackFonts;
	static LoadedFaceRegistry loadedFaces;
};

} // namespace font
