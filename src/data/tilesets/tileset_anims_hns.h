// Pokémon World: tileset animations of Heart & Soul's tilesets (Johto, later Kanto).
// Ported by hand from Heart & Soul's src/tileset_anims.c; included at the end of
// src/tileset_anims.c so it can use that file's animation buffer. Heart & Soul's
// primary tilesets have 640 tiles, so the tile offsets below are Heart & Soul's own.
// The importer (tools/pokemon_world/import_hns.py, ANIM_PORTED) points the
// tilesets at these. TODO: port the rest (Celadon/Silph Co fountains, National Park,
// Ecruteak theater, Azalea gym, bike shop, Lavaridge-style lava, Sootopolis gym).

static const u16 sTilesetAnims_JohtoGeneralHns_Flower_Frame0[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/flower/0.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Flower_Frame1[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/flower/1.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Flower_Frame2[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/flower/2.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Flower_Frame3[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/flower/3.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Flower_Frame4[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/flower/4.png", ".4bpp");

static const u16 *const sTilesetAnims_JohtoGeneralHns_Flower[] = {
    sTilesetAnims_JohtoGeneralHns_Flower_Frame0,
    sTilesetAnims_JohtoGeneralHns_Flower_Frame1,
    sTilesetAnims_JohtoGeneralHns_Flower_Frame2,
    sTilesetAnims_JohtoGeneralHns_Flower_Frame3,
    sTilesetAnims_JohtoGeneralHns_Flower_Frame4,
};

static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame0[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/0.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame1[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/1.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame2[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/2.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame3[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/3.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame4[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/4.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame5[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/5.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame6[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/6.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_Water_Frame7[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/water_current_landwatersedge/7.png", ".4bpp");

static const u16 *const sTilesetAnims_JohtoGeneralHns_Water[] = {
    sTilesetAnims_JohtoGeneralHns_Water_Frame0,
    sTilesetAnims_JohtoGeneralHns_Water_Frame1,
    sTilesetAnims_JohtoGeneralHns_Water_Frame2,
    sTilesetAnims_JohtoGeneralHns_Water_Frame3,
    sTilesetAnims_JohtoGeneralHns_Water_Frame4,
    sTilesetAnims_JohtoGeneralHns_Water_Frame5,
    sTilesetAnims_JohtoGeneralHns_Water_Frame6,
    sTilesetAnims_JohtoGeneralHns_Water_Frame7,
};

static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame0[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/0.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame1[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/1.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame2[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/2.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame3[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/3.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame4[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/4.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame5[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/5.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame6[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/6.png", ".4bpp");
static const u16 sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame7[] = INCGFX_U16("data/tilesets/primary/johto_general_hns/anim/sandwatersedge/7.png", ".4bpp");

static const u16 *const sTilesetAnims_JohtoGeneralHns_SandWatersEdge[] = {
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame0,
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame1,
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame2,
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame3,
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame4,
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame5,
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame6,
    sTilesetAnims_JohtoGeneralHns_SandWatersEdge_Frame7,
};

static void QueueAnimTiles_JohtoGeneralHns_Flower(u16 timer)
{
    u16 i = timer % ARRAY_COUNT(sTilesetAnims_JohtoGeneralHns_Flower);
    AppendTilesetAnimToBuffer(sTilesetAnims_JohtoGeneralHns_Flower[i], (u16 *)(BG_VRAM + TILE_OFFSET_4BPP(508)), 4 * TILE_SIZE_4BPP);
}

static void QueueAnimTiles_JohtoGeneralHns_SandWatersEdge(u16 timer)
{
    u16 i = timer % ARRAY_COUNT(sTilesetAnims_JohtoGeneralHns_SandWatersEdge);
    AppendTilesetAnimToBuffer(sTilesetAnims_JohtoGeneralHns_SandWatersEdge[i], (u16 *)(BG_VRAM + TILE_OFFSET_4BPP(416)), 18 * TILE_SIZE_4BPP);
}

// The waterfall frames are tiles 34-45 of each water frame.
static void QueueAnimTiles_JohtoGeneralHns_Waterfall(u16 timer)
{
    u16 i = timer % ARRAY_COUNT(sTilesetAnims_JohtoGeneralHns_Water);
    const u16 *src = sTilesetAnims_JohtoGeneralHns_Water[i] + 34 * (TILE_SIZE_4BPP / 2);
    AppendTilesetAnimToBuffer(src, (u16 *)(BG_VRAM + TILE_OFFSET_4BPP(450)), 12 * TILE_SIZE_4BPP);
}

static void TilesetAnim_JohtoGeneral_Hns(u16 timer)
{
    if (timer % 8 == 0)
        QueueAnimTiles_JohtoGeneralHns_SandWatersEdge(timer >> 3);
    if (timer % 16 == 2)
        QueueAnimTiles_JohtoGeneralHns_Flower(timer >> 4);
    if (timer % 16 == 3)
        QueueAnimTiles_JohtoGeneralHns_Waterfall(timer / 16);
}

void InitTilesetAnim_JohtoGeneral_Hns(void)
{
    sPrimaryTilesetAnimCounter = 0;
    sPrimaryTilesetAnimCounterMax = 256;
    sPrimaryTilesetAnimCallback = TilesetAnim_JohtoGeneral_Hns;
}
