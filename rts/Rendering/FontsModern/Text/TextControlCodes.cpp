/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "TextControlCodes.h"

#include <algorithm>

namespace fonts::text {

std::size_t LineBreakLength(std::string_view text, std::size_t offset)
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

bool TryParseColorCode(std::string_view text, std::size_t offset, ParsedColorCode* parsed, bool enableLegacy)
{
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

std::size_t SkipColorCodes(std::string_view text, std::size_t offset, ColorCodeText* copiedText, bool enableLegacy)
{
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
