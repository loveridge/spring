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

namespace spring::font {

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
	explicit FontFileBytes(std::size_t size);
	explicit FontFileBytes(container_type data);

	FontFileBytes(const FontFileBytes&) = default;
	FontFileBytes& operator=(const FontFileBytes&) = default;
	FontFileBytes(FontFileBytes&&) noexcept = default;
	FontFileBytes& operator=(FontFileBytes&&) noexcept = default;
	~FontFileBytes() = default;

	value_type* Data() noexcept;
	const value_type* Data() const noexcept;

	std::size_t Size() const noexcept;
	bool Empty() const noexcept;

	container_type& Storage() noexcept;
	const container_type& Storage() const noexcept;

	void Resize(std::size_t size);
	void Clear() noexcept;

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
	explicit FontFace(FT_Face ftFace, std::shared_ptr<FontFileBytes> fileBytes = {}) noexcept;

	~FontFace();

	FontFace(const FontFace&) = delete;
	FontFace& operator=(const FontFace&) = delete;

	FontFace(FontFace&& other) noexcept;

	FontFace& operator=(FontFace&& other) noexcept;

	void Reset(FT_Face newFace = nullptr, std::shared_ptr<FontFileBytes> newMemory = {}) noexcept;

	FT_Face GetFTFace() const noexcept;
	operator FT_Face() const noexcept { return face; }

	bool IsValid() const noexcept { return face != nullptr; }
	explicit operator bool() const noexcept { return IsValid(); }

	const std::shared_ptr<FontFileBytes>& GetBackingBytes() const noexcept { return memory; }
	std::shared_ptr<FontFileBytes>& GetBackingBytes() noexcept { return memory; }
	bool HasBackingBytes() const noexcept { return static_cast<bool>(memory); }

	const char* GetFamilyName() const noexcept;

	const char* GetStyleName() const noexcept;

	std::string GetFamilyNameString() const;

	std::string GetStyleNameString() const;

	long GetFaceIndex() const noexcept;

	long GetNumGlyphs() const noexcept;

	bool HasKerning() const noexcept;

	FT_UInt GetCharIndex(char32_t codepoint) const noexcept;

	FT_Error LoadGlyph(FT_UInt glyphIndex, FT_Int32 loadFlags) const noexcept;

private:
	FT_Face face = nullptr;
	std::shared_ptr<FontFileBytes> memory;
};

} // namespace font
