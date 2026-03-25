/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "FontFace.h"

#include <utility>

namespace fonts {

FontFileBytes::FontFileBytes(std::size_t size)
	: bytes(size)
{}

FontFileBytes::FontFileBytes(container_type data)
	: bytes(std::move(data))
{}

FontFileBytes::value_type* FontFileBytes::Data() noexcept
{
	return bytes.data();
}

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

FontFileBytes::container_type& FontFileBytes::Storage() noexcept
{
	return bytes;
}

const FontFileBytes::container_type& FontFileBytes::Storage() const noexcept
{
	return bytes;
}

void FontFileBytes::Resize(std::size_t size)
{
	bytes.resize(size);
}

void FontFileBytes::Clear() noexcept
{
	bytes.clear();
	bytes.shrink_to_fit();
}

FontFace::FontFace(FT_Face ftFace, std::shared_ptr<FontFileBytes> fileBytes) noexcept
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

void FontFace::Reset(FT_Face newFace, std::shared_ptr<FontFileBytes> newMemory) noexcept
{
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
	return (GetFamilyName() != nullptr) ? std::string(GetFamilyName()) : std::string();
}

std::string FontFace::GetStyleNameString() const
{
	return (GetStyleName() != nullptr) ? std::string(GetStyleName()) : std::string();
}

long FontFace::GetFaceIndex() const noexcept
{
	return (face != nullptr) ? face->face_index : 0;
}

long FontFace::GetNumGlyphs() const noexcept
{
	return (face != nullptr) ? face->num_glyphs : 0;
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
	return (face != nullptr) ? FT_Load_Glyph(face, glyphIndex, loadFlags) : static_cast<FT_Error>(1);
}

} // namespace font
