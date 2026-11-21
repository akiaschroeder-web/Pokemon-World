#ifndef GUARD_REGIONS_H
#define GUARD_REGIONS_H

#include "global.h"
#include "constants/regions.h"

enum KantoSubRegion GetKantoSubregion(u32 mapSecId);

// Pokémon World: the region is worked out from the map section.
// Sinnoh's map sections are one block, from Twinleaf Town to Rock Peak Ruins.
#define SINNOH_MAPSEC_START MAPSEC_TWINLEAF_TOWN
#define SINNOH_MAPSEC_END   MAPSEC_ROCK_PEAK_RUINS

static inline enum Region GetRegionForSectionId(u32 sectionId)
{
    if (sectionId >= SINNOH_MAPSEC_START && sectionId <= SINNOH_MAPSEC_END)
        return REGION_SINNOH;
    if (sectionId >= KANTO_MAPSEC_START && sectionId < KANTO_MAPSEC_END && KANTO_MAPSEC_COUNT > 0)
        return REGION_KANTO;
    return REGION_HOENN;
}

static inline enum Region GetCurrentRegion(void)
{
    return GetRegionForSectionId(gMapHeader.regionMapSectionId);
}

#endif // GUARD_REGIONS_H
