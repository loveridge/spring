/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <type_traits>

/**
 * Shared font render/layout option flags.
 *
 * Intent:
 *  - move option definitions out of glFont.h
 *  - provide a strongly typed bitmask API for new code
 *  - preserve legacy FONT_* integer constants during migration
 *
 * The numeric bit layout intentionally matches the existing FONT_* constants
 * so old and new call-sites can interoperate.
 */
enum class FontOption : std::uint32_t {
	None       = 0u,

	// horizontal alignment
	Left       = 1u << 0,
	Right      = 1u << 1,
	Center     = 1u << 2,

	// vertical alignment
	Baseline   = 1u << 3,  // align to face baseline
	VCenter    = 1u << 4,
	Top        = 1u << 5,  // align to text ascender
	Bottom     = 1u << 6,  // align to text descender
	Ascender   = 1u << 7,  // align to face ascender
	Descender  = 1u << 8,  // align to face descender

	// render modifiers
	Outline    = 1u << 9,
	Shadow     = 1u << 10,
	Norm       = 1u << 11, // render in 0..1 space instead of 0..vsx|vsy
	Scale      = 1u << 12, // size argument is scale, not absolute font size
	Nearest    = 1u << 13, // snap x/y to nearest integer for crisp small text
	Buffered   = 1u << 14, // append outside Begin/End pair
};

using FontOptionFlags = std::underlying_type_t<FontOption>;

constexpr FontOptionFlags operator+(FontOption option)
{
	return static_cast<FontOptionFlags>(option);
}

constexpr FontOption operator|(FontOption lhs, FontOption rhs)
{
	return static_cast<FontOption>(+lhs | +rhs);
}

constexpr FontOption operator&(FontOption lhs, FontOption rhs)
{
	return static_cast<FontOption>(+lhs & +rhs);
}

constexpr FontOption operator^(FontOption lhs, FontOption rhs)
{
	return static_cast<FontOption>(+lhs ^ +rhs);
}

constexpr FontOption operator~(FontOption option)
{
	return static_cast<FontOption>(~(+option));
}

inline FontOption& operator|=(FontOption& lhs, FontOption rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

inline FontOption& operator&=(FontOption& lhs, FontOption rhs)
{
	lhs = lhs & rhs;
	return lhs;
}

inline FontOption& operator^=(FontOption& lhs, FontOption rhs)
{
	lhs = lhs ^ rhs;
	return lhs;
}

constexpr FontOptionFlags ToFontOptionFlags(FontOption options)
{
	return +options;
}

constexpr FontOption ToFontOptions(FontOptionFlags flags)
{
	return static_cast<FontOption>(flags);
}

constexpr bool HasOption(FontOption options, FontOption option)
{
	return ((+options & +option) != 0u);
}

constexpr bool HasOption(FontOptionFlags options, FontOption option)
{
	return ((options & +option) != 0u);
}

constexpr bool HasAllOptions(FontOption options, FontOption mask)
{
	return ((+options & +mask) == +mask);
}

constexpr bool HasAllOptions(FontOptionFlags options, FontOption mask)
{
	return ((options & +mask) == +mask);
}

constexpr bool HasAnyOption(FontOption options, FontOption mask)
{
	return ((+options & +mask) != 0u);
}

constexpr bool HasAnyOption(FontOptionFlags options, FontOption mask)
{
	return ((options & +mask) != 0u);
}

constexpr FontOptionFlags AddOption(FontOptionFlags options, FontOption option)
{
	return (options | +option);
}

constexpr FontOptionFlags RemoveOption(FontOptionFlags options, FontOption option)
{
	return (options & ~(+option));
}

namespace FontOptions {
	constexpr FontOption HorizontalMask = FontOption::Left | FontOption::Right | FontOption::Center;
	constexpr FontOption VerticalMask   = FontOption::Baseline | FontOption::VCenter | FontOption::Top |
	                                     FontOption::Bottom | FontOption::Ascender | FontOption::Descender;
	constexpr FontOption RenderMask     = FontOption::Outline | FontOption::Shadow | FontOption::Norm |
	                                     FontOption::Scale | FontOption::Nearest | FontOption::Buffered;
}

/**
 * Legacy compatibility constants.
 *
 * Keep these during migration so existing call-sites that still pass `int`
 * options do not break when glFont.h stops defining the flags directly.
 */
static constexpr int FONT_LEFT      = static_cast<int>(FontOption::Left);
static constexpr int FONT_RIGHT     = static_cast<int>(FontOption::Right);
static constexpr int FONT_CENTER    = static_cast<int>(FontOption::Center);
static constexpr int FONT_BASELINE  = static_cast<int>(FontOption::Baseline);
static constexpr int FONT_VCENTER   = static_cast<int>(FontOption::VCenter);
static constexpr int FONT_TOP       = static_cast<int>(FontOption::Top);
static constexpr int FONT_BOTTOM    = static_cast<int>(FontOption::Bottom);
static constexpr int FONT_ASCENDER  = static_cast<int>(FontOption::Ascender);
static constexpr int FONT_DESCENDER = static_cast<int>(FontOption::Descender);
static constexpr int FONT_OUTLINE   = static_cast<int>(FontOption::Outline);
static constexpr int FONT_SHADOW    = static_cast<int>(FontOption::Shadow);
static constexpr int FONT_NORM      = static_cast<int>(FontOption::Norm);
static constexpr int FONT_SCALE     = static_cast<int>(FontOption::Scale);
static constexpr int FONT_NEAREST   = static_cast<int>(FontOption::Nearest);
static constexpr int FONT_BUFFERED  = static_cast<int>(FontOption::Buffered);

