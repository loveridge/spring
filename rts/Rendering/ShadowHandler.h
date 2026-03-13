/* ShadowHandler.h */

#ifndef SHADOW_HANDLER_H
#define SHADOW_HANDLER_H

#include <array>
#include <limits>

#include "Rendering/GL/FBO.h"
#include "System/float4.h"
#include "System/Matrix44f.h"

namespace Shader {
	struct IProgramObject;
}

class CCamera;
class CShadowHandler
{
public:
	CShadowHandler()
		: smOpaqFBO(true)
	{}

	void Init();
	void Kill();
	void Reload(const char* argv);
	void Update();

	void SetupShadowTexSampler(unsigned int texUnit, bool enable = false) const;
	void SetupShadowTexSamplerRaw() const;
	void ResetShadowTexSampler(unsigned int texUnit, bool disable = false) const;
	void ResetShadowTexSamplerRaw() const;
	void CreateShadows();

	void EnableColorOutput(bool enable) const;

	enum ShadowGenerationBits {
		SHADOWGEN_BIT_NONE  = 0,
		SHADOWGEN_BIT_MAP   = 2,
		SHADOWGEN_BIT_MODEL = 4,
		SHADOWGEN_BIT_PROJ  = 8,
		SHADOWGEN_BIT_TREE  = 16,
	};

	enum ShadowProjectionMode {
		SHADOWPROMODE_MAP_CENTER = 0,
		SHADOWPROMODE_CAM_CENTER = 1,
		SHADOWPROMODE_MIX_CAMMAP = 2,
	};

	enum ShadowMapSizes {
		MIN_SHADOWMAP_SIZE =   512,
		DEF_SHADOWMAP_SIZE =  2048,
		MAX_SHADOWMAP_SIZE = 16384,
	};

	enum ShadowGenProgram {
		SHADOWGEN_PROGRAM_MODEL      = 0,
		SHADOWGEN_PROGRAM_MODEL_GL4  = 1,
		SHADOWGEN_PROGRAM_MAP        = 2,
		SHADOWGEN_PROGRAM_PROJECTILE = 3,
		SHADOWGEN_PROGRAM_COUNT      = 4,
	};

	enum ShadowMatrixType {
		SHADOWMAT_TYPE_CULLING = 0,
		SHADOWMAT_TYPE_DRAWING = 1,
	};

	static constexpr unsigned int SHADOW_CASCADE_COUNT = 4;

	struct ShadowCascade {
		float splitNear = 0.0f; // normalized [0, 1] along camera depth range
		float splitFar  = 1.0f; // normalized [0, 1] along camera depth range

		float3 projMidPos;
		float4 projScales;

		// xy = atlas scale, zw = atlas bias
		float4 atlasXform = {0.5f, 0.5f, 0.0f, 0.0f};

		CMatrix44f projMatrix[2];
		CMatrix44f viewMatrix[2];
	};

	Shader::IProgramObject* GetShadowGenProg(ShadowGenProgram p) {
		return shadowGenProgs[p];
	}

	// Legacy getters keep cascade 0 for compatibility
	const CMatrix44f& GetShadowMatrix   (unsigned int idx = SHADOWMAT_TYPE_DRAWING) const { return cascades[0].viewMatrix[idx]; }
	const      float* GetShadowMatrixRaw(unsigned int idx = SHADOWMAT_TYPE_DRAWING) const { return &cascades[0].viewMatrix[idx].m[0]; }

	const CMatrix44f& GetShadowViewMatrix(unsigned int idx = SHADOWMAT_TYPE_DRAWING) const { return cascades[0].viewMatrix[idx]; }
	const CMatrix44f& GetShadowProjMatrix(unsigned int idx = SHADOWMAT_TYPE_DRAWING) const { return cascades[0].projMatrix[idx]; }
	const      float* GetShadowViewMatrixRaw(unsigned int idx = SHADOWMAT_TYPE_DRAWING) const { return &cascades[0].viewMatrix[idx].m[0]; }
	const      float* GetShadowProjMatrixRaw(unsigned int idx = SHADOWMAT_TYPE_DRAWING) const { return &cascades[0].projMatrix[idx].m[0]; }

	// New cascade getters
	const CMatrix44f& GetShadowViewMatrix(unsigned int cascadeIdx, unsigned int matIdx) const { return cascades[cascadeIdx].viewMatrix[matIdx]; }
	const CMatrix44f& GetShadowProjMatrix(unsigned int cascadeIdx, unsigned int matIdx) const { return cascades[cascadeIdx].projMatrix[matIdx]; }
	const      float* GetShadowViewMatrixRaw(unsigned int cascadeIdx, unsigned int matIdx) const { return &cascades[cascadeIdx].viewMatrix[matIdx].m[0]; }
	const      float* GetShadowProjMatrixRaw(unsigned int cascadeIdx, unsigned int matIdx) const { return &cascades[cascadeIdx].projMatrix[matIdx].m[0]; }

	const float4& GetShadowParams() const { return shadowTexProjCenter; }

	const float4& GetCascadeAtlasXform(unsigned int cascadeIdx) const { return cascades[cascadeIdx].atlasXform; }
	float GetCascadeSplitFar(unsigned int cascadeIdx) const { return cascades[cascadeIdx].splitFar; }
	unsigned int GetCascadeCount() const { return SHADOW_CASCADE_COUNT; }
	unsigned int GetActiveShadowCascade() const { return activeShadowCascade; }
	void SetActiveShadowCascade(unsigned int cascadeIdx) { activeShadowCascade = cascadeIdx; }
	void SetShadowSamplingUniforms(Shader::IProgramObject* shader) const;

	uint32_t GetShadowTextureID() const { return shadowDepthTexture; }
	uint32_t GetColorTextureID() const { return shadowColorTexture; }

	static bool ShadowsInitialized() { return firstInit; }
	static bool ShadowsSupported() { return shadowsSupported; }

	bool ShadowsLoaded() const { return shadowsLoaded; }
	bool InShadowPass() const { return inShadowPass; }

	void SaveShadowMapTextures() const;
	void DrawFrustumDebug() const;

	bool& DebugFrustumRef() { return debugFrustum; }

private:
	void FreeFBOAndTextures();
	bool InitFBOAndTextures();

	void DrawShadowPasses();
	void LoadProjectionMatrix(const CCamera* shadowCam);
	void LoadShadowGenShaders();

	void ComputeCascadeSplits(CCamera* playerCam);
	void SetShadowMatrix(CCamera* playerCam, CCamera* shadowCam, unsigned int cascadeIdx);
	void SetShadowCamera(CCamera* shadowCam, unsigned int cascadeIdx);

	float4 GetShadowProjectionScales(CCamera* playerCam, const CMatrix44f& lightViewMat, unsigned int cascadeIdx);
	float3 CalcShadowProjectionPos(CCamera* playerCam, float3* frustumPoints, float splitNear, float splitFar);

	float GetOrthoProjectedMapRadius(const float3&, float3&);
	float GetOrthoProjectedFrustumRadius(CCamera*, const CMatrix44f&, float3&, float splitNear, float splitFar);

	float4 GetCascadeAtlasRect(unsigned int cascadeIdx) const;

public:
	int shadowConfig;
	int shadowMapSize;
	int shadowGenBits;
	int shadowProMode;
	int shadowColorMode;

private:
	bool shadowsLoaded = false;
	bool inShadowPass = false;
	bool debugFrustum = false;

	inline static bool firstInit = true;
	inline static bool shadowsSupported = false;

	std::array<Shader::IProgramObject*, SHADOWGEN_PROGRAM_COUNT> shadowGenProgs;

	float3 projMidPos[2 + 1];
	float3 sunProjDir;

	float4 shadowProjScales;

	// retained for compatibility; cascade 0 mirrors these semantics
	CMatrix44f projMatrix[2];
	CMatrix44f viewMatrix[2];

	std::array<ShadowCascade, SHADOW_CASCADE_COUNT> cascades;
	unsigned int activeShadowCascade = 0;

	uint32_t shadowDepthTexture;
	uint32_t shadowColorTexture;

	FBO smOpaqFBO;

	static constexpr float4 shadowTexProjCenter = {
		0.5f,
		0.5f,
		std::numeric_limits<float>::max(),
		1.0f
	};
};

extern CShadowHandler shadowHandler;

#endif
