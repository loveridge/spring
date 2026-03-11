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
#include <limits>


namespace
{
	struct GJKSimplex {
		void PushFront(const float3& p)
		{
			points[3] = points[2];
			points[2] = points[1];
			points[1] = points[0];
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
	static constexpr float GJK_EPS = 1e-6f;
	static constexpr float GJK_EPS_SQ = GJK_EPS * GJK_EPS;
	static constexpr float VOL_EXTENT_EPS = 1e-6f;

	static bool IsRigidTransform(const CMatrix44f& m)
	{
		return m.IsRotOrRotTranMatrix();
	}

	static float3 TransformDirection(const CMatrix44f& m, const float3& d)
	{
		return m.MulDir(d);
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

		if (p.SqLength() < GJK_EPS_SQ)
			p = float3(v.y, -v.x, 0.0f);

		return p;
	}

	static float3 EdgeTowardOriginDirection(const float3& edge, const float3& ao)
	{
		float3 dir = TripleCross(edge, ao, edge);

		if (dir.SqLength() < GJK_EPS_SQ)
			dir = PerpendicularVector(edge);

		return dir;
	}

	static bool OriginOnSegment(const float3& a, const float3& b)
	{
		const float3 ab = (b - a);
		const float abSq = ab.SqLength();

		if (abSq <= GJK_EPS_SQ)
			return (a.SqLength() <= GJK_EPS_SQ);

		const float t = (-a).dot(ab) / abSq;
		if (t < -GJK_EPS || t > (1.0f + GJK_EPS))
			return false;

		const float3 closest = a + (ab * std::clamp(t, 0.0f, 1.0f));
		return (closest.SqLength() <= GJK_EPS_SQ);
	}

	static bool OriginOnTriangle(const float3& a, const float3& b, const float3& c)
	{
		const float3 ab = (b - a);
		const float3 ac = (c - a);
		const float3 normal = ab.cross(ac);
		const float normalSq = normal.SqLength();

		if (normalSq <= GJK_EPS_SQ)
			return false;

		const float planeDistSq = Square(normal.dot(-a)) / normalSq;
		if (planeDistSq > GJK_EPS_SQ)
			return false;

		const float d00 = ab.dot(ab);
		const float d01 = ab.dot(ac);
		const float d11 = ac.dot(ac);
		const float d20 = (-a).dot(ab);
		const float d21 = (-a).dot(ac);
		const float denom = (d00 * d11) - (d01 * d01);

		if (math::fabs(denom) <= GJK_EPS_SQ)
			return false;

		const float v = ((d11 * d20) - (d01 * d21)) / denom;
		const float w = ((d00 * d21) - (d01 * d20)) / denom;
		const float u = 1.0f - v - w;

		return (u >= -GJK_EPS &&
		        v >= -GJK_EPS &&
		        w >= -GJK_EPS);
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

				if (dirLen > GJK_EPS)
					p += dir * (radius / dirLen);
			} break;

			case CollisionVolume::COLVOL_TYPE_ELLIPSOID: {
				const float denom =
					math::sqrt((ahs.x * ahs.x * dir.x * dir.x) + (ahs.y * ahs.y * dir.y * dir.y) +
					           (ahs.z * ahs.z * dir.z * dir.z));

				if (denom > GJK_EPS) {
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

				if (denom > GJK_EPS) {
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

	static bool UpdateLineSimplex(GJKSimplex& simplex, float3& dir)
	{
		const float3& a = simplex.points[0];
		const float3& b = simplex.points[1];
		const float3 ao = -a;
		const float3 ab = b - a;

		if (OriginOnSegment(a, b))
			return true;

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

		if (abc.SqLength() < GJK_EPS_SQ) {
			GJKSimplex edgeAB;
			GJKSimplex edgeAC;
			float3 dirAB = ZeroVector;
			float3 dirAC = ZeroVector;

			edgeAB.Assign(a, b);
			edgeAC.Assign(a, c);

			if (UpdateLineSimplex(edgeAB, dirAB)) {
				simplex = edgeAB;
				return true;
			}
			if (UpdateLineSimplex(edgeAC, dirAC)) {
				simplex = edgeAC;
				return true;
			}

			if (dirAB.SqLength() <= dirAC.SqLength()) {
				simplex = edgeAB;
				dir = dirAB;
			} else {
				simplex = edgeAC;
				dir = dirAC;
			}
			return false;
		}

		if (OriginOnTriangle(a, b, c))
			return true;

		if ((abc.cross(ac)).dot(ao) > 0.0f) {
			if (ac.dot(ao) > 0.0f) {
				simplex.Assign(a, c);
				return UpdateLineSimplex(simplex, dir);
			}

			if (ab.dot(ao) > 0.0f) {
				simplex.Assign(a, b);
				return UpdateLineSimplex(simplex, dir);
			}

			simplex.Assign(a);
			dir = ao;
			return (dir.SqLength() <= GJK_EPS_SQ);
		}

		if ((ab.cross(abc)).dot(ao) > 0.0f) {
			if (ab.dot(ao) > 0.0f) {
				simplex.Assign(a, b);
				return UpdateLineSimplex(simplex, dir);
			}

			simplex.Assign(a);
			dir = ao;
			return (dir.SqLength() <= GJK_EPS_SQ);
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
		const float tetVolume6 = (ab.cross(ac)).dot(ad);

		if (math::fabs(tetVolume6) <= COLLISION_VOLUME_EPS) {
			GJKSimplex bestFace;
			float3 bestDir = ZeroVector;
			float bestSqDist = std::numeric_limits<float>::infinity();
			bool foundFace = false;

			const float3 faceVerts[4][3] = {
				{a, b, c},
				{a, c, d},
				{a, d, b},
				{b, d, c},
			};

			for (const auto& face: faceVerts) {
				GJKSimplex faceSimplex;
				float3 faceDir = ZeroVector;

				faceSimplex.Assign(face[0], face[1], face[2]);

				if (UpdateTriangleSimplex(faceSimplex, faceDir)) {
					simplex = faceSimplex;
					return true;
				}

				const float sqDist = faceDir.SqLength();
				if (!foundFace || sqDist < bestSqDist) {
					bestFace = faceSimplex;
					bestDir = faceDir;
					bestSqDist = sqDist;
					foundFace = true;
				}
			}

			if (foundFace) {
				simplex = bestFace;
				dir = bestDir;
				return false;
			}

			simplex.Assign(a);
			dir = ao;
			return (dir.SqLength() <= GJK_EPS_SQ);
		}

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
			if ((simplex.points[i] - point).SqLength() <= GJK_EPS_SQ)
				return true;
		}

		return false;
	}

	static float GetMaxSimplexAdvance(const GJKSimplex& simplex, const float3& dir)
	{
		float maxAdvance = -std::numeric_limits<float>::infinity();

		for (int i = 0; i < simplex.size; ++i)
			maxAdvance = std::max(maxAdvance, simplex.points[i].dot(dir));

		return maxAdvance;
	}

	static bool SimplexContainsOriginWithinTolerance(const GJKSimplex& simplex)
	{
		switch (simplex.size) {
			case 1:
				return (simplex.points[0].SqLength() <= GJK_EPS_SQ);

			case 2:
				return OriginOnSegment(simplex.points[0], simplex.points[1]);

			case 3:
				return (OriginOnTriangle(simplex.points[0], simplex.points[1], simplex.points[2]) ||
				        OriginOnSegment(simplex.points[0], simplex.points[1]) ||
				        OriginOnSegment(simplex.points[0], simplex.points[2]) ||
				        OriginOnSegment(simplex.points[1], simplex.points[2]));

			case 4: {
				const float3& a = simplex.points[0];
				const float3& b = simplex.points[1];
				const float3& c = simplex.points[2];
				const float3& d = simplex.points[3];

				if (OriginOnTriangle(a, b, c) || OriginOnTriangle(a, c, d) ||
				    OriginOnTriangle(a, d, b) || OriginOnTriangle(b, d, c))
					return true;

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

				return (abc.dot(ao) <= GJK_EPS &&
				        acd.dot(ao) <= GJK_EPS &&
				        adb.dot(ao) <= GJK_EPS);
			}

			default:
				return false;
		}
	}

	static bool ClassifyStalledSimplex(const GJKSimplex& simplex, const float3& support,
	                                  const float3& dir)
	{
		if (SimplexContainsOriginWithinTolerance(simplex))
			return true;

		GJKSimplex trialSimplex = simplex;
		float3 trialDir = dir;

		trialSimplex.PushFront(support);

		if (UpdateSimplex(trialSimplex, trialDir))
			return true;

		return (trialDir.SqLength() <= GJK_EPS_SQ ||
		        SimplexContainsOriginWithinTolerance(trialSimplex));
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

		if (initialDirRef.SqLength() < GJK_EPS_SQ)
			initialDirRef = fallbackDirRef;
		if (initialDirRef.SqLength() < GJK_EPS_SQ)
			initialDirRef = float3(1.0f, 0.0f, 0.0f);

		GJKSimplex simplex;
		simplex.PushFront(GetMinkowskiSupportPointInReferenceSpace(refVol, otherVol, otherToRef,
		                                                        refToOther, initialDirRef));

		float3 dir = -simplex.points[0];

		if (dir.SqLength() < GJK_EPS_SQ)
			return true;

		for (int n = 0; n < GJK_MAX_ITERATIONS; ++n) {
			const float3 support = GetMinkowskiSupportPointInReferenceSpace(refVol, otherVol,
			                                                              otherToRef, refToOther,
			                                                              dir);
			const float supportAdvance = support.dot(dir);
			const float simplexAdvance = GetMaxSimplexAdvance(simplex, dir);

			if (supportAdvance < -GJK_EPS)
				return false;

			if ((supportAdvance - simplexAdvance) <= GJK_EPS || HasSupportPoint(simplex, support))
				return ClassifyStalledSimplex(simplex, support, dir);

			simplex.PushFront(support);

			if (UpdateSimplex(simplex, dir))
				return true;

			if (dir.SqLength() < GJK_EPS_SQ)
				return true;
		}

		return false;
	}

}  // namespace

bool CCollisionHandler::IntersectVolume(const CollisionVolume* vol1,
	                                           const CMatrix44f& vol1Mat, const CollisionVolume* vol2,
	                                           const CMatrix44f& vol2Mat)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (vol1 == nullptr || vol2 == nullptr)
		return false;

	const float3 vol1Ctr = vol1Mat.Mul(vol1->GetOffsets());
	const float3 vol2Ctr = vol2Mat.Mul(vol2->GetOffsets());
	const float sumRadii = vol1->GetBoundingRadius() + vol2->GetBoundingRadius();

	if ((vol1Ctr - vol2Ctr).SqLength() > (sumRadii * sumRadii))
		return false;

	const CMatrix44f pyrInv = vol1Mat.InvertAffine();
	const CMatrix44f volInv = vol2Mat.InvertAffine();

	float3 fallbackDir = ZeroVector;
	fallbackDir[vol1->GetPrimaryAxis()] = 1.0f;

	const float3 dir = GetCenterDeltaInLocalSpace(vol1, pyrInv, vol2Ctr);
	return IntersectVolumesGJK(vol1, vol1Mat, vol2, vol2Mat, pyrInv, volInv, dir, fallbackDir);
}
