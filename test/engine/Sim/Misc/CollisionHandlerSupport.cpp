#include "Map/MapDimensions.h"
#include "Sim/Misc/GroundBlockingObjectMap.h"
#include "System/float3.h"
#include "Sim/Objects/SolidObject.h"
#include "Sim/Units/Unit.h"

MapDimensions mapDims;
CGroundBlockingObjectMap groundBlockingObjectMap;

const CMatrix44f& LocalModelPiece::GetModelSpaceMatrix() const
{
	return modelSpaceMat;
}

CMatrix44f CUnit::GetTransformMatrix(bool synced, bool fullread) const
{
	float3 interPos = synced ? pos : drawPos;

	return (ComposeMatrix(interPos));
}
