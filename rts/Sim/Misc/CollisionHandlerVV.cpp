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
		float3 p =
			(math::fabs(v.x) < math::fabs(v.y)) ? float3(0.0f, -v.z, v.y) : float3(-v.z, 0.0f, v.x);

		if (p.SqLength() < COLLISION_VOLUME_EPS)
			p = float3(v.y, -v.x, 0.0f);

		return p;
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

				// Support of the rectangular pyramid:
				// maximize d·x over either apex or base rectangle.
				const float baseCoeff = dir[pAx] +
				                        (math::fabs(dir[sAx0]) * ahs[sAx0]) / (2.0f * h) +
				                        (math::fabs(dir[sAx1]) * ahs[sAx1]) / (2.0f * h);

				if (baseCoeff >= 0.0f) {
					// one of the 4 base corners
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

	static float3 GetSupportPointBoxSpace(const CollisionVolume* v, const CMatrix44f& vMat,
	                                      const CMatrix44f& vInv, const CMatrix44f& boxMat,
	                                      const CMatrix44f& boxInv, const float3& dirBox)
	{
		const float3 dirWorld = TransformDirection(boxMat, dirBox);
		const float3 dirLocal = TransformDirection(vInv, dirWorld);
		const float3 pointLocal = GetSupportPointLocal(v, dirLocal);
		return (boxInv.Mul(vMat.Mul(pointLocal)));
	}

	static float3 GetMinkowskiSupportPoint(const CollisionVolume* box, const CollisionVolume* vol,
	                                       const CMatrix44f& volMat, const CMatrix44f& volInv,
	                                       const CMatrix44f& boxMat, const CMatrix44f& boxInv,
	                                       const float3& dirBox)
	{
		const float3 pBox = GetSupportPointLocal(box, dirBox);
		const float3 pVol = GetSupportPointBoxSpace(vol, volMat, volInv, boxMat, boxInv, -dirBox);
		return (pBox - pVol);
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
			const float3 absAxis = float3::fabs(axis);

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
			axisSign[i] = (axis[majorAxis] >= 0.0f) ? 1 : -1;
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

		// Express the box's primary-axis interval in the pyramid's local-primary coordinate u,
		// where the pyramid occupies u in [-h, +h], apex is at -h, base at +h.
		const float boxMinP = boxCenter[primaryAxis] - boxHScales[primaryAxis];
		const float boxMaxP = boxCenter[primaryAxis] + boxHScales[primaryAxis];

		const float u0 = primarySign * (boxMinP - pyrCenter[primaryAxis]);
		const float u1 = primarySign * (boxMaxP - pyrCenter[primaryAxis]);

		const float overlapUMin = std::max(std::min(u0, u1), -h);
		const float overlapUMax = std::min(std::max(u0, u1), h);

		if (overlapUMin > overlapUMax)
			return false;

		// In any slice u, the pyramid cross-section rectangle is centered at pyrCenter on the
		// secondary axes, with half-extents that grow linearly from 0 at the apex to hb/hc at the
		// base.
		const float deltaB = math::fabs(boxCenter[secondaryAxis0] - pyrCenter[secondaryAxis0]);
		const float deltaC = math::fabs(boxCenter[secondaryAxis1] - pyrCenter[secondaryAxis1]);

		// Required pyramid half-width/half-height beyond the box's own half-scales.
		const float needB = std::max(deltaB - boxHScales[secondaryAxis0], 0.0f);
		const float needC = std::max(deltaC - boxHScales[secondaryAxis1], 0.0f);

		// Solve for the earliest u at which the pyramid slice is wide enough on each secondary
		// axis:
		//   hb * ((u + h) / (2h)) >= needB
		//   hc * ((u + h) / (2h)) >= needC
		const float reqUB = -h + ((2.0f * h * needB) / hb);
		const float reqUC = -h + ((2.0f * h * needC) / hc);
		const float reqU = std::max(reqUB, reqUC);

		// There is an intersection iff the overlapping primary interval reaches a slice that is
		// wide enough.
		return (reqU <= (overlapUMax + COLLISION_VOLUME_EPS));
	}
	static bool IntersectAxisAlignedBoxVolume(const CollisionVolume* box, const CMatrix44f& boxMat,
	                                          const CollisionVolume* vol, const CMatrix44f& volMat,
	                                          const CMatrix44f& boxInv, bool& handled)
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
				return (GetWeightedSqDistToBox(volCenter, boxCenter, boxHScales, invRadiusSq) <=
				        1.0f);
			} break;

			case CollisionVolume::COLVOL_TYPE_ELLIPSOID: {
				const float3 radii = GetAlignedHalfScales(vol, axisMap);
				const float3 invRadiiSq(1.0f / (radii.x * radii.x), 1.0f / (radii.y * radii.y),
				                        1.0f / (radii.z * radii.z));
				return (GetWeightedSqDistToBox(volCenter, boxCenter, boxHScales, invRadiiSq) <=
				        1.0f);
			} break;

			case CollisionVolume::COLVOL_TYPE_CYLINDER: {
				const int primaryAxis = axisMap[vol->GetPrimaryAxis()];
				const float3 volHScales = GetAlignedHalfScales(vol, axisMap);
				const float radius =
					std::max(volHScales[(primaryAxis + 1) % 3], volHScales[(primaryAxis + 2) % 3]);

				if (math::fabs(volCenter[primaryAxis] - boxCenter[primaryAxis]) >
				    (volHScales[primaryAxis] + boxHScales[primaryAxis]))
					return false;

				float sqDist = 0.0f;
				for (int axis = 0; axis < 3; ++axis) {
					if (axis == primaryAxis)
						continue;

					const float boxMin = boxCenter[axis] - boxHScales[axis];
					const float boxMax = boxCenter[axis] + boxHScales[axis];
					const float d = (volCenter[axis] < boxMin)   ? (boxMin - volCenter[axis])
					                : (volCenter[axis] > boxMax) ? (volCenter[axis] - boxMax)
					                                             : 0.0f;

					sqDist += (d * d);
				}

				return (sqDist <= (radius * radius));
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
			dir = TripleCross(ab, ao, ab);

			if (dir.SqLength() < COLLISION_VOLUME_EPS)
				dir = PerpendicularVector(ab);
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
			// Degenerate triangle: reduce to the more useful edge.
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
				dir = TripleCross(ac, ao, ac);

				if (dir.SqLength() < COLLISION_VOLUME_EPS)
					dir = PerpendicularVector(ac);
			} else {
				if (ab.dot(ao) > 0.0f) {
					simplex.Assign(a, b);
					dir = TripleCross(ab, ao, ab);

					if (dir.SqLength() < COLLISION_VOLUME_EPS)
						dir = PerpendicularVector(ab);
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
				dir = TripleCross(ab, ao, ab);

				if (dir.SqLength() < COLLISION_VOLUME_EPS)
					dir = PerpendicularVector(ab);
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

		float3 abc = ab.cross(ac);  // opposite vertex: d
		if (abc.dot(ad) > 0.0f)
			abc = -abc;

		float3 acd = ac.cross(ad);  // opposite vertex: b
		if (acd.dot(ab) > 0.0f)
			acd = -acd;

		float3 adb = ad.cross(ab);  // opposite vertex: c
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

	static bool PointInPyramidLocal(const CollisionVolume* pyramid, const float3& point)
	{
		const float3 relPoint = point - pyramid->GetOffsets();
		const int primaryAxis = pyramid->GetPrimaryAxis();
		const int secondaryAxis0 = pyramid->GetSecondaryAxis(0);
		const int secondaryAxis1 = pyramid->GetSecondaryAxis(1);
		const float3& pyrHScales = pyramid->GetHScales();
		const float h = pyrHScales[primaryAxis];
		const float u = relPoint[primaryAxis];

		if (u < (-h - COLLISION_VOLUME_EPS) || u > (h + COLLISION_VOLUME_EPS))
			return false;

		const float extentScale = (u + h) / (2.0f * h);
		const float extent0 = pyrHScales[secondaryAxis0] * extentScale;
		const float extent1 = pyrHScales[secondaryAxis1] * extentScale;

		return (math::fabs(relPoint[secondaryAxis0]) <= (extent0 + COLLISION_VOLUME_EPS) &&
		        math::fabs(relPoint[secondaryAxis1]) <= (extent1 + COLLISION_VOLUME_EPS));
	}

	static float3 GetClosestPointOnPyramidLocal(const CollisionVolume* pyramid, const float3& point)
	{
		const float3 relPoint = point - pyramid->GetOffsets();
		const int primaryAxis = pyramid->GetPrimaryAxis();
		const int secondaryAxis0 = pyramid->GetSecondaryAxis(0);
		const int secondaryAxis1 = pyramid->GetSecondaryAxis(1);
		const float3& pyrHScales = pyramid->GetHScales();
		const float h = pyrHScales[primaryAxis];
		const float h0 = pyrHScales[secondaryAxis0];
		const float h1 = pyrHScales[secondaryAxis1];
		const float p = relPoint[primaryAxis];
		const float s0 = math::fabs(relPoint[secondaryAxis0]);
		const float s1 = math::fabs(relPoint[secondaryAxis1]);
		const float k0 = h0 / (2.0f * h);
		const float k1 = h1 / (2.0f * h);
		const float sign0 = (relPoint[secondaryAxis0] >= 0.0f) ? 1.0f : -1.0f;
		const float sign1 = (relPoint[secondaryAxis1] >= 0.0f) ? 1.0f : -1.0f;

		auto clampPrimary = [h](float x) { return std::clamp(x, -h, h); };
		auto extent0At = [h0, h](float x) { return h0 * ((x + h) / (2.0f * h)); };
		auto extent1At = [h1, h](float x) { return h1 * ((x + h) / (2.0f * h)); };

		const float xBreak0 = clampPrimary((2.0f * h * s0 / h0) - h);
		const float xBreak1 = clampPrimary((2.0f * h * s1 / h1) - h);

		float xs[4] = {-h, xBreak0, xBreak1, h};

		for (int i = 0; i < 4; ++i) {
			for (int j = i + 1; j < 4; ++j) {
				if (xs[j] < xs[i])
					std::swap(xs[i], xs[j]);
			}
		}

		float bestPrimary = clampPrimary(p);
		float bestDistSq = std::numeric_limits<float>::max();

		const auto updateBest = [&](float x) {
			x = clampPrimary(x);

			const float extent0 = extent0At(x);
			const float extent1 = extent1At(x);
			const float q0 = sign0 * std::min(s0, extent0);
			const float q1 = sign1 * std::min(s1, extent1);
			const float dp = x - p;
			const float ds0 = q0 - relPoint[secondaryAxis0];
			const float ds1 = q1 - relPoint[secondaryAxis1];
			const float distSq = (dp * dp) + (ds0 * ds0) + (ds1 * ds1);

			if (distSq < bestDistSq) {
				bestDistSq = distSq;
				bestPrimary = x;
			}
		};

		updateBest(p);

		for (int i = 0; i < 4; ++i)
			updateBest(xs[i]);

		for (int i = 0; i < 3; ++i) {
			const float xl = xs[i + 0];
			const float xr = xs[i + 1];

			if ((xr - xl) <= COLLISION_VOLUME_EPS)
				continue;

			const float xm = (xl + xr) * 0.5f;
			const bool active0 = (xm < xBreak0);
			const bool active1 = (xm < xBreak1);

			float denom = 1.0f;
			float numer = p;

			if (active0) {
				denom += k0 * k0;
				numer += k0 * s0 - k0 * k0 * h;
			}
			if (active1) {
				denom += k1 * k1;
				numer += k1 * s1 - k1 * k1 * h;
			}

			updateBest(std::clamp(numer / denom, xl, xr));
		}

		const float bestExtent0 = extent0At(bestPrimary);
		const float bestExtent1 = extent1At(bestPrimary);
		float3 closest = pyramid->GetOffsets();

		closest[primaryAxis] += bestPrimary;
		closest[secondaryAxis0] += sign0 * std::min(s0, bestExtent0);
		closest[secondaryAxis1] += sign1 * std::min(s1, bestExtent1);
		return closest;
	}

	static bool IntersectRoundedVolumePyramid(const CollisionVolume* pyramid,
	                                          const CMatrix44f& pyrMat, const CMatrix44f& pyrInv,
	                                          const CollisionVolume* vol, const CMatrix44f& volMat,
	                                          const CMatrix44f& volInv)
	{
		const float3 centerLocal = pyrInv.Mul(volMat.Mul(vol->GetOffsets()));

		if (PointInPyramidLocal(pyramid, centerLocal))
			return true;

		const float3 closestLocal = GetClosestPointOnPyramidLocal(pyramid, centerLocal);
		const float3 dir = closestLocal - centerLocal;

		if (dir.SqLength() <= COLLISION_VOLUME_EPS)
			return true;

		const float3 supportLocal =
			GetSupportPointBoxSpace(vol, volMat, volInv, pyrMat, pyrInv, dir);
		return PointInPyramidLocal(pyramid, supportLocal);
	}

	static bool IntersectSpherePyramid(const CollisionVolume* pyramid, const CMatrix44f& pyrInv,
	                                   const CollisionVolume* sphere, const CMatrix44f& sphereMat)
	{
		const float3 centerLocal = pyrInv.Mul(sphereMat.Mul(sphere->GetOffsets()));

		if (PointInPyramidLocal(pyramid, centerLocal))
			return true;

		const float3 closestLocal = GetClosestPointOnPyramidLocal(pyramid, centerLocal);
		const float radius = sphere->GetHScales().x;
		return ((closestLocal - centerLocal).SqLength() <=
		        ((radius * radius) + COLLISION_VOLUME_EPS));
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
	if (IntersectAxisAlignedBoxVolume(box, boxMat, vol, volMat, boxInv,
	                                  handledByAxisAlignedFastPath))
		return true;
	if (handledByAxisAlignedFastPath)
		return false;

	float3 dir = boxInv.Mul(volCtr);

	if (dir.SqLength() < COLLISION_VOLUME_EPS)
		dir = float3(1.0f, 0.0f, 0.0f);

	GJKSimplex simplex;
	simplex.PushFront(GetMinkowskiSupportPoint(box, vol, volMat, volInv, boxMat, boxInv, dir));
	dir = -simplex.points[0];

	if (dir.SqLength() < COLLISION_VOLUME_EPS)
		return true;

	for (int n = 0; n < 32; ++n) {
		const float3 support =
			GetMinkowskiSupportPoint(box, vol, volMat, volInv, boxMat, boxInv, dir);

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

	switch (vol->GetVolumeType()) {
		case CollisionVolume::COLVOL_TYPE_SPHERE:
			return IntersectSpherePyramid(pyramid, pyrInv, vol, volMat);
		case CollisionVolume::COLVOL_TYPE_ELLIPSOID:
		case CollisionVolume::COLVOL_TYPE_CYLINDER:
			return IntersectRoundedVolumePyramid(pyramid, pyrMat, pyrInv, vol, volMat, volInv);
		default:
			break;
	}

	// Initial search direction in pyramid-local space.
	float3 dir = pyrInv.Mul(volCtr);

	if (dir.SqLength() < COLLISION_VOLUME_EPS) {
		dir = ZeroVector;
		dir[pyramid->GetPrimaryAxis()] = 1.0f;
	}

	GJKSimplex simplex;
	simplex.PushFront(GetMinkowskiSupportPoint(pyramid, vol, volMat, volInv, pyrMat, pyrInv, dir));
	dir = -simplex.points[0];

	if (dir.SqLength() < COLLISION_VOLUME_EPS)
		return true;

	for (int n = 0; n < 32; ++n) {
		const float3 support =
			GetMinkowskiSupportPoint(pyramid, vol, volMat, volInv, pyrMat, pyrInv, dir);

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
