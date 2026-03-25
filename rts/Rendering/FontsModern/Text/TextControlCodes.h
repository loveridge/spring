/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/**
 * Spring-specific inline text control codes.
 *
 * Intent:
 *  - keep control-code recognition isolated from glyph caching, shaping,
 *    layout, wrapping, and rendering
 *  - provide small parsing helpers usable from multiple layers
 *  - keep the interface data-oriented and dependency-light
 */
namespace font::text {

static constexpr char8_t OldColorCodeIndicator   = 0xFF; // legacy \xffRGB
static constexpr char8_t OldColorCodeIndicatorEx = 0xFE; // legacy \xfeRGBA,RGBA
static constexpr char8_t ColorCodeIndicator      = 0x11; // current  \x11RGB
static constexpr char8_t ColorCodeIndicatorEx    = 0x12; // current  \x12RGBA,RGBA
static constexpr char8_t ColorResetIndicator     = 0x08; // current  backspace reset

static constexpr char8_t CR = '\r';
static constexpr char8_t LF = '\n';

static constexpr std::size_t ColorCodeSize3   = 1 + 3;
static constexpr std::size_t ColorCodeSize8   = 1 + 4 + 4;
static constexpr std::size_t ColorResetSize   = 1;
static constexpr std::size_t MaxColorCodeSize = ColorCodeSize8;

struct InlineColorBytes {
	std::uint8_t r = 255;
	std::uint8_t g = 255;
	std::uint8_t b = 255;
	std::uint8_t a = 255;
};

struct ColorCodeText {
	std::array<char, MaxColorCodeSize + 1> storage{};
	std::uint8_t size = 0;

	void Clear()
	{
		storage.fill('\0');
		size = 0;
	}

	std::string_view View() const
	{
		return std::string_view(storage.data(), size);
	}

	std::string ToString() const
	{
		return std::string(storage.data(), size);
	}
};

struct ParsedColorCode {
	enum class Type : std::uint8_t {
		None = 0,
		Color3,
		Color8,
		Reset,
	};

	Type type = Type::None;
	std::size_t offset = 0;
	std::size_t size = 0;
	bool usesLegacyIndicator = false;
	bool hasTextColor = false;
	bool hasOutlineColor = false;
	InlineColorBytes textColor{};
	InlineColorBytes outlineColor{};
	ColorCodeText raw;

	bool IsValid() const { return type != Type::None; }
	bool IsReset() const { return type == Type::Reset; }
};

constexpr bool IsCarriageReturn(char8_t c) { return c == CR; }
constexpr bool IsLineFeed(char8_t c) { return c == LF; }
constexpr bool IsLineBreak(char8_t c) { return IsCarriageReturn(c) || IsLineFeed(c); }

constexpr bool IsColorCodeIndicator(char8_t c, bool enableLegacy = true)
{
	return
		(c == ColorCodeIndicator) ||
		(c == ColorCodeIndicatorEx) ||
		(c == ColorResetIndicator) ||
		(enableLegacy && (c == OldColorCodeIndicator || c == OldColorCodeIndicatorEx));
}

std::size_t LineBreakLength(std::string_view text, std::size_t offset);

bool TryParseColorCode(
	std::string_view text,
	std::size_t offset,
	ParsedColorCode* parsed = nullptr,
	bool enableLegacy = true
);

std::size_t SkipColorCodes(
	std::string_view text,
	std::size_t offset,
	ColorCodeText* copiedText = nullptr,
	bool enableLegacy = true
);

} // namespace font::text
