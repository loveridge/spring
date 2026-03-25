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

inline std::size_t LineBreakLength(std::string_view text, std::size_t offset)
{
	if (offset >= text.size())
		return 0;

	const char8_t c = static_cast<char8_t>(text[offset]);
	if (c == CR)
		return (offset + 1 < text.size() && static_cast<char8_t>(text[offset + 1]) == LF) ? 2u : 1u;
	if (c == LF)
		return 1u;

	return 0;
}

inline bool TryParseColorCode(
	std::string_view text,
	std::size_t offset,
	ParsedColorCode* parsed = nullptr,
	bool enableLegacy = true
) {
	if (parsed != nullptr)
		*parsed = {};

	if (offset >= text.size())
		return false;

	const char8_t indicator = static_cast<char8_t>(text[offset]);
	const bool isLegacy = (indicator == OldColorCodeIndicator) || (indicator == OldColorCodeIndicatorEx);

	if (!enableLegacy && isLegacy)
		return false;

	ParsedColorCode local;
	local.offset = offset;
	local.usesLegacyIndicator = isLegacy;

	auto copyRaw = [&local, &text, offset](std::size_t count) {
		local.raw.Clear();
		local.size = count;
		local.raw.size = static_cast<std::uint8_t>(count);
		std::copy_n(text.data() + offset, count, local.raw.storage.data());
	};

	switch (indicator) {
		case OldColorCodeIndicator:
		case ColorCodeIndicator: {
			if (offset + ColorCodeSize3 > text.size())
				return false;

			local.type = ParsedColorCode::Type::Color3;
			local.hasTextColor = true;
			local.textColor.r = static_cast<std::uint8_t>(text[offset + 1]);
			local.textColor.g = static_cast<std::uint8_t>(text[offset + 2]);
			local.textColor.b = static_cast<std::uint8_t>(text[offset + 3]);
			local.textColor.a = 255;
			copyRaw(ColorCodeSize3);
		} break;

		case OldColorCodeIndicatorEx:
		case ColorCodeIndicatorEx: {
			if (offset + ColorCodeSize8 > text.size())
				return false;

			local.type = ParsedColorCode::Type::Color8;
			local.hasTextColor = true;
			local.hasOutlineColor = true;
			local.textColor.r = static_cast<std::uint8_t>(text[offset + 1]);
			local.textColor.g = static_cast<std::uint8_t>(text[offset + 2]);
			local.textColor.b = static_cast<std::uint8_t>(text[offset + 3]);
			local.textColor.a = static_cast<std::uint8_t>(text[offset + 4]);
			local.outlineColor.r = static_cast<std::uint8_t>(text[offset + 5]);
			local.outlineColor.g = static_cast<std::uint8_t>(text[offset + 6]);
			local.outlineColor.b = static_cast<std::uint8_t>(text[offset + 7]);
			local.outlineColor.a = static_cast<std::uint8_t>(text[offset + 8]);
			copyRaw(ColorCodeSize8);
		} break;

		case ColorResetIndicator: {
			local.type = ParsedColorCode::Type::Reset;
			copyRaw(ColorResetSize);
		} break;

		default:
			return false;
	}

	if (parsed != nullptr)
		*parsed = local;
	return true;
}

inline std::size_t SkipColorCodes(
	std::string_view text,
	std::size_t offset,
	ColorCodeText* copiedText = nullptr,
	bool enableLegacy = true
) {
	if (copiedText != nullptr)
		copiedText->Clear();

	std::size_t idx = offset;
	ParsedColorCode parsed;

	while (TryParseColorCode(text, idx, &parsed, enableLegacy)) {
		if (copiedText != nullptr)
			*copiedText = parsed.raw;
		idx += parsed.size;
	}

	return std::min(idx, text.size());
}

} // namespace font::text

