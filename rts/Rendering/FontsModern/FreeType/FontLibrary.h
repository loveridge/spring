#pragma once

#include <memory>
#include <mutex>

#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef USE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

namespace font {

class FontLibrary {
public:
	FontLibrary();
	~FontLibrary();

	FontLibrary(const FontLibrary&) = delete;
	FontLibrary& operator=(const FontLibrary&) = delete;
	FontLibrary(FontLibrary&&) = delete;
	FontLibrary& operator=(FontLibrary&&) = delete;

	// Returns the process-wide FreeType subsystem instance.
	static FontLibrary& Instance();

	// Explicit one-time bootstrap for call-sites that want deterministic init order.
	static void Initialize();

	// FreeType access.
	FT_Library GetFTLibrary() const noexcept { return ftLibrary; }
	operator FT_Library() const noexcept { return ftLibrary; }

	// Optional Fontconfig bootstrap. Safe to call repeatedly.
	bool InitializeFontconfig(bool consoleOutput = false);
	bool HasFontconfig() const noexcept;

#ifdef USE_FONTCONFIG
	FcConfig* GetFontconfig() const noexcept { return fcConfig; }
	FcFontSet* GetGameFontSet() const noexcept { return gameFontSet; }
	FcPattern* GetBasePattern() const noexcept { return basePattern; }

	void ClearGameFontSet();
	void ClearBasePattern();

	bool GetSearchSystemFonts() const noexcept { return searchSystemFonts; }
	bool GetSearchFontAttributes() const noexcept { return searchFontAttributes; }
	bool GetSearchApplySubstitutions() const noexcept { return searchApplySubstitutions; }

	static bool UseFontconfig();
	static bool CheckFontconfig();
#else
	static bool UseFontconfig() noexcept { return false; }
	static bool CheckFontconfig() noexcept { return false; }
#endif

private:
	void DestroyFontconfig() noexcept;
	void HandleFontconfigInitFailure() noexcept;

	FT_Library ftLibrary = nullptr;

#ifdef USE_FONTCONFIG
	FcConfig* fcConfig = nullptr;
	FcFontSet* gameFontSet = nullptr;
	FcPattern* basePattern = nullptr;
	bool searchSystemFonts = false;
	bool searchFontAttributes = false;
	bool searchApplySubstitutions = false;
	bool fontconfigInitialized = false;
#endif

	static std::once_flag initFlag;
	static std::unique_ptr<FontLibrary> instance;
};

} // namespace font

