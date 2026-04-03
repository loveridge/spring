/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "FontFaceSet.h"

#include <algorithm>
#include <utility>

namespace fonts {

FontFaceSet::FontFaceSet(FacePtr primaryFace)
	: primary(std::move(primaryFace))
{}

FontFaceSet::FontFaceSet(FacePtr primaryFace, FaceList fallbackFaces)
	: primary(std::move(primaryFace))
	, fallbacks(std::move(fallbackFaces))
{}

bool FontFaceSet::Empty() const noexcept
{
	return (primary == nullptr) && fallbacks.empty();
}

FontFaceSet::operator bool() const noexcept
{
	return !Empty();
}

void FontFaceSet::Clear() noexcept
{
	primary.reset();
	fallbacks.clear();
}

void FontFaceSet::SetPrimaryFace(FacePtr face)
{
	primary = std::move(face);

	if (primary == nullptr)
		return;

	fallbacks.erase(std::remove(fallbacks.begin(), fallbacks.end(), primary), fallbacks.end());
}

const FontFaceSet::FacePtr& FontFaceSet::GetPrimaryFace() const noexcept
{
	return primary;
}

FontFaceSet::FacePtr& FontFaceSet::GetPrimaryFace() noexcept
{
	return primary;
}

void FontFaceSet::AddFallbackFace(FacePtr face)
{
	if (face == nullptr)
		return;

	if (ContainsFace(face))
		return;

	fallbacks.emplace_back(std::move(face));
}

void FontFaceSet::SetFallbackFaces(FaceList faces)
{
	fallbacks.clear();
	fallbacks.reserve(faces.size());

	for (auto& face: faces)
		AddFallbackFace(std::move(face));
}

const FontFaceSet::FaceList& FontFaceSet::GetFallbackFaces() const noexcept
{
	return fallbacks;
}

FontFaceSet::FaceList FontFaceSet::GetFaces() const
{
	FaceList result;
	result.reserve((primary != nullptr ? 1u : 0u) + fallbacks.size());

	if (primary != nullptr)
		result.emplace_back(primary);

	result.insert(result.end(), fallbacks.begin(), fallbacks.end());
	return result;
}

std::size_t FontFaceSet::GetFaceCount() const noexcept
{
	return (primary != nullptr ? 1u : 0u) + fallbacks.size();
}

bool FontFaceSet::ContainsFace(const FacePtr& face) const noexcept
{
	if (face == nullptr)
		return false;

	if (primary == face)
		return true;

	return std::find(fallbacks.begin(), fallbacks.end(), face) != fallbacks.end();
}

FontFaceSet::FacePtr FontFaceSet::FindFaceForCodepoint(char32_t codepoint) const
{
	return FindFaceIf([codepoint](const FacePtr& face) {
		return SupportsCodepoint(face, codepoint);
	});
}

std::pair<FontFaceSet::FacePtr, std::uint32_t> FontFaceSet::FindGlyphForCodepoint(char32_t codepoint) const
{
	const auto findGlyph = [codepoint](const FacePtr& face) -> std::pair<FacePtr, std::uint32_t> {
		if (face == nullptr)
			return {nullptr, 0u};

		const std::uint32_t glyphIndex = face->GetCharIndex(codepoint);
		return (glyphIndex != 0u) ? std::pair<FacePtr, std::uint32_t>{face, glyphIndex} : std::pair<FacePtr, std::uint32_t>{nullptr, 0u};
	};

	if (auto result = findGlyph(primary); result.first != nullptr)
		return result;

	for (const FacePtr& face: fallbacks) {
		if (auto result = findGlyph(face); result.first != nullptr)
			return result;
	}

	return {nullptr, 0u};
}

FontFaceSet::FacePtr FontFaceSet::FindFaceForGlyphName(const std::string& glyphName) const
{
	if (glyphName.empty())
		return nullptr;

	return FindFaceIf([&glyphName](const FacePtr& face) {
		return SupportsGlyphName(face, glyphName);
	});
}

bool FontFaceSet::SupportsCodepoint(char32_t codepoint) const
{
	return FindFaceForCodepoint(codepoint) != nullptr;
}

bool FontFaceSet::SupportsCodepoint(const FacePtr& face, char32_t codepoint)
{
	return (face != nullptr) && (face->GetCharIndex(codepoint) != 0u);
}

bool FontFaceSet::SupportsGlyphName(const FacePtr& face, const std::string& glyphName)
{
	if (face == nullptr || glyphName.empty())
		return false;

#if defined(FT_HAS_GLYPH_NAMES)
	FT_Face ftFace = face->GetFTFace();
	if (ftFace == nullptr || !FT_HAS_GLYPH_NAMES(ftFace))
		return false;

	char buffer[128] = {0};
	for (FT_UInt glyphIndex = 1; glyphIndex < face->GetNumGlyphs(); ++glyphIndex) {
		buffer[0] = '\0';
		if (FT_Get_Glyph_Name(ftFace, glyphIndex, buffer, sizeof(buffer)) != 0)
			continue;
		if (glyphName == buffer)
			return true;
	}
#endif

	return false;
}

} // namespace fonts
