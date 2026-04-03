/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "FontFace.h"

#include <limits>
#include <utility>

namespace fonts {

FontFileBytes::FontFileBytes(std::size_t size)
	: bytes(size)
{}

FontFileBytes::FontFileBytes(container_type data)
	: bytes(std::move(data))
{}

const FontFileBytes::value_type* FontFileBytes::Data() const noexcept
{
	return bytes.data();
}

std::size_t FontFileBytes::Size() const noexcept
{
	return bytes.size();
}

bool FontFileBytes::Empty() const noexcept
{
	return bytes.empty();
}

FontFace::FontFace(FT_Face ftFace, std::shared_ptr<const FontFileBytes> fileBytes) noexcept
	: face(ftFace)
	, memory(std::move(fileBytes))
{}

FontFace::~FontFace()
{
	Reset();
}

FontFace::FontFace(FontFace&& other) noexcept
	: face(std::exchange(other.face, nullptr))
	, memory(std::move(other.memory))
{}

FontFace& FontFace::operator=(FontFace&& other) noexcept
{
	if (this == &other)
		return *this;

	Reset();
	face = std::exchange(other.face, nullptr);
	memory = std::move(other.memory);
	return *this;
}

void FontFace::Reset(FT_Face newFace, std::shared_ptr<const FontFileBytes> newMemory) noexcept
{
	if (face == newFace && memory == newMemory)
		return;

	if (face != nullptr)
		FT_Done_Face(face);

	face = newFace;
	memory = std::move(newMemory);
}

FT_Face FontFace::GetFTFace() const noexcept
{
	return face;
}

const char* FontFace::GetFamilyName() const noexcept
{
	return (face != nullptr) ? face->family_name : nullptr;
}

const char* FontFace::GetStyleName() const noexcept
{
	return (face != nullptr) ? face->style_name : nullptr;
}

std::string FontFace::GetFamilyNameString() const
{
	if (const char* familyName = GetFamilyName(); familyName != nullptr)
		return std::string(familyName);

	return {};
}

std::string FontFace::GetStyleNameString() const
{
	if (const char* styleName = GetStyleName(); styleName != nullptr)
		return std::string(styleName);

	return {};
}

long FontFace::GetFaceIndex() const noexcept
{
	return (face != nullptr) ? face->face_index : 0;
}

FT_UInt FontFace::GetNumGlyphs() const noexcept
{
	if (face == nullptr || face->num_glyphs <= 0)
		return 0u;

	const auto glyphCount = static_cast<unsigned long>(face->num_glyphs);
	if (glyphCount > std::numeric_limits<FT_UInt>::max())
		return std::numeric_limits<FT_UInt>::max();

	return static_cast<FT_UInt>(glyphCount);
}

bool FontFace::HasKerning() const noexcept
{
	return (face != nullptr) && FT_HAS_KERNING(face);
}

FT_UInt FontFace::GetCharIndex(char32_t codepoint) const noexcept
{
	return (face != nullptr) ? FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint)) : 0u;
}

FT_Error FontFace::LoadGlyph(FT_UInt glyphIndex, FT_Int32 loadFlags) const noexcept
{
	return (face != nullptr) ? FT_Load_Glyph(face, glyphIndex, loadFlags) : FT_Err_Invalid_Face_Handle;
}

} // namespace fonts
