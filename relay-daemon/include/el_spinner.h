#ifndef RELAY_EL_SPINNER_H
#define RELAY_EL_SPINNER_H

/* ── Spinner animation frames (working indicator) ───────────────────── */

/* Returns the total number of distinct animation frames. */
int el_spinner_frame_count(void);

/* Returns the animation frame for the given tick, cycling through all
 * frames.  tick=0 returns the first frame; tick=N wraps around. */
const char *el_spinner_frame(int tick);

#endif /* RELAY_EL_SPINNER_H */
