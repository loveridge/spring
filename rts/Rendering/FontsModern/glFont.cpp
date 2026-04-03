/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "glFont.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fmt/printf.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include "FontRegistry.h"
#include "FreeType/FontFace.h"
#include "FreeType/FontFaceSet.h"
#include "FreeType/FontLibrary.h"
#include "Glyphs/GlyphAtlasCache.h"
#include "Render/FontRendererFactory.h"
#include "Text/HarfBuzzTextShaper.h"
#include "Text/TextControlCodes.h"
#include "Text/TextLayouter.h"
#include "Text/TextWrapper.h"

#include "Game/Camera.h"
#include "Rendering/Fonts/FontHandler.h"
#include "Rendering/Fonts/FontLogSection.h"
#include "Rendering/GlobalRendering.h"
#include "System/Color.h"
#include "System/Config/ConfigHandler.h"
#include "System/Exceptions.h"
#include "System/FileSystem/DataDirsAccess.h"
#include "System/FileSystem/FileHandler.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Log/ILog.h"
#include "System/StringUtil.h"

#include "System/Misc/TracyDefs.h"

CONFIG(std::string,      FontFile).defaultValue("fonts/FreeSansBold.otf").description("Sets the font of Spring engine text.");
CONFIG(std::string, SmallFontFile).defaultValue("fonts/FreeSansBold.otf").description("Sets the font of Spring engine small text.");

CONFIG(int,      FontSize).defaultValue(23).description("Sets the font size (in pixels) of the MainMenu and more.");
CONFIG(int, SmallFontSize).defaultValue(14).description("Sets the font size (in pixels) of the engine GUIs and more.");
CONFIG(int,      FontOutlineWidth).defaultValue(2).description("Sets the width of the black outline around Spring engine text, such as the title screen version number, clock, and basic UI. Does not affect LuaUI elements.");
CONFIG(int, SmallFontOutlineWidth).defaultValue(2).description("see FontOutlineWidth");
CONFIG(float,      FontOutlineWeight).defaultValue(25.0f).description("Sets the opacity of Spring engine text, such as the title screen version number, clock, and basic UI. Does not affect LuaUI elements.");
CONFIG(float, SmallFontOutlineWeight).defaultValue(10.0f).description("see FontOutlineWeight");
CONFIG(std::string, FontRendererBackend).defaultValue("slug").description("Selects the modern font renderer backend: opengl, slug, auto, or null.");

std::shared_ptr<CglFont> font = nullptr;
std::shared_ptr<CglFont> smallFont = nullptr;

const bool debug_DumpAtlases = false;

namespace {

static constexpr float4 WhiteColor(1.00f, 1.00f, 1.00f, 0.95f);
static constexpr float4 DarkOutlineColor(0.05f, 0.05f, 0.05f, 0.95f);
static constexpr float4 LightOutlineColor(0.95f, 0.95f, 0.95f, 0.80f);
static std::atomic<std::uint32_t> nextFontDebugId = 1u;

std::vector<std::weak_ptr<CglFont>>& GetLoadedFonts()
{
	static std::vector<std::weak_ptr<CglFont>> loadedFonts;
	return loadedFonts;
}

[[nodiscard]] FontOption NormalizeOptions(FontOption options)
{
	if (!HasAnyOption(options, FontOptions::HorizontalMask))
		options |= FontOption::Left;

	if (!HasAnyOption(options, FontOptions::VerticalMask))
		options |= FontOption::Baseline;

	return options;
}

[[nodiscard]] float ResolveLuminosity(const float4& color)
{
	return
		0.2126f * std::pow(color.x, 2.2f) +
		0.7152f * std::pow(color.y, 2.2f) +
		0.0722f * std::pow(color.z, 2.2f);
}

[[nodiscard]] const float4& ChooseOutlineColor(const float4& textColor)
{
	const float luminosity = ResolveLuminosity(textColor);
	const float darkLuminosity = 0.05f + ResolveLuminosity(DarkOutlineColor);
	const float maxLum = std::max(luminosity + 0.05f, darkLuminosity);
	const float minLum = std::min(luminosity + 0.05f, darkLuminosity);

	if ((maxLum / minLum) > 5.0f)
		return DarkOutlineColor;

	return LightOutlineColor;
}

[[nodiscard]] float4 ToFloat4(const fonts::text::InlineColorBytes& color) noexcept
{
	constexpr float inv = 1.0f / 255.0f;
	return float4(color.r * inv, color.g * inv, color.b * inv, color.a * inv);
}

[[nodiscard]] FontColor ToFontColor(const float4& color) noexcept
{
	return FontColor {color.x, color.y, color.z, color.w};
}

[[nodiscard]] float ResolveRenderSize(float size, FontDescriptor descriptor, FontOption options)
{
	if (HasOption(options, FontOption::Scale))
		size *= std::max(descriptor.pixelSize, 1);

	return size;
}

void ConvertNormalizedToPixels(float& x, float& y)
{
	if (globalRendering == nullptr)
		return;

	x *= globalRendering->viewSizeX;
	y *= globalRendering->viewSizeY;
}

[[nodiscard]] std::string NormalizeFontPath(std::string fontFile)
{
	if (fontFile.empty())
		return fontFile;

	return FileSystem::ForwardSlashes(fontFile);
}

[[nodiscard]] std::string ResolveFontPathForRead(std::string fontFile)
{
	fontFile = NormalizeFontPath(std::move(fontFile));
	if (fontFile.empty())
		return fontFile;

	if (CFileHandler(fontFile).FileExists())
		return fontFile;

	if (FileSystem::IsAbsolutePath(fontFile))
		return fontFile;

	const std::string resolvedPath = dataDirsAccess.LocateFile(fontFile);
	if (!resolvedPath.empty() && FileSystem::FileExists(resolvedPath))
		return FileSystem::ForwardSlashes(resolvedPath);

	if (!fontFile.starts_with("fonts/")) {
		const std::string prefixedPath = "fonts/" + fontFile;
		if (CFileHandler(prefixedPath).FileExists())
			return prefixedPath;

		const std::string resolvedPrefixedPath = dataDirsAccess.LocateFile(prefixedPath);
		if (!resolvedPrefixedPath.empty() && FileSystem::FileExists(resolvedPrefixedPath))
			return FileSystem::ForwardSlashes(resolvedPrefixedPath);

		return prefixedPath;
	}

	return fontFile;
}

[[nodiscard]] std::string SanitizeFilenameComponent(std::string text)
{
	if (text.empty())
		return "font";

	for (char& c: text) {
		const bool isSafe =
			(c >= '0' && c <= '9') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c == '-') ||
			(c == '_') ||
			(c == '.');

		if (!isSafe)
			c = '_';
	}

	return text;
}

[[nodiscard]] std::string BuildAtlasDumpFilename(const FontDescriptor& descriptor, std::uint32_t debugId, const char* atlasName)
{
	const std::string fontName = SanitizeFilenameComponent(FileSystem::GetBasename(descriptor.filePath));

	return fmt::sprintf(
		"font-atlas-debug/font-%u-%s-%dpx-ol%d-%s.png",
		debugId,
		fontName.c_str(),
		descriptor.pixelSize,
		descriptor.outlineSize,
		atlasName
	);
}

void DumpAtlasBitmap(const GlyphAtlasTexture& atlas, const FontDescriptor& descriptor, std::uint32_t debugId, const char* atlasName)
{
	if (atlas.GetBitmap().Empty())
		return;

	static constexpr const char* debugDir = "font-atlas-debug";
	if (!FileSystem::CreateDirectory(debugDir)) {
		LOG_L(L_WARNING, "[CglFont::%s] Failed to create debug atlas directory \"%s\"", __func__, debugDir);
		return;
	}

	const std::string filename = BuildAtlasDumpFilename(descriptor, debugId, atlasName);
	if (!atlas.GetBitmap().Save(filename, false, false))
		LOG_L(L_WARNING, "[CglFont::%s] Failed to save atlas dump \"%s\"", __func__, filename.c_str());
}

[[nodiscard]] std::string DescribeFontPathResolution(const std::string& fontFile)
{
	const std::string normalizedFontFile = NormalizeFontPath(fontFile);
	const bool directExists = CFileHandler(normalizedFontFile).FileExists();
	const bool isAbsolutePath = FileSystem::IsAbsolutePath(normalizedFontFile);
	const bool hasFontsPrefix = normalizedFontFile.starts_with("fonts/");

	const std::string locatedPath = dataDirsAccess.LocateFile(normalizedFontFile);
	const bool locatedExists = !locatedPath.empty() && FileSystem::FileExists(locatedPath);

	const std::string prefixedPath = hasFontsPrefix ? std::string {} : ("fonts/" + normalizedFontFile);
	const bool prefixedExists = !prefixedPath.empty() && CFileHandler(prefixedPath).FileExists();

	const std::string locatedPrefixedPath = prefixedPath.empty() ? std::string {} : dataDirsAccess.LocateFile(prefixedPath);
	const bool locatedPrefixedExists = !locatedPrefixedPath.empty() && FileSystem::FileExists(locatedPrefixedPath);

	return fmt::sprintf(
		"original=\"%s\", normalized=\"%s\", absolute=%d, directExists=%d, "
		"located=\"%s\", locatedExists=%d, prefixed=\"%s\", prefixedExists=%d, "
		"locatedPrefixed=\"%s\", locatedPrefixedExists=%d",
		fontFile.c_str(),
		normalizedFontFile.c_str(),
		isAbsolutePath,
		directExists,
		locatedPath.c_str(),
		locatedExists,
		prefixedPath.c_str(),
		prefixedExists,
		locatedPrefixedPath.c_str(),
		locatedPrefixedExists
	);
}

[[nodiscard]] bool LoadFontFileBytes(const std::string& fontFile, std::shared_ptr<fonts::FontFileBytes>& fileBytes)
{
	const std::string resolvedFontFile = ResolveFontPathForRead(fontFile);
	CFileHandler fileHandler(resolvedFontFile);
	if (!fileHandler.FileExists()) {
		LOG_L(L_ERROR, "[CglFont::%s] Font resolution failed: resolved=\"%s\"; %s",
			__func__,
			resolvedFontFile.c_str(),
			DescribeFontPathResolution(fontFile).c_str()
		);
		return false;
	}

	fileBytes = std::make_shared<fonts::FontFileBytes>(static_cast<std::size_t>(fileHandler.FileSize()));
	if (fileBytes->Empty())
		return false;

	fileHandler.Read(fileBytes->Data(), fileHandler.FileSize());
	return true;
}

std::shared_ptr<fonts::FontFace> LoadSizedFace(const FontDescriptor& descriptor)
{
#ifdef HEADLESS
	(void)descriptor;
	return {};
#else
	if (descriptor.filePath.empty())
		return {};

	if (auto cachedFace = fonts::FontRegistry::FindLoadedFace(descriptor); cachedFace != nullptr)
		return cachedFace;

	std::shared_ptr<fonts::FontFileBytes> fileBytes;
	if (!LoadFontFileBytes(descriptor.filePath, fileBytes))
		throw content_error("Couldn't find font '" + descriptor.filePath + "'.");

	FT_Face ftFace = nullptr;
	const FT_Error newFaceError = FT_New_Memory_Face(
		fonts::FontRegistry::GetLibrary().GetFTLibrary(),
		fileBytes->Data(),
		static_cast<FT_Long>(fileBytes->Size()),
		0,
		&ftFace
	);

	if (newFaceError != 0 || ftFace == nullptr)
		throw content_error("Failed to create FreeType face for '" + descriptor.filePath + "'.");

	auto face = std::make_shared<fonts::FontFace>(ftFace, fileBytes);
	const FT_Error sizeError = FT_Set_Pixel_Sizes(face->GetFTFace(), 0, std::max(descriptor.pixelSize, 1));
	if (sizeError != 0) {
		throw content_error("Failed to set pixel size for font '" + descriptor.filePath + "'.");
	}

	fonts::FontRegistry::RegisterLoadedFace(descriptor, face);
	return face;
#endif
}

std::shared_ptr<fonts::FontFaceSet> BuildFaceSet(const FontDescriptor& descriptor)
{
	auto primaryFace = LoadSizedFace(descriptor);
	if (primaryFace == nullptr)
		return {};

	std::vector<std::shared_ptr<fonts::FontFace>> fallbackFaces;
	fallbackFaces.reserve(fonts::FontRegistry::GetFallbackFonts().size());

	for (const std::string& fallbackPath: fonts::FontRegistry::GetFallbackFonts()) {
		if (fallbackPath.empty() || fallbackPath == descriptor.filePath)
			continue;

		try {
			FontDescriptor fallbackDescriptor = descriptor;
			fallbackDescriptor.filePath = fallbackPath;

			if (auto fallbackFace = LoadSizedFace(fallbackDescriptor); fallbackFace != nullptr)
				fallbackFaces.emplace_back(std::move(fallbackFace));
		} catch (const content_error& ex) {
			LOG_L(L_WARNING, "[CglFont::%s] Failed to load fallback font \"%s\": %s", __func__, fallbackPath.c_str(), ex.what());
		}
	}

	return std::make_shared<fonts::FontFaceSet>(std::move(primaryFace), std::move(fallbackFaces));
}

[[nodiscard]] std::string ResolveConfiguredFontFile(bool isSmallFont)
{
	return configHandler->GetString(isSmallFont ? "SmallFontFile" : "FontFile");
}

[[nodiscard]] FontDescriptor ResolveDescriptor(const std::string& fontFile, bool isSmallFont)
{
	FontDescriptor descriptor;
	descriptor.filePath = NormalizeFontPath(fontFile.empty() ? ResolveConfiguredFontFile(isSmallFont) : fontFile);
	descriptor.pixelSize = configHandler->GetInt(isSmallFont ? "SmallFontSize" : "FontSize");
	descriptor.outlineSize = configHandler->GetInt(isSmallFont ? "SmallFontOutlineWidth" : "FontOutlineWidth");
	descriptor.outlineWeight = configHandler->GetFloat(isSmallFont ? "SmallFontOutlineWeight" : "FontOutlineWeight");
	return descriptor;
}

[[nodiscard]] fonts::render::FontRendererBackend ResolveRendererBackend()
{
	if (configHandler == nullptr)
		return fonts::render::FontRendererBackend::OpenGL;

	const std::string configuredBackend = StringToLower(configHandler->GetString("FontRendererBackend"));

	if (configuredBackend == "auto")
		return fonts::render::FontRendererBackend::Auto;
	if (configuredBackend == "slug")
		return fonts::render::FontRendererBackend::Slug;
	if (configuredBackend == "null")
		return fonts::render::FontRendererBackend::Null;

	return fonts::render::FontRendererBackend::OpenGL;
}

[[nodiscard]] bool SameSpan(const fonts::text::TextSpan& lhs, const fonts::text::TextSpan& rhs) noexcept
{
	return
		(lhs.sourceOffset == rhs.sourceOffset) &&
		(lhs.sourceLength == rhs.sourceLength) &&
		(lhs.printableOffset == rhs.printableOffset) &&
		(lhs.printableLength == rhs.printableLength);
}

} // namespace

class CglFont::Impl
{
public:
	struct RenderCommand {
		fonts::text::TextLayout layout;
		fonts::render::FontRenderState state{};
		float4 baseTextColor = WhiteColor;
		float4 baseOutlineColor = DarkOutlineColor;
		bool autoOutlineColor = false;
		float renderSize = 1.0f;
		bool drawOutline = false;
		bool drawShadow = false;
	};

public:
	explicit Impl(FontDescriptor descriptor_)
		: descriptor(std::move(descriptor_))
	{
		debugId = nextFontDebugId.fetch_add(1u);
		fonts::FontRegistry::Init(true, false);

		faceSet = BuildFaceSet(descriptor);
		if (faceSet == nullptr || faceSet->Empty())
			throw content_error("Failed to load font file: " + descriptor.filePath);

		fonts::render::FontRendererCreateOptions rendererOptions;
		rendererOptions.backend = ResolveRendererBackend();

		const bool enableSlugRendering = (rendererOptions.backend == fonts::render::FontRendererBackend::Slug);
		glyphCache = std::make_shared<fonts::GlyphAtlasCache>(faceSet, descriptor.pixelSize, descriptor.outlineSize, descriptor.outlineWeight, enableSlugRendering);
		shaper = std::make_shared<fonts::text::HarfBuzzTextShaper>(faceSet);
		layouter = std::make_shared<fonts::text::TextLayouter>(shaper, glyphCache);
		wrapper = std::make_shared<fonts::text::TextWrapper>(std::make_shared<fonts::text::TextLayouterMeasurer>(layouter));
		renderer = fonts::render::FontRendererFactory::Create(rendererOptions);
		if (renderer == nullptr)
			throw content_error("Failed to create modern font renderer");

		textColor = WhiteColor;
		outlineColor = DarkOutlineColor;
		baseTextColor = textColor;
		baseOutlineColor = outlineColor;

		viewMatrix = CglFont::DefViewMatrix();
		projMatrix = CglFont::DefProjMatrix();
	}

	void QueueCommand(RenderCommand command, bool worldSpace)
	{
		RECOIL_DETAILED_TRACY_ZONE;

		if (worldSpace)
			worldCommands.emplace_back(std::move(command));
		else
			screenCommands.emplace_back(std::move(command));
	}

	void FlushCommands(std::vector<RenderCommand>& commands, bool userDefinedBlending)
	{
		RECOIL_DETAILED_TRACY_ZONE;

#ifdef HEADLESS
		(void)commands;
		(void)userDefinedBlending;
		return;
#else
		if (commands.empty())
			return;

		auto& primaryAtlas = glyphCache->GetAtlasTexture();
		auto& outlineAtlas = glyphCache->GetShadowAtlasTexture();
		const bool dumpPrimaryAtlas = primaryAtlas.NeedsUpload() || !primaryAtlas.HasTexture();
		const bool dumpOutlineAtlas = outlineAtlas.NeedsUpload() || !outlineAtlas.HasTexture();

		glyphCache->UpdateAtlases();
		renderer->HandleGlyphCacheUpdate(*glyphCache, false);

		if (debug_DumpAtlases && dumpPrimaryAtlas)
			DumpAtlasBitmap(primaryAtlas, descriptor, debugId, "primary");
		if (debug_DumpAtlases && dumpOutlineAtlas)
			DumpAtlasBitmap(outlineAtlas, descriptor, debugId, "outline");

		for (const RenderCommand& command: commands) {
			fonts::render::FontRenderState state = command.state;
			state.userDefinedBlending = userDefinedBlending;

			renderer->PushState(state);
			QueueLayout(command);
			renderer->DrawQueued();
			renderer->PopState();
		}

		commands.clear();
#endif
	}

	void FlushScreenCommands(bool userDefinedBlending)
	{
		RECOIL_DETAILED_TRACY_ZONE;

		FlushCommands(screenCommands, userDefinedBlending);
	}

	void FlushWorldCommands(bool userDefinedBlending)
	{
		RECOIL_DETAILED_TRACY_ZONE;

		FlushCommands(worldCommands, userDefinedBlending);
	}

	fonts::text::LayoutOptions MakeLayoutOptions(float x, float y, float size, FontOption options) const
	{
		RECOIL_DETAILED_TRACY_ZONE;

		fonts::text::LayoutOptions layoutOptions;
		layoutOptions.options = NormalizeOptions(options);
		layoutOptions.fontSize = std::max(size, 1.0f);
		layoutOptions.x = x;
		layoutOptions.y = y;
		layoutOptions.z = 0.0f;
		layoutOptions.parseColorCodes = true;
		layoutOptions.preserveLineBreaks = true;
		layoutOptions.preloadGlyphs = true;
		return layoutOptions;
	}

	fonts::render::FontRenderState MakeScreenState(FontOption options, bool buffered) const
	{
		RECOIL_DETAILED_TRACY_ZONE;

		fonts::render::FontRenderState state;
		state.primaryColor = ToFontColor(textColor);
		state.outlineColor = ToFontColor(outlineColor);
		state.depth = depth;
		state.modelViewMatrix = viewMatrix;
		state.projectionMatrix = projMatrix;
		state.hasModelViewMatrix = true;
		state.hasProjectionMatrix = true;
		state.useCurrentGLMatrices = useCurrentGLMatrices;
		state.useWorldSpace = false;
		state.normalizedCoordinates = false;
		state.useColorAtlas = glyphCache->HasColorGlyphs();
		state.useOutline = HasOption(options, FontOption::Outline);
		state.useShadow = HasOption(options, FontOption::Shadow);
		state.buffered = buffered;
		state.rawPrimaryColor = nullptr;
		state.rawOutlineColor = nullptr;
		return state;
	}

	fonts::render::FontRenderState MakeWorldState(FontOption options, bool buffered)
	{
		RECOIL_DETAILED_TRACY_ZONE;

		fonts::render::FontRenderState state;
		state.primaryColor = ToFontColor(textColor);
		state.outlineColor = ToFontColor(outlineColor);
		state.depth = depth;
		state.useWorldSpace = true;
		state.normalizedCoordinates = false;
		state.useColorAtlas = glyphCache->HasColorGlyphs();
		state.useOutline = HasOption(options, FontOption::Outline);
		state.useShadow = HasOption(options, FontOption::Shadow);
		state.buffered = buffered;
		state.useCurrentGLMatrices = useCurrentGLMatrices;

		CCamera* activeCam = CCamera::GetActive();
		const bool useSyncedMatrices =
			useCurrentGLMatrices ||
			(viewMatrix != CglFont::DefViewMatrix()) ||
			(projMatrix != CglFont::DefProjMatrix());

		if (activeCam != nullptr) {
			worldViewMatrix = useSyncedMatrices ? viewMatrix : activeCam->GetViewMatrix();
			worldProjMatrix = useSyncedMatrices ? projMatrix : activeCam->GetProjectionMatrix();
			state.modelViewMatrix = worldViewMatrix;
			state.projectionMatrix = worldProjMatrix;
			state.localTransformMatrix = activeCam->GetBillBoardMatrix();
			state.hasModelViewMatrix = true;
			state.hasProjectionMatrix = true;
			state.hasLocalTransformMatrix = true;
		} else {
			state.modelViewMatrix = viewMatrix;
			state.projectionMatrix = projMatrix;
			state.hasModelViewMatrix = true;
			state.hasProjectionMatrix = true;
		}

		return state;
	}

	void QueueLayout(const RenderCommand& command)
	{
		RECOIL_DETAILED_TRACY_ZONE;

		std::vector<const fonts::text::LaidOutRun*> orderedRuns;
		for (const auto& line: command.layout.lines) {
			for (const auto& run: line.runs)
				orderedRuns.emplace_back(&run);
		}

		std::size_t runIndex = 0;
		float4 currentTextColor = command.baseTextColor;
		float4 currentOutlineColor = command.baseOutlineColor;

		for (const auto& span: command.layout.spans) {
			if (span.isControl) {
				fonts::text::ParsedColorCode parsed;
				if (!fonts::text::TryParseColorCode(span.text, 0, &parsed, true))
					continue;

				if (parsed.usesLegacyIndicator && fontHandler.disableOldColorIndicators)
					continue;

				if (parsed.IsReset()) {
					currentTextColor = command.baseTextColor;
					currentOutlineColor = command.baseOutlineColor;
					continue;
				}

				if (parsed.hasTextColor) {
					currentTextColor = ToFloat4(parsed.textColor);

					if (command.autoOutlineColor && !parsed.hasOutlineColor)
						currentOutlineColor = ChooseOutlineColor(currentTextColor);
				}

				if (parsed.hasOutlineColor)
					currentOutlineColor = ToFloat4(parsed.outlineColor);

				continue;
			}

			if (span.isLineBreak)
				continue;

			while (runIndex < orderedRuns.size() && SameSpan(orderedRuns[runIndex]->sourceSpan, span)) {
				QueueRun(*orderedRuns[runIndex], command, currentTextColor, currentOutlineColor);
				++runIndex;
			}
		}
	}

	void QueueRun(const fonts::text::LaidOutRun& run, const RenderCommand& command, const float4& currentTextColor, const float4& currentOutlineColor)
	{
		RECOIL_DETAILED_TRACY_ZONE;

		const float outlineExpand = (descriptor.pixelSize > 0)
			? (command.renderSize * descriptor.outlineSize / static_cast<float>(descriptor.pixelSize))
			: 0.0f;
		const float shadowShift = command.drawShadow ? (command.renderSize * 0.1f) : 0.0f;

		for (const auto& glyph: run.glyphs) {
			if (!glyph.visible || (glyph.atlasUV.Empty() && glyph.slugInfo.Empty()))
				continue;

			const GlyphRect& bounds = glyph.shaped.metrics.bounds;
			const float x0 = glyph.x + (command.renderSize * bounds.x0());
			const float y0 = glyph.y + (command.renderSize * bounds.y0());
			const float w = command.renderSize * bounds.w;
			const float h = command.renderSize * bounds.h;
			const bool useSlugDecoratedPass = !glyph.slugInfo.Empty();
			const float slugShadowPadding = command.drawShadow ? std::max(shadowShift, outlineExpand * 0.5f) : 0.0f;
			const float slugOutlinePadding = command.drawOutline ? (outlineExpand * 0.5f) : 0.0f;
			const float slugDecoratedPadding = std::max(slugShadowPadding, slugOutlinePadding);

			fonts::render::PreparedGlyphQuad primaryQuad;
			primaryQuad.position = GlyphRect(x0, y0, w, h);
			primaryQuad.atlasUV = glyph.atlasUV;
			primaryQuad.slugInfo = glyph.slugInfo;
			primaryQuad.slugCoordRect = GlyphRect(0.0f, 0.0f, glyph.slugInfo.width, glyph.slugInfo.height);
			primaryQuad.color = ToFontColor(currentTextColor);
			primaryQuad.z = glyph.z;
			primaryQuad.visible = true;
			renderer->AddPrimaryQuad(primaryQuad);

			if ((!command.drawOutline && !command.drawShadow) || (glyph.outlineAtlasUV.Empty() && glyph.slugInfo.Empty()))
				continue;

			if (useSlugDecoratedPass) {
				auto addSlugDecoratedQuad = [&](float offsetX, float offsetY, float quadPadding, float samplePadding) {
					fonts::render::PreparedGlyphQuad decoratedQuad;
					decoratedQuad.position = GlyphRect(
						x0 + offsetX - quadPadding,
						y0 + offsetY + quadPadding,
						w + (2.0f * quadPadding),
						h - (2.0f * quadPadding)
					);
					decoratedQuad.atlasUV = glyph.outlineAtlasUV;
					decoratedQuad.slugInfo = glyph.slugInfo;
					decoratedQuad.slugCoordRect = GlyphRect(
						-samplePadding,
						-samplePadding,
						glyph.slugInfo.width + (2.0f * samplePadding),
						glyph.slugInfo.height + (2.0f * samplePadding)
					);
					decoratedQuad.color = ToFontColor(currentOutlineColor);
					decoratedQuad.z = glyph.z;
					decoratedQuad.visible = true;
					renderer->AddOutlineQuad(decoratedQuad);
				};

				if (command.drawShadow) {
					const float quadPadding = std::max(shadowShift, outlineExpand * 0.25f);
					const float samplePadding = ((command.renderSize > 0.0f) ? (quadPadding / command.renderSize) : 0.0f) * 0.20f;
					addSlugDecoratedQuad(shadowShift, -shadowShift, quadPadding, samplePadding);
				}

				if (command.drawOutline) {
					const float quadPadding = std::max(outlineExpand * 0.75f, 0.0f);
					const float samplePadding = ((command.renderSize > 0.0f) ? (quadPadding / command.renderSize) : 0.0f) * 0.45f;
					addSlugDecoratedQuad(0.0f, 0.0f, quadPadding, samplePadding);
				}
			} else {
				fonts::render::PreparedGlyphQuad decoratedQuad;
				decoratedQuad.position = GlyphRect(
					x0 + shadowShift - outlineExpand,
					y0 - shadowShift + outlineExpand,
					w + (2.0f * outlineExpand),
					h - (2.0f * outlineExpand)
				);
				decoratedQuad.atlasUV = glyph.outlineAtlasUV;
				decoratedQuad.slugInfo = glyph.slugInfo;
				decoratedQuad.color = ToFontColor(currentOutlineColor);
				decoratedQuad.z = glyph.z;
				decoratedQuad.visible = true;
				renderer->AddOutlineQuad(decoratedQuad);
			}
		}
	}

public:
	FontDescriptor descriptor;
	std::shared_ptr<fonts::FontFaceSet> faceSet;
	std::shared_ptr<fonts::GlyphAtlasCache> glyphCache;
	std::shared_ptr<fonts::text::HarfBuzzTextShaper> shaper;
	std::shared_ptr<fonts::text::TextLayouter> layouter;
	std::shared_ptr<fonts::text::TextWrapper> wrapper;
	fonts::render::FontRendererPtr renderer;

	mutable std::recursive_mutex mutex;
	std::vector<RenderCommand> screenCommands;
	std::vector<RenderCommand> worldCommands;

	bool inBeginEndBlock = false;
	bool autoOutlineColor = false;
	bool beginUserDefinedBlending = false;

	float4 textColor = WhiteColor;
	float4 outlineColor = DarkOutlineColor;
	float4 baseTextColor = WhiteColor;
	float4 baseOutlineColor = DarkOutlineColor;
	FontDepth depth{};
	CMatrix44f viewMatrix = CMatrix44f::Identity();
	CMatrix44f projMatrix = CMatrix44f::Identity();
	CMatrix44f worldViewMatrix = CMatrix44f::Identity();
	CMatrix44f worldProjMatrix = CMatrix44f::Identity();
	bool useCurrentGLMatrices = false;
	std::uint32_t debugId = 0;
	std::string familyName;
	std::string styleName;
};

CglFont::CglFont(const std::string& fontFile, int size, int outlineWidth, float outlineWeight)
	: impl(std::make_unique<Impl>(FontDescriptor {fontFile, size > 0 ? size : 14, outlineWidth, outlineWeight}))
{
	if (const auto& primaryFace = impl->faceSet->GetPrimaryFace(); primaryFace != nullptr) {
		impl->familyName = primaryFace->GetFamilyNameString();
		impl->styleName = primaryFace->GetStyleNameString();
	}
}

CglFont::~CglFont() = default;

CglFont::CglFont(CglFont&&) noexcept = default;
CglFont& CglFont::operator=(CglFont&&) noexcept = default;

bool CglFont::LoadConfigFonts()
{
	RECOIL_DETAILED_TRACY_ZONE;
	font = CglFont::LoadFont("", false);
	smallFont = CglFont::LoadFont("", true);

	if (font == nullptr)
		throw content_error("Failed to load FontFile \"" + configHandler->GetString("FontFile") + "\"");

	if (smallFont == nullptr)
		throw content_error("Failed to load SmallFontFile \"" + configHandler->GetString("SmallFontFile") + "\"");

	return true;
}

bool CglFont::LoadCustomFonts(const std::string& smallFontFile, const std::string& largeFontFile)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (auto newFont = CglFont::LoadFont(largeFontFile, false); newFont != nullptr) {
		font = newFont;
		configHandler->SetString("FontFile", newFont->GetFilePath());
		LOG("[%s] loaded large font \"%s\"", __func__, newFont->GetFilePath().c_str());
	}

	if (auto newFont = CglFont::LoadFont(smallFontFile, true); newFont != nullptr) {
		smallFont = newFont;
		configHandler->SetString("SmallFontFile", newFont->GetFilePath());
		LOG("[%s] loaded small font \"%s\"", __func__, newFont->GetFilePath().c_str());
	}

	return true;
}

std::shared_ptr<CglFont> CglFont::LoadFont(const std::string& fontFile, bool small)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const FontDescriptor descriptor = ResolveDescriptor(fontFile, small);
	return LoadFont(descriptor.filePath, descriptor.pixelSize, descriptor.outlineSize, descriptor.outlineWeight);
}

std::shared_ptr<CglFont> CglFont::LoadFont(const std::string& fontFile, int size, int outlineWidth, float outlineWeight)
{
	RECOIL_DETAILED_TRACY_ZONE;
	LOG("[%s] loading font \"%s\"", __func__, fontFile.c_str());
	const std::string normalizedFontFile = NormalizeFontPath(fontFile);

	if (auto existing = FindFont(normalizedFontFile, size, outlineWidth, outlineWeight); existing != nullptr)
		return existing;

	try {
		auto loadedFont = std::make_shared<CglFont>(normalizedFontFile, size, outlineWidth, outlineWeight);
		::GetLoadedFonts().emplace_back(loadedFont);
		return loadedFont;
	} catch (const content_error& ex) {
		LOG_L(L_ERROR, "Failed creating font: %s", ex.what());
		return nullptr;
	}
}

std::shared_ptr<CglFont> CglFont::FindFont(const std::string& fontFile, int size, int outlineWidth, float outlineWeight)
{
	RECOIL_DETAILED_TRACY_ZONE;

	FontDescriptor wanted {NormalizeFontPath(fontFile), size, outlineWidth, outlineWeight};
	auto& loadedFonts = ::GetLoadedFonts();

	for (std::size_t i = 0; i < loadedFonts.size(); ) {
		auto loadedFont = loadedFonts[i].lock();
		if (loadedFont == nullptr) {
			loadedFonts[i] = std::move(loadedFonts.back());
			loadedFonts.pop_back();
			continue;
		}

		if (loadedFont->GetDescriptor() == wanted)
			return loadedFont;

		++i;
	}

	return nullptr;
}

const std::vector<std::weak_ptr<CglFont>>& CglFont::GetLoadedFonts()
{
	return ::GetLoadedFonts();
}

void CglFont::ReallocSystemFontAtlases(bool pre)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (font != nullptr)
		font->impl->glyphCache->ReallocAtlases(pre);

	if (smallFont != nullptr && smallFont != font)
		smallFont->impl->glyphCache->ReallocAtlases(pre);
}

void CglFont::Begin(bool userDefinedBlending)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);

	if (impl->inBeginEndBlock)
		return;

	impl->inBeginEndBlock = true;
	impl->beginUserDefinedBlending = userDefinedBlending;
}

void CglFont::End()
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);

	if (!impl->inBeginEndBlock) {
		LOG_L(L_ERROR, "called End() without Begin()");
		return;
	}

	impl->FlushScreenCommands(impl->beginUserDefinedBlending);
	impl->FlushWorldCommands(impl->beginUserDefinedBlending);
	impl->inBeginEndBlock = false;
	impl->beginUserDefinedBlending = false;
}

void CglFont::DrawBuffered(bool userDefinedBlending)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->FlushScreenCommands(userDefinedBlending);
}

void CglFont::DrawWorldBuffered(bool userDefinedBlending)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->FlushWorldCommands(userDefinedBlending);
}

void CglFont::Print(float x, float y, float size, int options, const std::string& text)
{
	Print(x, y, size, ToFontOptions(static_cast<FontOptionFlags>(options)), text);
}

void CglFont::Print(float x, float y, float size, FontOption options, const std::string& text)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (text.empty())
		return;

	std::scoped_lock lock(impl->mutex);

	options = NormalizeOptions(options);
	const bool buffered = impl->inBeginEndBlock || HasOption(options, FontOption::Buffered);
	const float renderSize = ResolveRenderSize(size, impl->descriptor, options);
	if (renderSize <= 0.0f)
		return;

	if (HasOption(options, FontOption::Norm))
		ConvertNormalizedToPixels(x, y);

	fonts::text::LayoutOptions layoutOptions = impl->MakeLayoutOptions(x, y, renderSize, options);
	fonts::text::TextLayout layout = impl->layouter->LayoutText(text, layoutOptions);

	CglFont::Impl::RenderCommand command;
	command.layout = std::move(layout);
	command.state = impl->MakeScreenState(options, buffered);
	command.baseTextColor = impl->textColor;
	command.baseOutlineColor = impl->outlineColor;
	command.autoOutlineColor = impl->autoOutlineColor;
	command.renderSize = renderSize;
	command.drawOutline = HasOption(options, FontOption::Outline);
	command.drawShadow = HasOption(options, FontOption::Shadow);

	if (!buffered) {
		impl->QueueCommand(std::move(command), false);
		impl->FlushScreenCommands(false);
		return;
	}

	impl->QueueCommand(std::move(command), false);
}

void CglFont::PrintTable(float x, float y, float size, int options, const std::string& text)
{
	PrintTable(x, y, size, ToFontOptions(static_cast<FontOptionFlags>(options)), text);
}

void CglFont::PrintTable(float x, float y, float size, FontOption options, const std::string& text)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (text.empty())
		return;

	std::vector<std::string> colLines;
	std::vector<float> colWidths;
	std::vector<SColor> colColors;

	SColor defaultColor(255, 0, 0);
	SColor currentColor(255, 0, 0);
	for (int i = 0; i < 3; ++i) {
		defaultColor[i + 1] = static_cast<std::uint8_t>(std::clamp(impl->textColor[i] * 255.0f, 0.0f, 255.0f));
		currentColor[i + 1] = defaultColor[i + 1];
	}

	colLines.emplace_back("");
	colColors.emplace_back(defaultColor);

	int col = 0;
	int row = 0;

	for (int pos = 0; pos < static_cast<int>(text.size()); ++pos) {
		const unsigned char c = static_cast<unsigned char>(text[pos]);

		switch (c) {
			case fonts::text::OldColorCodeIndicator:
				if (fontHandler.disableOldColorIndicators) {
					colLines[col] += static_cast<char>(c);
					break;
				}
				[[fallthrough]];
			case fonts::text::ColorCodeIndicator: {
				for (int i = 0; i < 4 && pos < static_cast<int>(text.size()); ++i, ++pos) {
					colLines[col] += text[pos];
					currentColor[i] = static_cast<std::uint8_t>(text[pos]);
				}

				colColors[col] = currentColor;
				pos -= 1;
			} break;

			case fonts::text::OldColorCodeIndicatorEx:
				if (fontHandler.disableOldColorIndicators) {
					colLines[col] += static_cast<char>(c);
					break;
				}
				[[fallthrough]];
			case fonts::text::ColorCodeIndicatorEx:
				assert(false);
				break;

			case '\t': {
				if ((col += 1) >= static_cast<int>(colLines.size())) {
					colLines.emplace_back("");
					for (int i = 0; i < row; ++i)
						colLines[col] += fonts::text::LF;
					colColors.emplace_back(defaultColor);
				}

				if (colColors[col] != currentColor) {
					for (int i = 0; i < 4; ++i)
						colLines[col] += currentColor[i];
					colColors[col] = currentColor;
				}
			} break;

			case fonts::text::CR:
				pos += ((pos + 1) < static_cast<int>(text.size()) && text[pos + 1] == fonts::text::LF);
				[[fallthrough]];
			case fonts::text::LF: {
				for (auto& line: colLines)
					line += fonts::text::LF;

				if (colColors[0] != currentColor) {
					for (int i = 0; i < 4; ++i)
						colLines[0] += currentColor[i];
					colColors[0] = currentColor;
				}

				col = 0;
				row += 1;
			} break;

			default:
				colLines[col] += static_cast<char>(c);
				break;
		}
	}

	colWidths.resize(colLines.size(), 0.0f);

	float totalWidth = 0.0f;
	float maxHeight = 0.0f;
	float minDescender = 0.0f;

	for (std::size_t i = 0; i < colLines.size(); ++i) {
		colWidths[i] = GetTextWidth(colLines[i]);
		totalWidth += colWidths[i];

		float textDescender = 0.0f;
		const float textHeight = GetTextHeight(colLines[i], &textDescender);
		maxHeight = std::max(maxHeight, textHeight);
		minDescender = std::min(minDescender, textDescender);
	}

	float renderSize = ResolveRenderSize(size, impl->descriptor, options);
	if (renderSize <= 0.0f)
		return;
	if (HasOption(options, FontOption::Norm))
		ConvertNormalizedToPixels(x, y);

	options = NormalizeOptions(options);
	if (HasOption(options, FontOption::Center))
		x -= 0.5f * renderSize * totalWidth;
	else if (HasOption(options, FontOption::Right))
		x -= renderSize * totalWidth;

	if (HasOption(options, FontOption::Descender)) {
		y -= renderSize * impl->glyphCache->GetDescender();
	} else if (HasOption(options, FontOption::VCenter)) {
		y -= renderSize * 0.5f * maxHeight;
		y -= renderSize * 0.5f * minDescender;
	} else if (HasOption(options, FontOption::Top)) {
		y -= renderSize * maxHeight;
	} else if (HasOption(options, FontOption::Ascender)) {
		y -= renderSize * (impl->glyphCache->GetDescender() + 1.0f);
	} else if (HasOption(options, FontOption::Bottom)) {
		y -= renderSize * minDescender;
	}

	for (std::size_t i = 0; i < colLines.size(); ++i) {
		Print(x, y, size, (options | FontOption::Baseline) & ~(FontOption::Right | FontOption::Center), colLines[i]);
		x += renderSize * colWidths[i];
	}
}

void CglFont::PrintWorld(const float3& position, float size, const std::string& text, int options)
{
	PrintWorld(position, size, text, ToFontOptions(static_cast<FontOptionFlags>(options)));
}

void CglFont::PrintWorld(const float3& position, float size, const std::string& text, FontOption options)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (text.empty())
		return;

	std::scoped_lock lock(impl->mutex);

	options = NormalizeOptions(options);
	const bool buffered = HasOption(options, FontOption::Buffered);
	const float renderSize = ResolveRenderSize(size, impl->descriptor, options);
	if (renderSize <= 0.0f)
		return;

	CCamera* activeCam = CCamera::GetActive();
	float3 billboardPosition = position;

	if (activeCam != nullptr) {
		CMatrix44f billboardMatrix = activeCam->GetBillBoardMatrix();
		billboardPosition = billboardMatrix.Transpose() * position;
	}

	fonts::text::LayoutOptions layoutOptions = impl->MakeLayoutOptions(billboardPosition.x, billboardPosition.y, renderSize, options);
	layoutOptions.z = billboardPosition.z;
	fonts::text::TextLayout layout = impl->layouter->LayoutText(text, layoutOptions);

	CglFont::Impl::RenderCommand command;
	command.layout = std::move(layout);
	command.state = impl->MakeWorldState(options, buffered);
	command.baseTextColor = impl->textColor;
	command.baseOutlineColor = impl->outlineColor;
	command.autoOutlineColor = impl->autoOutlineColor;
	command.renderSize = renderSize;
	command.drawOutline = HasOption(options, FontOption::Outline);
	command.drawShadow = HasOption(options, FontOption::Shadow);

	if (!buffered) {
		impl->QueueCommand(std::move(command), true);
		impl->FlushWorldCommands(false);
		return;
	}

	impl->QueueCommand(std::move(command), true);
}

float CglFont::GetCharacterWidth(char32_t c) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	return impl->glyphCache->GetGlyphByCodepoint(c).advance;
}

int CglFont::GetSize() const
{
	return impl->descriptor.pixelSize;
}

int CglFont::GetTextureWidth() const
{
	std::scoped_lock lock(impl->mutex);
	return impl->glyphCache->GetAtlasTexture().GetWidth();
}

int CglFont::GetTextureHeight() const
{
	std::scoped_lock lock(impl->mutex);
	return impl->glyphCache->GetAtlasTexture().GetHeight();
}

int CglFont::GetOutlineWidth() const
{
	return impl->descriptor.outlineSize;
}

float CglFont::GetOutlineWeight() const
{
	return impl->descriptor.outlineWeight;
}

float CglFont::GetLineHeight() const
{
	std::scoped_lock lock(impl->mutex);
	return impl->glyphCache->GetLineHeight();
}

float CglFont::GetDescender() const
{
	std::scoped_lock lock(impl->mutex);
	return impl->glyphCache->GetDescender();
}

int CglFont::GetTexture() const
{
	std::scoped_lock lock(impl->mutex);
	return impl->glyphCache->GetAtlasTexture().GetTextureId();
}

const std::string& CglFont::GetFamily() const
{
	return impl->familyName;
}

const std::string& CglFont::GetStyle() const
{
	return impl->styleName;
}

float CglFont::GetTextWidth(const std::string& text) const
{
	return GetTextWidth(toustring(text));
}

float CglFont::GetTextWidth(const spring::u8string& text) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	return impl->layouter->MeasureText(text, impl->MakeLayoutOptions(0.0f, 0.0f, 1.0f, FontOption::Left | FontOption::Baseline)).width;
}

float CglFont::GetTextHeight(const std::string& text, float* descender, int* numLines) const
{
	return GetTextHeight(toustring(text), descender, numLines);
}

float CglFont::GetTextHeight(const spring::u8string& text, float* descender, int* numLines) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);

	const auto measurement = impl->layouter->MeasureText(text, impl->MakeLayoutOptions(0.0f, 0.0f, 1.0f, FontOption::Left | FontOption::Baseline));

	if (descender != nullptr)
		*descender = measurement.descent;

	if (numLines != nullptr)
		*numLines = static_cast<int>(measurement.lineCount);

	return measurement.height;
}

int CglFont::WrapInPlace(std::string& text, float fontSize, float maxWidth, float maxHeight) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);

	fonts::text::WrapOptions wrapOptions;
	wrapOptions.fontSize = fontSize;
	wrapOptions.maxWidth = maxWidth;
	wrapOptions.maxHeight = maxHeight;
	return static_cast<int>(impl->wrapper->WrapInPlace(text, wrapOptions));
}

int CglFont::WrapInPlace(spring::u8string& text, float fontSize, float maxWidth, float maxHeight) const
{
	return WrapInPlace(static_cast<std::string&>(text), fontSize, maxWidth, maxHeight);
}

std::string CglFont::Wrap(const std::string& text, float fontSize, float maxWidth, float maxHeight) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);

	fonts::text::WrapOptions wrapOptions;
	wrapOptions.fontSize = fontSize;
	wrapOptions.maxWidth = maxWidth;
	wrapOptions.maxHeight = maxHeight;
	return impl->wrapper->Wrap(text, wrapOptions);
}

spring::u8string CglFont::Wrap(const spring::u8string& text, float fontSize, float maxWidth, float maxHeight) const
{
	return spring::u8string(Wrap(static_cast<const std::string&>(text), fontSize, maxWidth, maxHeight));
}

std::deque<std::string> CglFont::SplitIntoLines(const spring::u8string& text)
{
	RECOIL_DETAILED_TRACY_ZONE;

	std::deque<std::string> lines;
	std::deque<std::string> colorCodeStack;

	if (text.empty())
		return lines;

	lines.emplace_back("");

	for (int idx = 0, end = static_cast<int>(text.length()); idx < end; ++idx) {
		const char8_t c = text[idx];

		switch (c) {
			case fonts::text::OldColorCodeIndicator:
				if (fontHandler.disableOldColorIndicators) {
					lines.back() += static_cast<char>(c);
					break;
				}
				[[fallthrough]];
			case fonts::text::ColorCodeIndicator:
				if ((idx + 4) <= end) {
					colorCodeStack.emplace_back(text.substr(idx, 4));
					lines.back() += colorCodeStack.back();
					idx += 3;
				}
				break;

			case fonts::text::OldColorCodeIndicatorEx:
				if (fontHandler.disableOldColorIndicators) {
					lines.back() += static_cast<char>(c);
					break;
				}
				[[fallthrough]];
			case fonts::text::ColorCodeIndicatorEx:
				if ((idx + 9) <= end) {
					colorCodeStack.emplace_back(text.substr(idx, 9));
					lines.back() += colorCodeStack.back();
					idx += 8;
				}
				break;

			case fonts::text::ColorResetIndicator:
				if (!colorCodeStack.empty())
					colorCodeStack.pop_back();
				lines.back() += static_cast<char>(c);
				break;

			case fonts::text::CR:
				idx += ((idx + 1) < end && text[idx + 1] == fonts::text::LF);
				[[fallthrough]];
			case fonts::text::LF:
				lines.emplace_back("");
				if (!colorCodeStack.empty())
					lines.back() = colorCodeStack.back();
				break;

			default:
				lines.back() += static_cast<char>(c);
				break;
		}
	}

	return lines;
}

void CglFont::SetAutoOutlineColor(bool enable)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->autoOutlineColor = enable;
}

void CglFont::SetTextColor(const float4* color)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->textColor = (color != nullptr) ? *color : WhiteColor;
}

void CglFont::SetOutlineColor(const float4* color)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->outlineColor = (color != nullptr) ? *color : ChooseOutlineColor(impl->textColor);
}

void CglFont::SetColors(const float4* textColor, const float4* outlineColor)
{
	RECOIL_DETAILED_TRACY_ZONE;
	SetTextColor(textColor);
	SetOutlineColor(outlineColor);
}

void CglFont::SetTextColor(float r, float g, float b, float a)
{
	const float4 color {r, g, b, a};
	SetTextColor(&color);
}

void CglFont::SetOutlineColor(float r, float g, float b, float a)
{
	const float4 color {r, g, b, a};
	SetOutlineColor(&color);
}

void CglFont::SetTextColor(SColor rgba)
{
	const float4 color = rgba;
	SetTextColor(&color);
}

void CglFont::SetOutlineColor(SColor rgba)
{
	const float4 color = rgba;
	SetOutlineColor(&color);
}

void CglFont::SetTextDepth(float z)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->depth.text = z;
}

void CglFont::SetOutlineDepth(float z)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->depth.outline = z;
}

void CglFont::SetDepths(const FontDepth& depth)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	impl->depth = depth;
}

FontDepth CglFont::GetDepths() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);
	return impl->depth;
}

void CglFont::SetViewMatrix(const CMatrix44f& mat)
{
	std::scoped_lock lock(impl->mutex);
	impl->viewMatrix = mat;
}

void CglFont::SetProjMatrix(const CMatrix44f& mat)
{
	std::scoped_lock lock(impl->mutex);
	impl->projMatrix = mat;
}

const CMatrix44f& CglFont::GetViewMatrix() const
{
	return impl->viewMatrix;
}

const CMatrix44f& CglFont::GetProjMatrix() const
{
	return impl->projMatrix;
}

void CglFont::SetUseCurrentGLMatrices(bool enable)
{
	std::scoped_lock lock(impl->mutex);
	impl->useCurrentGLMatrices = enable;
}

bool CglFont::GetUseCurrentGLMatrices() const
{
	std::scoped_lock lock(impl->mutex);
	return impl->useCurrentGLMatrices;
}

CMatrix44f CglFont::DefViewMatrix()
{
	if (globalRendering != nullptr)
		return globalRendering->screenViewMatrix;

	return CMatrix44f::Identity();
}

CMatrix44f CglFont::DefProjMatrix()
{
	if (globalRendering != nullptr)
		return globalRendering->screenProjMatrix;

	return CMatrix44f::ClipOrthoProj01();
}

const std::string& CglFont::GetFilePath() const
{
	return impl->descriptor.filePath;
}

FontDescriptor CglFont::GetDescriptor() const
{
	return impl->descriptor;
}

void CglFont::ScanForWantedGlyphs(const spring::u8string& text)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);

	std::vector<char32_t> codepoints;
	codepoints.reserve(text.size());

	for (int idx = 0, end = static_cast<int>(text.size()); idx < end; ) {
		const char32_t cp = utf8::GetNextChar(text, idx);

		if (cp == fonts::text::ColorResetIndicator)
			continue;

		if ((cp == fonts::text::ColorCodeIndicator) || (cp == fonts::text::ColorCodeIndicatorEx) || (cp == fonts::text::OldColorCodeIndicator) || (cp == fonts::text::OldColorCodeIndicatorEx)) {
			if ((cp == fonts::text::OldColorCodeIndicator || cp == fonts::text::OldColorCodeIndicatorEx) && fontHandler.disableOldColorIndicators) {
				codepoints.emplace_back(cp);
				continue;
			}

			idx = static_cast<int>(fonts::text::SkipColorCodes(text, idx - 1, nullptr, true));
			continue;
		}

		if (cp == fonts::text::CR || cp == fonts::text::LF)
			continue;

		codepoints.emplace_back(cp);
	}

	if (!codepoints.empty())
		impl->glyphCache->EnsureGlyphs(codepoints);
}

void CglFont::GetStats(std::array<size_t, 8>& stats) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::scoped_lock lock(impl->mutex);

	stats.fill(0u);
	if (impl->renderer == nullptr)
		return;

	const auto rendererStats = impl->renderer->GetStats();
	stats[0] = rendererStats.queuedPrimaryQuads;
	stats[1] = rendererStats.primaryTextureUploads;
	stats[2] = rendererStats.drawCalls;
	stats[3] = rendererStats.droppedQuads;
	stats[4] = rendererStats.queuedOutlineQuads;
	stats[5] = rendererStats.outlineTextureUploads;
	stats[6] = rendererStats.statePushes;
	stats[7] = rendererStats.statePops;
}
