/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace font {

/**
 * Owns the raw font file bytes used to construct an FT_Face via
 * FT_New_Memory_Face. The bytes must outlive the face, so they are held by
 * shared ownership and can be reused across multiple face wrappers if needed.
 */
class FontFileBytes {
public:
	using value_type = FT_Byte;
	using container_type = std::vector<value_type>;

public:
	FontFileBytes() = default;
	explicit FontFileBytes(std::size_t size)
		: bytes(size)
	{}
	explicit FontFileBytes(container_type data)
		: bytes(std::move(data))
	{}

	FontFileBytes(const FontFileBytes&) = default;
	FontFileBytes& operator=(const FontFileBytes&) = default;
	FontFileBytes(FontFileBytes&&) noexcept = default;
	FontFileBytes& operator=(FontFileBytes&&) noexcept = default;
	~FontFileBytes() = default;

	value_type* Data() noexcept { return bytes.data(); }
	const value_type* Data() const noexcept { return bytes.data(); }

	std::size_t Size() const noexcept { return bytes.size(); }
	bool Empty() const noexcept { return bytes.empty(); }

	container_type& Storage() noexcept { return bytes; }
	const container_type& Storage() const noexcept { return bytes; }

	void Resize(std::size_t size) { bytes.resize(size); }
	void Clear() noexcept { bytes.clear(); bytes.shrink_to_fit(); }

private:
	container_type bytes;
};


/**
 * RAII wrapper for a single FT_Face and its optional backing memory.
 *
 * This type owns the FreeType face handle and guarantees that any memory used
 * with FT_New_Memory_Face remains alive for at least as long as the face.
 *
 * It intentionally knows nothing about glyph atlases, rendering, shaping, or
 * text wrapping.
 */
class FontFace {
public:
	FontFace() = default;
	explicit FontFace(FT_Face ftFace, std::shared_ptr<FontFileBytes> fileBytes = {}) noexcept
		: face(ftFace)
		, memory(std::move(fileBytes))
	{}

	~FontFace() { Reset(); }

	FontFace(const FontFace&) = delete;
	FontFace& operator=(const FontFace&) = delete;

	FontFace(FontFace&& other) noexcept
		: face(std::exchange(other.face, nullptr))
		, memory(std::move(other.memory))
	{}

	FontFace& operator=(FontFace&& other) noexcept
	{
		if (this == &other)
			return *this;

		Reset();
		face = std::exchange(other.face, nullptr);
		memory = std::move(other.memory);
		return *this;
	}

	void Reset(FT_Face newFace = nullptr, std::shared_ptr<FontFileBytes> newMemory = {}) noexcept
	{
		if (face != nullptr) {
			FT_Done_Face(face);
		}

		face = newFace;
		memory = std::move(newMemory);
	}

	FT_Face GetFTFace() const noexcept { return face; }
	operator FT_Face() const noexcept { return face; }

	bool IsValid() const noexcept { return face != nullptr; }
	explicit operator bool() const noexcept { return IsValid(); }

	const std::shared_ptr<FontFileBytes>& GetBackingBytes() const noexcept { return memory; }
	std::shared_ptr<FontFileBytes>& GetBackingBytes() noexcept { return memory; }
	bool HasBackingBytes() const noexcept { return static_cast<bool>(memory); }

	const char* GetFamilyName() const noexcept
	{
		return (face != nullptr) ? face->family_name : nullptr;
	}

	const char* GetStyleName() const noexcept
	{
		return (face != nullptr) ? face->style_name : nullptr;
	}

	std::string GetFamilyNameString() const
	{
		return (GetFamilyName() != nullptr) ? std::string(GetFamilyName()) : std::string();
	}

	std::string GetStyleNameString() const
	{
		return (GetStyleName() != nullptr) ? std::string(GetStyleName()) : std::string();
	}

	long GetFaceIndex() const noexcept
	{
		return (face != nullptr) ? face->face_index : 0;
	}

	long GetNumGlyphs() const noexcept
	{
		return (face != nullptr) ? face->num_glyphs : 0;
	}

	bool HasKerning() const noexcept
	{
		return (face != nullptr) && FT_HAS_KERNING(face);
	}

	FT_UInt GetCharIndex(char32_t codepoint) const noexcept
	{
		return (face != nullptr) ? FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint)) : 0u;
	}

	FT_Error LoadGlyph(FT_UInt glyphIndex, FT_Int32 loadFlags) const noexcept
	{
		return (face != nullptr) ? FT_Load_Glyph(face, glyphIndex, loadFlags) : static_cast<FT_Error>(1);
	}

private:
	FT_Face face = nullptr;
	std::shared_ptr<FontFileBytes> memory;
};

} // namespace font
