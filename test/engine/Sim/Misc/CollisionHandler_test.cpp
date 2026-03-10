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

constexpr float EPS = 0.001f;
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

struct TestFrustum {
	float4 l;
	float4 r;
	float4 t;
	float4 b;
	float4 n;
	float4 f;
};

static TestFrustum MakeAxisAlignedFrustum(const float3& mins, const float3& maxs)
{
	return {
		{ 1.0f,  0.0f,  0.0f, -mins.x},
		{-1.0f,  0.0f,  0.0f,  maxs.x},
		{ 0.0f, -1.0f,  0.0f,  maxs.y},
		{ 0.0f,  1.0f,  0.0f, -mins.y},
		{ 0.0f,  0.0f,  1.0f, -mins.z},
		{ 0.0f,  0.0f, -1.0f,  maxs.z},
	};
}

static float4 TransformPlane(const float4& plane, const CMatrix44f& mat)
{
	float3 normal(plane.x, plane.y, plane.z);
	float3 point = normal * (-plane.w);

	normal = float3(mat.Mul(float4(normal, 0.0f))).SafeANormalize();
	point = mat.Mul(point);

	return float4(normal, -normal.dot(point));
}

static TestFrustum TransformFrustum(const TestFrustum& frustum, const CMatrix44f& mat)
{
	return {
		TransformPlane(frustum.l, mat),
		TransformPlane(frustum.r, mat),
		TransformPlane(frustum.t, mat),
		TransformPlane(frustum.b, mat),
		TransformPlane(frustum.n, mat),
		TransformPlane(frustum.f, mat),
	};
}

static float4 ToVolumePlane(const CMatrix44f& invMat, const float4& plane)
{
	float3 normal(plane.x, plane.y, plane.z);
	float3 point = normal * (-plane.w);

	normal = float3(invMat.Mul(float4(normal, 0.0f))).SafeANormalize();
	point = invMat.Mul(point);

	return float4(normal, -normal.dot(point));
}

static bool Intersects(const CollisionVolume& volume, const TestFrustum& frustum)
{
	return CCollisionHandler::IntersectFrustum(&volume, frustum.l, frustum.r, frustum.t, frustum.b, frustum.n, frustum.f);
}

static bool Intersects(const CollisionVolume& volume, const CMatrix44f& volumeMat, const TestFrustum& frustum)
{
	const CMatrix44f invMat = volumeMat.InvertAffine();

	return CCollisionHandler::IntersectFrustum(
		&volume,
		ToVolumePlane(invMat, frustum.l),
		ToVolumePlane(invMat, frustum.r),
		ToVolumePlane(invMat, frustum.t),
		ToVolumePlane(invMat, frustum.b),
		ToVolumePlane(invMat, frustum.n),
		ToVolumePlane(invMat, frustum.f)
	);
}

} // namespace

TEST_CASE("CollisionHandler_IntersectFrustum_BoxVolume")
{
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));

	SECTION("containment") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-2.0f, -2.0f, -2.0f), float3(2.0f, 2.0f, 2.0f));
		CHECK(Intersects(box, frustum));
	}

	SECTION("overlap") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(0.5f, -2.0f, -2.0f), float3(2.5f, 2.0f, 2.0f));
		CHECK(Intersects(box, frustum));
	}

	SECTION("touching faces counts as intersecting") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.0f, -2.0f, -2.0f), float3(3.0f, 2.0f, 2.0f));
		CHECK(Intersects(box, frustum));
	}

	SECTION("separated") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.1f, -2.0f, -2.0f), float3(3.1f, 2.0f, 2.0f));
		CHECK_FALSE(Intersects(box, frustum));
	}

	SECTION("touching corners counts as intersecting") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.0f, 1.0f, 1.0f), float3(3.0f, 3.0f, 3.0f));
		CHECK(Intersects(box, frustum));
	}

	SECTION("corner separation") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.1f, 1.1f, 1.1f), float3(3.1f, 3.1f, 3.1f));
		CHECK_FALSE(Intersects(box, frustum));
	}
}

TEST_CASE("CollisionHandler_IntersectFrustum_SphereVolume")
{
	const CollisionVolume sphere = MakeSphereVolume(1.0f);

	SECTION("containment") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-2.0f, -2.0f, -2.0f), float3(2.0f, 2.0f, 2.0f));
		CHECK(Intersects(sphere, frustum));
	}

	SECTION("overlap") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(0.25f, -2.0f, -2.0f), float3(2.0f, 2.0f, 2.0f));
		CHECK(Intersects(sphere, frustum));
	}

	SECTION("touching face counts as intersecting") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.0f, -2.0f, -2.0f), float3(2.0f, 2.0f, 2.0f));
		CHECK(Intersects(sphere, frustum));
	}

	SECTION("separated") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.01f, -2.0f, -2.0f), float3(2.5f, 2.0f, 2.0f));
		CHECK_FALSE(Intersects(sphere, frustum));
	}
}

TEST_CASE("CollisionHandler_IntersectFrustum_RotatedBoxVolume")
{
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-2.0f, -2.0f, -2.0f), float3(2.0f, 2.0f, 2.0f));

	SECTION("rotated volume containment") {
		const CMatrix44f boxMat = MakeTransform(ZeroVector, float3(QUARTER_PI, QUARTER_PI, 0.0f));
		CHECK(Intersects(box, boxMat, frustum));
	}

	SECTION("rotated volume separated") {
		const CMatrix44f boxMat = MakeTransform(float3(4.0f, 0.0f, 0.0f), float3(QUARTER_PI, QUARTER_PI, 0.0f));
		CHECK_FALSE(Intersects(box, boxMat, frustum));
	}
}

TEST_CASE("CollisionHandler_IntersectFrustum_RotatedFrustum")
{
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const TestFrustum baseFrustum = MakeAxisAlignedFrustum(float3(-2.0f, -2.0f, -2.0f), float3(2.0f, 2.0f, 2.0f));

	SECTION("rotated frustum containment") {
		const TestFrustum frustum = TransformFrustum(baseFrustum, MakeTransform(ZeroVector, float3(0.0f, QUARTER_PI, 0.0f)));
		CHECK(Intersects(box, MakeTransform(), frustum));
	}

	SECTION("rotated frustum separated") {
		const TestFrustum frustum = TransformFrustum(baseFrustum, MakeTransform(ZeroVector, float3(0.0f, QUARTER_PI, 0.0f)));
		const CMatrix44f boxMat = MakeTransform(float3(6.0f, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(box, boxMat, frustum));
	}

	SECTION("rotated frustum and rotated volume overlap") {
		const TestFrustum frustum = TransformFrustum(baseFrustum, MakeTransform(float3(0.5f, 0.0f, 0.0f), float3(0.0f, QUARTER_PI, 0.0f)));
		const CMatrix44f boxMat = MakeTransform(float3(1.4f, 0.0f, 0.0f), float3(0.0f, HALF_PI * 0.5f, 0.0f));
		CHECK(Intersects(box, boxMat, frustum));
	}
}

TEST_CASE("CollisionHandler_IntersectFrustum_CylinderVolume")
{
	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 4.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Y);

	SECTION("containment") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-2.0f, -3.0f, -2.0f), float3(2.0f, 3.0f, 2.0f));
		CHECK(Intersects(cylinder, frustum));
	}

	SECTION("overlap") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(0.5f, -3.0f, -2.0f), float3(2.5f, 3.0f, 2.0f));
		CHECK(Intersects(cylinder, frustum));
	}

	SECTION("separated") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.1f, -3.0f, -2.0f), float3(3.0f, 3.0f, 2.0f));
		CHECK_FALSE(Intersects(cylinder, frustum));
	}

	SECTION("rotated cylinder containment") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-2.5f, -3.0f, -2.5f), float3(2.5f, 3.0f, 2.5f));
		const CMatrix44f cylinderMat = MakeTransform(ZeroVector, float3(0.0f, 0.0f, QUARTER_PI));
		CHECK(Intersects(cylinder, cylinderMat, frustum));
	}
}

TEST_CASE("CollisionHandler_IntersectFrustum_EllipsoidVolume")
{
	const CollisionVolume ellipsoid = MakeEllipsoidVolume(float3(4.0f, 2.0f, 2.0f));

	SECTION("containment") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-3.0f, -2.0f, -2.0f), float3(3.0f, 2.0f, 2.0f));
		CHECK(Intersects(ellipsoid, frustum));
	}

	SECTION("overlap") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.25f, -2.0f, -2.0f), float3(3.5f, 2.0f, 2.0f));
		CHECK(Intersects(ellipsoid, frustum));
	}

	SECTION("separated") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(2.1f, -2.0f, -2.0f), float3(4.0f, 2.0f, 2.0f));
		CHECK_FALSE(Intersects(ellipsoid, frustum));
	}

	SECTION("rotated ellipsoid containment") {
		const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-2.5f, -2.5f, -2.5f), float3(2.5f, 2.5f, 2.5f));
		const CMatrix44f ellipsoidMat = MakeTransform(ZeroVector, float3(0.0f, 0.0f, QUARTER_PI));
		CHECK(Intersects(ellipsoid, ellipsoidMat, frustum));
	}
}


TEST_CASE("CollisionHandler_Rigorous_Precision")
{
    const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f)); // Rad 1.0
    const CMatrix44f identity;

    SECTION("Numerical Near-Miss") {
        // Frustum edge is 1.001 units away from a 1.0 unit radius box
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.0f + EPS, -1.0f, -1.0f), float3(3.0f, 1.0f, 1.0f));
        CHECK_FALSE(Intersects(box, identity, frustum));
    }

    SECTION("Numerical Near-Hit") {
        // Frustum edge is 0.999 units away (overlaps by 0.001)
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(1.0f - EPS, -1.0f, -1.0f), float3(3.0f, 1.0f, 1.0f));
        CHECK(Intersects(box, identity, frustum));
    }
}

TEST_CASE("CollisionHandler_Rigorous_Offsets")
{
    // Volume is size 2, but offset by 10 units in X
    const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f), float3(10.0f, 0.0f, 0.0f));

    SECTION("Volume Offset correctly handled in World Space") {
        // Frustum at origin should NOT hit the offset box
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-1.0f, -1.0f, -1.0f), float3(1.0f, 1.0f, 1.0f));
        CHECK_FALSE(Intersects(box, CMatrix44f(), frustum));

        // Frustum at X=10 should hit
        const TestFrustum frustumHit = MakeAxisAlignedFrustum(float3(9.0f, -1.0f, -1.0f), float3(11.0f, 1.0f, 1.0f));
        CHECK(Intersects(box, CMatrix44f(), frustumHit));
    }
}

TEST_CASE("CollisionHandler_Rigorous_CylinderAxes")
{
    // Test that the engine correctly handles cylinders oriented along different axes
    const float3 scales(2.0f, 4.0f, 2.0f); // Length 4, Radius 1
    const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-0.5f, -0.5f, -0.5f), float3(0.5f, 0.5f, 0.5f));

    SECTION("Cylinder Y-Axis (Default)") {
        const CollisionVolume cyl = MakeCylinderVolume(scales, CollisionVolume::COLVOL_AXIS_Y);
        CHECK(Intersects(cyl, CMatrix44f(), frustum));
    }

    SECTION("Cylinder X-Axis") {
        const CollisionVolume cyl = MakeCylinderVolume(scales, CollisionVolume::COLVOL_AXIS_X);
        CHECK(Intersects(cyl, CMatrix44f(), frustum));
    }

    SECTION("Cylinder Z-Axis") {
        const CollisionVolume cyl = MakeCylinderVolume(scales, CollisionVolume::COLVOL_AXIS_Z);
        CHECK(Intersects(cyl, CMatrix44f(), frustum));
    }
}

TEST_CASE("CollisionHandler_Rigorous_Scaling")
{
    const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f)); // Unit box

    SECTION("Non-uniform World Scaling") {
        // Stretch the box 10x in X via the Matrix
        CMatrix44f stretchMat;
        stretchMat.Scale(float3(10.0f, 1.0f, 1.0f));

        // Frustum at X=8 (should hit because box is stretched to -10...+10)
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(7.0f, -0.5f, -0.5f), float3(9.0f, 0.5f, 0.5f));
        CHECK(Intersects(box, stretchMat, frustum));
    }
}

TEST_CASE("CollisionHandler_Rigorous_PerspectiveFrustum")
{
    const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));

    SECTION("Converging Perspective Planes") {
        // Create a frustum where the left/right planes are not parallel (V-shape)
        // Plane Eq: ax + by + cz + d = 0. Normal (1, 0, 1) is a 45 degree inward slant.
        TestFrustum perspective;
        perspective.l = float4( 0.707f, 0.0f, 0.707f, 0.0f); // Left plane slanting right
        perspective.r = float4(-0.707f, 0.0f, 0.707f, 0.0f); // Right plane slanting left
        perspective.t = float4(0.0f, -1.0f, 0.0f, 10.0f);
        perspective.b = float4(0.0f,  1.0f, 0.0f, 10.0f);
        perspective.n = float4(0.0f, 0.0f,  1.0f, -1.0f);
        perspective.f = float4(0.0f, 0.0f, -1.0f, 20.0f);

        // A box at Z=5 should be inside the "V"
        CMatrix44f boxMat; boxMat.SetPos(float3(0.0f, 0.0f, 5.0f));
        CHECK(Intersects(box, boxMat, perspective));

        // A box at Z=5, X=10 should be outside the "V"
        boxMat.SetPos(float3(10.0f, 0.0f, 5.0f));
        CHECK_FALSE(Intersects(box, boxMat, perspective));
    }
}

TEST_CASE("CollisionHandler_Rigorous_Degenerate")
{
    SECTION("Zero Scale Volume") {
        const CollisionVolume tinyBox = MakeBoxVolume(float3(0.0f, 0.0f, 0.0f));
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-1.0f, -1.0f, -1.0f), float3(1.0f, 1.0f, 1.0f));

        // Technically a point at origin is inside the frustum.
        // We verify the engine doesn't crash or return NaN.
        CHECK(Intersects(tinyBox, CMatrix44f(), frustum));
    }

    SECTION("Inverse Scale Matrix") {
        const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
        CMatrix44f flipMat;
        flipMat.Scale(float3(-1.0f, 1.0f, 1.0f)); // Mirrored X

        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-0.5f, -0.5f, -0.5f), float3(0.5f, 0.5f, 0.5f));
        // Mirrored box should still intersect
        CHECK(Intersects(box, flipMat, frustum));
    }
}


TEST_CASE("CollisionHandler_Rigorous_ExtremeCoordinates")
{
    // Spring maps can be very large. Tests for precision loss at distance.
    const float farPos = 10000.0f;
    const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));

    CMatrix44f boxMat;
    boxMat.SetPos(float3(farPos, 0.0f, farPos));

    SECTION("High-Coordinate Intersection") {
        const TestFrustum frustum = MakeAxisAlignedFrustum(
            float3(farPos - 1.0f, -1.0f, farPos - 1.0f),
            float3(farPos + 1.0f,  1.0f, farPos + 1.0f)
        );
        CHECK(Intersects(box, boxMat, frustum));
    }

    SECTION("High-Coordinate Separation") {
        const TestFrustum frustum = MakeAxisAlignedFrustum(
            float3(farPos + 5.0f, -1.0f, farPos + 5.0f),
            float3(farPos + 7.0f,  1.0f, farPos + 7.0f)
        );
        CHECK_FALSE(Intersects(box, boxMat, frustum));
    }
}

TEST_CASE("CollisionHandler_Rigorous_RotationPivot")
{
    // Test: A box offset from its origin, then rotated by the world matrix.
    // If the box is at (10,0,0) and we rotate the matrix 180 degrees around Y,
    // the box should now be at (-10,0,0).
    const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f), float3(10.0f, 0.0f, 0.0f));

    CMatrix44f rotMat;
    rotMat.RotateY(M_PI); // 180 degrees

    SECTION("Box rotated to opposite side") {
        // Frustum at (-10, 0, 0)
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-11.0f, -1.0f, -1.0f), float3(-9.0f, 1.0f, 1.0f));
        CHECK(Intersects(box, rotMat, frustum));

        // Frustum at old position (10, 0, 0) should now be empty
        const TestFrustum frustumOld = MakeAxisAlignedFrustum(float3(9.0f, -1.0f, -1.0f), float3(11.0f, 1.0f, 1.0f));
        CHECK_FALSE(Intersects(box, rotMat, frustumOld));
    }
}

TEST_CASE("CollisionHandler_Rigorous_CylinderCapping")
{
    // Cylinders are tricky because they are a hybrid of a circle and a rectangle.
    // Test intersection specifically against the "Flat" ends (caps).
    const CollisionVolume cyl = MakeCylinderVolume(float3(2.0f, 10.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Y); // Height 10
    const CMatrix44f identity;

    SECTION("Frustum strictly above cap") {
        // Cylinder ends at Y=5. Frustum starts at Y=5.001
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-1.0f, 5.0f + EPS, -1.0f), float3(1.0f, 7.0f, 1.0f));
        CHECK_FALSE(Intersects(cyl, identity, frustum));
    }

    SECTION("Frustum slicing through cap") {
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-1.0f, 4.0f, -1.0f), float3(1.0f, 6.0f, 1.0f));
        CHECK(Intersects(cyl, identity, frustum));
    }
}

TEST_CASE("CollisionHandler_Rigorous_TheCornerCase")
{
    /**
     * The "Corner Case" (False Positive):
     * Standard frustum tests check if the box is "outside any one plane".
     * If a box is outside two planes (e.g. Left and Top) it can technically be
     * "outside" the frustum but still "inside" the half-spaces of all planes
     * if the check isn't mathematically rigorous (Separating Axis Theorem).
     */

    // Create a narrow perspective frustum
    TestFrustum narrow;
    narrow.l = float4( 0.9f, 0.0f, 0.1f, 0.0f);
    narrow.r = float4(-0.9f, 0.0f, 0.1f, 0.0f);
    narrow.t = float4(0.0f, -0.9f, 0.1f, 0.0f);
    narrow.b = float4(0.0f,  0.9f, 0.1f, 0.0f);
    narrow.n = float4(0.0f, 0.0f,  1.0f, -1.0f);
    narrow.f = float4(0.0f, 0.0f, -1.0f, 100.0f);

    SECTION("Box in Diagonal Dead-Zone") {
        // Place a box diagonally outside the corner where Top and Left planes meet.
        // A simple "is it behind plane A or B or C" check might pass (true),
        // but it is not actually inside the frustum volume.
        const CollisionVolume box = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f));
        CMatrix44f boxMat;
        boxMat.SetPos(float3(-20.0f, 20.0f, 50.0f));

        // Note: Many engines allow this false positive for performance (conservative culling).
        // This test identifies if Spring is using Conservative or Exact intersection.
        bool result = Intersects(box, boxMat, narrow);

        // If Spring aims for accuracy, this should be FALSE.
        // If Spring aims for speed, this might be TRUE.
        // We use a warning or log here or decide on the expected engine behavior.
        CHECK_FALSE(result);
    }
}

TEST_CASE("CollisionHandler_Rigorous_ComplexTransformChain")
{
    // Test combination: Scale -> Rotate -> Offset -> Translate
    // This ensures the matrix stack/math doesn't drift.
    CollisionVolume box = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f), float3(0.0f, 5.0f, 0.0f));

    CMatrix44f mat;
    mat.Translate(float3(0.0f, 10.0f, 0.0f)); // Move to 10
    mat.RotateX(HALF_PI);                     // Rotate 90 deg: The Y-offset of 5 becomes a Z-offset
    mat.Scale(float3(1.0f, 1.0f, 10.0f));     // Scale Z by 10

    // Expected center: Pos(0, 10, 0) + (Offset(0, 5, 0) rotated 90deg X) * ScaleZ(10)
    // = (0, 10, 0) + (0, 0, 5) * 10 = (0, 10, 50)

    SECTION("Complex Chain Intersection") {
        const TestFrustum frustum = MakeAxisAlignedFrustum(float3(-1.0f, 9.0f, 49.0f), float3(1.0f, 11.0f, 51.0f));
        CHECK(Intersects(box, mat, frustum));
    }
}