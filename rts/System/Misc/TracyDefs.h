#pragma once

#include <tracy/Tracy.hpp>

#ifdef RECOIL_DETAILED_TRACY_ZONING
	#define RECOIL_DETAILED_TRACY_ZONE ZoneNamed(TracyConcat(__recoil_tracy_zone_, TracyLine), true)
#else
	#define RECOIL_DETAILED_TRACY_ZONE do {} while(0)
#endif
