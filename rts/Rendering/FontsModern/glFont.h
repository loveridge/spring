/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <deque>
#include <memory>
#include <string>

#include "FontOptions.h"
#include "FontTypes.h"
#include "ustring.h"

#undef GetCharWidth // winapi.h

struct float2;
struct float3;
struct float4;
struct SColor;
class CMatrix44f;

class CglFont
{
public:
	class Impl;

public:
	static bool LoadConfigFonts();
	static bool LoadCustomFonts(const std::string& smallFontFile, const std::string& largeFontFile);

	static std::shared_ptr<CglFont> LoadFont(const std::string& fontFile, bool small);
	static std::shared_ptr<CglFont> LoadFont(const std::string& fontFile, int size, int outlineWidth = 2, float outlineWeight = 5.0f);

	static std::shared_ptr<CglFont> FindFont(const std::string& fontFile, int size, int outlineWidth = 2, float outlineWeight = 5.0f);

	static void ReallocSystemFontAtlases(bool pre);

public:
	explicit CglFont(const std::string& fontFile, int size, int outlineWidth = 2, float outlineWeight = 5.0f);
	~CglFont();

	CglFont(const CglFont&) = delete;
	CglFont& operator=(const CglFont&) = delete;
	CglFont(CglFont&&) noexcept;
	CglFont& operator=(CglFont&&) noexcept;

public:
	void Begin(bool userDefinedBlending = false);
	void End();

	void DrawBuffered(bool userDefinedBlending = false);
	void DrawWorldBuffered(bool userDefinedBlending = false);

	/**
	 * @param size absolute font size, or relative scale when FONT_SCALE is set
	 * @param options FONT_SCALE | FONT_NORM |
	 *                (FONT_LEFT | FONT_CENTER | FONT_RIGHT) |
	 *                (FONT_BASELINE | FONT_DESCENDER | FONT_VCENTER |
	 *                 FONT_TOP | FONT_ASCENDER | FONT_BOTTOM) |
	 *                FONT_NEAREST | FONT_OUTLINE | FONT_SHADOW |
	 *                FONT_BUFFERED
	 */
	void Print(float x, float y, float size, int options, const std::string& text);
	void Print(float x, float y, float size, FontOption options, const std::string& text);

	void PrintTable(float x, float y, float size, int options, const std::string& text);
	void PrintTable(float x, float y, float size, FontOption options, const std::string& text);

	void PrintWorld(const float3& position, float size, const std::string& text,
		int options = FONT_DESCENDER | FONT_CENTER | FONT_OUTLINE | FONT_BUFFERED);
	void PrintWorld(const float3& position, float size, const std::string& text,
		FontOption options);

	// Compatibility aliases used across the engine during migration.
	void glPrint(float x, float y, float size, int options, const std::string& text) {
		Print(x, y, size, options, text);
	}
	void glPrint(float x, float y, float size, FontOption options, const std::string& text) {
		Print(x, y, size, options, text);
	}
	void glPrintTable(float x, float y, float size, int options, const std::string& text) {
		PrintTable(x, y, size, options, text);
	}
	void glPrintTable(float x, float y, float size, FontOption options, const std::string& text) {
		PrintTable(x, y, size, options, text);
	}
	void glWorldPrint(const float3& position, float size, const std::string& text,
		int options = FONT_DESCENDER | FONT_CENTER | FONT_OUTLINE | FONT_BUFFERED) {
		PrintWorld(position, size, text, options);
	}
	void glWorldPrint(const float3& position, float size, const std::string& text,
		FontOption options) {
		PrintWorld(position, size, text, options);
	}

	template <typename... Args>
	void Format(float x, float y, float size, int options, const char* fmt, Args&&... args);
	template <typename... Args>
	void Format(float x, float y, float size, FontOption options, const char* fmt, Args&&... args);

	template <typename... Args>
	void glFormat(float x, float y, float size, int options, const char* fmt, Args&&... args) {
		Format(x, y, size, options, fmt, std::forward<Args>(args)...);
	}
	template <typename... Args>
	void glFormat(float x, float y, float size, FontOption options, const char* fmt, Args&&... args) {
		Format(x, y, size, options, fmt, std::forward<Args>(args)...);
	}

public:
	float GetCharacterWidth(char32_t c) const;

	float GetTextWidth(const std::string& text) const;
	float GetTextWidth(const spring::u8string& text) const;

	float GetTextHeight(const std::string& text, float* descender = nullptr, int* numLines = nullptr) const;
	float GetTextHeight(const spring::u8string& text, float* descender = nullptr, int* numLines = nullptr) const;

	int WrapInPlace(std::string& text, float fontSize, float maxWidth, float maxHeight = 100000.0f) const;
	int WrapInPlace(spring::u8string& text, float fontSize, float maxWidth, float maxHeight = 100000.0f) const;

	std::string Wrap(const std::string& text, float fontSize, float maxWidth, float maxHeight = 100000.0f) const;
	spring::u8string Wrap(const spring::u8string& text, float fontSize, float maxWidth, float maxHeight = 100000.0f) const;

	static std::deque<std::string> SplitIntoLines(const spring::u8string& text);

public:
	void SetAutoOutlineColor(bool enable);

	void SetTextColor(const float4* color = nullptr);
	void SetOutlineColor(const float4* color = nullptr);
	void SetColors(const float4* textColor = nullptr, const float4* outlineColor = nullptr);

	void SetTextColor(float r, float g, float b, float a);
	void SetOutlineColor(float r, float g, float b, float a);
	void SetTextColor(SColor rgba);
	void SetOutlineColor(SColor rgba);

	void SetTextDepth(float z = 0.0f);
	void SetOutlineDepth(float z = 0.0f);
	void SetDepths(const FontDepth& depth);
	FontDepth GetDepths() const;

	void SetViewMatrix(const CMatrix44f& mat);
	void SetProjMatrix(const CMatrix44f& mat);
	const CMatrix44f& GetViewMatrix() const;
	const CMatrix44f& GetProjMatrix() const;

	static CMatrix44f DefViewMatrix();
	static CMatrix44f DefProjMatrix();

public:
	const std::string& GetFilePath() const;
	FontDescriptor GetDescriptor() const;

	void ScanForWantedGlyphs(const spring::u8string& text);
	void GetStats(std::array<size_t, 8>& stats) const;

	Impl* GetImpl() noexcept { return impl.get(); }
	const Impl* GetImpl() const noexcept { return impl.get(); }

private:
	std::unique_ptr<Impl> impl;
};


extern std::shared_ptr<CglFont> font;
extern std::shared_ptr<CglFont> smallFont;


#ifdef HEADLESS

template<typename ...Args>
inline void CglFont::Format(float x, float y, float size, int options, const char* fmt, Args&& ...args) {}

template<typename ...Args>
inline void CglFont::Format(float x, float y, float size, FontOption options, const char* fmt, Args&& ...args) {}

#else

#include "fmt/printf.h"

template<typename ...Args>
inline void CglFont::Format(float x, float y, float size, int options, const char* fmt, Args&& ...args)
{
	if (fmt == nullptr)
		return;

	Print(x, y, size, options, fmt::sprintf(fmt, std::forward<Args>(args)...));
}

template<typename ...Args>
inline void CglFont::Format(float x, float y, float size, FontOption options, const char* fmt, Args&& ...args)
{
	if (fmt == nullptr)
		return;

	Print(x, y, size, options, fmt::sprintf(fmt, std::forward<Args>(args)...));
}

#endif
