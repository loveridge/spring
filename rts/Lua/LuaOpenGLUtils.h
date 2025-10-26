/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef LUA_OPENGLUTILS_H
#define LUA_OPENGLUTILS_H

#include <string>
#include <tuple>

#include "System/type2.h"

class CMatrix44f;
class LuaMatTexture;
struct lua_State;

typedef unsigned int GLuint;

enum LuaMatrixType {
	LUAMATRICES_SHADOW,
	LUAMATRICES_VIEW,
	LUAMATRICES_VIEWINVERSE,
	LUAMATRICES_PROJECTION,
	LUAMATRICES_PROJECTIONINVERSE,
	LUAMATRICES_VIEWPROJECTION,
	LUAMATRICES_VIEWPROJECTIONINVERSE,
	LUAMATRICES_BILLBOARD,
	LUAMATRICES_NONE
};


class LuaMatTexture {
	public:
		enum Type {
			LUATEX_NONE = 0,

			LUATEX_NAMED,
			LUATEX_LUATEXTURE,
			LUATEX_LUATEXTUREATLAS,
			LUATEX_UNITTEXTURE1,
			LUATEX_UNITTEXTURE2,
			LUATEX_3DOTEXTURE,
			LUATEX_UNITBUILDPIC,

			LUATEX_MAP_REFLECTION,
			LUATEX_SKY_REFLECTION,
			LUATEX_SPECULAR,


			LUATEX_SHADOWMAP,
			LUATEX_SHADOWCOLOR,
			LUATEX_HEIGHTMAP,

			// NOTE: must be in same order as MAP_BASE_*!
			LUATEX_SMF_GRASS,
			LUATEX_SMF_DETAIL,
			LUATEX_SMF_MINIMAP,
			LUATEX_SMF_SHADING,
			LUATEX_SMF_NORMALS,
			// NOTE: must be in same order as MAP_SSMF_*!
			LUATEX_SSMF_NORMALS,
			LUATEX_SSMF_SPECULAR,
			LUATEX_SSMF_SDISTRIB,
			LUATEX_SSMF_SDETAIL,
			LUATEX_SSMF_SNORMALS,
			LUATEX_SSMF_SKYREFL,
			LUATEX_SSMF_EMISSION,
			LUATEX_SSMF_PARALLAX,

			LUATEX_INFOTEX_SUFFIX,
			LUATEX_INFOTEX_ACTIVE,

			LUATEX_MAP_GBUFFER_NORM,
			LUATEX_MAP_GBUFFER_DIFF,
			LUATEX_MAP_GBUFFER_SPEC,
			LUATEX_MAP_GBUFFER_EMIT,
			LUATEX_MAP_GBUFFER_MISC,
			LUATEX_MAP_GBUFFER_ZVAL,

			LUATEX_MODEL_GBUFFER_NORM,
			LUATEX_MODEL_GBUFFER_DIFF,
			LUATEX_MODEL_GBUFFER_SPEC,
			LUATEX_MODEL_GBUFFER_EMIT,
			LUATEX_MODEL_GBUFFER_MISC,
			LUATEX_MODEL_GBUFFER_ZVAL,

			LUATEX_FONT,
			LUATEX_FONTSMALL,

			LUATEX_EXPLOSIONS_ATLAS,
			LUATEX_GROUNDFX_ATLAS,

			LUATEX_ICONS_ATLAS0,
			LUATEX_ICONS_ATLAS1,
		};

	public:
		LuaMatTexture() = default;

		void Enable(bool b) { enable = b; }
		void Finalize() { /*enableTexParams = true;*/ }
		void Bind() const;
		void Unbind() const;

		void Print(const std::string& indent) const;

		static int Compare(const LuaMatTexture& a, const LuaMatTexture& b);

		bool operator <(const LuaMatTexture& mt) const { return (Compare(*this, mt) <  0); }
		bool operator==(const LuaMatTexture& mt) const { return (Compare(*this, mt) == 0); }
		bool operator!=(const LuaMatTexture& mt) const { return (Compare(*this, mt) != 0); }

	public:
		Type type = LUATEX_NONE;
		GLuint texType = 0;

		const void* data = nullptr;
		      void* state = nullptr;

		bool enable = false;
		// bool enableTexParams = false;

		std::tuple<int, int, int> GetSize() const;
		GLuint GetTextureID() const;
		GLuint GetTextureTarget() const;
};


class LuaOpenGLUtils {
public:
	static void ResetState();

	static const CMatrix44f* GetNamedMatrix(const char* name);

	static LuaMatTexture::Type GetLuaMatTextureType(const std::string& name);
	static LuaMatrixType GetLuaMatrixType(const char* name);

	static bool ParseTextureImage(lua_State* L, LuaMatTexture& texUnit, const std::string& image);
};

#endif // LUA_OPENGLUTILS_H
