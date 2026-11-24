#ifndef GUARD_HNS_SLIDING_PUZZLE_H
#define GUARD_HNS_SLIDING_PUZZLE_H

// Ruins of Alph sliding puzzles. Ported from Pokémon Heart & Soul
// (include/sliding_puzzle.h); used with permission, see PROJECT_CREDITS.md.

#include "constants/sliding_puzzles_hns.h"

enum
{
    ROTATE_NONE,
    ROTATE_ANTICLOCKWISE,
    ROTATE_CLOCKWISE,
};

enum
{
    ORIENTATION_0,
    ORIENTATION_90,
    ORIENTATION_180,
    ORIENTATION_270,
    ORIENTATION_MAX,
};

#define IMMOVABLE_TILE ORIENTATION_MAX

#define NUM_SLIDING_PUZZLE_COLS 6
#define NUM_SLIDING_PUZZLE_ROWS 4

#define FIRST_SLIDING_PUZZLE_COL  0
#define FINAL_SLIDING_PUZZLE_COL  (NUM_SLIDING_PUZZLE_COLS - 1)
#define FIRST_SLIDING_PUZZLE_ROW  0
#define FINAL_SLIDING_PUZZLE_ROW  (NUM_SLIDING_PUZZLE_ROWS - 1)

struct SlidingPuzzle
{
    u8 tiles[NUM_SLIDING_PUZZLE_ROWS][NUM_SLIDING_PUZZLE_COLS];
    u8 puzzleId;
    u8 cursorSpriteId;
    u8 heldTile;
    bool8 solved;
};

// Special. In: VAR_0x8004 = SLIDING_PUZZLE_* id, VAR_0x8005 = TRUE to only show the
// solved picture. Follow with `waitstate`. Out: VAR_RESULT = TRUE if solved.
void Hns_DoSlidingPuzzle(void);

#endif // GUARD_HNS_SLIDING_PUZZLE_H
