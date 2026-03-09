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

static bool Intersects(
	const CollisionVolume& boxVol,
	const CMatrix44f& boxMat,
	const CollisionVolume& otherVol,
	const CMatrix44f& otherMat
) {
	return CCollisionHandler::IntersectBoxVolume(&boxVol, boxMat, &otherVol, otherMat);
}

} // namespace


// NOTE:
// This test file targets the box-vs-volume overlap API added as:
//   static bool CCollisionHandler::IntersectBoxVolume(
//       const CollisionVolume* box,
//       const CMatrix44f& boxMat,
//       const CollisionVolume* vol,
//       const CMatrix44f& volMat);

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsBox")
{
	const CollisionVolume boxA = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CollisionVolume boxB = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CMatrix44f boxAMat = MakeTransform();

	SECTION("containment") {
		const CollisionVolume smallBox = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f));
		CHECK(Intersects(boxA, boxAMat, smallBox, MakeTransform(float3(0.1f, 0.1f, 0.1f))));
	}

	SECTION("overlap") {
		const CMatrix44f boxBMat = MakeTransform(float3(1.5f, 0.0f, 0.0f));
		CHECK(Intersects(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("touching faces counts as intersecting") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.0f, 0.0f, 0.0f));
		CHECK(Intersects(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("separated") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.5f, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("touching corners counts as intersecting") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.0f, 2.0f, 2.0f));
		CHECK(Intersects(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("corner separation") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.1f, 2.1f, 2.1f));
		CHECK_FALSE(Intersects(boxA, boxAMat, boxB, boxBMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsSphere")
{
	const CollisionVolume box    = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CollisionVolume sphere = MakeSphereVolume(1.0f);
	const CMatrix44f boxMat = MakeTransform();

	SECTION("containment") {
		const CMatrix44f sphereMat = MakeTransform(float3(0.5f, 0.5f, 0.5f));
		CHECK(Intersects(box, boxMat, sphere, sphereMat));
	}

	SECTION("overlap") {
		const CMatrix44f sphereMat = MakeTransform(float3(2.75f, 0.0f, 0.0f));
		CHECK(Intersects(box, boxMat, sphere, sphereMat));
	}

	SECTION("tangent sphere counts as intersecting") {
		const CMatrix44f sphereMat = MakeTransform(float3(3.0f, 0.0f, 0.0f));
		CHECK(Intersects(box, boxMat, sphere, sphereMat));
	}

	SECTION("separated") {
		const CMatrix44f sphereMat = MakeTransform(float3(3.1f, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(box, boxMat, sphere, sphereMat));
	}

	SECTION("corner intersection") {
		// Closest box corner to sphere is (2, 2, 2).
		// Distance from sphere center (2.5, 2.5, 2.5) to corner is sqrt(3 * 0.5^2) ≈ 0.866. (0.866 < radius 1.0)
		const CMatrix44f sphereMat = MakeTransform(float3(2.5f, 2.5f, 2.5f));
		CHECK(Intersects(box, boxMat, sphere, sphereMat));
	}

	SECTION("corner separation (verifies not just testing AABBs)") {
		// Individual AABB axes would overlap (Box is [-2, 2], Sphere AABB is [1.8, 3.8])
		// But sphere surface doesn't reach the corner! Distance is sqrt(3 * 0.8^2) ≈ 1.38 (1.38 > radius 1.0)
		const CMatrix44f sphereMat = MakeTransform(float3(2.8f, 2.8f, 2.8f));
		CHECK_FALSE(Intersects(box, boxMat, sphere, sphereMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsEllipsoid")
{
	const CollisionVolume box       = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CollisionVolume ellipsoid = MakeEllipsoidVolume(float3(4.0f, 2.0f, 2.0f));
	const CMatrix44f boxMat = MakeTransform();

	SECTION("overlap") {
		const CMatrix44f ellipsoidMat = MakeTransform(float3(3.25f, 0.0f, 0.0f));
		CHECK(Intersects(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("touching counts as intersecting") {
		const CMatrix44f ellipsoidMat = MakeTransform(float3(4.0f, 0.0f, 0.0f));
		CHECK(Intersects(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("separated") {
		const CMatrix44f ellipsoidMat = MakeTransform(float3(4.1f, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("rotated ellipsoid separates") {
		// Unrotated, it reaches into the box. Rotated by 90 on Y shrinks its X influence.
		const CMatrix44f ellipsoidMat = MakeTransform(float3(4.0f, 0.0f, 0.0f), float3(0.0f, HALF_PI, 0.0f));
		CHECK_FALSE(Intersects(box, boxMat, ellipsoid, ellipsoidMat));
	}

	SECTION("corner intersection") {
		// Ellipsoid scales: X=6, Y=2, Z=2 (radii: X=3, Y=1, Z=1).
		// Center at (4.0, 2.5, 2.0). Box corner is at (2.0, 2.0, 2.0).
		// Relative: x=(-2), y=(-0.5). Ellipsoid Equation: (4/9) + (0.25/1) + 0 = ~0.694 < 1.0 (Intersect!)
		const CollisionVolume ell = MakeEllipsoidVolume(float3(6.0f, 2.0f, 2.0f));
		const CMatrix44f ellMat = MakeTransform(float3(4.0f, 2.5f, 2.0f));
		CHECK(Intersects(box, boxMat, ell, ellMat));
	}

	SECTION("corner separation") {
		// Center at (5.0, 3.0, 2.0). Box corner is at (2.0, 2.0, 2.0).
		// Relative: x=(-3), y=(-1). Ellipsoid Equation: (9/9) + (1/1) + 0 = 2.0 > 1.0 (Separates!)
		const CollisionVolume ell = MakeEllipsoidVolume(float3(6.0f, 2.0f, 2.0f));
		const CMatrix44f ellMat = MakeTransform(float3(5.0f, 3.0f, 2.0f));
		CHECK_FALSE(Intersects(box, boxMat, ell, ellMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_BoxVsCylinder")
{
	const CollisionVolume box = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CMatrix44f boxMat = MakeTransform();

	SECTION("x-axis cylinder bounds (length)") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(6.0f, 2.0f, 2.0f), CollisionVolume::COLVOL_AXIS_X);

		CHECK(Intersects(box, boxMat, cylinder, MakeTransform(float3(4.5f, 0.0f, 0.0f))));
		CHECK(Intersects(box, boxMat, cylinder, MakeTransform(float3(5.0f, 0.0f, 0.0f))));
		CHECK_FALSE(Intersects(box, boxMat, cylinder, MakeTransform(float3(5.2f, 0.0f, 0.0f))));
	}

	SECTION("x-axis cylinder bounds (radial)") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(6.0f, 2.0f, 2.0f), CollisionVolume::COLVOL_AXIS_X);

		CHECK(Intersects(box, boxMat, cylinder, MakeTransform(float3(0.0f, 2.5f, 0.0f))));
		CHECK(Intersects(box, boxMat, cylinder, MakeTransform(float3(0.0f, 3.0f, 0.0f))));
		CHECK_FALSE(Intersects(box, boxMat, cylinder, MakeTransform(float3(0.0f, 3.1f, 0.0f))));
	}

	SECTION("y-axis cylinder") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 6.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Y);

		CHECK(Intersects(box, boxMat, cylinder, MakeTransform(float3(2.75f, 0.0f, 0.0f))));
		CHECK(Intersects(box, boxMat, cylinder, MakeTransform(float3(3.0f, 0.0f, 0.0f))));
		CHECK_FALSE(Intersects(box, boxMat, cylinder, MakeTransform(float3(3.1f, 0.0f, 0.0f))));
	}

	SECTION("z-axis cylinder corner check") {
		const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 2.0f, 6.0f), CollisionVolume::COLVOL_AXIS_Z);

		// Distance from box corner (2, 2) in XY to cylinder core (2.5, 2.5) is ~0.707 < radius 1.0
		CHECK(Intersects(box, boxMat, cylinder, MakeTransform(float3(2.5f, 2.5f, 0.0f))));

		// Distance from box corner (2, 2) to cylinder core (2.8, 2.8) is ~1.13 > radius 1.0 (AABB separates)
		CHECK_FALSE(Intersects(box, boxMat, cylinder, MakeTransform(float3(2.8f, 2.8f, 0.0f))));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_RespectsRotation")
{
	const CollisionVolume boxA = MakeBoxVolume(float3(4.0f, 2.0f, 2.0f));
	const CollisionVolume boxB = MakeBoxVolume(float3(1.0f, 1.0f, 1.0f));

	const CMatrix44f boxAMat = MakeTransform(ZeroVector, float3(0.0f, QUARTER_PI, 0.0f));

	SECTION("rotated box intersects") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.1f, 0.0f, 0.0f));
		CHECK(Intersects(boxA, boxAMat, boxB, boxBMat));
	}

	SECTION("rotated box separates") {
		const CMatrix44f boxBMat = MakeTransform(float3(2.5f, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(boxA, boxAMat, boxB, boxBMat));
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
		CHECK(Intersects(boxA, aMat, boxB, bMatIntersects));

		const CMatrix44f bMatSeparates = MakeTransform(float3(0.0f, 0.0f, 2.9f), float3(0.0f, QUARTER_PI, 0.0f));
		CHECK_FALSE(Intersects(boxA, aMat, boxB, bMatSeparates));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_HandlesRotatedOtherVolume")
{
	const CollisionVolume box = MakeBoxVolume(float3(4.0f, 4.0f, 4.0f));
	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 6.0f, 2.0f), CollisionVolume::COLVOL_AXIS_Y);
	const CMatrix44f boxMat = MakeTransform();

	SECTION("rotated cylinder still intersects") {
		const CMatrix44f cylinderMat = MakeTransform(float3(2.75f, 0.0f, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
		CHECK(Intersects(box, boxMat, cylinder, cylinderMat));
	}

	SECTION("rotated cylinder still separates when moved away") {
		const CMatrix44f cylinderMat = MakeTransform(float3(3.6f, 0.0f, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(box, boxMat, cylinder, cylinderMat));
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
		CHECK(Intersects(boxWithOffset, offsetBoxMat, targetBox, targetMatIntersects));

		// Separated if target moved slightly to the left of the offset bounds
		const CMatrix44f targetMatSeparates = MakeTransform(float3(-0.1f, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(boxWithOffset, offsetBoxMat, targetBox, targetMatSeparates));
	}

	SECTION("negative offsets") {
		const CollisionVolume negativeOffsetBox = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f), float3(-2.0f, -2.0f, -2.0f));

		// Negative offset box reaches up to (-1, -1, -1). Target at origin reaches down to (-1, -1, -1). They touch.
		CHECK(Intersects(negativeOffsetBox, offsetBoxMat, targetBox, MakeTransform(ZeroVector)));

		// Move target box slightly right/up/forward -> they separate.
		CHECK_FALSE(Intersects(negativeOffsetBox, offsetBoxMat, targetBox, MakeTransform(float3(0.1f, 0.1f, 0.1f))));
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
		CHECK_FALSE(Intersects(offsetBox, offsetBoxMat, targetBox, targetMat));
	}

	SECTION("hits rotated offset position") {
		// +10 X rotated 90 degrees around Y becomes either +10 Z or -10 Z depending on engine handedness.
		// We test both to ensure it correctly moved to the Z axis rather than staying on X.
		const CMatrix44f targetMatPosZ = MakeTransform(float3(0.0f, 0.0f, 10.0f));
		const CMatrix44f targetMatNegZ = MakeTransform(float3(0.0f, 0.0f, -10.0f));

		bool hitsRotatedPos = Intersects(offsetBox, offsetBoxMat, targetBox, targetMatPosZ) ||
		                      Intersects(offsetBox, offsetBoxMat, targetBox, targetMatNegZ);

		CHECK(hitsRotatedPos);
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_ThinGeometries")
{
	const CMatrix44f originMat = MakeTransform();

	SECTION("plane vs plane (2D boxes)") {
		const CollisionVolume planeA = MakeBoxVolume(float3(10.0f, 1.f, 10.0f));
		const CollisionVolume planeB = MakeBoxVolume(float3(10.0f, 1.f, 10.0f));

		CHECK(Intersects(planeA, originMat, planeB, originMat));

		// Y-scale is clamped to 1.0, so separation starts just beyond 1.0 on Y.
		const CMatrix44f sepMat = MakeTransform(float3(0.0f, 1.01f, 0.0f));
		CHECK_FALSE(Intersects(planeA, originMat, planeB, sepMat));
	}

	SECTION("line vs line (1D boxes)") {
		const CollisionVolume lineA = MakeBoxVolume(float3(10.0f, 1.f, 0.001f));
		// Perpendicular line
		const CollisionVolume lineB = MakeBoxVolume(float3(1.f, 10.0f, 1.f));

		CHECK(Intersects(lineA, originMat, lineB, originMat));

		// Z-scale is clamped to 1.0, so separation starts just beyond 1.0 on Z.
		const CMatrix44f sepMat = MakeTransform(float3(0.0f, 0.0f, 1.01f));
		CHECK_FALSE(Intersects(lineA, originMat, lineB, sepMat));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_InvertedContainment")
{
	const CollisionVolume smallBox = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CMatrix44f boxMat = MakeTransform();

	SECTION("massive sphere completely surrounds box") {
		const CollisionVolume massiveSphere = MakeSphereVolume(100.0f);
		const CMatrix44f sphereMat = MakeTransform(float3(5.0f, 5.0f, 5.0f)); // Offset center but still totally swallowing the box
		CHECK(Intersects(smallBox, boxMat, massiveSphere, sphereMat));
	}

	SECTION("massive cylinder completely surrounds box") {
		const CollisionVolume massiveCyl = MakeCylinderVolume(float3(100.0f, 100.0f, 100.0f), CollisionVolume::COLVOL_AXIS_Y);
		const CMatrix44f cylMat = MakeTransform(float3(-10.0f, 0.0f, 10.0f));
		CHECK(Intersects(smallBox, boxMat, massiveCyl, cylMat));
	}

	SECTION("massive ellipsoid completely surrounds box") {
		const CollisionVolume massiveEllipsoid = MakeEllipsoidVolume(float3(200.0f, 100.0f, 50.0f));
		const CMatrix44f ellMat = MakeTransform();
		CHECK(Intersects(smallBox, boxMat, massiveEllipsoid, ellMat));
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
		CHECK(Intersects(box, boxMat, cylinder, cylMatTouch));

		// Move slightly away
		const CMatrix44f cylMatSep = MakeTransform(float3(0.0f, 0.0f, 4.01f));
		CHECK_FALSE(Intersects(box, boxMat, cylinder, cylMatSep));
	}

	SECTION("end cap rim touching box corner") {
		// Box corner at (1, 1, 1).
		// Cylinder length reaches Z=1.0 if center is at Z=4.0.
		// Cylinder rim radius is 1.0. If center X is 1.0, and Y is 2.0, rim hits (1, 1, 1).
		const CMatrix44f cylMatRimTouch = MakeTransform(float3(1.0f, 2.0f, 4.0f));
		CHECK(Intersects(box, boxMat, cylinder, cylMatRimTouch));

		// Move cylinder up slightly (Y=2.1), rim misses the box corner
		const CMatrix44f cylMatRimSep = MakeTransform(float3(1.0f, 2.1f, 4.0f));
		CHECK_FALSE(Intersects(box, boxMat, cylinder, cylMatRimSep));
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
		CHECK(Intersects(box, boxMat, pancake, MakeTransform(float3(0.0f, 1.9f, 0.0f))));

		// Lifted just out of reach (Center > 2.0)
		CHECK_FALSE(Intersects(box, boxMat, pancake, MakeTransform(float3(0.0f, 2.1f, 0.0f))));
	}

	SECTION("cigar ellipsoid (highly elongated on X)") {
		// Scales: X=100.0, Y=2.0, Z=2.0 -> Radii: X=50.0, Y=1.0, Z=1.0
		const CollisionVolume cigar = MakeEllipsoidVolume(float3(100.0f, 2.0f, 2.0f));

		// Passing right next to the Z face, slightly intersecting (Center < 2.0)
		CHECK(Intersects(box, boxMat, cigar, MakeTransform(float3(0.0f, 0.0f, 1.9f))));

		// Moved slightly away on Z (Center > 2.0)
		CHECK_FALSE(Intersects(box, boxMat, cigar, MakeTransform(float3(0.0f, 0.0f, 2.1f))));
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
		CHECK_FALSE(Intersects(box, boxMatUnrotated, sphere, sphereMat));
	}

	SECTION("rotated box corner hits the sphere") {
		// Rotating the box 45 degrees around Z turns its flat X-face into a pointing diamond.
		// The corner now stretches to X = sqrt(1^2 + 1^2) = 1.414.
		// Sphere edge is at X = 1.2. Because 1.414 > 1.2, the sharp corner pierces the sphere.
		const CMatrix44f boxMatRotated = MakeTransform(ZeroVector, float3(0.0f, 0.0f, QUARTER_PI));
		CHECK(Intersects(box, boxMatRotated, sphere, sphereMat));
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
		CHECK_FALSE(Intersects(box, boxMat, ellipsoid, ellMatUnrotated));
	}

	SECTION("ellipsoid rotated perfectly toward box intersects") {
		// Spring's positive Z rotation turns +X toward -Y, so -45 degrees points the major axis
		// along the diagonal from (4, 4, 0) toward the box.
		// Distance from (4,4,0) to box corner (1,1,0) is sqrt(3^2 + 3^2) ≈ 4.24.
		// Since the rotated major radius (5.0) is greater than 4.24, it reaches the corner.
		const CMatrix44f ellMatAimed = MakeTransform(pos, float3(0.0f, 0.0f, -QUARTER_PI));
		CHECK(Intersects(box, boxMat, ellipsoid, ellMatAimed));
	}

	SECTION("ellipsoid rotated perpendicular to box misses") {
		// +45 degrees leaves the major axis perpendicular to the diagonal back toward the box.
		// Only the minor radius (1.0) faces the box, which is much less than the 4.24 distance.
		const CMatrix44f ellMatAway = MakeTransform(pos, float3(0.0f, 0.0f, QUARTER_PI));
		CHECK_FALSE(Intersects(box, boxMat, ellipsoid, ellMatAway));
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
		CHECK_FALSE(Intersects(box, boxMat, cylinder, cylMatUpright));
	}

	SECTION("slanted towards box hits") {
		// Spring's positive X rotation tilts +Y toward -Z, so -45 degrees slants the lower end-cap
		// downward and inward toward the box.
		// Tip travel is approx 5.0 * 0.707 = 3.53 units on Y and Z axes.
		// New tip position: Y = 4.0 - 3.53 = 0.47, Z = 4.0 - 3.53 = 0.47.
		// Since (0, 0.47, 0.47) is inside the box [-1, 1], it pierces the top face!
		const CMatrix44f cylMatSlanted = MakeTransform(float3(0.0f, 4.0f, 4.0f), float3(-QUARTER_PI, 0.0f, 0.0f));
		CHECK(Intersects(box, boxMat, cylinder, cylMatSlanted));
	}

	SECTION("slanted too far out misses") {
		// Same 45 degree slant, but moved further out.
		// Tip reaches Y = 6.0 - 3.53 = 2.47. Z = 6.0 - 3.53 = 2.47.
		// Box max is 1.0, so it stops short.
		const CMatrix44f cylMatSlantedFar = MakeTransform(float3(0.0f, 6.0f, 6.0f), float3(-QUARTER_PI, 0.0f, 0.0f));
		CHECK_FALSE(Intersects(box, boxMat, cylinder, cylMatSlantedFar));
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
		CHECK(Intersects(boxA, matA, boxB, matB_Hits));
	}

	SECTION("glancing multi-axis corner touch") {
		// For this specific three-axis rotation, the limiting SAT axes are the A.y x B.z and
		// A.z x B.z cross-products, which separate at about 2.828 on each center coordinate.
		// Keep boxB just inside that bound so this remains a true near-tangent overlap.
		const CMatrix44f matB_Glance = MakeTransform(float3(2.82f, 2.82f, 2.82f), crazyRotation);
		CHECK(Intersects(boxA, matA, boxB, matB_Glance));
		const CMatrix44f matB_Far = MakeTransform(float3(2.89f, 2.89f, 2.89f), crazyRotation);
		CHECK_FALSE(Intersects(boxA, matA, boxB, matB_Far));
	}

	SECTION("multi-axis separation") {
		// Moved entirely out of the collision zone.
		const CMatrix44f matB_Misses = MakeTransform(float3(4.5f, 4.5f, 4.5f), crazyRotation);
		CHECK_FALSE(Intersects(boxA, matA, boxB, matB_Misses));
	}
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_AxisAligned_Performance")
{
	SKIP("performance");
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 2.0f, 6.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CMatrix44f boxMat = MakeTransform();

	std::array<CMatrix44f, 64> hitMats;
	std::array<CMatrix44f, 64> missMats;

	for (std::size_t i = 0; i < hitMats.size(); ++i) {
		const float x = 0.75f + ((i % 8) * 0.05f);
		hitMats[i] = MakeTransform(float3(x, 2.0f, 4.0f));
		missMats[i] = MakeTransform(float3(x, 2.2f, 4.0f));
	}

	const std::int64_t iterations = 10000000;
	volatile std::int64_t sink = 0;

	LOG("CollisionHandler axis-aligned box-volume:");
	{
		ScopedOnceTimer timer(" axis-aligned box vs cylinder (intersecting)");
		for (std::int64_t j = iterations; j > 0; --j) {
			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, hitMats[j % hitMats.size()])) * j;
		}
	}
	{
		ScopedOnceTimer timer(" axis-aligned box vs cylinder (separated)");
		for (std::int64_t j = iterations; j > 0; --j) {
			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, missMats[j % missMats.size()])) * j;
		}
	}

	CHECK((sink | 1) != 0);
}

TEST_CASE("CollisionHandler_IntersectBoxVolume_Rotated_Performance")
{
	SKIP("performance");
	const CollisionVolume box = MakeBoxVolume(float3(2.0f, 2.0f, 2.0f));
	const CollisionVolume cylinder = MakeCylinderVolume(float3(2.0f, 2.0f, 6.0f), CollisionVolume::COLVOL_AXIS_Z);
	const CMatrix44f boxMat = MakeTransform(ZeroVector, float3(0.0f, QUARTER_PI, 0.0f));

	std::array<CMatrix44f, 64> hitMats;
	std::array<CMatrix44f, 64> missMats;

	for (std::size_t i = 0; i < hitMats.size(); ++i) {
		const float y = 0.75f + ((i % 8) * 0.05f);
		hitMats[i] = MakeTransform(float3(2.75f, y, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
		missMats[i] = MakeTransform(float3(3.6f, y, 0.0f), float3(HALF_PI, 0.0f, 0.0f));
	}

	const std::int64_t iterations = 10000000;
	volatile std::int64_t sink = 0;

	LOG("CollisionHandler rotated box-volume:");
	{
		ScopedOnceTimer timer(" rotated box vs cylinder (intersecting)");
		for (std::int64_t j = iterations; j > 0; --j) {
			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, hitMats[j % hitMats.size()])) * j;
		}
	}
	{
		ScopedOnceTimer timer(" rotated box vs cylinder (separated)");
		for (std::int64_t j = iterations; j > 0; --j) {
			sink ^= static_cast<std::int64_t>(Intersects(box, boxMat, cylinder, missMats[j % missMats.size()])) * j;
		}
	}

	CHECK((sink | 1) != 0);
}
