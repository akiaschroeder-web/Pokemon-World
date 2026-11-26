#ifndef GUARD_REGIONS_H
#define GUARD_REGIONS_H

#include "global.h"
#include "constants/regions.h"
#include "constants/map_groups_hns.h"

enum KantoSubRegion GetKantoSubregion(u32 mapSecId);

// Pokémon World: the region is worked out from the map section.
// Sinnoh's map sections are one block, from Twinleaf Town to the Sinnoh copies of
// Victory Road, the Pokémon League and the Pokémon Mansion.
#define SINNOH_MAPSEC_START MAPSEC_TWINLEAF_TOWN
#define SINNOH_MAPSEC_END   MAPSEC_SN_POKEMON_MANSION

// Unova's map sections come after everything else, from Nuvema Town on (tools/pokemon_world/import_bw.py).
#define UNOVA_MAPSEC_START MAPSEC_UNOVA_NUVEMA_TOWN

static inline enum Region GetRegionForSectionId(u32 sectionId)
{
    if (sectionId >= UNOVA_MAPSEC_START && sectionId < MAPSEC_NONE)
        return REGION_UNOVA;
    if (sectionId >= SINNOH_MAPSEC_START && sectionId <= SINNOH_MAPSEC_END)
        return REGION_SINNOH;
    if (sectionId >= KANTO_MAPSEC_START && sectionId < KANTO_MAPSEC_END && KANTO_MAPSEC_COUNT > 0)
        return REGION_KANTO;
    // Heart & Soul's Johto, then the later-Kanto places FireRed's Kanto doesn't have
    if (sectionId >= JOHTO_MAPSEC_START && sectionId <= JOHTO_MAPSEC_END)
        return REGION_JOHTO;
    if (sectionId >= KANTO_HNS_MAPSEC_START && sectionId <= KANTO_HNS_MAPSEC_END && KANTO_HNS_MAPSEC_START != MAPSEC_NONE)
        return REGION_KANTO;
    return REGION_HOENN;
}

static inline enum Region GetCurrentRegion(void)
{
    return GetRegionForSectionId(gMapHeader.regionMapSectionId);
}

// Pokémon World: whose music a place uses for battles, victories and fanfares.
// Kanto has FireRed's maps and Heart & Soul's Gold/Silver-era ("3 years later") maps, which share
// map sections; Heart & Soul's maps are told apart by their map groups (constants/map_groups_hns.h).
enum MusicStyle
{
    MUSIC_STYLE_HOENN,      // Emerald
    MUSIC_STYLE_FRLG,       // FireRed's Kanto
    MUSIC_STYLE_SINNOH,     // the Sinnoh port's
    MUSIC_STYLE_HGSS_JOHTO, // Heart & Soul's Johto
    MUSIC_STYLE_HGSS_KANTO, // Heart & Soul's later Kanto
    MUSIC_STYLE_UNOVA,      // Pokémon Black's
};

static inline bool32 IsHnsMapGroup(u32 mapGroup)
{
    return mapGroup >= HNS_MAP_GROUP_FIRST && mapGroup <= HNS_MAP_GROUP_LAST;
}

static inline enum MusicStyle GetCurrentMusicStyle(void)
{
    switch (GetCurrentRegion())
    {
    case REGION_JOHTO:
        return MUSIC_STYLE_HGSS_JOHTO;
    case REGION_KANTO:
        return IsHnsMapGroup(gSaveBlock1Ptr->location.mapGroup) ? MUSIC_STYLE_HGSS_KANTO : MUSIC_STYLE_FRLG;
    case REGION_SINNOH:
        return MUSIC_STYLE_SINNOH;
    case REGION_UNOVA:
        return MUSIC_STYLE_UNOVA;
    default:
        return MUSIC_STYLE_HOENN;
    }
}

static inline bool32 IsHgssMusicStyle(enum MusicStyle style)
{
    return style == MUSIC_STYLE_HGSS_JOHTO || style == MUSIC_STYLE_HGSS_KANTO;
}

#endif // GUARD_REGIONS_H
