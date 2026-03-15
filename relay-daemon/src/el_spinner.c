#include "el_spinner.h"

static const char * const FRAMES[] = {
    ".",
    "..",
    "...",
    "....",
    ".....",
    "......",
    ".......",
    "........",
    ".........",
    "..........",
    "...........",
    "............",
    ".............",
    "..............",
    "...............",
    "................",
    ".................",
    "..................",
    "...................",
    "....................",
};

#define FRAME_COUNT ((int)(sizeof(FRAMES) / sizeof(FRAMES[0])))

int el_spinner_frame_count(void)
{
    return FRAME_COUNT;
}

const char *el_spinner_frame(int tick)
{
    return FRAMES[((tick % FRAME_COUNT) + FRAME_COUNT) % FRAME_COUNT];
}
