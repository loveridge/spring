#include "Map/MapDimensions.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "System/float3.h"
#include "Sim/Objects/SolidObject.h"

float3 CollisionVolume::GetWorldSpacePos(const CSolidObject* o, const float3& extOffsets) const {
	// collision-volumes are always centered on midPos
	return (o->midPos + o->GetObjectSpaceVec(axisOffsets + extOffsets));
}

MapDimensions mapDims;
CGroundBlockingObjectMap groundBlockingObjectMap;

const CMatrix44f& LocalModelPiece::GetModelSpaceMatrix() const
{
	return modelSpaceMat;
}

void CollisionVolume::InitShape(
	const float3& scales,
	const float3& offsets,
	const int vType,
	const int tType,
	const int pAxis
) {
	axisOffsets = offsets;

	// make sure none of the scales are ever negative or zero
	//
	// if the clamped vector is <1, 1, 1> (ie. all scales were <= 1.0f)
	// then we assume a "default volume" is wanted and the unit/feature
	// instances will be assigned spheres (of size model->radius)
	//
	float3 clampedScales = float3::max(scales, OnesVector);

	// assign these here, since we can be
	// called from outside the constructor
	volumeType    = std::max(vType, 0) % (COLVOL_TYPE_SPHERE + 1);
	volumeAxes[0] = std::max(pAxis, 0) % (COLVOL_AXIS_Z + 1);

	///< [0] is primary axis, [1] and [2] are secondary (all COLVOL_AXIS_*)
	switch (volumeAxes[0]) {
		case COLVOL_AXIS_X: {
			volumeAxes[1] = COLVOL_AXIS_Y;
			volumeAxes[2] = COLVOL_AXIS_Z;
		} break;
		case COLVOL_AXIS_Y: {
			volumeAxes[1] = COLVOL_AXIS_X;
			volumeAxes[2] = COLVOL_AXIS_Z;
		} break;
		case COLVOL_AXIS_Z: {
			volumeAxes[1] = COLVOL_AXIS_X;
			volumeAxes[2] = COLVOL_AXIS_Y;
		} break;
	}

	FixTypeAndScale(clampedScales);
	SetAxisScales(clampedScales);
	SetBoundingRadius();
	SetUseContHitTest(tType == COLVOL_HITTEST_CONT);
}


void CollisionVolume::SetBoundingRadius() {
	// set the radius of the minimum bounding sphere
	// that encompasses this custom collision volume
	// (for early-out testing)
	// NOTE:
	//   this must be called manually after either
	//   a call to SetAxisScales or to RescaleAxes
	switch (volumeType) {
		case COLVOL_TYPE_BOX: {
			// would be an over-estimation for cylinders
			volumeBoundingRadiusSq = halfAxisScalesSqr.x + halfAxisScalesSqr.y + halfAxisScalesSqr.z;
			volumeBoundingRadius = math::sqrt(volumeBoundingRadiusSq);
		} break;
		case COLVOL_TYPE_CYLINDER: {
			const float prhs = halfAxisScales[volumeAxes[0]];   // primary axis half-scale
			const float sahs = halfAxisScales[volumeAxes[1]];   // 1st secondary axis half-scale
			const float sbhs = halfAxisScales[volumeAxes[2]];   // 2nd secondary axis half-scale
			const float mshs = std::max(sahs, sbhs);            // max. secondary axis half-scale

			volumeBoundingRadiusSq = prhs * prhs + mshs * mshs;
			volumeBoundingRadius = math::sqrt(volumeBoundingRadiusSq);
		} break;
		case COLVOL_TYPE_SPHERE: {
			volumeBoundingRadius = halfAxisScales.x;
			volumeBoundingRadiusSq = volumeBoundingRadius * volumeBoundingRadius;
		} break;
		case COLVOL_TYPE_ELLIPSOID: {
			volumeBoundingRadius = std::max(halfAxisScales.x, std::max(halfAxisScales.y, halfAxisScales.z));
			volumeBoundingRadiusSq = volumeBoundingRadius * volumeBoundingRadius;
		} break;
	}
}

void CollisionVolume::SetAxisScales(const float3& scales) {
	fullAxisScales = scales;
	halfAxisScales = fullAxisScales * 0.5f;

	halfAxisScalesSqr = halfAxisScales * halfAxisScales;
	halfAxisScalesInv = OnesVector / halfAxisScales;
}

void CollisionVolume::RescaleAxes(const float3& scales) {
	fullAxisScales *= scales;
	halfAxisScales *= scales;

	// h*h --> h*h*s*s; 1/h --> 1/h/s = 1/(h*s)
	halfAxisScalesSqr *= (scales * scales);
	halfAxisScalesInv /= scales;
}

void CollisionVolume::FixTypeAndScale(float3& scales) {
	// NOTE:
	//   prevent Lua (which calls InitShape directly) from
	//   creating non-uniform spheres to emulate ellipsoids
	switch (volumeType) {
		case COLVOL_TYPE_SPHERE: {
			scales = OnesVector * std::max(scales.x, std::max(scales.y, scales.z));
			return;
		} break;

		case COLVOL_TYPE_ELLIPSOID: {
			if (scales.x == scales.y && scales.y == scales.z) {
				volumeType = COLVOL_TYPE_SPHERE;
			} else {
				// disallow insane ellipsoids
				scales = float3::max(scales, OnesVector * std::fmax(scales.x, std::max(scales.y, scales.z)) * 0.02f);
			}

			return;
		} break;

		case COLVOL_TYPE_CYLINDER: {
			scales[volumeAxes[1]] = std::max(scales[volumeAxes[1]], scales[volumeAxes[2]]);
			scales[volumeAxes[2]] =          scales[volumeAxes[1]];
		} break;

		default: {
		} break;
	}
}

