/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <array>

#include "Sim/Misc/CollisionHandler.h"
#include "Sim/Misc/CollisionVolume.h"
#include "System/Log/ILog.h"
#include "System/Matrix44f.h"
#include "System/TimeProfiler.h"
#include "System/float3.h"
#include "System/Misc/SpringTime.h"

#include <catch_amalgamated.hpp>

InitSpringTime ist;

namespace {

constexpr float HALF_PI    = 1.57079632679f;
constexpr float QUARTER_PI = 0.78539816339f;

static CollisionVolume MakeBoxVolume(const float3& scales, const float3& offsets = ZeroVector)
{
	CollisionVolume v;
	v.InitBox(scales, offsets);
	return v;
}

static CollisionVolume MakeSphereVolume(const float radius, const float3& offsets = ZeroVector)
{
	CollisionVolume v;
	v.InitSphere(radius);
	v.SetOffsets(offsets);
	return v;
}

static CollisionVolume MakeEllipsoidVolume(const float3& scales, const float3& offsets = ZeroVector)
{
	CollisionVolume v;
	v.InitShape(
		scales,
		offsets,
		CollisionVolume::COLVOL_TYPE_ELLIPSOID,
		CollisionVolume::COLVOL_HITTEST_CONT,
		CollisionVolume::COLVOL_AXIS_Z
	);
	return v;
}

static CollisionVolume MakeCylinderVolume(const float3& scales, const int axis, const float3& offsets = ZeroVector)
{
	CollisionVolume v;
	v.InitShape(
		scales,
		offsets,
		CollisionVolume::COLVOL_TYPE_CYLINDER,
		CollisionVolume::COLVOL_HITTEST_CONT,
		axis
	);
	return v;
}

static CollisionVolume MakePyramidVolume(const float3& scales, const int axis, const float3& offsets = ZeroVector)
{
	CollisionVolume v;
	v.InitShape(
		scales,
		offsets,
		CollisionVolume::COLVOL_TYPE_PYRAMID,
		CollisionVolume::COLVOL_HITTEST_CONT,
		axis
	);
	return v;
}

static CMatrix44f MakeTransform(const float3& pos = ZeroVector, const float3& rotXYZ = ZeroVector)
{
	CMatrix44f m;

	if (rotXYZ.x != 0.0f)
		m.RotateX(rotXYZ.x);
	if (rotXYZ.y != 0.0f)
		m.RotateY(rotXYZ.y);
	if (rotXYZ.z != 0.0f)
		m.RotateZ(rotXYZ.z);

	m.SetPos(pos);
	return m;
}

static bool IntersectsVolume(
	const CollisionVolume& vol,
	const CMatrix44f& mat,
	const CollisionVolume& otherVol,
	const CMatrix44f& otherMat
) {
	return CCollisionHandler::IntersectVolume(&vol, mat, &otherVol, otherMat);
}

} // namespace

// generated with the following in IntersectUnitSelectionVolumeInScreenRect
//  printf("SECTION(\"\") {");
// 	printf("CollisionVolume pyramid;");
// 	printf("pyramid.InitShape(");
// 	printf("%s,",selVol.GetScales().str().c_str());
// 	printf("%s,", selVol.GetOffsets().str().c_str());
// 	printf("%d,", selVol.GetVolumeType());
// 	printf("1,");
// 	printf("%d",selVol.GetPrimaryAxis());
// 	printf(");");
// 	printf("const CMatrix44f pyrMat = {%s};", selMat.str_serialize().c_str());
// 	printf("CollisionVolume testVol;");
// 	printf("testVol.InitShape(");
// 	printf("%s,",unitVol->GetScales().str().c_str());
// 	printf("%s,", unitVol->GetOffsets().str().c_str());
// 	printf("%d,", unitVol->GetVolumeType());
// 	printf("1,");
// 	printf("%d",unitVol->GetPrimaryAxis());
// 	printf(");");
// 	printf("const CMatrix44f testMat = {%s};", unitMat.str_serialize().c_str());
// 	if (result) {
// 	printf("CHECK(IntersectsPyramid(pyramid, pyrMat, testVol, testMat));");
// 	} else {
// 	printf("CHECK_FALSE(IntersectsPyramid(pyramid, pyrMat, testVol, testMat));");
// 	}
// 	printf("}\n");
TEST_CASE("CollisionHandler_PyramidVsCylinder")
{
	SECTION("camera overhead, selection outside corner of cylinder") {CollisionVolume pyramid;pyramid.InitShape(float3(4946.859, 4953.576, 33535.227),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.992f, 0.125f, 0.023f, 0.000f, -0.000f, -0.182f, 0.983f, 0.000f, 0.128f, -0.975f, -0.180f, 0.000f, 2139.423f, -15565.261f, 9268.336f, 1.000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {1.000f, -0.000f, -0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 599.556f, 12284.000f, 1.000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("camera overhead, selection above top") {CollisionVolume pyramid;pyramid.InitShape(float3(13539.558, 3602.985, 33260.023),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {1.000f, -0.005f, -0.001f, 0.000f, -0.000f, -0.181f, 0.983f, 0.000f, -0.005f, -0.983f, -0.181f, 0.000f, -89.609f, -15565.310f, 9272.988f, 1.000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {1.000f, -0.000f, -0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 599.556f, 12284.000f, 1.000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("camera overhead, selection outside left corner of cylinder") {CollisionVolume pyramid;pyramid.InitShape(float3(8782.085, 2429.013, 33692.617),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.980f, -0.196f, -0.027f, 0.000f, -0.000f, -0.135f, 0.991f, 0.000f, -0.198f, -0.971f, -0.132f, 0.000f, -3336.510f, -15573.462f, 10064.639f, 1.000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {1.000f, -0.000f, -0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 599.556f, 12284.000f, 1.000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, selection on left side") {CollisionVolume pyramid;pyramid.InitShape(float3(12662.091, 15218.569, 34043.207),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.963f, -0.271f, 0.001f, 0.000f, -0.000f, 0.004f, 1.000f, 0.000f, -0.271f, -0.963f, 0.004f, 0.000f, -4609.448f, -15597.006f, 12350.267f, 1.000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {1.000f, -0.000f, -0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 599.556f, 12284.000f, 1.000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, selection on bottom") {CollisionVolume pyramid;pyramid.InitShape(float3(14485.936, 5780.520, 33162.523),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {1.000f, -0.031f, 0.004f, 0.000f, -0.000f, 0.140f, 0.990f, 0.000f, -0.032f, -0.990f, 0.140f, 0.000f, -523.282f, -15620.354f, 14616.951f, 1.000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {1.000f, -0.000f, -0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 599.556f, 12284.000f, 1.000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}

	SECTION("cam overhead, selection on top right corner touching") {CollisionVolume pyramid;pyramid.InitShape(float3(4825.485, 4466.979, 33048.695),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99611f, 0.08762f, 0.00936f, 0.00000f, -0.00000f, -0.10627f, 0.99434f, 0.00000f, 0.08812f, -0.99047f, -0.10586f, 0.00000f, 1456.17041f, -15291.25195f, 10543.66895f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, selection on top touching") {CollisionVolume pyramid;pyramid.InitShape(float3(8733.372, 4008.335, 32931.742),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99990f, -0.01423f, -0.00156f, 0.00000f, -0.00000f, -0.10882f, 0.99406f, 0.00000f, -0.01431f, -0.99396f, -0.10881f, 0.00000f, -235.67889f, -15290.82129f, 10501.34180f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, selection complete cover") {CollisionVolume pyramid;pyramid.InitShape(float3(19613.719, 15580.658, 32805.727),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.999f, -0.048f, -0.000f, 0.000f, -0.000f, -0.006f, 1.000f, 0.000f, -0.048f, -0.999f, -0.006f, 0.000f, -781.478f, -15599.557f, 12193.991f, 1.000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {1.000f, -0.000f, -0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 0.000f, 0.000f, 1.000f, 0.000f, 0.000f, 599.556f, 12284.000f, 1.000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, selection inside, not including centerpos") {CollisionVolume pyramid;pyramid.InitShape(float3(358.245, 905.194, 32774.824),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99983f, 0.01869f, 0.00035f, 0.00000f, -0.00000f, -0.01848f, 0.99983f, 0.00000f, 0.01869f, -0.99965f, -0.01847f, 0.00000f, 306.34171f, -15445.67188f, 11985.80078f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}

	SECTION("cam 45, selection outside corner") {CollisionVolume pyramid;pyramid.InitShape(float3(7407.396, 6360.157, 32262.871),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.98874f, 0.09284f, 0.11737f, 0.00000f, -0.00000f, -0.78430f, 0.62038f, 0.00000f, 0.14965f, -0.61340f, -0.77547f, 0.00000f, 2414.12134f, -9039.30859f, 2.57227f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, selection above top") {CollisionVolume pyramid;pyramid.InitShape(float3(12262.979, 3520.008, 31733.789),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99964f, -0.01719f, -0.02058f, 0.00000f, -0.00000f, -0.76743f, 0.64113f, 0.00000f, -0.02681f, -0.64090f, -0.76715f, 0.00000f, -425.44968f, -9313.49219f, 339.63379f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, selection below bottom") {CollisionVolume pyramid;pyramid.InitShape(float3(12146.539, 4068.301, 31314.023),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99920f, -0.03338f, -0.02212f, 0.00000f, -0.00000f, -0.55243f, 0.83356f, 0.00000f, -0.04004f, -0.83289f, -0.55198f, 0.00000f, -626.96686f, -12184.95020f, 3869.54688f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}

	SECTION("cam 45, selection left side") {CollisionVolume pyramid;pyramid.InitShape(float3(6369.395, 72.348, 31550.445),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99020f, -0.10092f, -0.09658f, 0.00000f, -0.00000f, -0.69139f, 0.72248f, 0.00000f, -0.13968f, -0.71540f, -0.68461f, 0.00000f, -2203.52759f, -10429.92285f, 1712.07031f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, selection complete cover") {CollisionVolume pyramid;pyramid.InitShape(float3(9741.429, 10347.375, 31165.438),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {1.00000f, 0.00173f, 0.00152f, 0.00000f, -0.00000f, -0.66186f, 0.74963f, 0.00000f, 0.00230f, -0.74963f, -0.66186f, 0.00000f, 35.89650f, -10825.54004f, 2198.41211f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, selection inside, not including centerpos") {CollisionVolume pyramid;pyramid.InitShape(float3(503.404, 1666.904, 31172.430),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99976f, 0.01637f, 0.01445f, 0.00000f, -0.00000f, -0.66164f, 0.74982f, 0.00000f, 0.02184f, -0.74964f, -0.66149f, 0.00000f, 340.38980f, -10828.39258f, 2201.91309f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}

	SECTION("cam 90, selection outside corner of cylinder") {CollisionVolume pyramid;pyramid.InitShape(float3(4755.428, 1256.933, 18665.125),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.97794f, -0.06061f, 0.19991f, 0.00000f, -0.00000f, -0.95699f, -0.29013f, 0.00000f, 0.20890f, 0.28373f, -0.93587f, 0.00000f, 1949.56860f, 3243.81738f, 3682.33105f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 90, selection through middle") {CollisionVolume pyramid;pyramid.InitShape(float3(11874.237, 341.919, 17574.648),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99986f, -0.00180f, 0.01648f, 0.00000f, -0.00000f, -0.99408f, -0.10865f, 0.00000f, 0.01658f, 0.10864f, -0.99394f, 0.00000f, 145.72449f, 1550.55872f, 3682.33301f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 90, selection inside") {CollisionVolume pyramid;pyramid.InitShape(float3(1046.949, 224.509, 18016.047),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99997f, 0.00205f, -0.00811f, 0.00000f, -0.00000f, -0.96963f, -0.24460f, 0.00000f, -0.00837f, 0.24459f, -0.96959f, 0.00000f, -75.36008f, 2799.15015f, 3682.33008f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}

	SECTION("cam inside,") {CollisionVolume pyramid;pyramid.InitShape(float3(6911.847, 7870.038, 17812.121),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {-0.20841f, 0.01813f, -0.97787f, 0.00000f, -0.11242f, -0.99364f, 0.00554f, 0.00000f, -0.97156f, 0.11109f, 0.20912f, 0.00000f, -8646.52832f, 1590.73901f, 14150.46387f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam inside, pointing upwards") {CollisionVolume pyramid;pyramid.InitShape(float3(2690.407, 3344.530, 17535.477),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {-0.25423f, 0.11189f, -0.96065f, 0.00000f, -0.96575f, -0.08267f, 0.24596f, 0.00000f, -0.05189f, 0.99028f, 0.12908f, 0.00000f, -448.76093f, 9283.84082f, 13419.71289f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("fps cam, 3 fov, inside volume") {CollisionVolume pyramid;pyramid.InitShape(float3(270.500, 186.473, 17373.867),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {-0.24645f, -0.00125f, -0.96915f, 0.00000f, -0.95842f, -0.14811f, 0.24391f, 0.00000f, -0.14385f, 0.98897f, 0.03530f, 0.00000f, -1243.35095f, 9192.48145f, 12594.67676f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("fps cam, 3 fov, selection outside corner") {CollisionVolume pyramid;pyramid.InitShape(float3(179.159, 165.436, 19869.779),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {-0.01150f, 0.00370f, -0.99993f, 0.00000f, 0.20433f, -0.97888f, -0.00597f, 0.00000f, -0.97883f, -0.20438f, 0.01050f, 0.00000f, -8216.60156f, -1078.39661f, 12353.71582f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("fps cam, 3 fov, selection inside cylinder") {CollisionVolume pyramid;pyramid.InitShape(float3(34.699, 78.686, 19866.195),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {-0.01834f, 0.00247f, -0.99983f, 0.00000f, 0.22173f, -0.97509f, -0.00648f, 0.00000f, -0.97494f, -0.22181f, 0.01734f, 0.00000f, -8176.11914f, -1251.13330f, 12421.60059f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	// SECTION("fps cam, 90 fov, unit near edge, shouldnt hit") {CollisionVolume pyramid;pyramid.InitShape(float3(35004.949, 5512.005, 22581.885),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {-0.35658f, -0.00279f, -0.93426f, 0.00000f, 0.00847f, -0.99996f, -0.00025f, 0.00000f, -0.93423f, -0.00800f, 0.35659f, 0.00000f, -10421.45508f, 531.83728f, 16087.72168f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(28.000, 52.000, 28.000),float3(0.000, 3.000, 0.000),1,1,1);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 599.55585f, 12284.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
}
TEST_CASE("CollisionHandler_PyramidVsBox")
{
	SECTION("cam overhead, outside corner") {CollisionVolume pyramid;pyramid.InitShape(float3(2473.956, 2022.063, 32909.621),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99773f, 0.06712f, 0.00501f, 0.00000f, -0.00000f, -0.07441f, 0.99723f, 0.00000f, 0.06731f, -0.99497f, -0.07425f, 0.00000f, 1107.50830f, -15275.38672f, 11071.57617f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, small corner overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(2682.963, 2305.427, 32937.918),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99787f, 0.06499f, 0.00577f, 0.00000f, -0.00000f, -0.08840f, 0.99609f, 0.00000f, 0.06524f, -0.99396f, -0.08821f, 0.00000f, 1074.51514f, -15272.93945f, 10840.51758f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, top overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(6519.016, 2575.506, 32921.043),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99884f, -0.04789f, -0.00453f, 0.00000f, -0.00000f, -0.09409f, 0.99556f, 0.00000f, -0.04810f, -0.99441f, -0.09398f, 0.00000f, -791.74121f, -15271.94141f, 10746.34082f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, inside selection") {CollisionVolume pyramid;pyramid.InitShape(float3(245.141, 414.870, 32774.449),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99995f, -0.01006f, -0.00028f, 0.00000f, -0.00000f, -0.02768f, 0.99962f, 0.00000f, -0.01007f, -0.99957f, -0.02768f, 0.00000f, -164.93733f, -15283.52539f, 11839.68164f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam rotated, small corner overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(3404.790, 6794.942, 26380.340),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.97275f, 0.05260f, -0.22581f, 0.00000f, 0.12623f, -0.93707f, 0.32551f, 0.00000f, -0.19448f, -0.34515f, -0.91818f, 0.00000f, -2517.89673f, -3916.15601f, 274.30566f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam rotated, small side overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(4110.993, 578.906, 27373.383),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.77230f, -0.15606f, -0.61579f, 0.00000f, 0.16912f, -0.88386f, 0.43611f, 0.00000f, -0.61233f, -0.44096f, -0.65621f, 0.00000f, -8333.41992f, -5398.82764f, 3403.86523f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam rotated, complete cover") {CollisionVolume pyramid;pyramid.InitShape(float3(10601.073, 9393.225, 18002.875),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.91500f, 0.00044f, -0.40346f, 0.00000f, -0.00355f, -0.99995f, -0.00915f, 0.00000f, -0.40345f, 0.00981f, -0.91495f, 0.00000f, -3578.69434f, 669.62903f, 4163.81152f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("fps cam inside volume") {CollisionVolume pyramid;pyramid.InitShape(float3(1287.219, 6537.732, 17506.188),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.65916f, -0.05785f, 0.74978f, 0.00000f, 0.37424f, -0.83957f, -0.39378f, 0.00000f, 0.65227f, 0.54016f, -0.53176f, 0.00000f, 5709.41406f, 5309.41113f, 7625.11621f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("far away side overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(11147.938, 4569.580, 32794.816),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.96058f, -0.01757f, -0.27746f, 0.00000f, 0.27572f, -0.06778f, 0.95885f, 0.00000f, -0.03565f, -0.99755f, -0.06026f, 0.00000f, -558.47101f, -13007.40723f, 11276.16406f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(26.000, 28.000, 46.000),float3(0.000, 0.000, 1.000),2,1,2);const CMatrix44f testMat = {0.99865f, -0.05184f, 0.00100f, 0.00000f, 0.05184f, 0.99790f, -0.03862f, 0.00000f, 0.00100f, 0.03862f, 0.99924f, 0.00000f, 0.29548f, 582.04340f, 12286.78027f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
}
TEST_CASE("CollisionHandler_PyramidVsEllipse")
{
	SECTION("cam overhead, outside corner") {CollisionVolume pyramid;pyramid.InitShape(float3(3766.359, 3012.594, 32989.598),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99653f, 0.08286f, 0.00757f, 0.00000f, -0.00000f, -0.09096f, 0.99585f, 0.00000f, 0.08320f, -0.99240f, -0.09065f, 0.00000f, 1372.37646f, -14768.59570f, 10802.91895f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(77.000, 780.000, 77.000),float3(0.000, 0.000, 0.000),0,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 580.35535f, 12288.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam overhead, corner overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(2887.371, 1946.611, 32836.586),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99874f, 0.05017f, 0.00256f, 0.00000f, -0.00000f, -0.05096f, 0.99870f, 0.00000f, 0.05023f, -0.99744f, -0.05090f, 0.00000f, 824.72717f, -14775.38867f, 11462.48145f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(77.000, 780.000, 77.000),float3(0.000, 0.000, 0.000),0,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 580.35535f, 12288.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, selection outside corner of ellipse") {CollisionVolume pyramid;pyramid.InitShape(float3(8093.500, 926.036, 24689.268),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99585f, 0.00382f, 0.09095f, 0.00000f, -0.00000f, -0.99912f, 0.04197f, 0.00000f, 0.09103f, -0.04180f, -0.99497f, 0.00000f, 1180.86426f, 431.71051f, 514.58203f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(77.000, 780.000, 77.000),float3(0.000, 0.000, 0.000),0,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 580.35535f, 12288.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, selection inside corner of ellipse") {CollisionVolume pyramid;pyramid.InitShape(float3(7880.953, 1007.565, 24846.783),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99751f, 0.00117f, 0.07055f, 0.00000f, -0.00000f, -0.99986f, 0.01663f, 0.00000f, 0.07056f, -0.01658f, -0.99737f, 0.00000f, 933.78717f, 741.65442f, 406.41992f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(77.000, 780.000, 77.000),float3(0.000, 0.000, 0.000),0,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 580.35535f, 12288.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, selection inside ellipse top") {CollisionVolume pyramid;pyramid.InitShape(float3(1095.962, 841.478, 23664.766),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99506f, -0.02940f, -0.09480f, 0.00000f, -0.00000f, -0.95512f, 0.29623f, 0.00000f, -0.09926f, -0.29476f, -0.95040f, 0.00000f, -1117.30334f, -2540.07324f, 1551.62305f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(77.000, 780.000, 77.000),float3(0.000, 0.000, 0.000),0,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 580.35535f, 12288.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, large overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(14657.289, 18940.803, 23617.162),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99677f, -0.02453f, -0.07643f, 0.00000f, -0.00000f, -0.95217f, 0.30556f, 0.00000f, -0.08026f, -0.30457f, -0.94910f, 0.00000f, -890.65515f, -2648.88330f, 1589.59277f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(77.000, 780.000, 77.000),float3(0.000, 0.000, 0.000),0,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 580.35535f, 12288.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("cam 45, small side intrusion") {CollisionVolume pyramid;pyramid.InitShape(float3(5782.885, 194.578, 30748.346),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99165f, -0.08461f, -0.09735f, 0.00000f, -0.00000f, -0.75478f, 0.65597f, 0.00000f, -0.12898f, -0.65049f, -0.74848f, 0.00000f, -1982.97302f, -8706.42383f, 1513.01562f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(77.000, 780.000, 77.000),float3(0.000, 0.000, 0.000),0,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 0.00000f, 580.35535f, 12288.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
}
TEST_CASE("CollisionHandler_PyramidVsSphere")
{
	SECTION("outside corner") {CollisionVolume pyramid;pyramid.InitShape(float3(3521.685, 3665.413, 32203.281),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99861f, 0.04203f, 0.03186f, 0.00000f, 0.00376f, -0.65935f, 0.75183f, 0.00000f, 0.05260f, -0.75066f, -0.65859f, 0.00000f, 862.04120f, -11239.06543f, 1869.90039f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(46.000, 46.000, 46.000),float3(0.000, 0.000, 0.000),4,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 1.00000f, 576.36536f, 12287.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("outside top") {CollisionVolume pyramid;pyramid.InitShape(float3(7471.025, 3685.417, 33070.977),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99938f, -0.03346f, -0.01050f, 0.00000f, 0.00494f, -0.16218f, 0.98675f, 0.00000f, -0.03472f, -0.98619f, -0.16192f, 0.00000f, -560.03210f, -15401.72461f, 9621.08008f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(46.000, 46.000, 46.000),float3(0.000, 0.000, 0.000),4,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 1.00000f, 576.36536f, 12287.00000f, 1.00000f};CHECK_FALSE(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("touching top") {CollisionVolume pyramid;pyramid.InitShape(float3(7308.042, 3080.405, 33003.332),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99940f, -0.03305f, -0.00988f, 0.00000f, 0.00495f, -0.14595f, 0.98928f, 0.00000f, -0.03414f, -0.98874f, -0.14570f, 0.00000f, -549.24292f, -15410.39258f, 9894.20996f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(46.000, 46.000, 46.000),float3(0.000, 0.000, 0.000),4,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 1.00000f, 576.36536f, 12287.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("touching corner") {CollisionVolume pyramid;pyramid.InitShape(float3(4383.747, 2346.379, 32960.035),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99793f, 0.06418f, 0.00264f, 0.00000f, 0.00497f, -0.11800f, 0.99300f, 0.00000f, 0.06405f, -0.99094f, -0.11807f, 0.00000f, 1069.61316f, -15425.19531f, 10352.55859f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(46.000, 46.000, 46.000),float3(0.000, 0.000, 0.000),4,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 1.00000f, 576.36536f, 12287.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("overlap") {CollisionVolume pyramid;pyramid.InitShape(float3(14580.339, 8530.826, 32977.977),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99365f, -0.11228f, -0.00752f, 0.00000f, 0.00500f, -0.02271f, 0.99973f, 0.00000f, -0.11242f, -0.99342f, -0.02200f, 0.00000f, -1839.57678f, -15474.97461f, 11935.65723f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(46.000, 46.000, 46.000),float3(0.000, 0.000, 0.000),4,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 1.00000f, 576.36536f, 12287.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("inside selection") {CollisionVolume pyramid;pyramid.InitShape(float3(905.302, 849.550, 32850.859),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99804f, -0.06184f, -0.00910f, 0.00000f, 0.00499f, -0.06636f, 0.99778f, 0.00000f, -0.06230f, -0.99588f, -0.06592f, 0.00000f, -1009.23425f, -15452.25781f, 11215.72070f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(46.000, 46.000, 46.000),float3(0.000, 0.000, 0.000),4,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 1.00000f, 576.36536f, 12287.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
	SECTION("inside volume") {CollisionVolume pyramid;pyramid.InitShape(float3(7916.566, 4656.177, 32806.605),float3(0.000, 0.000, 0.000),3,1,2);const CMatrix44f pyrMat = {0.99984f, -0.01661f, -0.00628f, 0.00000f, 0.00499f, -0.07691f, 0.99703f, 0.00000f, -0.01705f, -0.99690f, -0.07682f, 0.00000f, -278.24249f, -15755.90820f, 11026.75781f, 1.00000f};CollisionVolume testVol;testVol.InitShape(float3(46.000, 46.000, 46.000),float3(0.000, 0.000, 0.000),4,1,2);const CMatrix44f testMat = {0.99999f, -0.00000f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.99999f, 0.00000f, 1.00000f, 576.36536f, 12287.00000f, 1.00000f};CHECK(IntersectsVolume(pyramid, pyrMat, testVol, testMat));}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsBox")
{
	const CollisionVolume boxA = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CollisionVolume boxB = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CMatrix44f boxAMat = MakeTransform();

	SECTION("containment") {
		const CollisionVolume smallBox = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f));
		CHECK(IntersectsVolume(boxA, boxAMat, smallBox, MakeTransform(float3(0.1f, 0.1f, 0.1f))));
	}

	SECTION("overlap") {
		const CMatrix44f boxBMat = MakeTransform(float3(1.5f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("touching faces counts as intersecting") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.0f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("separated") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.5f, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("touching corners counts as intersecting") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.0f, 2.0f, 2.0f));
		CHECK(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("corner separation") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.1f, 2.1f, 2.1f));
		CHECK_FALSE(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsSphere")
{
	const CollisionVolume box    = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CollisionVolume sphere = MakeSphereVolume(1.0f);
	const CMatrix44f boxMat = MakeTransform();

	SECTION("containment") {
		const CMatrix44f sphereMat = MakeTransform(float3(0.5f, 0.5f, 0.5f));
		CHECK(IntersectsVolume(box, boxMat, sphere, sphereMat));
	}

	SECTION("overlap") {
		const CMatrix44f sphereMat = MakeTransform(float3(2.75f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(box, boxMat, sphere, sphereMat));
	}

	SECTION("tangent sphere counts as intersecting") {
		const CMatrix44f sphereMat = MakeTransform(float3(3.0f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(box, boxMat, sphere, sphereMat));
	}

	SECTION("separated") {
		const CMatrix44f sphereMat = MakeTransform(float3(3.1f, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, sphere, sphereMat));
	}

	SECTION("corner intersection") {
		// Closest box corner to sphere is (2, 2, 2).
		// Distance from sphere center (2.5, 2.5, 2.5) to corner is sqrt(3 * 0.5^2) ≈ 0.866. (0.866 < radius 1.0)
		const CMatrix44f sphereMat = MakeTransform(float3(2.5f, 2.5f, 2.5f));
		CHECK(IntersectsVolume(box, boxMat, sphere, sphereMat));
	}

	SECTION("corner separation (verifies not just testing AABBs)") {
		// Individual AABB axes would overlap (Box is [-2, 2], Sphere AABB is [1.8, 3.8])
		// But sphere surface doesn't reach the corner! Distance is sqrt(3 * 0.8^2) ≈ 1.38 (1.38 > radius 1.0)
		const CMatrix44f sphereMat = MakeTransform(float3(2.8f, 2.8f, 2.8f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, sphere, sphereMat));
	}
}

TEST_CASE("CollisionHandler_IntersectVolume_SphereVsSphere")
{
	const CollisionVolume sphereA = MakeSphereVolume(3.0f);
	const CollisionVolume sphereB = MakeSphereVolume(4.0f);
	const CMatrix44f sphereAMat = MakeTransform();

	SECTION("containment") {
		CHECK(IntersectsVolume(sphereA, sphereAMat, sphereB, MakeTransform(float3(1.0f, 0.0f, 0.0f))));
	}

	SECTION("overlap") {
		CHECK(IntersectsVolume(sphereA, sphereAMat, sphereB, MakeTransform(float3(6.5f, 0.0f, 0.0f))));
	}

	SECTION("tangent counts as intersecting") {
		CHECK(IntersectsVolume(sphereA, sphereAMat, sphereB, MakeTransform(float3(7.0f, 0.0f, 0.0f))));
	}

	SECTION("separated") {
		CHECK_FALSE(IntersectsVolume(sphereA, sphereAMat, sphereB, MakeTransform(float3(7.1f, 0.0f, 0.0f))));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsEllipsoid")
{
	const CollisionVolume box       = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CollisionVolume ellipsoid = MakeEllipsoidVolume(float3(4.0f, 2.0f, 2.0f));
	const CMatrix44f boxMat = MakeTransform();

	SECTION("overlap") {
		const CMatrix44f ellipsoidMat = MakeTransform(float3(3.25f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("touching counts as intersecting") {
		const CMatrix44f ellipsoidMat = MakeTransform(float3(4.0f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("separated") {
		const CMatrix44f ellipsoidMat = MakeTransform(float3(4.1f, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("rotated ellipsoid separates") {
		// Unrotated, it reaches into the box. Rotated by 90 on Y shrinks its X influence.
		const CMatrix44f ellipsoidMat = MakeTransform(float3(4.0f, 0.0f, 0.0f), float3(0.0f, HALF_PI, 0.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("corner intersection") {
		// Ellipsoid scales: X=6, Y=2, Z=2 (radii: X=3, Y=1, Z=1).
		// Center at (4.0, 2.5, 2.0). Box corner is at (2.0, 2.0, 2.0).
		// Relative: x=(-2), y=(-0.5). Ellipsoid Equation: (4/9) + (0.25/1) + 0 = ~0.694 < 1.0 (Intersect!)
		const CollisionVolume ell = MakeEllipsoidVolume(float3(6.0f, 2.0f, 2.0f));
		const CMatrix44f ellMat = MakeTransform(float3(4.0f, 2.5f, 2.0f));
		CHECK(IntersectsVolume(box, boxMat, ell, ellMat));
	}

	SECTION("corner separation") {
		// Center at (5.0, 3.0, 2.0). Box corner is at (2.0, 2.0, 2.0).
		// Relative: x=(-3), y=(-1). Ellipsoid Equation: (9/9) + (1/1) + 0 = 2.0 > 1.0 (Separates!)
		const CollisionVolume ell = MakeEllipsoidVolume(float3(6.0f, 2.0f, 2.0f));
		const CMatrix44f ellMat = MakeTransform(float3(5.0f, 3.0f, 2.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, ell, ellMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsCylinder")
{
	const CollisionVolume box = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CMatrix44f boxMat = MakeTransform();

	SECTION("x-axis cylinder bounds (length)") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(6.0f, 2.0f, 2.0f), CollisionVolume::COLVOL_AXIS_X);

		CHECK(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(4.5f, 0.0f, 0.0f))));
		CHECK(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(5.0f, 0.0f, 0.0f))));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(5.2f, 0.0f, 0.0f))));
	}

	SECTION("x-axis cylinder bounds (radial)") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(6.0f, 2.0f, 2.0f), CollisionVolume::COLVOL_AXIS_X);

		CHECK(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(0.0f, 2.5f, 0.0f))));
		CHECK(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(0.0f, 3.0f, 0.0f))));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(0.0f, 3.1f, 0.0f))));
	}

	SECTION("y-axis cylinder") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 6.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Y);

		CHECK(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(2.75f, 0.0f, 0.0f))));
		CHECK(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(3.0f, 0.0f, 0.0f))));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(3.1f, 0.0f, 0.0f))));
	}

	SECTION("z-axis cylinder corner check") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 2.0f, 6.0f), CollisionVolume::COLVOL_AXIS_Z);

		// Distance from box corner (2, 2) in XY to cylinder core (2.5, 2.5) is ~0.707 < radius 1.0
		CHECK(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(2.5f, 2.5f, 0.0f))));

		// Distance from box corner (2, 2) to cylinder core (2.8, 2.8) is ~1.13 > radius 1.0 (AABB separates)
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, MakeTransform(float3(2.8f, 2.8f, 0.0f))));
	}
}

TEST_CASE("CollisionHandler_IntersectVolume_CylinderVsCylinder")
{
	const CollisionVolume cylinderA = MakeCylinderVolume(float3(6.0f, 6.0f, 8.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CollisionVolume cylinderB = MakeCylinderVolume(float3(6.0f, 6.0f, 8.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CMatrix44f cylinderAMat = MakeTransform();

	SECTION("parallel overlap") {
		CHECK(IntersectsVolume(cylinderA, cylinderAMat, cylinderB, MakeTransform(float3(5.5f, 0.0f, 0.0f))));
	}

	SECTION("parallel tangent counts as intersecting") {
		CHECK(IntersectsVolume(cylinderA, cylinderAMat, cylinderB, MakeTransform(float3(6.0f, 0.0f, 0.0f))));
	}

	SECTION("parallel separation") {
		CHECK_FALSE(IntersectsVolume(cylinderA, cylinderAMat, cylinderB, MakeTransform(float3(6.1f, 0.0f, 0.0f))));
	}

	SECTION("orthogonal crossing intersects") {
		const CollisionVolume cylinderX = MakeCylinderVolume(float3(8.0f, 6.0f, 6.0f), CollisionVolume::COLVOL_AXIS_X);
		CHECK(IntersectsVolume(cylinderA, cylinderAMat, cylinderX, MakeTransform()));
	}

	SECTION("orthogonal offset separates") {
		const CollisionVolume cylinderX = MakeCylinderVolume(float3(8.0f, 6.0f, 6.0f), CollisionVolume::COLVOL_AXIS_X);
		CHECK_FALSE(IntersectsVolume(cylinderA, cylinderAMat, cylinderX, MakeTransform(float3(0.0f, 6.5f, 0.0f))));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsPyramid")
{
	// Box is 1x1x1 => half-scales 0.5
	const CollisionVolume box = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f));

	// Pyramid is represented as:
	//   primary axis in [-2, +2]
	//   apex at -2
	//   base at +2
	//   base half-extents 2 on the secondary axes
	const CollisionVolume pyramidZ = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z);

	const CMatrix44f boxMat = MakeTransform();
	const CMatrix44f pyramidMat = MakeTransform();

	SECTION("containment near base") {
		// Box spans z in [0.5, 1.5]. At z=0.5 the pyramid half-width is already 1.25.
		const CMatrix44f movedBoxMat = MakeTransform(float3(0.0f, 0.0f, 1.0f));
		CHECK(IntersectsVolume(box, movedBoxMat, pyramidZ, pyramidMat));
	}

	SECTION("touching base face counts as intersecting") {
		// Box spans z in [2.0, 3.0], so it touches the base plane at z=2.
		const CMatrix44f movedBoxMat = MakeTransform(float3(0.0f, 0.0f, 2.5f));
		CHECK(IntersectsVolume(box, movedBoxMat, pyramidZ, pyramidMat));
	}

	SECTION("separated beyond base plane") {
		// Box spans z in [2.01, 3.01], entirely beyond the base plane.
		const CMatrix44f movedBoxMat = MakeTransform(float3(0.0f, 0.0f, 2.51f));
		CHECK_FALSE(IntersectsVolume(box, movedBoxMat, pyramidZ, pyramidMat));
	}

	SECTION("touching apex counts as intersecting") {
		// Box spans z in [-3.0, -2.0], touching the apex point at z=-2.
		const CMatrix44f movedBoxMat = MakeTransform(float3(0.0f, 0.0f, -2.5f));
		CHECK(IntersectsVolume(box, movedBoxMat, pyramidZ, pyramidMat));
	}

	SECTION("same lateral offset separates near apex but intersects near base") {
		// Near apex: box spans z in [-1.5, -0.5], pyramid half-width there is at most 0.75.
		// Box x interval is [0.8, 1.8], so this should separate.
		CHECK_FALSE(IntersectsVolume(box, MakeTransform(float3(1.3f, 0.0f, -1.0f)), pyramidZ, pyramidMat));

		// Near base: box spans z in [0.5, 1.5], pyramid half-width is at least 1.25.
		// Same x interval [0.8, 1.8] now overlaps.
		CHECK(IntersectsVolume(box, MakeTransform(float3(1.3f, 0.0f, 1.0f)), pyramidZ, pyramidMat));
	}

	SECTION("sign-flipped primary axis is handled") {
		// Rotate the pyramid 180 degrees around Y so its local +Z points toward world -Z.
		const CMatrix44f flippedPyramidMat = MakeTransform(ZeroVector, float3(0.0f, 2.0f * HALF_PI, 0.0f));

		// Same lateral offset, but now the wide end is on negative world-Z.
		CHECK(IntersectsVolume(box, MakeTransform(float3(1.3f, 0.0f, -1.0f)), pyramidZ, flippedPyramidMat));
		CHECK_FALSE(IntersectsVolume(box, MakeTransform(float3(1.3f, 0.0f,  1.0f)), pyramidZ, flippedPyramidMat));
	}

	SECTION("non-Z primary axis") {
		const CollisionVolume pyramidY = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Y);

		// Wide end is at +Y, apex at -Y.
		CHECK(IntersectsVolume(box, MakeTransform(float3(1.3f,  1.0f, 0.0f)), pyramidY, MakeTransform()));
		CHECK_FALSE(IntersectsVolume(box, MakeTransform(float3(1.3f, -1.0f, 0.0f)), pyramidY, MakeTransform()));
	}

	SECTION("offset pyramid") {
		const CollisionVolume offsetPyramid =
			MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z, float3(3.0f, 0.0f, 0.0f));

		CHECK(IntersectsVolume(box, MakeTransform(float3(4.3f, 0.0f, 1.0f)), offsetPyramid, MakeTransform()));
		CHECK_FALSE(IntersectsVolume(box, MakeTransform(float3(4.3f, 0.0f, -1.0f)), offsetPyramid, MakeTransform()));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_RespectsRotation")
{
	const CollisionVolume boxA = MakeBoxVolume(float3(4.0f, 2.0f, 2.0f));
	const CollisionVolume boxB = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f));

	const CMatrix44f boxAMat = MakeTransform(ZeroVector, float3(0.0f, QUARTER_PI, 0.0f));

	SECTION("rotated box intersects") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.1f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("rotated box separates") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.5f, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BothRotated_SAT_EdgeEdge")
{
	const CollisionVolume boxA = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CollisionVolume boxB = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));

	// Rotate A around X, turning its Y/Z faces diagonal.
	const CMatrix44f aMat = MakeTransform(ZeroVector, float3(QUARTER_PI, 0.0f, 0.0f));

	SECTION("edge-edge crossing") {
		// Rotate B around Y, turning its X/Z faces diagonal.
		// Moving it closely on the Z axis forces a cross-product (Edge-vs-Edge) intersection.
		const CMatrix44f bMatIntersects = MakeTransform(float3(0.0f, 0.0f, 2.8f), float3(0.0f, QUARTER_PI, 0.0f));
		CHECK(IntersectsVolume(boxA, aMat, boxB, bMatIntersects));

		const CMatrix44f bMatSeparates = MakeTransform(float3(0.0f, 0.0f, 2.9f), float3(0.0f, QUARTER_PI, 0.0f));
		CHECK_FALSE(IntersectsVolume(boxA, aMat, boxB, bMatSeparates));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_HandlesRotatedOtherVolume")
{
	const CollisionVolume box = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 6.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Y);
	const CMatrix44f boxMat = MakeTransform();

	SECTION("rotated cylinder still intersects") {
		const CMatrix44f cylinderMat = MakeTransform(float3(2.75f, 0.0f, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
		CHECK(IntersectsVolume(box, boxMat, cylinder, cylinderMat));
	}

	SECTION("rotated cylinder still separates when moved away") {
		const CMatrix44f cylinderMat = MakeTransform(float3(3.6f, 0.0f, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, cylinderMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_RespectsOffsets")
{
	// Provide a volume whose local geometry is shifted forward by 2.0 in the X-axis
	const CollisionVolume boxWithOffset = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f), float3(2.0f, 0.0f, 0.0f));
	const CMatrix44f offsetBoxMat = MakeTransform();

	const CollisionVolume targetBox = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));

	SECTION("positive offset") {
		// The target at X=3.0 (reaches back to X=2.0). The offsetBox spans X=[1.0, 3.0]. They intersect.
		const CMatrix44f targetMatIntersects = MakeTransform(float3(3.0f, 0.0f, 0.0f));
		CHECK(IntersectsVolume(boxWithOffset, offsetBoxMat, targetBox, targetMatIntersects));

		// Separated if target moved slightly to the left of the offset bounds
		const CMatrix44f targetMatSeparates = MakeTransform(float3(-0.1f, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(boxWithOffset, offsetBoxMat, targetBox, targetMatSeparates));
	}

	SECTION("negative offsets") {
		const CollisionVolume negativeOffsetBox = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f), float3(-2.0f, -2.0f, -2.0f));

		// Negative offset box reaches up to (-1, -1, -1). Target at origin reaches down to (-1, -1, -1). They touch.
		CHECK(IntersectsVolume(negativeOffsetBox, offsetBoxMat, targetBox, MakeTransform(ZeroVector)));

		// Move target box slightly right/up/forward -> they separate.
		CHECK_FALSE(IntersectsVolume(negativeOffsetBox, offsetBoxMat, targetBox, MakeTransform(float3(0.1f, 0.1f, 0.1f))));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_OffsetAndRotationCoupling")
{
	// Box with local offset of +10 on the X axis
	const CollisionVolume offsetBox = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f), float3(10.0f, 0.0f, 0.0f));

	// Rotated 90 degrees around the Y axis
	const CMatrix44f offsetBoxMat = MakeTransform(ZeroVector, float3(0.0f, HALF_PI, 0.0f));

	const CollisionVolume targetBox = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));

	SECTION("misses unrotated offset position") {
		// Because the box is rotated, it should no longer be at +10 X.
		const CMatrix44f targetMat = MakeTransform(float3(10.0f, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(offsetBox, offsetBoxMat, targetBox, targetMat));
	}

	SECTION("hits rotated offset position") {
		// +10 X rotated 90 degrees around Y becomes either +10 Z or -10 Z depending on engine handedness.
		// We test both to ensure it correctly moved to the Z axis rather than staying on X.
		const CMatrix44f targetMatPosZ = MakeTransform(float3(0.0f, 0.0f, 10.0f));
		const CMatrix44f targetMatNegZ = MakeTransform(float3(0.0f, 0.0f, -10.0f));

		bool hitsRotatedPos = IntersectsVolume(offsetBox, offsetBoxMat, targetBox, targetMatPosZ) ||
		                      IntersectsVolume(offsetBox, offsetBoxMat, targetBox, targetMatNegZ);

		CHECK(hitsRotatedPos);
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_ThinGeometries")
{
	const CMatrix44f originMat = MakeTransform();

	SECTION("plane vs plane (2D boxes)") {
		const CollisionVolume planeA = MakeBoxVolume(float3(10.0f, 1.f, 10.0f));
		const CollisionVolume planeB = MakeBoxVolume(float3(10.0f, 1.f, 10.0f));

		CHECK(IntersectsVolume(planeA, originMat, planeB, originMat));

		// Y-scale is clamped to 1.0, so separation starts just beyond 1.0 on Y.
		const CMatrix44f sepMat = MakeTransform(float3(0.0f, 1.01f, 0.0f));
		CHECK_FALSE(IntersectsVolume(planeA, originMat, planeB, sepMat));
	}

	SECTION("line vs line (1D boxes)") {
		const CollisionVolume lineA = MakeBoxVolume(float3(10.0f, 1.f, 0.001f));
		// Perpendicular line
		const CollisionVolume lineB = MakeBoxVolume(float3(1.f, 10.0f, 1.f));

		CHECK(IntersectsVolume(lineA, originMat, lineB, originMat));

		// Z-scale is clamped to 1.0, so separation starts just beyond 1.0 on Z.
		const CMatrix44f sepMat = MakeTransform(float3(0.0f, 0.0f, 1.01f));
		CHECK_FALSE(IntersectsVolume(lineA, originMat, lineB, sepMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_InvertedContainment")
{
	const CollisionVolume smallBox = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CMatrix44f boxMat = MakeTransform();

	SECTION("massive sphere completely surrounds box") {
		const CollisionVolume massiveSphere = MakeSphereVolume(100.0f);
		const CMatrix44f sphereMat = MakeTransform(float3(5.0f, 5.0f, 5.0f)); // Offset center but still totally swallowing the box
		CHECK(IntersectsVolume(smallBox, boxMat, massiveSphere, sphereMat));
	}

	SECTION("massive cylinder completely surrounds box") {
		const CollisionVolume massiveCyl = MakeCylinderVolume(float3(100.0f, 100.0f, 100.0f), CollisionVolume::COLVOL_AXIS_Y);
		const CMatrix44f cylMat = MakeTransform(float3(-10.0f, 0.0f, 10.0f));
		CHECK(IntersectsVolume(smallBox, boxMat, massiveCyl, cylMat));
	}

	SECTION("massive ellipsoid completely surrounds box") {
		const CollisionVolume massiveEllipsoid = MakeEllipsoidVolume(float3(200.0f, 100.0f, 50.0f));
		const CMatrix44f ellMat = MakeTransform();
		CHECK(IntersectsVolume(smallBox, boxMat, massiveEllipsoid, ellMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_CylinderEndCaps")
{
	// Box half-extents: (1, 1, 1)
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CMatrix44f boxMat = MakeTransform();

	// Z-axis cylinder. Radius=1.0 (X/Y=2.0), Length=6.0 (Z half-extent=3.0)
	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 2.0f, 6.0f), CollisionVolume::COLVOL_AXIS_Z);

	SECTION("flat cap flush against box face") {
		// Box face is at Z=1.0. Cylinder cap is at Z=3.0 local.
		// If cylinder center is at Z=4.0, its cap reaches Z=1.0. (Touching)
		const CMatrix44f cylMatTouch = MakeTransform(float3(0.0f, 0.0f, 4.0f));
		CHECK(IntersectsVolume(box, boxMat, cylinder, cylMatTouch));

		// Move slightly away
		const CMatrix44f cylMatSep = MakeTransform(float3(0.0f, 0.0f, 4.01f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, cylMatSep));
	}

	SECTION("end cap rim touching box corner") {
		// Box corner at (1, 1, 1).
		// Cylinder length reaches Z=1.0 if center is at Z=4.0.
		// Cylinder rim radius is 1.0. If center X is 1.0, and Y is 2.0, rim hits (1, 1, 1).
		const CMatrix44f cylMatRimTouch = MakeTransform(float3(1.0f, 2.0f, 4.0f));
		CHECK(IntersectsVolume(box, boxMat, cylinder, cylMatRimTouch));

		// Move cylinder up slightly (Y=2.1), rim misses the box corner
		const CMatrix44f cylMatRimSep = MakeTransform(float3(1.0f, 2.1f, 4.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, cylMatRimSep));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_ExtremeEllipsoids")
{
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f)); // Box half extents 1, 1, 1
	const CMatrix44f boxMat = MakeTransform();

	// Input scale values must also be > 1.0f.
	// We use a major axis of 100.0f, making the clamped minimum exactly 2.0f (radius 1.0f).

	SECTION("pancake ellipsoid (highly squashed on Y)") {
		// Scales: X=100.0, Y=2.0, Z=100.0 -> Radii: X=50.0, Y=1.0, Z=50.0
		const CollisionVolume pancake = MakeEllipsoidVolume(float3(100.0f, 2.0f, 100.0f));

		// Penetrating the top face (Center < 2.0)
		CHECK(IntersectsVolume(box, boxMat, pancake, MakeTransform(float3(0.0f, 1.9f, 0.0f))));

		// Lifted just out of reach (Center > 2.0)
		CHECK_FALSE(IntersectsVolume(box, boxMat, pancake, MakeTransform(float3(0.0f, 2.1f, 0.0f))));
	}

	SECTION("cigar ellipsoid (highly elongated on X)") {
		// Scales: X=100.0, Y=2.0, Z=2.0 -> Radii: X=50.0, Y=1.0, Z=1.0
		const CollisionVolume cigar = MakeEllipsoidVolume(float3(100.0f, 2.0f, 2.0f));

		// Passing right next to the Z face, slightly intersecting (Center < 2.0)
		CHECK(IntersectsVolume(box, boxMat, cigar, MakeTransform(float3(0.0f, 0.0f, 1.9f))));

		// Moved slightly away on Z (Center > 2.0)
		CHECK_FALSE(IntersectsVolume(box, boxMat, cigar, MakeTransform(float3(0.0f, 0.0f, 2.1f))));
	}
}
TEST_CASE("CollisionHandler_IntersectBoxVolume_SphereVsRotatedBox")
{
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f)); // Extents: [-1, 1]
	const CollisionVolume sphere = MakeSphereVolume(1.0f);               // Radius: 1.0
	const CMatrix44f sphereMat = MakeTransform(float3(2.2f, 0.0f, 0.0f));

	SECTION("unrotated box misses the sphere") {
		// Box face stops at X = 1.0. Sphere edge starts at X = 1.2. (Gap of 0.2)
		const CMatrix44f boxMatUnrotated = MakeTransform();
		CHECK_FALSE(IntersectsVolume(box, boxMatUnrotated, sphere, sphereMat));
	}

	SECTION("rotated box corner hits the sphere") {
		// Rotating the box 45 degrees around Z turns its flat X-face into a pointing diamond.
		// The corner now stretches to X = sqrt(1^2 + 1^2) = 1.414.
		// Sphere edge is at X = 1.2. Because 1.414 > 1.2, the sharp corner pierces the sphere.
		const CMatrix44f boxMatRotated = MakeTransform(ZeroVector, float3(0.0f, 0.0f, QUARTER_PI));
		CHECK(IntersectsVolume(box, boxMatRotated, sphere, sphereMat));
	}
}
TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsRotatedEllipsoid")
{
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f)); // Extents: [-1, 1]
	const CMatrix44f boxMat = MakeTransform();

	// Scales: X=10.0, Y=2.0, Z=2.0 -> Radii: X=5.0, Y=1.0, Z=1.0
	// Satisfies the max scale 2% constraint (10.0 * 0.02 = 0.2 < 2.0)
	const CollisionVolume ellipsoid = MakeEllipsoidVolume(float3(10.0f, 2.0f, 2.0f));

	// Place ellipsoid off-center diagonally from the box corner
	const float3 pos(4.0f, 4.0f, 0.0f);

	SECTION("unrotated ellipsoid misses") {
		// Unrotated, the long X-axis stretches from X=-1 to X=9.
		// However, it remains at Y=4, which completely bypasses the Box's Y extents [-1, 1].
		const CMatrix44f ellMatUnrotated = MakeTransform(pos);
		CHECK_FALSE(IntersectsVolume(box, boxMat, ellipsoid, ellMatUnrotated));
	}

	SECTION("ellipsoid rotated perfectly toward box intersects") {
		// Spring's positive Z rotation turns +X toward -Y, so -45 degrees points the major axis
		// along the diagonal from (4, 4, 0) toward the box.
		// Distance from (4,4,0) to box corner (1,1,0) is sqrt(3^2 + 3^2) ≈ 4.24.
		// Since the rotated major radius (5.0) is greater than 4.24, it reaches the corner.
		const CMatrix44f ellMatAimed = MakeTransform(pos, float3(0.0f, 0.0f, -QUARTER_PI));
		CHECK(IntersectsVolume(box, boxMat, ellipsoid, ellMatAimed));
	}

	SECTION("ellipsoid rotated perpendicular to box misses") {
		// +45 degrees leaves the major axis perpendicular to the diagonal back toward the box.
		// Only the minor radius (1.0) faces the box, which is much less than the 4.24 distance.
		const CMatrix44f ellMatAway = MakeTransform(pos, float3(0.0f, 0.0f, QUARTER_PI));
		CHECK_FALSE(IntersectsVolume(box, boxMat, ellipsoid, ellMatAway));
	}
}
TEST_CASE("CollisionHandler_IntersectBoxVolume_SlantedCylinder")
{
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f)); // Extents [-1, 1]
	const CMatrix44f boxMat = MakeTransform();

	// Y-axis cylinder. Radius=1.0, Half-length=5.0
	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 10.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Y);

	SECTION("unrotated misses overhead") {
		// Cylinder is suspended directly above the box. Lowest tip reaches Y = 4.0 - 5.0 = -1.0.
		// But wait, Z is at 4.0. The box only covers Z up to 1.0. Misses completely.
		const CMatrix44f cylMatUpright = MakeTransform(float3(0.0f, 4.0f, 4.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, cylMatUpright));
	}

	SECTION("slanted towards box hits") {
		// Spring's positive X rotation tilts +Y toward -Z, so -45 degrees slants the lower end-cap
		// downward and inward toward the box.
		// Tip travel is approx 5.0 * 0.707 = 3.53 units on Y and Z axes.
		// New tip position: Y = 4.0 - 3.53 = 0.47, Z = 4.0 - 3.53 = 0.47.
		// Since (0, 0.47, 0.47) is inside the box [-1, 1], it pierces the top face!
		const CMatrix44f cylMatSlanted = MakeTransform(float3(0.0f, 4.0f, 4.0f), float3(-QUARTER_PI, 0.0f, 0.0f));
		CHECK(IntersectsVolume(box, boxMat, cylinder, cylMatSlanted));
	}

	SECTION("slanted too far out misses") {
		// Same 45 degree slant, but moved further out.
		// Tip reaches Y = 6.0 - 3.53 = 2.47. Z = 6.0 - 3.53 = 2.47.
		// Box max is 1.0, so it stops short.
		const CMatrix44f cylMatSlantedFar = MakeTransform(float3(0.0f, 6.0f, 6.0f), float3(-QUARTER_PI, 0.0f, 0.0f));
		CHECK_FALSE(IntersectsVolume(box, boxMat, cylinder, cylMatSlantedFar));
	}
}
TEST_CASE("CollisionHandler_IntersectBoxVolume_MultiAxisObliqueBoxes")
{
	const CollisionVolume boxA = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f)); // Extents: 2, 2, 2
	const CollisionVolume boxB = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f)); // Extents: 1, 1, 1

	const CMatrix44f matA = MakeTransform();

	// Rotate boxB by 45 degrees on ALL three axes.
	const float3 crazyRotation(QUARTER_PI, QUARTER_PI, QUARTER_PI);

	SECTION("deep multi-axis overlap") {
		const CMatrix44f matB_Hits = MakeTransform(float3(2.5f, 2.5f, 2.5f), crazyRotation);
		CHECK(IntersectsVolume(boxA, matA, boxB, matB_Hits));
	}

	SECTION("glancing multi-axis corner touch") {
		// For this specific three-axis rotation, the limiting SAT axes are the A.y x B.z and
		// A.z x B.z cross-products, which separate at about 2.828 on each center coordinate.
		// Keep boxB just inside that bound so this remains a true near-tangent overlap.
		const CMatrix44f matB_Glance = MakeTransform(float3(2.82f, 2.82f, 2.82f), crazyRotation);
		CHECK(IntersectsVolume(boxA, matA, boxB, matB_Glance));
		const CMatrix44f matB_Far = MakeTransform(float3(2.89f, 2.89f, 2.89f), crazyRotation);
		CHECK_FALSE(IntersectsVolume(boxA, matA, boxB, matB_Far));
	}

	SECTION("multi-axis separation") {
		// Moved entirely out of the collision zone.
		const CMatrix44f matB_Misses = MakeTransform(float3(4.5f, 4.5f, 4.5f), crazyRotation);
		CHECK_FALSE(IntersectsVolume(boxA, matA, boxB, matB_Misses));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_RotatedLineContactCountsAsIntersection")
{
	const CollisionVolume boxA = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CollisionVolume boxB = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CMatrix44f boxAMat = MakeTransform();
	const CMatrix44f boxBMat =
		MakeTransform(float3(2.41421356f, 0.0f, 0.0f), float3(0.0f, 0.0f, QUARTER_PI));

	CHECK(IntersectsVolume(boxA, boxAMat, boxB, boxBMat));
}

// TEST_CASE("CollisionHandler_IntersectBoxVolume_AxisAligned_Performance")
// {
// 	SKIP("performance");
// 	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
// 	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 2.0f, 6.0f), CollisionVolume::COLVOL_AXIS_Z);
// 	const CMatrix44f boxMat = MakeTransform();

// 	std::array<CMatrix44f, 64> hitMats;
// 	std::array<CMatrix44f, 64> missMats;

// 	for (std::size_t i = 0; i < hitMats.size(); ++i) {
// 		const float x = 0.75f + ((i % 8) * 0.05f);
// 		hitMats[i] = MakeTransform(float3(x, 2.0f, 4.0f));
// 		missMats[i] = MakeTransform(float3(x, 2.2f, 4.0f));
// 	}

// 	const std::int64_t iterations = 10000000;
// 	volatile std::int64_t sink = 0;

// 	LOG("CollisionHandler axis-aligned box-volume:");
// 	{
// 		ScopedOnceTimer timer(" axis-aligned box vs cylinder (intersecting)");
// 		for (std::int64_t j = iterations; j > 0; --j) {
// 			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, hitMats[j % hitMats.size()])) * j;
// 		}
// 	}
// 	{
// 		ScopedOnceTimer timer(" axis-aligned box vs cylinder (separated)");
// 		for (std::int64_t j = iterations; j > 0; --j) {
// 			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, missMats[j % missMats.size()])) * j;
// 		}
// 	}

// 	CHECK((sink | 1) != 0);
// }

// TEST_CASE("CollisionHandler_IntersectBoxVolume_Rotated_Performance")
// {
// 	SKIP("performance");
// 	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
// 	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 2.0f, 6.0f), CollisionVolume::COLVOL_AXIS_Z);
// 	const CMatrix44f boxMat = MakeTransform(ZeroVector, float3(0.0f, QUARTER_PI, 0.0f));

// 	std::array<CMatrix44f, 64> hitMats;
// 	std::array<CMatrix44f, 64> missMats;

// 	for (std::size_t i = 0; i < hitMats.size(); ++i) {
// 		const float y = 0.75f + ((i % 8) * 0.05f);
// 		hitMats[i] = MakeTransform(float3(2.75f, y, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
// 		missMats[i] = MakeTransform(float3(3.6f, y, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
// 	}

// 	const std::int64_t iterations = 10000000;
// 	volatile std::int64_t sink = 0;

// 	LOG("CollisionHandler rotated box-volume:");
// 	{
// 		ScopedOnceTimer timer(" rotated box vs cylinder (intersecting)");
// 		for (std::int64_t j = iterations; j > 0; --j) {
// 			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, hitMats[j % hitMats.size()])) * j;
// 		}
// 	}
// 	{
// 		ScopedOnceTimer timer(" rotated box vs cylinder (separated)");
// 		for (std::int64_t j = iterations; j > 0; --j) {
// 			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, missMats[j % missMats.size()])) * j;
// 		}
// 	}

// 	CHECK((sink | 1) != 0);
// }
TEST_CASE("CollisionHandler_IntersectPyramidVolume_PyramidVsBox")
{
	// Pyramid:
	//   primary axis Z in [-2, +2]
	//   apex at z = -2
	//   base at z = +2
	//   base half-extents 2 on X/Y
	const CollisionVolume pyramid = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CollisionVolume box = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f));
	const CMatrix44f pyramidMat = MakeTransform();

	SECTION("box intersects near the wide end") {
		CHECK(IntersectsVolume(pyramid, pyramidMat, box, MakeTransform(float3(1.3f, 0.0f, 1.0f))));
	}

	SECTION("same box placement separates near the apex") {
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, box, MakeTransform(float3(1.3f, 0.0f, -1.0f))));
	}

	SECTION("touching apex counts as intersecting") {
		CHECK(IntersectsVolume(pyramid, pyramidMat, box, MakeTransform(float3(0.0f, 0.0f, -2.5f))));
	}

	SECTION("sign-flipped pyramid primary axis is handled") {
		const CMatrix44f flippedPyramidMat = MakeTransform(ZeroVector, float3(0.0f, 2.0f * HALF_PI, 0.0f));

		CHECK(IntersectsVolume(pyramid, flippedPyramidMat, box, MakeTransform(float3(1.3f, 0.0f, -1.0f))));
		CHECK_FALSE(IntersectsVolume(pyramid, flippedPyramidMat, box, MakeTransform(float3(1.3f, 0.0f, 1.0f))));
	}
}

TEST_CASE("CollisionHandler_IntersectPyramidVolume_PyramidVsSphere")
{
	const CollisionVolume pyramid = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CMatrix44f pyramidMat = MakeTransform();

	SECTION("sphere contained") {
		const CollisionVolume sphere = MakeSphereVolume(0.5f);
		CHECK(IntersectsVolume(pyramid, pyramidMat, sphere, MakeTransform(float3(0.0f, 0.0f, 0.0f))));
	}

	SECTION("sphere tangent to base face counts as intersecting") {
		const CollisionVolume sphere = MakeSphereVolume(0.5f);
		CHECK(IntersectsVolume(pyramid, pyramidMat, sphere, MakeTransform(float3(0.0f, 0.0f, 2.5f))));
	}

	SECTION("sphere tangent to a side face counts as intersecting") {
		const CollisionVolume sphere = MakeSphereVolume(1.5f);
		CHECK(IntersectsVolume(
			pyramid,
			pyramidMat,
			sphere,
			MakeTransform(float3(2.34164079f, 0.0f, -0.67082039f))
		));
	}

	SECTION("sphere separated beyond base face") {
		const CollisionVolume sphere = MakeSphereVolume(0.5f);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, sphere, MakeTransform(float3(0.0f, 0.0f, 2.6f))));
	}
}

TEST_CASE("CollisionHandler_IntersectPyramidVolume_PyramidVsEllipsoid")
{
	const CollisionVolume pyramid = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CMatrix44f pyramidMat = MakeTransform();

	SECTION("ellipsoid contained") {
		const CollisionVolume ellipsoid = MakeEllipsoidVolume(float3(2.0f, 1.0f, 1.0f));
		CHECK(IntersectsVolume(pyramid, pyramidMat, ellipsoid, MakeTransform(float3(0.0f, 0.0f, 0.5f))));
	}

	SECTION("ellipsoid separated beyond base face") {
		const CollisionVolume ellipsoid = MakeEllipsoidVolume(float3(2.0f, 2.0f, 1.0f));
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, ellipsoid, MakeTransform(float3(0.0f, 0.0f, 2.6f))));
	}

	SECTION("rotated ellipsoid still intersects when centered in pyramid") {
		const CollisionVolume ellipsoid = MakeEllipsoidVolume(float3(3.0f, 1.0f, 1.0f));
		const CMatrix44f ellipsoidMat = MakeTransform(float3(0.0f, 0.0f, 0.5f), float3(0.0f, HALF_PI, 0.0f));
		CHECK(IntersectsVolume(pyramid, pyramidMat, ellipsoid, ellipsoidMat));
	}
}

TEST_CASE("CollisionHandler_IntersectPyramidVolume_PyramidVsCylinder")
{
	const CollisionVolume pyramid = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CMatrix44f pyramidMat = MakeTransform();

	SECTION("z-axis cylinder contained") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 0.0f, 0.5f))));
	}

	SECTION("z-axis cylinder separated beyond base face") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 0.0f, 2.6f))));
	}

	SECTION("x-axis cylinder intersects near base but not near apex") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 1.0f, 1.0f), CollisionVolume::COLVOL_AXIS_X);

		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(2.1f, 0.0f, 1.0f))));
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(2.1f, 0.0f, -1.0f))));
	}

	SECTION("z-axis cylinder centered at apex still intersects") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.5f, 0.5f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 0.0f, -1.5f))));
	}

	SECTION("z-axis cylinder completely below apex is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.5f, 0.5f, 0.5f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 0.0f, -2.6f))));
	}

	SECTION("z-axis cylinder outside positive x side plane is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(3.1f, 0.0f, 1.5f))));
	}

	SECTION("z-axis cylinder outside negative x side plane is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(-3.1f, 0.0f, 1.5f))));
	}

	SECTION("z-axis cylinder outside positive y side plane is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 3.1f, 1.5f))));
	}

	SECTION("z-axis cylinder outside negative y side plane is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, -3.1f, 1.5f))));
	}

	SECTION("z-axis cylinder near pyramid corner still intersects") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.75f, 0.75f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(1.8f, 1.8f, 1.75f))));
	}

	SECTION("z-axis cylinder beyond pyramid corner is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.75f, 0.75f, 1.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(3.0f, 3.0f, 1.75f))));
	}

	SECTION("large z-axis cylinder enclosing pyramid intersects") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(8.0f, 8.0f, 8.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform()));
	}

	SECTION("thin tall z-axis cylinder passing through pyramid axis intersects") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.2f, 0.2f, 8.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform()));
	}

	SECTION("thin tall z-axis cylinder offset from axis misses pyramid") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.2f, 0.2f, 8.0f), CollisionVolume::COLVOL_AXIS_Z);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(3.0f, 0.0f, 0.0f))));
	}

	SECTION("x-axis cylinder crossing the pyramid center intersects") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(3.0f, 0.5f, 0.5f), CollisionVolume::COLVOL_AXIS_X);
		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 0.0f, 1.0f))));
	}

	SECTION("x-axis cylinder fully to the side is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(3.0f, 0.5f, 0.5f), CollisionVolume::COLVOL_AXIS_X);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 3.0f, 1.0f))));
	}

	SECTION("y-axis cylinder crossing the pyramid center intersects") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.5f, 3.0f, 0.5f), CollisionVolume::COLVOL_AXIS_Y);
		CHECK(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 0.0f, 1.0f))));
	}

	SECTION("y-axis cylinder fully to the side is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(0.5f, 3.0f, 0.5f), CollisionVolume::COLVOL_AXIS_Y);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(3.0f, 0.0f, 1.0f))));
	}

	SECTION("x-axis cylinder beyond base face is separated") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.5f, 0.5f, 0.5f), CollisionVolume::COLVOL_AXIS_X);
		CHECK_FALSE(IntersectsVolume(pyramid, pyramidMat, cylinder, MakeTransform(float3(0.0f, 0.0f, 2.6f))));
	}

	// SECTION("rotated z-axis cylinder intersects when tilted through pyramid") {
	// 	const CollisionVolume cylinder = MakeCylinderVolume(float3(0.75f, 0.75f, 2.0f), CollisionVolume::COLVOL_AXIS_Z);
	// 	const CMatrix44f cylMat = MakeTransform(float3(0.5f, 0.0f, 1.0f), EulerAnglesToMatrix33(float3(0.0f, math::HALFPI * 0.5f, 0.0f)));
	// 	CHECK(IntersectsPyramid(pyramid, pyramidMat, cylinder, cylMat));
	// }

	// SECTION("rotated z-axis cylinder separated when tilted and offset away") {
	// 	const CollisionVolume cylinder = MakeCylinderVolume(float3(0.75f, 0.75f, 2.0f), CollisionVolume::COLVOL_AXIS_Z);
	// 	const CMatrix44f cylMat = MakeTransform(float3(3.0f, 0.0f, 1.0f), EulerAnglesToMatrix33(float3(0.0f, math::HALFPI * 0.5f, 0.0f)));
	// 	CHECK_FALSE(IntersectsPyramid(pyramid, pyramidMat, cylinder, cylMat));
	// }

	SECTION("pyramid transformed and cylinder still intersects in world space") {
		const CMatrix44f movedPyramidMat = MakeTransform(float3(10.0f, -2.0f, 3.0f));
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Z);
		const CMatrix44f cylMat = MakeTransform(float3(10.5f, -2.0f, 3.5f));

		CHECK(IntersectsVolume(pyramid, movedPyramidMat, cylinder, cylMat));
	}

	SECTION("pyramid transformed and cylinder separated in world space") {
		const CMatrix44f movedPyramidMat = MakeTransform(float3(10.0f, -2.0f, 3.0f));
		const CollisionVolume cylinder = MakeCylinderVolume(float3(1.0f, 1.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Z);
		const CMatrix44f cylMat = MakeTransform(float3(14.0f, -2.0f, 3.5f));

		CHECK_FALSE(IntersectsVolume(pyramid, movedPyramidMat, cylinder, cylMat));
	}
}
TEST_CASE("CollisionHandler_IntersectPyramidVolume_PyramidVsPyramid")
{
	const CollisionVolume pyramidA = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CollisionVolume pyramidB = MakePyramidVolume(float3(4.0f, 4.0f, 4.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CMatrix44f pyramidAMat = MakeTransform();

	SECTION("identical pyramids intersect") {
		CHECK(IntersectsVolume(pyramidA, pyramidAMat, pyramidB, MakeTransform()));
	}

	SECTION("base-to-apex touching counts as intersecting") {
		// A base plane is at z=+2, B apex is at z=+2 when centered at z=4.
		CHECK(IntersectsVolume(pyramidA, pyramidAMat, pyramidB, MakeTransform(float3(0.0f, 0.0f, 4.0f))));
	}

	SECTION("separated along primary axis") {
		CHECK_FALSE(IntersectsVolume(pyramidA, pyramidAMat, pyramidB, MakeTransform(float3(0.0f, 0.0f, 4.1f))));
	}

	SECTION("oppositely oriented pyramids overlap") {
		const CMatrix44f pyramidBMat = MakeTransform(float3(0.0f, 0.0f, 2.0f), float3(0.0f, 2.0f * HALF_PI, 0.0f));
		CHECK(IntersectsVolume(pyramidA, pyramidAMat, pyramidB, pyramidBMat));
	}

	SECTION("offset pyramids separate laterally") {
		CHECK_FALSE(IntersectsVolume(pyramidA, pyramidAMat, pyramidB, MakeTransform(float3(5.0f, 0.0f, 0.0f))));
	}
}
