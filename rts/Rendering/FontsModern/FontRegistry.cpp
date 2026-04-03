/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "FontRegistry.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "FreeType/FontFace.h"
#include "FreeType/FontLibrary.h"

#ifdef USE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

namespace fonts {

std::once_flag FontRegistry::initOnce;
bool FontRegistry::initialized = false;
std::mutex FontRegistry::registryMutex;
FontRegistry::FallbackList FontRegistry::fallbackFonts;
FontRegistry::LoadedFaceRegistry FontRegistry::loadedFaces;

namespace {

#ifdef USE_FONTCONFIG
[[nodiscard]] std::string StringFromFcPattern(FcPattern* pattern, const char* object)
{
	FcChar8* value = nullptr;
	if (pattern == nullptr)
		return {};

	if (FcPatternGetString(pattern, object, 0, &value) != FcResultMatch || value == nullptr)
		return {};

	return std::string(reinterpret_cast<const char*>(value));
}

[[nodiscard]] std::string FindSystemFontLocked(const FontLibrary::FontconfigStateView& state,
                                               std::string_view family,
                                               std::string_view style,
                                               bool allowSubstitutions)
{
	if (state.config == nullptr)
		return {};

	FcPattern* query = FcPatternCreate();
	if (query == nullptr)
		return {};

	if (!family.empty()) {
		FcPatternAddString(query, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.data()));
	}
	if (!style.empty()) {
		FcPatternAddString(query, FC_STYLE, reinterpret_cast<const FcChar8*>(style.data()));
	}

	FcConfigSubstitute(state.config, query, FcMatchPattern);
	FcDefaultSubstitute(query);

	FcResult matchResult = FcResultNoMatch;
	FcPattern* match = FcFontMatch(state.config, query, &matchResult);
	FcPatternDestroy(query);

	if (match == nullptr || matchResult != FcResultMatch)
		return {};

	std::string resolvedFile = StringFromFcPattern(match, FC_FILE);

	if (!allowSubstitutions && !family.empty()) {
		const std::string matchedFamily = StringFromFcPattern(match, FC_FAMILY);
		if (!matchedFamily.empty() && matchedFamily != family) {
			FcPatternDestroy(match);
			return {};
		}
	}

	if (!allowSubstitutions && !style.empty()) {
		const std::string matchedStyle = StringFromFcPattern(match, FC_STYLE);
		if (!matchedStyle.empty() && matchedStyle != style) {
			FcPatternDestroy(match);
			return {};
		}
	}

	FcPatternDestroy(match);
	return resolvedFile;
}
#endif

} // namespace

void FontRegistry::Init(bool initializeFontconfig, bool consoleOutput)
{
	std::call_once(initOnce, []() {
		FontLibrary::Initialize();
	});

	if (initializeFontconfig) {
		FontLibrary::Instance().InitializeFontconfig(consoleOutput);
	}

	std::lock_guard<std::mutex> lock(registryMutex);
	initialized = true;
}

void FontRegistry::Shutdown()
{
	std::lock_guard<std::mutex> lock(registryMutex);

	fallbackFonts.clear();
	loadedFaces.clear();
	initialized = false;
}

bool FontRegistry::IsInitialized() noexcept
{
	std::lock_guard<std::mutex> lock(registryMutex);
	return initialized;
}

FontLibrary& FontRegistry::GetLibrary()
{
	Init(false, false);
	return FontLibrary::Instance();
}

bool FontRegistry::AddFallbackFont(std::string path)
{
	if (path.empty())
		return false;

	Init(false, false);

	std::lock_guard<std::mutex> lock(registryMutex);

	const auto it = std::find(fallbackFonts.begin(), fallbackFonts.end(), path);
	if (it != fallbackFonts.end())
		return false;

	fallbackFonts.emplace_back(std::move(path));
	return true;
}

void FontRegistry::ClearFallbackFonts()
{
	std::lock_guard<std::mutex> lock(registryMutex);
	fallbackFonts.clear();
}

const FontRegistry::FallbackList& FontRegistry::GetFallbackFonts()
{
	return fallbackFonts;
}

void FontRegistry::RegisterLoadedFace(const FontDescriptor& descriptor, const FacePtr& face)
{
	if (descriptor.Empty() || !face)
		return;

	Init(false, false);

	std::lock_guard<std::mutex> lock(registryMutex);
	loadedFaces[MakeRegistryKey(descriptor)] = face;
}

FontRegistry::FacePtr FontRegistry::FindLoadedFace(const FontDescriptor& descriptor)
{
	if (descriptor.Empty())
		return {};

	std::lock_guard<std::mutex> lock(registryMutex);

	const auto it = loadedFaces.find(MakeRegistryKey(descriptor));
	if (it == loadedFaces.end())
		return {};

	FacePtr face = it->second.lock();
	if (!face) {
		loadedFaces.erase(it);
	}

	return face;
}

void FontRegistry::UnregisterLoadedFace(const FontDescriptor& descriptor)
{
	if (descriptor.Empty())
		return;

	std::lock_guard<std::mutex> lock(registryMutex);
	loadedFaces.erase(MakeRegistryKey(descriptor));
}

void FontRegistry::ClearLoadedFaces()
{
	std::lock_guard<std::mutex> lock(registryMutex);
	loadedFaces.clear();
}

std::string FontRegistry::FindSystemFont(std::string_view family,
                                         std::string_view style,
                                         bool allowSubstitutions)
{
	Init(true, false);

#ifdef USE_FONTCONFIG
	FontLibrary& library = GetLibrary();
	if (!library.HasFontconfig())
		return {};

	return library.WithFontconfigStateLocked([family, style, allowSubstitutions](const FontLibrary::FontconfigStateView& state) {
		return FindSystemFontLocked(state, family, style, allowSubstitutions);
	});
#else
	(void)family;
	(void)style;
	(void)allowSubstitutions;
	return {};
#endif
}

std::string FontRegistry::MakeRegistryKey(const FontDescriptor& descriptor)
{
	std::ostringstream key;
	key << descriptor.filePath
	    << '|'
	    << descriptor.pixelSize
	    << '|'
	    << descriptor.outlineSize
	    << '|'
	    << descriptor.outlineWeight;
	return key.str();
}

} // namespace fonts
