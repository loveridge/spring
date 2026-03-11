/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "CollisionHandler.h"
#include "CollisionVolume.h"
#include "Map/ReadMap.h"  // mapDims
#include "Sim/Misc/GlobalConstants.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "Sim/Objects/SolidObject.h"
#include "System/Log/ILog.h"
#include "System/Matrix44f.h"

#include "System/Misc/TracyDefs.h"

#include <algorithm>


namespace
{
	struct GJKSimplex {
		void PushFront(const float3& p)
		{
			for (int i = std::min(size, 3); i > 0; --i)
				points[i] = points[i - 1];

			points[0] = p;
			size = std::min(size + 1, 4);
		}

		void Assign(float3 a)
		{
			points[0] = a;
			size = 1;
		}
		void Assign(float3 a, float3 b)
		{
			points[0] = a;
			points[1] = b;
			size = 2;
		}
		void Assign(float3 a, float3 b, float3 c)
		{
			points[0] = a;
			points[1] = b;
			points[2] = c;
			size = 3;
		}

		int size = 0;
		float3 points[4] = {ZeroVector, ZeroVector, ZeroVector, ZeroVector};
	};

	static constexpr int GJK_MAX_ITERATIONS = 32;

	static float3 TransformDirection(const CMatrix44f& m, const float3& d)
	{
		return (m.Mul(d) - m.Mul(ZeroVector));
	}

	static float3 TripleCross(const float3& a, const float3& b, const float3& c)
	{
		return (a.cross(b)).cross(c);
	}

	static float3 PerpendicularVector(const float3& v)
	{
		const float3 axis =
			(math::fabs(v.x) <= math::fabs(v.y) && math::fabs(v.x) <= math::fabs(v.z))
				? float3(1.0f, 0.0f, 0.0f)
				: ((math::fabs(v.y) <= math::fabs(v.z)) ? float3(0.0f, 1.0f, 0.0f)
				                                        : float3(0.0f, 0.0f, 1.0f));

		float3 p = v.cross(axis);

		if (p.SqLength() < COLLISION_VOLUME_EPS)
			p = float3(v.y, -v.x, 0.0f);

		return p;
	}

	static float3 EdgeTowardOriginDirection(const float3& edge, const float3& ao)
	{
		float3 dir = TripleCross(edge, ao, edge);

		if (dir.SqLength() < COLLISION_VOLUME_EPS)
			dir = PerpendicularVector(edge);

		return dir;
	}

	static float3 GetSupportPointLocal(const CollisionVolume* v, const float3& dir)
	{
		const float3& ahs = v->GetHScales();
		float3 p = v->GetOffsets();

		switch (v->GetVolumeType()) {
			case CollisionVolume::COLVOL_TYPE_BOX: {
				p.x += (dir.x >= 0.0f) ? ahs.x : -ahs.x;
				p.y += (dir.y >= 0.0f) ? ahs.y : -ahs.y;
				p.z += (dir.z >= 0.0f) ? ahs.z : -ahs.z;
			} break;

			case CollisionVolume::COLVOL_TYPE_SPHERE: {
				const float dirLen = dir.Length();
				const float radius = ahs.x;

				if (dirLen > COLLISION_VOLUME_EPS)
					p += dir * (radius / dirLen);
			} break;

			case CollisionVolume::COLVOL_TYPE_ELLIPSOID: {
				const float denom =
					math::sqrt((ahs.x * ahs.x * dir.x * dir.x) + (ahs.y * ahs.y * dir.y * dir.y) +
					           (ahs.z * ahs.z * dir.z * dir.z));

				if (denom > COLLISION_VOLUME_EPS) {
					p.x += (ahs.x * ahs.x * dir.x) / denom;
					p.y += (ahs.y * ahs.y * dir.y) / denom;
					p.z += (ahs.z * ahs.z * dir.z) / denom;
				}
			} break;

			case CollisionVolume::COLVOL_TYPE_CYLINDER: {
				const int pAx = v->GetPrimaryAxis();
				const int sAx0 = v->GetSecondaryAxis(0);
				const int sAx1 = v->GetSecondaryAxis(1);
				const float denom = math::sqrt((ahs[sAx0] * ahs[sAx0] * dir[sAx0] * dir[sAx0]) +
				                               (ahs[sAx1] * ahs[sAx1] * dir[sAx1] * dir[sAx1]));

				p[pAx] += (dir[pAx] >= 0.0f) ? ahs[pAx] : -ahs[pAx];

				if (denom > COLLISION_VOLUME_EPS) {
					p[sAx0] += (ahs[sAx0] * ahs[sAx0] * dir[sAx0]) / denom;
					p[sAx1] += (ahs[sAx1] * ahs[sAx1] * dir[sAx1]) / denom;
				}
			} break;

			case CollisionVolume::COLVOL_TYPE_PYRAMID: {
				const int pAx = v->GetPrimaryAxis();
				const int sAx0 = v->GetSecondaryAxis(0);
				const int sAx1 = v->GetSecondaryAxis(1);

				const float h = ahs[pAx];
				const float baseCoeff = dir[pAx] +
				                        (math::fabs(dir[sAx0]) * ahs[sAx0]) / (2.0f * h) +
				                        (math::fabs(dir[sAx1]) * ahs[sAx1]) / (2.0f * h);

				if (baseCoeff >= 0.0f) {
					p[pAx] += h;
					p[sAx0] += (dir[sAx0] >= 0.0f) ? ahs[sAx0] : -ahs[sAx0];
					p[sAx1] += (dir[sAx1] >= 0.0f) ? ahs[sAx1] : -ahs[sAx1];
				} else {
					p[pAx] -= h;
				}
			} break;
		}

		return p;
	}

	static float3 GetSupportPointInReferenceSpace(const CollisionVolume* v, const CMatrix44f& vToRef,
	                                             const CMatrix44f& refToV, const float3& dirRef)
	{
		const float3 dirLocal = TransformDirection(refToV, dirRef);
		const float3 pointLocal = GetSupportPointLocal(v, dirLocal);
		return vToRef.Mul(pointLocal);
	}

	static float3 GetMinkowskiSupportPointInReferenceSpace(const CollisionVolume* refVol,
	                                                      const CollisionVolume* otherVol,
	                                                      const CMatrix44f& otherToRef,
	                                                      const CMatrix44f& refToOther,
	                                                      const float3& dirRef)
	{
		const float3 pRef = GetSupportPointLocal(refVol, dirRef);
		const float3 pOther =
			GetSupportPointInReferenceSpace(otherVol, otherToRef, refToOther, -dirRef);
		return (pRef - pOther);
	}

	static bool GetAxisAlignedMapping(const CMatrix44f& relMat, int axisMap[3], int axisSign[3])
	{
		constexpr float axisEps = 1.0f - 1e-4f;
		constexpr float orthoEps = 1e-4f;
		const float3 basis[3] = {
			float3(1.0f, 0.0f, 0.0f),
			float3(0.0f, 1.0f, 0.0f),
			float3(0.0f, 0.0f, 1.0f),
		};
		bool usedAxes[3] = {false, false, false};

		for (int i = 0; i < 3; ++i) {
			const float3 axis = TransformDirection(relMat, basis[i]);
			const float axisLen = axis.Length();

			if (axisLen <= COLLISION_VOLUME_EPS)
				return false;

			const float3 normAxis = axis / axisLen;
			const float3 absAxis = float3::fabs(normAxis);

			int majorAxis = 0;
			if (absAxis.y > absAxis[majorAxis])
				majorAxis = 1;
			if (absAxis.z > absAxis[majorAxis])
				majorAxis = 2;

			if (usedAxes[majorAxis])
				return false;
			if (absAxis[majorAxis] < axisEps)
				return false;

			for (int j = 0; j < 3; ++j) {
				if (j == majorAxis)
					continue;
				if (absAxis[j] > orthoEps)
					return false;
			}

			usedAxes[majorAxis] = true;
			axisMap[i] = majorAxis;
			axisSign[i] = (normAxis[majorAxis] >= 0.0f) ? 1 : -1;
		}

		return true;
	}

	static float3 GetAlignedHalfScales(const CollisionVolume* vol, const int axisMap[3])
	{
		float3 alignedHScales = ZeroVector;
		const float3& volHScales = vol->GetHScales();

		for (int localAxis = 0; localAxis < 3; ++localAxis)
			alignedHScales[axisMap[localAxis]] = volHScales[localAxis];

		return alignedHScales;
	}

	static float GetWeightedSqDistToBox(const float3& point, const float3& boxCenter,
	                                    const float3& boxHScales, const float3& weights)
	{
		float weightedSqDist = 0.0f;

		for (int axis = 0; axis < 3; ++axis) {
			const float boxMin = boxCenter[axis] - boxHScales[axis];
			const float boxMax = boxCenter[axis] + boxHScales[axis];
			const float d = (point[axis] < boxMin)   ? (boxMin - point[axis])
			                : (point[axis] > boxMax) ? (point[axis] - boxMax)
			                                         : 0.0f;

			weightedSqDist += (d * d) * weights[axis];
		}

		return weightedSqDist;
	}

	static bool IntersectAxisAlignedBoxPyramid(const float3& boxCenter, const float3& boxHScales,
	                                           const float3& pyrCenter, const float3& pyrHScales,
	                                           int primaryAxis, int secondaryAxis0,
	                                           int secondaryAxis1, int primarySign)
	{
		const float h = pyrHScales[primaryAxis];
		const float hb = pyrHScales[secondaryAxis0];
		const float hc = pyrHScales[secondaryAxis1];

		assert(h > 0.0f);
		assert(hb > 0.0f);
		assert(hc > 0.0f);

		const float boxMinP = boxCenter[primaryAxis] - boxHScales[primaryAxis];
		const float boxMaxP = boxCenter[primaryAxis] + boxHScales[primaryAxis];

		const float u0 = primarySign * (boxMinP - pyrCenter[primaryAxis]);
		const float u1 = primarySign * (boxMaxP - pyrCenter[primaryAxis]);

		const float overlapUMin = std::max(std::min(u0, u1), -h);
		const float overlapUMax = std::min(std::max(u0, u1), h);

		if (overlapUMin > overlapUMax)
			return false;

		const float deltaB = math::fabs(boxCenter[secondaryAxis0] - pyrCenter[secondaryAxis0]);
		const float deltaC = math::fabs(boxCenter[secondaryAxis1] - pyrCenter[secondaryAxis1]);

		const float needB = std::max(deltaB - boxHScales[secondaryAxis0], 0.0f);
		const float needC = std::max(deltaC - boxHScales[secondaryAxis1], 0.0f);

		const float reqUB = -h + ((2.0f * h * needB) / hb);
		const float reqUC = -h + ((2.0f * h * needC) / hc);
		const float reqU = std::max(reqUB, reqUC);

		return (reqU <= (overlapUMax + COLLISION_VOLUME_EPS));
	}

	static bool IntersectAxisAlignedBoxEllipticCylinder(const float3& boxCenter,
	                                                   const float3& boxHScales,
	                                                   const float3& cylCenter,
	                                                   const float3& cylHScales,
	                                                   int primaryAxis)
	{
		if (math::fabs(cylCenter[primaryAxis] - boxCenter[primaryAxis]) >
		    (cylHScales[primaryAxis] + boxHScales[primaryAxis]))
			return false;

		const int secondaryAxis0 = (primaryAxis + 1) % 3;
		const int secondaryAxis1 = (primaryAxis + 2) % 3;
		const float boxMin0 = boxCenter[secondaryAxis0] - boxHScales[secondaryAxis0];
		const float boxMax0 = boxCenter[secondaryAxis0] + boxHScales[secondaryAxis0];
		const float boxMin1 = boxCenter[secondaryAxis1] - boxHScales[secondaryAxis1];
		const float boxMax1 = boxCenter[secondaryAxis1] + boxHScales[secondaryAxis1];

		const float d0 = (cylCenter[secondaryAxis0] < boxMin0)   ? (boxMin0 - cylCenter[secondaryAxis0])
		               : (cylCenter[secondaryAxis0] > boxMax0) ? (cylCenter[secondaryAxis0] - boxMax0)
		                                                       : 0.0f;
		const float d1 = (cylCenter[secondaryAxis1] < boxMin1)   ? (boxMin1 - cylCenter[secondaryAxis1])
		               : (cylCenter[secondaryAxis1] > boxMax1) ? (cylCenter[secondaryAxis1] - boxMax1)
		                                                       : 0.0f;

		const float r0 = cylHScales[secondaryAxis0];
		const float r1 = cylHScales[secondaryAxis1];

		assert(r0 > 0.0f);
		assert(r1 > 0.0f);

		return (((d0 * d0) / (r0 * r0)) + ((d1 * d1) / (r1 * r1)) <= (1.0f + COLLISION_VOLUME_EPS));
	}

	static bool IntersectAxisAlignedBoxVolume(const CollisionVolume* box, const CollisionVolume* vol,
	                                          const CMatrix44f& volMat, const CMatrix44f& boxInv,
	                                          bool& handled)
	{
		const CMatrix44f relMat = boxInv * volMat;
		int axisMap[3] = {0, 1, 2};
		int axisSign[3] = {1, 1, 1};

		handled = GetAxisAlignedMapping(relMat, axisMap, axisSign);

		if (!handled)
			return false;

		const float3 boxCenter = box->GetOffsets();
		const float3 boxHScales = box->GetHScales();
		const float3 volCenter = boxInv.Mul(volMat.Mul(vol->GetOffsets()));

		switch (vol->GetVolumeType()) {
			case CollisionVolume::COLVOL_TYPE_BOX: {
				const float3 volHScales = GetAlignedHalfScales(vol, axisMap);
				const float3 centerDelta = float3::fabs(volCenter - boxCenter);
				return (centerDelta.x <= (boxHScales.x + volHScales.x) &&
				        centerDelta.y <= (boxHScales.y + volHScales.y) &&
				        centerDelta.z <= (boxHScales.z + volHScales.z));
			} break;

			case CollisionVolume::COLVOL_TYPE_SPHERE: {
				const float radius = vol->GetHScales().x;
				const float3 invRadiusSq = OnesVector / (radius * radius);
				return (GetWeightedSqDistToBox(volCenter, boxCenter, boxHScales, invRadiusSq) <= 1.0f);
			} break;

			case CollisionVolume::COLVOL_TYPE_ELLIPSOID: {
				const float3 radii = GetAlignedHalfScales(vol, axisMap);
				const float3 invRadiiSq(1.0f / (radii.x * radii.x), 1.0f / (radii.y * radii.y),
				                        1.0f / (radii.z * radii.z));
				return (GetWeightedSqDistToBox(volCenter, boxCenter, boxHScales, invRadiiSq) <= 1.0f);
			} break;

			case CollisionVolume::COLVOL_TYPE_CYLINDER: {
				const int primaryAxis = axisMap[vol->GetPrimaryAxis()];
				const float3 volHScales = GetAlignedHalfScales(vol, axisMap);
				return IntersectAxisAlignedBoxEllipticCylinder(boxCenter, boxHScales, volCenter,
				                                             volHScales, primaryAxis);
			} break;

			case CollisionVolume::COLVOL_TYPE_PYRAMID: {
				const int primaryAxis = axisMap[vol->GetPrimaryAxis()];
				const int secondaryAxis0 = axisMap[vol->GetSecondaryAxis(0)];
				const int secondaryAxis1 = axisMap[vol->GetSecondaryAxis(1)];
				const int primarySign = axisSign[vol->GetPrimaryAxis()];
				const float3 volHScales = GetAlignedHalfScales(vol, axisMap);

				return IntersectAxisAlignedBoxPyramid(boxCenter, boxHScales, volCenter, volHScales,
				                                      primaryAxis, secondaryAxis0, secondaryAxis1,
				                                      primarySign);
			} break;
		}

		return false;
	}

	static bool UpdateLineSimplex(GJKSimplex& simplex, float3& dir)
	{
		const float3& a = simplex.points[0];
		const float3& b = simplex.points[1];
		const float3 ao = -a;
		const float3 ab = b - a;

		if (ab.dot(ao) > 0.0f) {
			dir = EdgeTowardOriginDirection(ab, ao);
		} else {
			simplex.Assign(a);
			dir = ao;
		}

		return false;
	}

	static bool UpdateTriangleSimplex(GJKSimplex& simplex, float3& dir)
	{
		const float3& a = simplex.points[0];
		const float3& b = simplex.points[1];
		const float3& c = simplex.points[2];

		const float3 ao = -a;
		const float3 ab = b - a;
		const float3 ac = c - a;
		const float3 abc = ab.cross(ac);

		if (abc.SqLength() < COLLISION_VOLUME_EPS) {
			if (ab.SqLength() >= ac.SqLength()) {
				simplex.Assign(a, b);
			} else {
				simplex.Assign(a, c);
			}
			return UpdateLineSimplex(simplex, dir);
		}

		if ((abc.cross(ac)).dot(ao) > 0.0f) {
			if (ac.dot(ao) > 0.0f) {
				simplex.Assign(a, c);
				dir = EdgeTowardOriginDirection(ac, ao);
			} else {
				if (ab.dot(ao) > 0.0f) {
					simplex.Assign(a, b);
					dir = EdgeTowardOriginDirection(ab, ao);
				} else {
					simplex.Assign(a);
					dir = ao;
				}
			}
			return false;
		}

		if ((ab.cross(abc)).dot(ao) > 0.0f) {
			if (ab.dot(ao) > 0.0f) {
				simplex.Assign(a, b);
				dir = EdgeTowardOriginDirection(ab, ao);
			} else {
				simplex.Assign(a);
				dir = ao;
			}
			return false;
		}

		if (abc.dot(ao) > 0.0f) {
			dir = abc;
		} else {
			simplex.Assign(a, c, b);
			dir = -abc;
		}

		return false;
	}

	static bool UpdateTetrahedronSimplex(GJKSimplex& simplex, float3& dir)
	{
		const float3& a = simplex.points[0];
		const float3& b = simplex.points[1];
		const float3& c = simplex.points[2];
		const float3& d = simplex.points[3];

		const float3 ao = -a;
		const float3 ab = b - a;
		const float3 ac = c - a;
		const float3 ad = d - a;

		float3 abc = ab.cross(ac);
		if (abc.dot(ad) > 0.0f)
			abc = -abc;

		float3 acd = ac.cross(ad);
		if (acd.dot(ab) > 0.0f)
			acd = -acd;

		float3 adb = ad.cross(ab);
		if (adb.dot(ac) > 0.0f)
			adb = -adb;

		if (abc.dot(ao) > 0.0f) {
			simplex.Assign(a, b, c);
			dir = abc;
			return false;
		}

		if (acd.dot(ao) > 0.0f) {
			simplex.Assign(a, c, d);
			dir = acd;
			return false;
		}

		if (adb.dot(ao) > 0.0f) {
			simplex.Assign(a, d, b);
			dir = adb;
			return false;
		}

		return true;
	}

	static bool UpdateSimplex(GJKSimplex& simplex, float3& dir)
	{
		switch (simplex.size) {
			case 2:
				return UpdateLineSimplex(simplex, dir);
			case 3:
				return UpdateTriangleSimplex(simplex, dir);
			case 4:
				return UpdateTetrahedronSimplex(simplex, dir);
			default: {
				dir = -simplex.points[0];
				return false;
			} break;
		}
	}

	static bool HasSupportPoint(const GJKSimplex& simplex, const float3& point)
	{
		for (int i = 0; i < simplex.size; ++i) {
			if ((simplex.points[i] - point).SqLength() <= COLLISION_VOLUME_EPS)
				return true;
		}

		return false;
	}

	static float3 GetCenterDeltaInLocalSpace(const CollisionVolume* refVol, const CMatrix44f& refInv,
	                                        const float3& otherWorldCenter)
	{
		return (refInv.Mul(otherWorldCenter) - refVol->GetOffsets());
	}

	static bool IntersectVolumesGJK(const CollisionVolume* refVol, const CMatrix44f& refMat,
	                                const CollisionVolume* otherVol, const CMatrix44f& otherMat,
	                                const CMatrix44f& refInv, const CMatrix44f& otherInv,
	                                float3 initialDirRef, const float3& fallbackDirRef)
	{
		const CMatrix44f otherToRef = refInv * otherMat;
		const CMatrix44f refToOther = otherInv * refMat;

		if (initialDirRef.SqLength() < COLLISION_VOLUME_EPS)
			initialDirRef = fallbackDirRef;
		if (initialDirRef.SqLength() < COLLISION_VOLUME_EPS)
			initialDirRef = float3(1.0f, 0.0f, 0.0f);

		GJKSimplex simplex;
		simplex.PushFront(GetMinkowskiSupportPointInReferenceSpace(refVol, otherVol, otherToRef,
		                                                        refToOther, initialDirRef));

		float3 dir = -simplex.points[0];

		if (dir.SqLength() < COLLISION_VOLUME_EPS)
			return true;

		for (int n = 0; n < GJK_MAX_ITERATIONS; ++n) {
			const float3 support = GetMinkowskiSupportPointInReferenceSpace(refVol, otherVol,
			                                                              otherToRef, refToOther,
			                                                              dir);

			if (support.dot(dir) < -COLLISION_VOLUME_EPS)
				return false;

			if (HasSupportPoint(simplex, support))
				return false;

			simplex.PushFront(support);

			if (UpdateSimplex(simplex, dir))
				return true;

			if (dir.SqLength() < COLLISION_VOLUME_EPS)
				return true;
		}

		return false;
	}

}  // namespace


bool CCollisionHandler::IntersectBoxVolume(const CollisionVolume* box, const CMatrix44f& boxMat,
	                                       const CollisionVolume* vol, const CMatrix44f& volMat)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (box == nullptr || vol == nullptr)
		return false;
	if (box->GetVolumeType() != CollisionVolume::COLVOL_TYPE_BOX)
		return false;

	const float3 boxCtr = boxMat.Mul(box->GetOffsets());
	const float3 volCtr = volMat.Mul(vol->GetOffsets());
	const float sumRadii = box->GetBoundingRadius() + vol->GetBoundingRadius();

	if ((boxCtr - volCtr).SqLength() > (sumRadii * sumRadii))
		return false;

	const CMatrix44f boxInv = boxMat.InvertAffine();
	const CMatrix44f volInv = volMat.InvertAffine();

	bool handledByAxisAlignedFastPath = false;
	if (IntersectAxisAlignedBoxVolume(box, vol, volMat, boxInv, handledByAxisAlignedFastPath))
		return true;
	if (handledByAxisAlignedFastPath)
		return false;

	const float3 dir = GetCenterDeltaInLocalSpace(box, boxInv, volCtr);
	return IntersectVolumesGJK(box, boxMat, vol, volMat, boxInv, volInv, dir,
	                           float3(1.0f, 0.0f, 0.0f));
}

bool CCollisionHandler::IntersectPyramidVolume(const CollisionVolume* pyramid,
	                                           const CMatrix44f& pyrMat, const CollisionVolume* vol,
	                                           const CMatrix44f& volMat)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (pyramid == nullptr || vol == nullptr)
		return false;
	if (pyramid->GetVolumeType() != CollisionVolume::COLVOL_TYPE_PYRAMID)
		return false;

	const float3 pyrCtr = pyrMat.Mul(pyramid->GetOffsets());
	const float3 volCtr = volMat.Mul(vol->GetOffsets());
	const float sumRadii = pyramid->GetBoundingRadius() + vol->GetBoundingRadius();

	if ((pyrCtr - volCtr).SqLength() > (sumRadii * sumRadii))
		return false;

	const CMatrix44f pyrInv = pyrMat.InvertAffine();
	const CMatrix44f volInv = volMat.InvertAffine();

	float3 fallbackDir = ZeroVector;
	fallbackDir[pyramid->GetPrimaryAxis()] = 1.0f;

	const float3 dir = GetCenterDeltaInLocalSpace(pyramid, pyrInv, volCtr);
	return IntersectVolumesGJK(pyramid, pyrMat, vol, volMat, pyrInv, volInv, dir, fallbackDir);
}
