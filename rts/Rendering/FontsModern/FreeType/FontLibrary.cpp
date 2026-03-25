/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "FontLibrary.h"

#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "System/Config/ConfigHandler.h"
#include "System/Log/ILog.h"
#include "fmt/printf.h"

#ifdef _WIN32
#include <windows.h>
#include <nowide/convert.hpp>
#endif

#if !defined(HEADLESS)
#undef __FTERRORS_H__
#define FT_ERRORDEF(e, v, s) { e, s },
#define FT_ERROR_START_LIST {
#define FT_ERROR_END_LIST { 0, nullptr } };
namespace {
struct FTErrorRecord {
	int errCode;
	const char* errMsg;
};

static constexpr FTErrorRecord ftErrorTable[] =
#include FT_ERRORS_H

[[nodiscard]] const char* GetFTErrorString(FT_Error error) noexcept
{
	for (const FTErrorRecord& errorRecord: ftErrorTable) {
		if (errorRecord.errCode == error)
			return errorRecord.errMsg;
	}

	return "Unknown error";
}

#ifdef USE_FONTCONFIG
void LogFontconfigMessage(bool consoleOutput, bool isError, const std::string& message)
{
	if (consoleOutput) {
		std::printf("%s\n", message.c_str());
		return;
	}

	if (isError) {
		LOG_L(L_ERROR, "%s", message.c_str());
	} else {
		LOG_L(L_INFO, "%s", message.c_str());
	}
}
#endif
} // namespace
#endif

namespace font {

std::once_flag FontLibrary::initFlag;
std::unique_ptr<FontLibrary> FontLibrary::instance;

FontLibrary::FontLibrary()
{
#ifndef HEADLESS
	const FT_Error error = FT_Init_FreeType(&ftLibrary);
	if (error != 0) {
		throw std::runtime_error(fmt::sprintf(
			"[%s] FT_Init_FreeType failed: \"%s\"",
			__func__,
			GetFTErrorString(error)
		));
	}
#endif
}

FontLibrary::~FontLibrary()
{
	DestroyFontconfig();

	if (ftLibrary != nullptr) {
		FT_Done_FreeType(ftLibrary);
		ftLibrary = nullptr;
	}
}

FontLibrary& FontLibrary::Instance()
{
	Initialize();
	return *instance;
}

void FontLibrary::Initialize()
{
	std::call_once(initFlag, []() {
		instance = std::make_unique<FontLibrary>();
	});
}

bool FontLibrary::InitializeFontconfig(bool consoleOutput)
{
#if defined(HEADLESS)
	(void)consoleOutput;
	return false;
#elif defined(USE_FONTCONFIG)
	if (!UseFontconfig())
		return false;

	if (fcConfig != nullptr)
		return true;

	searchSystemFonts = (configHandler == nullptr) || configHandler->GetBool("UseFontConfigSystemFonts");
	searchFontAttributes = (configHandler == nullptr) || configHandler->GetBool("FontConfigSearchAttributes");
	searchApplySubstitutions = (configHandler == nullptr) || configHandler->GetBool("FontConfigApplySubstitutions");

	const std::string errorPrefix = fmt::sprintf(
		"[%s] Fontconfig(version %d.%d.%d) failed to initialize",
		__func__,
		FC_MAJOR,
		FC_MINOR,
		FC_REVISION
	);

	FcConfigEnableHome(FcFalse);
	fcConfig = FcConfigCreate();
	if (fcConfig == nullptr) {
		LogFontconfigMessage(consoleOutput, true, errorPrefix + " config");
		HandleFontconfigInitFailure();
		return false;
	}

#ifdef _WIN32
	static constexpr auto winFontPath = L"%WINDIR%\\fonts";
	const int neededSize = ExpandEnvironmentStrings(winFontPath, nullptr, 0);
	std::vector<TCHAR> osFontsDir(neededSize);
	ExpandEnvironmentStrings(winFontPath, osFontsDir.data(), osFontsDir.size());

	std::wostringstream configStream;
	configStream << L"<fontconfig><dir>" << std::wstring(osFontsDir.data()) << L"</dir><cachedir>fontcache</cachedir></fontconfig>";
	const std::string configXml = nowide::narrow(configStream.str());
#else
	const std::string configXml = R"(<fontconfig><cachedir>fontcache</cachedir></fontconfig>)";
#endif

	FcBool result = FcFalse;
#ifdef _WIN32
	result = FcConfigParseAndLoadFromMemory(fcConfig, reinterpret_cast<const FcChar8*>(configXml.c_str()), FcTrue);
#else
	result = FcConfigParseAndLoad(fcConfig, nullptr, FcTrue);
#endif

	if (result) {
#ifndef _WIN32
		FcConfigParseAndLoadFromMemory(fcConfig, reinterpret_cast<const FcChar8*>(configXml.c_str()), FcTrue);
#endif

		LogFontconfigMessage(consoleOutput, false, fmt::sprintf("[%s] Using Fontconfig light init", __func__));

		result = FcConfigBuildFonts(fcConfig);
		if (!result) {
			LogFontconfigMessage(consoleOutput, true, errorPrefix + " fonts");
			HandleFontconfigInitFailure();
			return false;
		}
	} else {
		FcConfig* fallbackConfig = FcInitLoadConfigAndFonts();
		if (fallbackConfig != nullptr) {
			FcConfigDestroy(fcConfig);
			fcConfig = fallbackConfig;
			FcConfigParseAndLoadFromMemory(fcConfig, reinterpret_cast<const FcChar8*>(configXml.c_str()), FcTrue);
		} else {
			LogFontconfigMessage(consoleOutput, false, errorPrefix + " config and fonts. No system fallbacks will be available");
		}
	}

	gameFontSet = FcFontSetCreate();
	basePattern = FcPatternCreate();
	if (gameFontSet == nullptr || basePattern == nullptr) {
		LogFontconfigMessage(consoleOutput, true, errorPrefix + " state");
		HandleFontconfigInitFailure();
		return false;
	}

	if (!FcConfigAppFontAddDir(fcConfig, reinterpret_cast<const FcChar8*>("fonts"))) {
		LogFontconfigMessage(consoleOutput, true, errorPrefix + " font dir");
		HandleFontconfigInitFailure();
		return false;
	}

	if (FcStrList* cacheDirs = FcConfigGetCacheDirs(fcConfig); cacheDirs != nullptr) {
		FcStrListFirst(cacheDirs);
		for (FcChar8* dir = FcStrListNext(cacheDirs); dir != nullptr; dir = FcStrListNext(cacheDirs)) {
			LogFontconfigMessage(consoleOutput, false, fmt::sprintf("[%s] Using Fontconfig cache dir \"%s\"", __func__, reinterpret_cast<const char*>(dir)));
		}
		FcStrListDone(cacheDirs);
	}

	fontconfigInitialized = true;
	return true;
#else
	(void)consoleOutput;
	return false;
#endif
}

bool FontLibrary::HasFontconfig() const noexcept
{
#ifdef USE_FONTCONFIG
	return (fcConfig != nullptr) && fontconfigInitialized;
#else
	return false;
#endif
}

#ifdef USE_FONTCONFIG
void FontLibrary::ClearGameFontSet()
{
	if (gameFontSet != nullptr)
		FcFontSetDestroy(gameFontSet);

	gameFontSet = FcFontSetCreate();
}

void FontLibrary::ClearBasePattern()
{
	if (basePattern != nullptr)
		FcPatternDestroy(basePattern);

	basePattern = FcPatternCreate();
}

bool FontLibrary::UseFontconfig()
{
	return (configHandler == nullptr) || configHandler->GetBool("UseFontConfigLib");
}

bool FontLibrary::CheckFontconfig()
{
	if (!UseFontconfig())
		return false;

	FontLibrary& library = Instance();
	return library.InitializeFontconfig(false) && FcConfigUptoDate(library.GetFontconfig());
}
#endif

void FontLibrary::DestroyFontconfig() noexcept
{
#ifdef USE_FONTCONFIG
	if (gameFontSet != nullptr) {
		FcFontSetDestroy(gameFontSet);
		gameFontSet = nullptr;
	}

	if (basePattern != nullptr) {
		FcPatternDestroy(basePattern);
		basePattern = nullptr;
	}

	if (fcConfig != nullptr) {
		FcConfigDestroy(fcConfig);
		fcConfig = nullptr;
	}

	fontconfigInitialized = false;
#endif
}

void FontLibrary::HandleFontconfigInitFailure() noexcept
{
	DestroyFontconfig();
}

} // namespace font
