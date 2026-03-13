//#define FLAT_SHADING

uniform sampler2D shadingTex;
uniform sampler2D grassShadingTex;
uniform sampler2D bladeTex;

#ifdef HAVE_SHADOWS
	uniform sampler2DShadow shadowMap;
	uniform sampler2D shadowColorTex;
	uniform float groundShadowDensity;
	uniform mat4 shadowDepthTransform;
	uniform mat4 shadowViewMat[4];
	uniform vec4 shadowCascadeAtlas[4];
	uniform float shadowCascadeSplits[4];
#endif
uniform float infoTexIntensityMul;

#ifdef HAVE_INFOTEX
	uniform sampler2D infoMap;
#endif

uniform samplerCube specularTex;
uniform vec3 specularLightColor;
uniform vec3 ambientLightColor;
uniform vec3 camDir;

varying vec3 normal;
varying vec4 shadingTexCoords;
varying vec2 bladeTexCoords;
varying vec3 ambientDiffuseLightTerm;
varying vec4 vertexWorldPos;
#if defined(HAVE_SHADOWS) || defined(SHADOW_GEN)
	varying vec4 shadowTexCoords;
#endif

#ifdef HAVE_SHADOWS
int SelectShadowCascade(float depthValue) {
	if (depthValue <= shadowCascadeSplits[0]) return 0;
	if (depthValue <= shadowCascadeSplits[1]) return 1;
	if (depthValue <= shadowCascadeSplits[2]) return 2;
	return 3;
}

vec3 GetCascadeDebugTint(int cascadeIdx) {
	if (cascadeIdx == 0) return vec3(1.0, 0.25, 0.25);
	if (cascadeIdx == 3) return vec3(1.25, 0.25, 1.0);
	return vec3(0.25, 1.0, 0.25);
}

vec4 GetCascadeShadowCoord(vec4 worldPos) {
	float depthValue = -(shadowDepthTransform * worldPos).z;
	int cascadeIdx = SelectShadowCascade(depthValue);

	if (cascadeIdx == 0) {
		vec4 sc = shadowViewMat[0] * worldPos;
		sc.xy += vec2(0.5);
		sc.xy = sc.xy * shadowCascadeAtlas[0].xy + shadowCascadeAtlas[0].zw;
		return sc;
	}
	if (cascadeIdx == 1) {
		vec4 sc = shadowViewMat[1] * worldPos;
		sc.xy += vec2(0.5);
		sc.xy = sc.xy * shadowCascadeAtlas[1].xy + shadowCascadeAtlas[1].zw;
		return sc;
	}
	if (cascadeIdx == 2) {
		vec4 sc = shadowViewMat[2] * worldPos;
		sc.xy += vec2(0.5);
		sc.xy = sc.xy * shadowCascadeAtlas[2].xy + shadowCascadeAtlas[2].zw;
		return sc;
	}

	vec4 sc = shadowViewMat[3] * worldPos;
	sc.xy += vec2(0.5);
	sc.xy = sc.xy * shadowCascadeAtlas[3].xy + shadowCascadeAtlas[3].zw;
	return sc;
}
#endif


void main() {
#ifdef SHADOW_GEN
	{
  #ifdef DISTANCE_FAR
		gl_FragColor = texture2D(bladeTex, bladeTexCoords);
  #else
		gl_FragColor = vec4(1.0);
  #endif
		return;
	}
#endif

	vec4 matColor = texture2D(bladeTex, bladeTexCoords);
	matColor.rgb *= texture2D(grassShadingTex, shadingTexCoords.pq).rgb;
	matColor.rgb *= texture2D(shadingTex, shadingTexCoords.pq).rgb * 2.0;

#if defined(FLAT_SHADING) || defined(DISTANCE_FAR)
	vec3 specular = vec3(0.0);
#else
	vec3 reflectDir = reflect(camDir, normalize(normal));
	vec3 specular   = textureCube(specularTex, reflectDir).rgb;
#endif
	gl_FragColor.rgb = matColor.rgb * ambientDiffuseLightTerm + 0.1 * specular * specularLightColor; //TODO make `0.1` specular distr. customizable?
	gl_FragColor.a   = matColor.a * gl_Color.a;

#ifdef HAVE_SHADOWS
	float depthValue = -(shadowDepthTransform * vertexWorldPos).z;
	int cascadeIdx = SelectShadowCascade(depthValue);
	float shadowCoeff = mix(1.0, shadow2DProj(shadowMap, GetCascadeShadowCoord(vertexWorldPos)).r, groundShadowDensity);

	gl_FragColor.rgb *= mix(ambientLightColor, GetCascadeDebugTint(cascadeIdx), shadowCoeff);
#endif

#ifdef HAVE_INFOTEX
	gl_FragColor.rgb += (texture2D(infoMap, shadingTexCoords.st).rgb * infoTexIntensityMul);
	gl_FragColor.rgb -= (vec3(0.5, 0.5, 0.5) * float(infoTexIntensityMul == 1.0));
#endif

	gl_FragColor.rgb = mix(gl_Fog.color.rgb, gl_FragColor.rgb, gl_FogFragCoord);
}
