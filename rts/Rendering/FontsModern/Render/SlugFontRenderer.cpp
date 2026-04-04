/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "SlugFontRenderer.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "Rendering/Fonts/FontLogSection.h"
#include "Rendering/FontsModern/Glyphs/GlyphAtlasCache.h"
#include "Rendering/GL/VAO.h"
#include "Rendering/GL/VBO.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Shaders/Shader.h"
#include "System/Color.h"
#include "System/Matrix44f.h"

namespace fonts::render {
namespace {

static constexpr const char* vsSlug150 = R"(
#version 150 compatibility
#extension GL_ARB_explicit_attrib_location : enable

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUnionCoord;
layout (location = 2) in vec2 aFillCoordOffset;
layout (location = 3) in vec2 aOutlineCoordOffset;
layout (location = 4) in vec4 aFillBandTransform;
layout (location = 5) in vec4 aOutlineBandTransform;
layout (location = 6) in vec4 aFillColor;
layout (location = 7) in vec4 aOutlineColor;
layout (location = 8) in uvec4 aFillGlyphData;
layout (location = 9) in uvec4 aOutlineGlyphData;

uniform mat4 uLocalTransform;
uniform float uDepthBias;
uniform int uUsePixelAlignedCoordinates;
uniform int uHasLocalTransform;

out Data {
	vec2 vUnionCoord;
	flat vec2 vFillCoordOffset;
	flat vec2 vOutlineCoordOffset;
	flat vec4 vFillBandTransform;
	flat vec4 vOutlineBandTransform;
	vec4 vFillColor;
	vec4 vOutlineColor;
	flat uvec4 vFillGlyphData;
	flat uvec4 vOutlineGlyphData;
};

void main()
{
	vec3 pos = aPos;

	if (uUsePixelAlignedCoordinates != 0)
		pos.xy = floor(pos.xy) + vec2(0.5);

	pos.z += uDepthBias;

	vec4 worldPos = (uHasLocalTransform != 0)? (uLocalTransform * vec4(pos, 1.0)): vec4(pos, 1.0);

	vUnionCoord = aUnionCoord;
	vFillCoordOffset = aFillCoordOffset;
	vOutlineCoordOffset = aOutlineCoordOffset;
	vFillBandTransform = aFillBandTransform;
	vOutlineBandTransform = aOutlineBandTransform;
	vFillColor = aFillColor;
	vOutlineColor = aOutlineColor;
	vFillGlyphData = aFillGlyphData;
	vOutlineGlyphData = aOutlineGlyphData;
	gl_Position = gl_ModelViewProjectionMatrix * worldPos;
}
)";

static constexpr const char* fsSlug150 = R"(
#version 150

uniform sampler2D uCurveTex;
uniform usampler2D uBandTex;
uniform float uGamma;

in Data {
	vec2 vUnionCoord;
	flat vec2 vFillCoordOffset;
	flat vec2 vOutlineCoordOffset;
	flat vec4 vFillBandTransform;
	flat vec4 vOutlineBandTransform;
	vec4 vFillColor;
	vec4 vOutlineColor;
	flat uvec4 vFillGlyphData;
	flat uvec4 vOutlineGlyphData;
};

out vec4 outColor;

const int kLogBandTextureWidth = 12;
const int kBandTextureWidth = 1 << kLogBandTextureWidth;

uvec2 LoadBandTexel(ivec2 coord)
{
	return texelFetch(uBandTex, coord, 0).xy;
}

vec4 LoadCurveTexel(ivec2 coord)
{
	return texelFetch(uCurveTex, coord, 0);
}

uint CalcRootCode(float y1, float y2, float y3)
{
	uint shift = 0U;

	if (y1 < 0.0)
		shift += 1U;

	if (y2 < 0.0)
		shift += 2U;

	if (y3 < 0.0)
		shift += 4U;

	return ((0x2E74U >> shift) & 0x0101U);
}

vec2 SolveHorizPoly(vec4 p12, vec2 p3)
{
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.y;
	float rb = 0.5 / b.y;

	float d = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
	float t1 = (b.y - d) * ra;
	float t2 = (b.y + d) * ra;

	if (abs(a.y) < 1.0 / 65536.0)
		t1 = t2 = p12.y * rb;

	return vec2((a.x * t1 - b.x * 2.0) * t1 + p12.x, (a.x * t2 - b.x * 2.0) * t2 + p12.x);
}

vec2 SolveVertPoly(vec4 p12, vec2 p3)
{
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.x;
	float rb = 0.5 / b.x;

	float d = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
	float t1 = (b.x - d) * ra;
	float t2 = (b.x + d) * ra;

	if (abs(a.x) < 1.0 / 65536.0)
		t1 = t2 = p12.x * rb;

	return vec2((a.y * t1 - b.y * 2.0) * t1 + p12.y, (a.y * t2 - b.y * 2.0) * t2 + p12.y);
}

ivec2 CalcBandLoc(ivec2 glyphLoc, uint offset)
{
	ivec2 bandLoc = ivec2(glyphLoc.x + int(offset), glyphLoc.y);
	bandLoc.y += bandLoc.x >> kLogBandTextureWidth;
	bandLoc.x &= kBandTextureWidth - 1;
	return bandLoc;
}

float CalcCoverage(float xcov, float ycov, float xwgt, float ywgt)
{
	float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
	return clamp(coverage, 0.0, 1.0);
}

float SlugRender(vec2 renderCoord, vec4 bandTransform, uvec4 glyphData)
{
	vec2 emsPerPixel = fwidth(renderCoord);
	vec2 pixelsPerEm = 1.0 / max(emsPerPixel, vec2(1.0 / 65536.0));

	ivec2 bandMax = ivec2(glyphData.zw);
	ivec2 bandIndex = clamp(ivec2(renderCoord * bandTransform.xy + bandTransform.zw), ivec2(0), bandMax);
	ivec2 glyphLoc = ivec2(glyphData.xy);

	float xcov = 0.0;
	float xwgt = 0.0;

	uvec2 hbandData = LoadBandTexel(CalcBandLoc(glyphLoc, uint(bandIndex.y)));
	ivec2 hbandLoc = CalcBandLoc(glyphLoc, hbandData.y);

	for (int curveIndex = 0; curveIndex < int(hbandData.x); ++curveIndex) {
		ivec2 curveLoc = ivec2(LoadBandTexel(CalcBandLoc(hbandLoc, uint(curveIndex))));
		vec4 p12 = LoadCurveTexel(curveLoc) - vec4(renderCoord, renderCoord);
		vec2 p3 = LoadCurveTexel(curveLoc + ivec2(1, 0)).xy - renderCoord;

		if (max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5)
			break;

		uint code = CalcRootCode(p12.y, p12.w, p3.y);
		if (code == 0U)
			continue;

		vec2 r = SolveHorizPoly(p12, p3) * pixelsPerEm.x;

		if ((code & 1U) != 0U) {
			xcov += clamp(r.x + 0.5, 0.0, 1.0);
			xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
		}

		if (code > 1U) {
			xcov -= clamp(r.y + 0.5, 0.0, 1.0);
			xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
		}
	}

	float ycov = 0.0;
	float ywgt = 0.0;

	uvec2 vbandData = LoadBandTexel(CalcBandLoc(glyphLoc, uint(bandMax.y + 1 + bandIndex.x)));
	ivec2 vbandLoc = CalcBandLoc(glyphLoc, vbandData.y);

	for (int curveIndex = 0; curveIndex < int(vbandData.x); ++curveIndex) {
		ivec2 curveLoc = ivec2(LoadBandTexel(CalcBandLoc(vbandLoc, uint(curveIndex))));
		vec4 p12 = LoadCurveTexel(curveLoc) - vec4(renderCoord, renderCoord);
		vec2 p3 = LoadCurveTexel(curveLoc + ivec2(1, 0)).xy - renderCoord;

		if (max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5)
			break;

		uint code = CalcRootCode(p12.x, p12.z, p3.x);
		if (code == 0U)
			continue;

		vec2 r = SolveVertPoly(p12, p3) * pixelsPerEm.y;

		if ((code & 1U) != 0U) {
			ycov -= clamp(r.x + 0.5, 0.0, 1.0);
			ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
		}

		if (code > 1U) {
			ycov += clamp(r.y + 0.5, 0.0, 1.0);
			ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
		}
	}

	return CalcCoverage(xcov, ycov, xwgt, ywgt);
}

void main()
{
	vec2 fillRenderCoord = vUnionCoord - vFillCoordOffset;
	vec2 outlineRenderCoord = vUnionCoord - vOutlineCoordOffset;

	float rawFillCoverage = clamp(SlugRender(fillRenderCoord, vFillBandTransform, vFillGlyphData), 0.0, 1.0);
	float rawOuterCoverage = clamp(SlugRender(outlineRenderCoord, vOutlineBandTransform, vOutlineGlyphData), 0.0, 1.0);
	float rawOutlineCoverage = clamp(rawOuterCoverage - rawFillCoverage, 0.0, 1.0);
	float invGamma = 1.0 / max(uGamma, 0.0001);

	float fillCoverage = pow(rawFillCoverage, invGamma);
	float outlineCoverage = pow(rawOutlineCoverage, invGamma);
	float fillAlpha = clamp(vFillColor.a * fillCoverage, 0.0, 1.0);
	float outlineAlpha = clamp(vOutlineColor.a * outlineCoverage, 0.0, 1.0);
	float compositeAlpha = fillAlpha + outlineAlpha * (1.0 - fillAlpha);
	vec3 compositePremul = vFillColor.rgb * fillAlpha + vOutlineColor.rgb * outlineAlpha * (1.0 - fillAlpha);
	vec3 compositeColor = (compositeAlpha > 0.0)? (compositePremul / compositeAlpha): vec3(0.0);

	outColor = vec4(compositeColor, compositeAlpha);
}
)";

static constexpr const char* vsSlug130 = R"(
#version 130

in vec3 aPos;
in vec2 aUnionCoord;
in vec2 aFillCoordOffset;
in vec2 aOutlineCoordOffset;
in vec4 aFillBandTransform;
in vec4 aOutlineBandTransform;
in vec4 aFillColor;
in vec4 aOutlineColor;
in uvec4 aFillGlyphData;
in uvec4 aOutlineGlyphData;

uniform mat4 uLocalTransform;
uniform float uDepthBias;
uniform int uUsePixelAlignedCoordinates;
uniform int uHasLocalTransform;

out vec2 vUnionCoord;
flat out vec2 vFillCoordOffset;
flat out vec2 vOutlineCoordOffset;
flat out vec4 vFillBandTransform;
flat out vec4 vOutlineBandTransform;
out vec4 vFillColor;
out vec4 vOutlineColor;
flat out uvec4 vFillGlyphData;
flat out uvec4 vOutlineGlyphData;

void main()
{
	vec3 pos = aPos;

	if (uUsePixelAlignedCoordinates != 0)
		pos.xy = floor(pos.xy) + vec2(0.5);

	pos.z += uDepthBias;

	vec4 worldPos = (uHasLocalTransform != 0)? (uLocalTransform * vec4(pos, 1.0)): vec4(pos, 1.0);

	vUnionCoord = aUnionCoord;
	vFillCoordOffset = aFillCoordOffset;
	vOutlineCoordOffset = aOutlineCoordOffset;
	vFillBandTransform = aFillBandTransform;
	vOutlineBandTransform = aOutlineBandTransform;
	vFillColor = aFillColor;
	vOutlineColor = aOutlineColor;
	vFillGlyphData = aFillGlyphData;
	vOutlineGlyphData = aOutlineGlyphData;
	gl_Position = gl_ModelViewProjectionMatrix * worldPos;
}
)";

static constexpr const char* fsSlug130 = R"(
#version 130

uniform sampler2D uCurveTex;
uniform usampler2D uBandTex;
uniform float uGamma;

in vec2 vUnionCoord;
flat in vec2 vFillCoordOffset;
flat in vec2 vOutlineCoordOffset;
flat in vec4 vFillBandTransform;
flat in vec4 vOutlineBandTransform;
in vec4 vFillColor;
in vec4 vOutlineColor;
flat in uvec4 vFillGlyphData;
flat in uvec4 vOutlineGlyphData;

const int kLogBandTextureWidth = 12;
const int kBandTextureWidth = 1 << kLogBandTextureWidth;

uvec2 LoadBandTexel(ivec2 coord)
{
	return texelFetch(uBandTex, coord, 0).xy;
}

vec4 LoadCurveTexel(ivec2 coord)
{
	return texelFetch(uCurveTex, coord, 0);
}

uint CalcRootCode(float y1, float y2, float y3)
{
	uint shift = 0U;

	if (y1 < 0.0)
		shift += 1U;

	if (y2 < 0.0)
		shift += 2U;

	if (y3 < 0.0)
		shift += 4U;

	return ((0x2E74U >> shift) & 0x0101U);
}

vec2 SolveHorizPoly(vec4 p12, vec2 p3)
{
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.y;
	float rb = 0.5 / b.y;

	float d = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
	float t1 = (b.y - d) * ra;
	float t2 = (b.y + d) * ra;

	if (abs(a.y) < 1.0 / 65536.0)
		t1 = t2 = p12.y * rb;

	return vec2((a.x * t1 - b.x * 2.0) * t1 + p12.x, (a.x * t2 - b.x * 2.0) * t2 + p12.x);
}

vec2 SolveVertPoly(vec4 p12, vec2 p3)
{
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.x;
	float rb = 0.5 / b.x;

	float d = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
	float t1 = (b.x - d) * ra;
	float t2 = (b.x + d) * ra;

	if (abs(a.x) < 1.0 / 65536.0)
		t1 = t2 = p12.x * rb;

	return vec2((a.y * t1 - b.y * 2.0) * t1 + p12.y, (a.y * t2 - b.y * 2.0) * t2 + p12.y);
}

ivec2 CalcBandLoc(ivec2 glyphLoc, uint offset)
{
	ivec2 bandLoc = ivec2(glyphLoc.x + int(offset), glyphLoc.y);
	bandLoc.y += bandLoc.x >> kLogBandTextureWidth;
	bandLoc.x &= kBandTextureWidth - 1;
	return bandLoc;
}

float CalcCoverage(float xcov, float ycov, float xwgt, float ywgt)
{
	float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
	return clamp(coverage, 0.0, 1.0);
}

float SlugRender(vec2 renderCoord, vec4 bandTransform, uvec4 glyphData)
{
	vec2 emsPerPixel = fwidth(renderCoord);
	vec2 pixelsPerEm = 1.0 / max(emsPerPixel, vec2(1.0 / 65536.0));

	ivec2 bandMax = ivec2(glyphData.zw);
	ivec2 bandIndex = clamp(ivec2(renderCoord * bandTransform.xy + bandTransform.zw), ivec2(0), bandMax);
	ivec2 glyphLoc = ivec2(glyphData.xy);

	float xcov = 0.0;
	float xwgt = 0.0;

	uvec2 hbandData = LoadBandTexel(CalcBandLoc(glyphLoc, uint(bandIndex.y)));
	ivec2 hbandLoc = CalcBandLoc(glyphLoc, hbandData.y);

	for (int curveIndex = 0; curveIndex < int(hbandData.x); ++curveIndex) {
		ivec2 curveLoc = ivec2(LoadBandTexel(CalcBandLoc(hbandLoc, uint(curveIndex))));
		vec4 p12 = LoadCurveTexel(curveLoc) - vec4(renderCoord, renderCoord);
		vec2 p3 = LoadCurveTexel(curveLoc + ivec2(1, 0)).xy - renderCoord;

		if (max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5)
			break;

		uint code = CalcRootCode(p12.y, p12.w, p3.y);
		if (code == 0U)
			continue;

		vec2 r = SolveHorizPoly(p12, p3) * pixelsPerEm.x;

		if ((code & 1U) != 0U) {
			xcov += clamp(r.x + 0.5, 0.0, 1.0);
			xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
		}

		if (code > 1U) {
			xcov -= clamp(r.y + 0.5, 0.0, 1.0);
			xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
		}
	}

	float ycov = 0.0;
	float ywgt = 0.0;

	uvec2 vbandData = LoadBandTexel(CalcBandLoc(glyphLoc, uint(bandMax.y + 1 + bandIndex.x)));
	ivec2 vbandLoc = CalcBandLoc(glyphLoc, vbandData.y);

	for (int curveIndex = 0; curveIndex < int(vbandData.x); ++curveIndex) {
		ivec2 curveLoc = ivec2(LoadBandTexel(CalcBandLoc(vbandLoc, uint(curveIndex))));
		vec4 p12 = LoadCurveTexel(curveLoc) - vec4(renderCoord, renderCoord);
		vec2 p3 = LoadCurveTexel(curveLoc + ivec2(1, 0)).xy - renderCoord;

		if (max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5)
			break;

		uint code = CalcRootCode(p12.x, p12.z, p3.x);
		if (code == 0U)
			continue;

		vec2 r = SolveVertPoly(p12, p3) * pixelsPerEm.y;

		if ((code & 1U) != 0U) {
			ycov -= clamp(r.x + 0.5, 0.0, 1.0);
			ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
		}

		if (code > 1U) {
			ycov += clamp(r.y + 0.5, 0.0, 1.0);
			ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
		}
	}

	return CalcCoverage(xcov, ycov, xwgt, ywgt);
}

void main()
{
	vec2 fillRenderCoord = vUnionCoord - vFillCoordOffset;
	vec2 outlineRenderCoord = vUnionCoord - vOutlineCoordOffset;

	float rawFillCoverage = clamp(SlugRender(fillRenderCoord, vFillBandTransform, vFillGlyphData), 0.0, 1.0);
	float rawOuterCoverage = clamp(SlugRender(outlineRenderCoord, vOutlineBandTransform, vOutlineGlyphData), 0.0, 1.0);
	float rawOutlineCoverage = clamp(rawOuterCoverage - rawFillCoverage, 0.0, 1.0);
	float invGamma = 1.0 / max(uGamma, 0.0001);

	float fillCoverage = pow(rawFillCoverage, invGamma);
	float outlineCoverage = pow(rawOutlineCoverage, invGamma);
	float fillAlpha = clamp(vFillColor.a * fillCoverage, 0.0, 1.0);
	float outlineAlpha = clamp(vOutlineColor.a * outlineCoverage, 0.0, 1.0);
	float compositeAlpha = fillAlpha + outlineAlpha * (1.0 - fillAlpha);
	vec3 compositePremul = vFillColor.rgb * fillAlpha + vOutlineColor.rgb * outlineAlpha * (1.0 - fillAlpha);
	vec3 compositeColor = (compositeAlpha > 0.0)? (compositePremul / compositeAlpha): vec3(0.0);

	gl_FragColor = vec4(compositeColor, compositeAlpha);
}
)";

[[nodiscard]] FontColor ToFontColor(const SColor& color) noexcept
{
	constexpr float inv = 1.0f / 255.0f;
	return FontColor {color.r * inv, color.g * inv, color.b * inv, color.a * inv};
}

[[nodiscard]] const FontRenderState* GetActiveState(const std::vector<FontRenderState>& stateStack) noexcept
{
	return stateStack.empty() ? nullptr : &stateStack.back();
}

[[nodiscard]] bool IsCompilingDisplayList() noexcept
{
	GLint listIndex = 0;
	glGetIntegerv(GL_LIST_INDEX, &listIndex);
	return (listIndex != 0);
}

[[nodiscard]] FontColor ResolveGlyphColor(const FontRenderState* state, bool outlinePass) noexcept
{
	if (state == nullptr)
		return {};

	if (outlinePass) {
		if (state->rawOutlineColor != nullptr)
			return ToFontColor(*state->rawOutlineColor);

		return state->outlineColor;
	}

	if (state->rawPrimaryColor != nullptr)
		return ToFontColor(*state->rawPrimaryColor);

	return state->primaryColor;
}

[[nodiscard]] float ResolveGlyphDepth(const fonts::text::LaidOutGlyph& glyph, const FontRenderState* state) noexcept
{
	if (glyph.z != 0.0f)
		return glyph.z;

	if (state == nullptr)
		return 0.0f;

	return state->depth.text;
}

[[nodiscard]] FontColor TransparentColor(const FontColor& color) noexcept
{
	return FontColor {color.r, color.g, color.b, 0.0f};
}

[[nodiscard]] PreparedGlyphQuad MakePreparedGlyphQuad(const fonts::text::LaidOutGlyph& glyph, const FontRenderState* state)
{
	PreparedGlyphQuad quad;
	quad.fillSlugInfo = glyph.slugFillInfo;
	quad.outlineSlugInfo = glyph.slugOutlineInfo.Empty() ? glyph.slugFillInfo : glyph.slugOutlineInfo;
	quad.fillColor = ResolveGlyphColor(state, false);
	quad.outlineColor = glyph.usesOutline ? ResolveGlyphColor(state, true) : TransparentColor(ResolveGlyphColor(state, true));
	quad.z = ResolveGlyphDepth(glyph, state);
	quad.visible = glyph.visible && !quad.fillSlugInfo.Empty();

	if (!quad.visible)
		return quad;

	const float unionMinX = std::min(quad.fillSlugInfo.offsetX, quad.outlineSlugInfo.offsetX);
	const float unionMinY = std::min(quad.fillSlugInfo.offsetY, quad.outlineSlugInfo.offsetY);
	const float unionMaxX = std::max(quad.fillSlugInfo.offsetX + quad.fillSlugInfo.width, quad.outlineSlugInfo.offsetX + quad.outlineSlugInfo.width);
	const float unionMaxY = std::max(quad.fillSlugInfo.offsetY + quad.fillSlugInfo.height, quad.outlineSlugInfo.offsetY + quad.outlineSlugInfo.height);

	quad.position = GlyphRect(
		glyph.x + unionMinX,
		glyph.y + unionMaxY,
		unionMaxX - unionMinX,
		-(unionMaxY - unionMinY)
	);
	return quad;
}

[[nodiscard]] GLenum GetBufferUsage(const SlugFontRenderer::CreateOptions& options) noexcept
{
	return options.bufferedRendering ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW;
}

} // namespace

SlugFontRenderer::SlugFontRenderer()
	: SlugFontRenderer(CreateOptions {})
{}

SlugFontRenderer::SlugFontRenderer(const CreateOptions& options)
	: createOptions(options)
{
#ifndef HEADLESS
	EnsureInitialized();
#endif
}

SlugFontRenderer::~SlugFontRenderer() = default;

SlugFontRenderer::SlugFontRenderer(SlugFontRenderer&& other) noexcept
{
	*this = std::move(other);
}

SlugFontRenderer& SlugFontRenderer::operator=(SlugFontRenderer&& other) noexcept
{
	if (this == &other)
		return *this;

	createOptions = other.createOptions;
	uniformState = other.uniformState;
	curveTextureBinding = other.curveTextureBinding;
	bandTextureBinding = other.bandTextureBinding;
	stats = other.stats;
	stateStack = std::move(other.stateStack);
	programResources = std::move(other.programResources);
	slugBatch = std::move(other.slugBatch);
	slugBuffers = std::move(other.slugBuffers);
	initialized = other.initialized;

	other.curveTextureBinding = {};
	other.bandTextureBinding = {};
	other.stats = {};
	other.stateStack.clear();
	other.initialized = false;
	return *this;
}

void SlugFontRenderer::AddPrimaryQuad(const PreparedGlyphQuad& quad)
{
	QueueQuad(slugBatch, quad);

	if (createOptions.enableStatistics && !quad.Empty())
		stats.queuedPrimaryQuads += 1;
}

void SlugFontRenderer::AddOutlineQuad(const PreparedGlyphQuad& quad)
{
	QueueQuad(slugBatch, quad);

	if (createOptions.enableStatistics && !quad.Empty())
		stats.queuedOutlineQuads += 1;
}

void SlugFontRenderer::AddPrimaryGlyph(const fonts::text::LaidOutGlyph& glyph)
{
	AddPrimaryQuad(MakePreparedGlyphQuad(glyph, GetActiveState(stateStack)));
}

void SlugFontRenderer::AddOutlineGlyph(const fonts::text::LaidOutGlyph& glyph)
{
	(void)glyph;
}

void SlugFontRenderer::DrawQueued()
{
	if (slugBatch.quadCount == 0)
		return;

	bool autoPushedState = false;
	if (createOptions.autoPushPopState && stateStack.empty()) {
		PushState(FontRenderState {});
		autoPushedState = true;
	}

#ifndef HEADLESS
	if (globalRendering == nullptr) {
		if (createOptions.enableStatistics)
			stats.droppedQuads += slugBatch.quadCount;

		ClearQueuedBatches();

		if (autoPushedState)
			PopState();

		return;
	}

	GLint prevProgram = 0;
	GLint prevActiveTexture = GL_TEXTURE0;
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
#endif

	EnsureInitialized();

	if (!IsValid()) {
		if (createOptions.enableStatistics)
			stats.droppedQuads += slugBatch.quadCount;

#ifndef HEADLESS
		glUseProgram(static_cast<GLuint>(prevProgram));
		glActiveTexture(prevActiveTexture);
		glPopAttrib();
#endif

		ClearQueuedBatches();

		if (autoPushedState)
			PopState();

		return;
	}

#ifndef HEADLESS
	const FontRenderState* state = GetActiveState(stateStack);
	const bool useClientSideSubmission = IsCompilingDisplayList();

	if (!useClientSideSubmission)
		UploadBatch(slugBatch, slugBuffers);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);

	if (state == nullptr || !state->userDefinedBlending)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	programResources.program->Enable();
	SubmitBatch(slugBatch, slugBuffers);
	programResources.program->Disable();
	glActiveTexture(GL_TEXTURE0 + std::max(curveTextureBinding.textureUnit, 0));
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + std::max(bandTextureBinding.textureUnit, 0));
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(static_cast<GLuint>(prevProgram));
	glActiveTexture(prevActiveTexture);

	glPopAttrib();
#endif

	ClearQueuedBatches();

	if (autoPushedState)
		PopState();
}

void SlugFontRenderer::HandleGlyphCacheUpdate(fonts::GlyphAtlasCache& glyphCache, bool onlyUpload)
{
	GLint listIndex = 0;
#ifndef HEADLESS
	glGetIntegerv(GL_LIST_INDEX, &listIndex);
#endif

	if (createOptions.autoUploadTextures && listIndex == 0 && (onlyUpload || glyphCache.NeedsSlugUpload()))
		glyphCache.UploadSlugTextures();

	const auto textureInfo = glyphCache.GetSlugTextureInfo();
	curveTextureBinding.textureId = textureInfo.curveTextureId;
	curveTextureBinding.textureUnit = 0;
	curveTextureBinding.width = textureInfo.curveTextureWidth;
	curveTextureBinding.height = textureInfo.curveTextureHeight;

	bandTextureBinding.textureId = textureInfo.bandTextureId;
	bandTextureBinding.textureUnit = 1;
	bandTextureBinding.width = textureInfo.bandTextureWidth;
	bandTextureBinding.height = textureInfo.bandTextureHeight;

	if (createOptions.enableStatistics) {
		stats.primaryTextureUploads += 1;
		stats.outlineTextureUploads += 1;
	}
}

void SlugFontRenderer::PushState(const FontRenderState& state)
{
	stateStack.push_back(state);

	if (createOptions.enableStatistics)
		stats.statePushes += 1;
}

void SlugFontRenderer::PopState()
{
	if (!stateStack.empty()) {
		stateStack.pop_back();

		if (createOptions.enableStatistics)
			stats.statePops += 1;
	}
}

FontRendererStats SlugFontRenderer::GetStats() const
{
	return stats;
}

void SlugFontRenderer::ClearStats()
{
	stats = {};
}

bool SlugFontRenderer::IsValid() const
{
#ifdef HEADLESS
	return false;
#else
	return (programResources.program != nullptr) && programResources.program->IsValid();
#endif
}

void SlugFontRenderer::EnsureInitialized()
{
	if (initialized)
		return;

#ifdef HEADLESS
	return;
#else
	if (globalRendering == nullptr)
		return;

	InitializeProgram();
	InitializeBuffers();
	initialized = IsValid() && (slugBuffers.vbo != nullptr);
#endif
}

void SlugFontRenderer::InitializeProgram()
{
#ifdef HEADLESS
	return;
#else
	if (programResources.program != nullptr)
		return;

	auto program = std::make_unique<Shader::GLSLProgramObject>("[FontModern::SlugFontRenderer]");

	if (!globalRendering->supportExplicitAttribLoc) {
		program->BindAttribLocation("aPos", 0);
		program->BindAttribLocation("aUnionCoord", 1);
		program->BindAttribLocation("aFillCoordOffset", 2);
		program->BindAttribLocation("aOutlineCoordOffset", 3);
		program->BindAttribLocation("aFillBandTransform", 4);
		program->BindAttribLocation("aOutlineBandTransform", 5);
		program->BindAttribLocation("aFillColor", 6);
		program->BindAttribLocation("aOutlineColor", 7);
		program->BindAttribLocation("aFillGlyphData", 8);
		program->BindAttribLocation("aOutlineGlyphData", 9);
	}

	program->AttachShaderObject(new Shader::GLSLShaderObject(GL_VERTEX_SHADER, globalRendering->supportExplicitAttribLoc ? vsSlug150 : vsSlug130));
	program->AttachShaderObject(new Shader::GLSLShaderObject(GL_FRAGMENT_SHADER, globalRendering->supportExplicitAttribLoc ? fsSlug150 : fsSlug130));
	program->Link();

	if (!program->IsValid()) {
		LOG_L(L_WARNING, "[SlugFontRenderer::%s] Failed to link Slug font shader program", __func__);
		return;
	}

	program->Enable();
	static_cast<Shader::IProgramObject*>(program.get())->SetUniform("uCurveTex", 0);
	static_cast<Shader::IProgramObject*>(program.get())->SetUniform("uBandTex", 1);
	program->Disable();

	if (!program->Validate()) {
		LOG_L(L_WARNING, "[SlugFontRenderer::%s] Slug font shader validation failed", __func__);
		return;
	}

	programResources.program = std::move(program);
#endif
}

void SlugFontRenderer::InitializeBuffers()
{
#ifdef HEADLESS
	return;
#else
	auto initBufferResources = [this](BufferResources& bufferResources) {
		if (bufferResources.vbo == nullptr)
			bufferResources.vbo = std::make_unique<VBO>(GL_ARRAY_BUFFER, false);

		if (VAO::IsSupported() && bufferResources.vao == nullptr)
			bufferResources.vao = std::make_unique<VAO>();

		constexpr std::size_t bootstrapVertexCapacity = 64u;

		bufferResources.vbo->Bind();
		bufferResources.vbo->New(bootstrapVertexCapacity * sizeof(Vertex), GetBufferUsage(createOptions), nullptr);
		bufferResources.vbo->Unbind();
		bufferResources.uploadedVertexCapacity = bootstrapVertexCapacity;

		ConfigureVertexAttributes(bufferResources);
	};

	initBufferResources(slugBuffers);
#endif
}

void SlugFontRenderer::ConfigureVertexAttributes(BufferResources& bufferResources)
{
	if (bufferResources.vbo == nullptr)
		return;

	if (bufferResources.vao != nullptr)
		bufferResources.vao->Bind();

	bufferResources.vbo->Bind();

	for (GLuint attribIndex = 0; attribIndex < 10u; ++attribIndex)
		glEnableVertexAttribArray(attribIndex);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, posX)));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, unionX)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillCoordOffsetX)));
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineCoordOffsetX)));
	glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillBandScaleX)));
	glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineBandScaleX)));
	glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillColorR)));
	glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineColorR)));
	glVertexAttribIPointer(8, 4, GL_UNSIGNED_INT, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillGlyphX)));
	glVertexAttribIPointer(9, 4, GL_UNSIGNED_INT, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineGlyphX)));

	bufferResources.vbo->Unbind();

	if (bufferResources.vao != nullptr)
		bufferResources.vao->Unbind();
}

void SlugFontRenderer::EnsureBufferCapacity(RenderBatch& batch, BufferResources& bufferResources)
{
	if (bufferResources.vbo == nullptr)
		return;

	const std::size_t requiredVertices = std::max<std::size_t>(batch.vertices.size(), 1u);
	if (requiredVertices <= bufferResources.uploadedVertexCapacity)
		return;

	const std::size_t newCapacity = std::max<std::size_t>(std::bit_ceil(requiredVertices), 4u);

	bufferResources.vbo->Bind();
	bufferResources.vbo->New(newCapacity * sizeof(Vertex), GetBufferUsage(createOptions), nullptr);
	bufferResources.vbo->Unbind();
	bufferResources.uploadedVertexCapacity = newCapacity;

	ConfigureVertexAttributes(bufferResources);
}

void SlugFontRenderer::QueueQuad(RenderBatch& batch, const PreparedGlyphQuad& quad)
{
	const SlugGlyphInfo& fillInfo = quad.fillSlugInfo;
	const SlugGlyphInfo& outlineInfo = quad.outlineSlugInfo.Empty() ? quad.fillSlugInfo : quad.outlineSlugInfo;

	float x0 = quad.position.x;
	float y0 = quad.position.y;
	float x1 = quad.position.x + quad.position.w;
	float y1 = quad.position.y + quad.position.h;

	const float unionMinX = std::min(fillInfo.offsetX, outlineInfo.offsetX);
	const float unionMinY = std::min(fillInfo.offsetY, outlineInfo.offsetY);
	const float unionMaxX = std::max(fillInfo.offsetX + fillInfo.width, outlineInfo.offsetX + outlineInfo.width);
	const float unionMaxY = std::max(fillInfo.offsetY + fillInfo.height, outlineInfo.offsetY + outlineInfo.height);
	float u0 = 0.0f;
	float v0 = unionMaxY - unionMinY;
	float u1 = unionMaxX - unionMinX;
	float v1 = 0.0f;

	if (x1 < x0) {
		std::swap(x0, x1);
		std::swap(u0, u1);
	}

	if (y1 < y0) {
		std::swap(y0, y1);
		std::swap(v0, v1);
	}

	if (!quad.visible || x0 == x1 || y0 == y1 || fillInfo.Empty()) {
		if (createOptions.enableStatistics)
			stats.droppedQuads += 1;
		return;
	}

	const float fillOffsetX = fillInfo.offsetX - unionMinX;
	const float fillOffsetY = fillInfo.offsetY - unionMinY;
	const float outlineOffsetX = outlineInfo.offsetX - unionMinX;
	const float outlineOffsetY = outlineInfo.offsetY - unionMinY;

	const Vertex tl {
		x0, y0, quad.z,
		u0, v0,
		fillOffsetX, fillOffsetY,
		outlineOffsetX, outlineOffsetY,
		fillInfo.bandScaleX, fillInfo.bandScaleY, fillInfo.bandOffsetX, fillInfo.bandOffsetY,
		outlineInfo.bandScaleX, outlineInfo.bandScaleY, outlineInfo.bandOffsetX, outlineInfo.bandOffsetY,
		quad.fillColor.r, quad.fillColor.g, quad.fillColor.b, quad.fillColor.a,
		quad.outlineColor.r, quad.outlineColor.g, quad.outlineColor.b, quad.outlineColor.a,
		fillInfo.bandTexelX, fillInfo.bandTexelY, fillInfo.bandMaxX, fillInfo.bandMaxY,
		outlineInfo.bandTexelX, outlineInfo.bandTexelY, outlineInfo.bandMaxX, outlineInfo.bandMaxY
	};
	const Vertex tr {
		x1, y0, quad.z,
		u1, v0,
		fillOffsetX, fillOffsetY,
		outlineOffsetX, outlineOffsetY,
		fillInfo.bandScaleX, fillInfo.bandScaleY, fillInfo.bandOffsetX, fillInfo.bandOffsetY,
		outlineInfo.bandScaleX, outlineInfo.bandScaleY, outlineInfo.bandOffsetX, outlineInfo.bandOffsetY,
		quad.fillColor.r, quad.fillColor.g, quad.fillColor.b, quad.fillColor.a,
		quad.outlineColor.r, quad.outlineColor.g, quad.outlineColor.b, quad.outlineColor.a,
		fillInfo.bandTexelX, fillInfo.bandTexelY, fillInfo.bandMaxX, fillInfo.bandMaxY,
		outlineInfo.bandTexelX, outlineInfo.bandTexelY, outlineInfo.bandMaxX, outlineInfo.bandMaxY
	};
	const Vertex br {
		x1, y1, quad.z,
		u1, v1,
		fillOffsetX, fillOffsetY,
		outlineOffsetX, outlineOffsetY,
		fillInfo.bandScaleX, fillInfo.bandScaleY, fillInfo.bandOffsetX, fillInfo.bandOffsetY,
		outlineInfo.bandScaleX, outlineInfo.bandScaleY, outlineInfo.bandOffsetX, outlineInfo.bandOffsetY,
		quad.fillColor.r, quad.fillColor.g, quad.fillColor.b, quad.fillColor.a,
		quad.outlineColor.r, quad.outlineColor.g, quad.outlineColor.b, quad.outlineColor.a,
		fillInfo.bandTexelX, fillInfo.bandTexelY, fillInfo.bandMaxX, fillInfo.bandMaxY,
		outlineInfo.bandTexelX, outlineInfo.bandTexelY, outlineInfo.bandMaxX, outlineInfo.bandMaxY
	};
	const Vertex bl {
		x0, y1, quad.z,
		u0, v1,
		fillOffsetX, fillOffsetY,
		outlineOffsetX, outlineOffsetY,
		fillInfo.bandScaleX, fillInfo.bandScaleY, fillInfo.bandOffsetX, fillInfo.bandOffsetY,
		outlineInfo.bandScaleX, outlineInfo.bandScaleY, outlineInfo.bandOffsetX, outlineInfo.bandOffsetY,
		quad.fillColor.r, quad.fillColor.g, quad.fillColor.b, quad.fillColor.a,
		quad.outlineColor.r, quad.outlineColor.g, quad.outlineColor.b, quad.outlineColor.a,
		fillInfo.bandTexelX, fillInfo.bandTexelY, fillInfo.bandMaxX, fillInfo.bandMaxY,
		outlineInfo.bandTexelX, outlineInfo.bandTexelY, outlineInfo.bandMaxX, outlineInfo.bandMaxY
	};

	batch.vertices.emplace_back(tl);
	batch.vertices.emplace_back(bl);
	batch.vertices.emplace_back(tr);
	batch.vertices.emplace_back(tr);
	batch.vertices.emplace_back(bl);
	batch.vertices.emplace_back(br);

	batch.quadCount += 1;
	batch.dirty = true;
}

void SlugFontRenderer::UploadBatch(RenderBatch& batch, BufferResources& bufferResources)
{
	if (bufferResources.vbo == nullptr || batch.vertices.empty()) {
		batch.dirty = false;
		return;
	}

	if (!batch.dirty)
		return;

	EnsureBufferCapacity(batch, bufferResources);

	bufferResources.vbo->Bind();
	bufferResources.vbo->SetBufferSubData(0, batch.vertices.size() * sizeof(Vertex), batch.vertices.data());
	bufferResources.vbo->Unbind();
	batch.dirty = false;
}

void SlugFontRenderer::SubmitBatch(const RenderBatch& batch, const BufferResources& bufferResources)
{
	if (batch.vertices.empty() || programResources.program == nullptr)
		return;

	if (curveTextureBinding.textureId == 0 || bandTextureBinding.textureId == 0) {
		if (createOptions.enableStatistics)
			stats.droppedQuads += batch.quadCount;
		return;
	}

	BindTextures();
	ApplyUniforms();

	assert(batch.vertices.size() <= static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()));

	if (IsCompilingDisplayList()) {
		GLint prevArrayBuffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		for (GLuint attribIndex = 0; attribIndex < 10u; ++attribIndex)
			glEnableVertexAttribArray(attribIndex);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].posX));
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].unionX));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].fillCoordOffsetX));
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].outlineCoordOffsetX));
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].fillBandScaleX));
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].outlineBandScaleX));
		glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].fillColorR));
		glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), &(batch.vertices[0].outlineColorR));
		glVertexAttribIPointer(8, 4, GL_UNSIGNED_INT, sizeof(Vertex), &(batch.vertices[0].fillGlyphX));
		glVertexAttribIPointer(9, 4, GL_UNSIGNED_INT, sizeof(Vertex), &(batch.vertices[0].outlineGlyphX));

		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));

		for (GLuint attribIndex = 0; attribIndex < 10u; ++attribIndex)
			glDisableVertexAttribArray(attribIndex);
		glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
	} else if (bufferResources.vao != nullptr) {
		bufferResources.vao->Bind();
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
		bufferResources.vao->Unbind();
	} else if (bufferResources.vbo != nullptr) {
		GLint prevArrayBuffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

		bufferResources.vbo->Bind();

		for (GLuint attribIndex = 0; attribIndex < 10u; ++attribIndex)
			glEnableVertexAttribArray(attribIndex);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, posX)));
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, unionX)));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillCoordOffsetX)));
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineCoordOffsetX)));
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillBandScaleX)));
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineBandScaleX)));
		glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillColorR)));
		glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineColorR)));
		glVertexAttribIPointer(8, 4, GL_UNSIGNED_INT, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, fillGlyphX)));
		glVertexAttribIPointer(9, 4, GL_UNSIGNED_INT, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, outlineGlyphX)));

		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));

		for (GLuint attribIndex = 0; attribIndex < 10u; ++attribIndex)
			glDisableVertexAttribArray(attribIndex);
		bufferResources.vbo->Unbind();
		glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
	}

	if (createOptions.enableStatistics)
		stats.drawCalls += 1;
}

void SlugFontRenderer::ApplyUniforms()
{
	if (programResources.program == nullptr)
		return;
	const FontRenderState* state = GetActiveState(stateStack);
	const CMatrix44f localTransform = (state != nullptr && state->hasLocalTransformMatrix)
		? state->localTransformMatrix
		: CMatrix44f::Identity();
	const bool usePixelAlignedCoordinates = uniformState.usePixelAlignedCoordinates ||
		(state != nullptr && state->usePixelAlignedCoordinates);

	programResources.program->SetUniformMatrix4x4("uLocalTransform", false, localTransform.m);
	programResources.program->SetUniform("uDepthBias", uniformState.depth);
	programResources.program->SetUniform("uGamma", uniformState.gamma);
	programResources.program->SetUniform("uUsePixelAlignedCoordinates", static_cast<int>(usePixelAlignedCoordinates));
	programResources.program->SetUniform("uHasLocalTransform", static_cast<int>(state != nullptr && state->hasLocalTransformMatrix));
}

void SlugFontRenderer::BindTextures() const
{
	glActiveTexture(GL_TEXTURE0 + std::max(curveTextureBinding.textureUnit, 0));
	glBindTexture(GL_TEXTURE_2D, curveTextureBinding.textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glActiveTexture(GL_TEXTURE0 + std::max(bandTextureBinding.textureUnit, 0));
	glBindTexture(GL_TEXTURE_2D, bandTextureBinding.textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void SlugFontRenderer::ClearQueuedBatches()
{
	slugBatch.vertices.clear();
	slugBatch.quadCount = 0;
	slugBatch.dirty = false;
}

} // namespace fonts::render
