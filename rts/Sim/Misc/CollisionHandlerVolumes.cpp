/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "CollisionHandler.h"
#include "CollisionVolume.h"
#include "Map/ReadMap.h"  // mapDims
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
	static constexpr float EPS = 1e-4f;
	static constexpr float GJK_EPS = 1e-6f;
	static constexpr float GJK_EPS_SQ = GJK_EPS * GJK_EPS;

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

		return (u >= -GJK_EPS && v >= -GJK_EPS && w >= -GJK_EPS);
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
				const float lateral =
					(math::fabs(dir[sAx0]) * ahs[sAx0]) + (math::fabs(dir[sAx1]) * ahs[sAx1]);

				// Pyramid model:
				//   apex at p[pAx] - h
				//   base plane at p[pAx] + h
				//   best base corner adds ±ahs on the secondary axes
				//
				// Choose base iff:
				//   dir·bestBaseCorner >= dir·apex
				// => dir[pAx] * h + lateral >= -dir[pAx] * h
				// => 2*h*dir[pAx] + lateral >= 0

				if ((2.0f * h * dir[pAx] + lateral) >= 0.0f) {
					// best base corner
					p[pAx] += h;
					p[sAx0] += (dir[sAx0] >= 0.0f) ? ahs[sAx0] : -ahs[sAx0];
					p[sAx1] += (dir[sAx1] >= 0.0f) ? ahs[sAx1] : -ahs[sAx1];
				} else {
					// apex
					p[pAx] -= h;
				}
			} break;
		}

		return p;
	}
	static float3 GetSupportPointInReferenceSpace(const CollisionVolume* v,
	                                              const CMatrix44f& vToRef,
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
			GJKSimplex edgeAB, edgeAC, edgeBC;
			float3 dirAB = ZeroVector;
			float3 dirAC = ZeroVector;
			float3 dirBC = ZeroVector;

			edgeAB.Assign(a, b);
			edgeAC.Assign(a, c);
			edgeBC.Assign(b, c);

			if (UpdateLineSimplex(edgeAB, dirAB)) {
				simplex = edgeAB;
				return true;
			}
			if (UpdateLineSimplex(edgeAC, dirAC)) {
				simplex = edgeAC;
				return true;
			}
			if (UpdateLineSimplex(edgeBC, dirBC)) {
				simplex = edgeBC;
				return true;
			}

			const float distAB = dirAB.SqLength();
			const float distAC = dirAC.SqLength();
			const float distBC = dirBC.SqLength();

			if (distAB <= distAC && distAB <= distBC) {
				simplex = edgeAB;
				dir = dirAB;
			} else if (distAC <= distBC) {
				simplex = edgeAC;
				dir = dirAC;
			} else {
				simplex = edgeBC;
				dir = dirBC;
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

		if (math::fabs(tetVolume6) <= GJK_EPS_SQ) {
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

			for (const auto& face : faceVerts) {
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

				return (abc.dot(ao) <= GJK_EPS && acd.dot(ao) <= GJK_EPS && adb.dot(ao) <= GJK_EPS);
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

	static float3 GetCenterDeltaInLocalSpace(const CollisionVolume* refVol,
	                                         const CMatrix44f& refInv,
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
			const float3 support = GetMinkowskiSupportPointInReferenceSpace(
				refVol, otherVol, otherToRef, refToOther, dir);
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

bool CCollisionHandler::IntersectVolume(const CollisionVolume* vol1, const CMatrix44f& vol1Mat,
                                        const CollisionVolume* vol2, const CMatrix44f& vol2Mat)
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (vol1 == nullptr || vol2 == nullptr)
		return false;

	// This GJK implementation assumes support directions transform correctly
	// under rigid transforms only (rotation + translation).
	const bool rigid1 = IsRigidTransform(vol1Mat);
	const bool rigid2 = IsRigidTransform(vol2Mat);
	if (!rigid1 || !rigid2) {
		LOG_L(L_WARNING,
		      "[CollisionHandler::IntersectVolume] non-rigid transform passed to GJK "
		      "(vol1Rigid=%d, vol2Rigid=%d)",
		      rigid1, rigid2);
		return false;
	}

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

static inline float PlaneDistance(const float4& p, const float3& x)
{
	return (p.x * x.x + p.y * x.y + p.z * x.z + p.w);
}

static inline float3 PlaneNormal(const float4& p)
{
	return float3(p.x, p.y, p.z);
}

static inline float3 LocalDirFromWorldDir(const CMatrix44f& m, const float3& worldDir)
{
	// Assumes volumeToWorld is rigid / orthonormal.
	// localDir = R^T * worldDir
	return {worldDir.dot(m.GetX()), worldDir.dot(m.GetY()), worldDir.dot(m.GetZ())};
}

static inline float3 WorldPointFromLocalPoint(const CMatrix44f& m, const float3& localPoint)
{
	return m.GetPos() + m.GetX() * localPoint.x + m.GetY() * localPoint.y + m.GetZ() * localPoint.z;
}

static inline float3 SupportSphereLocal(const CollisionVolume& vol, const float3& dir)
{
	const float r = vol.GetHScale(0);  // uniform after FixTypeAndScale
	const float sql = dir.SqLength();

	if (sql <= COLLISION_VOLUME_EPS)
		return ZeroVector;

	return (dir * (r * math::isqrt(sql)));
}

static inline float3 SupportBoxLocal(const CollisionVolume& vol, const float3& dir)
{
	const float3& h = vol.GetHScales();

	return {(dir.x >= 0.0f) ? h.x : -h.x, (dir.y >= 0.0f) ? h.y : -h.y,
	        (dir.z >= 0.0f) ? h.z : -h.z};
}

static inline float3 SupportEllipsoidLocal(const CollisionVolume& vol, const float3& dir)
{
	// Support of ellipsoid x^2/a^2 + y^2/b^2 + z^2/c^2 <= 1:
	// s_i = a_i^2 * d_i / sqrt(sum(a_j^2 d_j^2))
	const float3& h2 = vol.GetHSqScales();

	const float denomSq = h2.x * dir.x * dir.x + h2.y * dir.y * dir.y + h2.z * dir.z * dir.z;

	if (denomSq <= COLLISION_VOLUME_EPS)
		return ZeroVector;

	const float invDenom = math::isqrt(denomSq);

	return {h2.x * dir.x * invDenom, h2.y * dir.y * invDenom, h2.z * dir.z * invDenom};
}

static inline float3 SupportCylinderLocal(const CollisionVolume& vol, const float3& dir)
{
	// Matches GetCylinderDistance:
	// - primary axis extent is [-h, +h]
	// - secondary cross-section is circular
	const int axisA = vol.GetPrimaryAxis();
	const int axisB = vol.GetSecondaryAxis(0);
	const int axisC = vol.GetSecondaryAxis(1);

	const float h = vol.GetHScale(axisA);
	const float r = vol.GetHScale(axisB);  // == axisC after FixTypeAndScale

	assert(vol.GetHScale(axisB) == vol.GetHScale(axisC));

	float3 p = ZeroVector;

	p[axisA] = (dir[axisA] >= 0.0f) ? h : -h;

	const float radialLenSq = dir[axisB] * dir[axisB] + dir[axisC] * dir[axisC];

	if (radialLenSq > COLLISION_VOLUME_EPS) {
		const float s = r * math::isqrt(radialLenSq);
		p[axisB] = dir[axisB] * s;
		p[axisC] = dir[axisC] * s;
	}

	return p;
}

static inline float3 SupportPyramidLocal(const CollisionVolume& vol, const float3& dir)
{
	// Matches GetPyramidDistance:
	// - primary axis in [-h, +h]
	// - apex at primary = -h
	// - rectangular base at primary = +h
	const int axisA = vol.GetPrimaryAxis();
	const int axisB = vol.GetSecondaryAxis(0);
	const int axisC = vol.GetSecondaryAxis(1);

	const float h = vol.GetHScale(axisA);
	const float hb = vol.GetHScale(axisB);
	const float hc = vol.GetHScale(axisC);

	float3 best = ZeroVector;
	float bestDot = -std::numeric_limits<float>::infinity();

	auto tryPoint = [&](const float3& p) {
		const float dp = dir.dot(p);
		if (dp > bestDot) {
			bestDot = dp;
			best = p;
		}
	};

	{
		float3 apex = ZeroVector;
		apex[axisA] = -h;
		tryPoint(apex);
	}

	for (int sb = -1; sb <= 1; sb += 2) {
		for (int sc = -1; sc <= 1; sc += 2) {
			float3 base = ZeroVector;
			base[axisA] = +h;
			base[axisB] = sb * hb;
			base[axisC] = sc * hc;
			tryPoint(base);
		}
	}

	return best;
}

static inline float3 SupportVolumeLocal(const CollisionVolume& vol, const float3& dir)
{
	switch (vol.GetVolumeType()) {
		case CollisionVolume::COLVOL_TYPE_SPHERE:
			return SupportSphereLocal(vol, dir);
		case CollisionVolume::COLVOL_TYPE_BOX:
			return SupportBoxLocal(vol, dir);
		case CollisionVolume::COLVOL_TYPE_ELLIPSOID:
			return SupportEllipsoidLocal(vol, dir);
		case CollisionVolume::COLVOL_TYPE_CYLINDER:
			return SupportCylinderLocal(vol, dir);
		case CollisionVolume::COLVOL_TYPE_PYRAMID:
			return SupportPyramidLocal(vol, dir);
	}

	assert(false);
	return ZeroVector;
}

static float3 GetFrustumSupportPoint(const CCamera::Frustum& frustum, const float3& dirWorld)
{
	float bestDot = -std::numeric_limits<float>::infinity();
	float3 bestPt = frustum.verts[0];

	for (const float3& p : frustum.verts) {
		const float dp = p.dot(dirWorld);
		if (dp > bestDot) {
			bestDot = dp;
			bestPt = p;
		}
	}

	return bestPt;
}

static float3 GetMinkowskiSupportPointVolumeFrustum(const CollisionVolume* vol,
                                                    const CCamera::Frustum& frustum,
                                                    const CMatrix44f& worldToVol,
                                                    const CMatrix44f& volToWorld,
                                                    const float3& dirVol)
{
	// Minkowski support in volume-local space:
	// support(vol - frustumInVol, d) = support(vol, d) - support(frustumInVol, -d)

	const float3 pVol = GetSupportPointLocal(vol, dirVol);

	// Convert -dirVol into world-space for frustum support
	const float3 dirWorld = TransformDirection(volToWorld, -dirVol);
	const float3 pFrustumWorld = GetFrustumSupportPoint(frustum, dirWorld);
	const float3 pFrustumVol = worldToVol.Mul(pFrustumWorld);

	return (pVol - pFrustumVol);
}

static float3 GetFrustumCenter(const CCamera::Frustum& frustum)
{
	float3 c = ZeroVector;

	for (const float3& p : frustum.verts)
		c += p;

	return (c * (1.0f / float(frustum.verts.size())));
}

static bool IntersectVolumeFrustumGJK(const CollisionVolume* vol, const CCamera::Frustum& frustum,
                                      const CMatrix44f& volToWorld, const CMatrix44f& worldToVol,
                                      float3 initialDirVol, const float3& fallbackDirVol)
{
	if (initialDirVol.SqLength() < GJK_EPS_SQ)
		initialDirVol = fallbackDirVol;
	if (initialDirVol.SqLength() < GJK_EPS_SQ)
		initialDirVol = float3(1.0f, 0.0f, 0.0f);

	GJKSimplex simplex;
	simplex.PushFront(
		GetMinkowskiSupportPointVolumeFrustum(vol, frustum, worldToVol, volToWorld, initialDirVol));

	float3 dir = -simplex.points[0];

	if (dir.SqLength() < GJK_EPS_SQ)
		return true;

	for (int n = 0; n < GJK_MAX_ITERATIONS; ++n) {
		const float3 support =
			GetMinkowskiSupportPointVolumeFrustum(vol, frustum, worldToVol, volToWorld, dir);

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


static bool FrustumRejectsVolumeByPlanes(const CCamera::Frustum& frustum,
                                         const CollisionVolume* vol, const CMatrix44f& volToWorld)
{
	for (const float4& plane : frustum.planes) {
		const float3 worldN = PlaneNormal(plane);
		const float3 localDir = LocalDirFromWorldDir(volToWorld, worldN);
		const float3 localSup = GetSupportPointLocal(vol, localDir);
		const float3 worldSup = volToWorld.Mul(localSup);

		if (PlaneDistance(plane, worldSup) < -EPS)
			return true;
	}

	return false;
}
bool CCollisionHandler::IntersectVolumeWithFrustum(const CCamera::Frustum& frustum,
                                                   const CollisionVolume& vol,
                                                   const CMatrix44f& volumeToWorld)
{
	RECOIL_DETAILED_TRACY_ZONE;

	// GJK support-direction transforms assume rigid transforms only.
	if (!IsRigidTransform(volumeToWorld)) {
		LOG_L(L_WARNING,
		      "[CollisionHandler::IntersectVolumeWithFrustum] non-rigid transform passed");
		return false;
	}

	// Broad-phase sphere reject. GetSupportPointLocal includes vol->GetOffsets(),
	// so the world-space center for the bounding sphere is volumeToWorld * offsets.
	const float3 volCtr = volumeToWorld.Mul(vol.GetOffsets());
	const float volRad = vol.GetBoundingRadius();
	printf("here %s, %f\n", volCtr.str().c_str(), volRad);
	printf("plane1 %s\n", frustum.planes[0].str().c_str());
	printf("plane2 %s\n", frustum.planes[1].str().c_str());
	printf("plane3 %s\n", frustum.planes[2].str().c_str());
	printf("plane4 %s\n", frustum.planes[3].str().c_str());
	printf("plane5 %s\n", frustum.planes[4].str().c_str());
	printf("plane6 %s\n", frustum.planes[5].str().c_str());
	if (!frustum.IntersectSphere(volCtr, volRad, 0x3F))
		return false;

	printf("here2\n");
	// Frustum planes use the same inward-facing convention as
	// CCamera::Frustum::IntersectSphere, so plane rejection must do the same.
	if (FrustumRejectsVolumeByPlanes(frustum, &vol, volumeToWorld))
		return false;

	const CMatrix44f worldToVol = volumeToWorld.InvertAffine();

	// Initial search direction in volume-local space: frustum center relative to volume center.
	const float3 frustumCtr = GetFrustumCenter(frustum);
	float3 initialDirVol = worldToVol.Mul(frustumCtr) - vol.GetOffsets();

	float3 fallbackDirVol = ZeroVector;
	fallbackDirVol[vol.GetPrimaryAxis()] = 1.0f;

	printf("here3\n");
	return IntersectVolumeFrustumGJK(&vol, frustum, volumeToWorld, worldToVol, initialDirVol,
	                                 fallbackDirVol);
}